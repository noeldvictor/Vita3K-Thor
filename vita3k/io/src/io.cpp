// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <io/device.h>
#include <io/functions.h>
#include <io/io.h>
#include <io/state.h>
#include <io/types.h>
#include <io/util.h>
#include <io/vfs.h>

#include <rtc/rtc.h>
#include <util/log.h>
#include <util/preprocessor.h>
#include <util/string_utils.h>

#include <miniz.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cassert>
#include <ctime>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(__aarch64__) && defined(__APPLE__)
#define stat64 stat
#endif

// ****************************
// * Utility functions *
// ****************************

static int io_error_impl(const int retval, const char *export_name, const char *func_name) {
    LOG_WARN("{} ({}) returned {}", func_name, export_name, log_hex(retval));
    return retval;
}

#define IO_ERROR(retval) io_error_impl(retval, export_name, __func__)
#define IO_ERROR_UNK() IO_ERROR(-1)

constexpr bool log_file_op = true;
constexpr bool log_file_read = false;
constexpr bool log_file_seek = false;
constexpr bool log_file_stat = false;
constexpr std::uint64_t archive_memory_file_limit = 64ull * 1024ull * 1024ull;

namespace vfs {

static size_t write_to_buffer(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    FileBuffer *const buffer = static_cast<FileBuffer *>(pOpaque);
    assert(file_ofs == buffer->size());
    const uint8_t *const first = static_cast<const uint8_t *>(pBuf);
    const uint8_t *const last = &first[n];
    buffer->insert(buffer->end(), first, last);

    return n;
}

static const char *miniz_get_error(mz_zip_archive *zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(zip));
}

static std::string normalize_archive_path(const fs::path &path) {
    auto normalized = path.generic_path().string();
    string_utils::replace(normalized, "\\", "/");

    while (!normalized.empty() && normalized.front() == '/')
        normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    return normalized;
}

static bool is_safe_archive_path(const std::string &path) {
    if (path.empty())
        return true;

    const fs::path candidate{ path };
    if (candidate.is_absolute())
        return false;

    for (const auto &part : candidate) {
        const auto part_str = part.generic_string();
        if (part_str == "..")
            return false;
    }

    return true;
}

static std::string parent_path_of(const std::string &path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return {};

    return path.substr(0, slash);
}

static std::string filename_of(const std::string &path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return path;

    return path.substr(slash + 1);
}

static void add_archive_directory(IOState::ArchiveMount &mount, const std::string &dir) {
    if (dir.empty()) {
        mount.dir_children.try_emplace("");
        return;
    }

    const auto parent = parent_path_of(dir);
    add_archive_directory(mount, parent);

    auto &parent_children = mount.dir_children[parent];
    parent_children.insert(filename_of(dir));
    mount.dir_children.try_emplace(dir);

    if (!mount.entries.contains(dir)) {
        mount.entries[dir] = { {}, 0, true };
        mount.lower_to_path[string_utils::tolower(dir)] = dir;
    }
}

static bool add_archive_entry(IOState::ArchiveMount &mount, const std::string &relative_path, const std::string &archive_name, const std::uint64_t size, const bool is_dir, const bool allow_override) {
    if (relative_path.empty())
        return false;

    if (!is_safe_archive_path(relative_path)) {
        LOG_WARN("Skipping unsafe app archive entry {}", archive_name);
        return false;
    }

    const auto parent = parent_path_of(relative_path);
    add_archive_directory(mount, parent);
    mount.dir_children[parent].insert(filename_of(relative_path));

    if (is_dir) {
        add_archive_directory(mount, relative_path);
        return true;
    }

    const auto lower_path = string_utils::tolower(relative_path);
    const auto existing_case = mount.lower_to_path.find(lower_path);
    if (existing_case != mount.lower_to_path.end() && existing_case->second != relative_path) {
        if (!allow_override)
            return false;

        mount.entries.erase(existing_case->second);
    } else if (!allow_override && mount.entries.contains(relative_path)) {
        return false;
    }

    mount.entries[relative_path] = { archive_name, size, false };
    mount.lower_to_path[lower_path] = relative_path;
    return true;
}

static size_t mount_archive_root_entries(mz_zip_archive &zip, IOState::ArchiveMount &mount, const std::string &content_root, const bool allow_override) {
    const auto normalized_root = normalize_archive_path(content_root);
    const auto root_prefix = normalized_root.empty() ? std::string{} : normalized_root + "/";
    const auto root_prefix_lower = string_utils::tolower(root_prefix);
    const mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    size_t mounted_entries = 0;

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat))
            continue;

        const std::string archive_name = file_stat.m_filename;
        const std::string normalized_archive_name = normalize_archive_path(archive_name);
        const std::string normalized_archive_name_lower = string_utils::tolower(normalized_archive_name);
        if (!root_prefix_lower.empty() && !normalized_archive_name_lower.starts_with(root_prefix_lower))
            continue;

        const auto relative_path = root_prefix.empty() ? normalized_archive_name : normalize_archive_path(normalized_archive_name.substr(root_prefix.size()));
        const bool is_dir = mz_zip_reader_is_file_a_directory(&zip, i);
        if (add_archive_entry(mount, relative_path, archive_name, file_stat.m_uncomp_size, is_dir, allow_override))
            mounted_entries++;
    }

    return mounted_entries;
}

static std::vector<std::string> split_archive_path_segments(const std::string &path) {
    std::vector<std::string> segments;
    size_t start = 0;

    while (start < path.size()) {
        const auto slash = path.find('/', start);
        const auto end = slash == std::string::npos ? path.size() : slash;
        if (end > start)
            segments.push_back(path.substr(start, end - start));

        if (slash == std::string::npos)
            break;

        start = slash + 1;
    }

    return segments;
}

static std::string join_archive_path_segments(const std::vector<std::string> &segments, const size_t count) {
    std::string path;
    for (size_t i = 0; i < count && i < segments.size(); i++) {
        if (!path.empty())
            path += "/";
        path += segments[i];
    }

    return path;
}

static std::vector<std::pair<std::string, std::string>> find_archive_overlay_roots(mz_zip_archive &zip, const std::string &title_id) {
    std::vector<std::string> patch_roots;
    std::vector<std::string> repatch_roots;
    std::set<std::string> patch_seen;
    std::set<std::string> repatch_seen;
    const auto title_id_lower = string_utils::tolower(title_id);
    if (title_id_lower.empty())
        return {};

    const auto remember_root = [](std::vector<std::string> &roots, std::set<std::string> &seen, const std::string &root) {
        const auto normalized_root = normalize_archive_path(root);
        const auto lower_root = string_utils::tolower(normalized_root);
        if (normalized_root.empty() || seen.contains(lower_root))
            return;

        seen.insert(lower_root);
        roots.push_back(normalized_root);
    };

    const mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat))
            continue;

        const auto segments = split_archive_path_segments(normalize_archive_path(file_stat.m_filename));
        if (segments.size() < 3)
            continue;

        for (size_t segment_index = 0; segment_index + 1 < segments.size(); segment_index++) {
            const auto segment = string_utils::tolower(segments[segment_index]);
            if (string_utils::tolower(segments[segment_index + 1]) != title_id_lower)
                continue;

            if (segment == "patch") {
                remember_root(patch_roots, patch_seen, join_archive_path_segments(segments, segment_index + 2));
            } else if (segment == "repatch") {
                remember_root(repatch_roots, repatch_seen, join_archive_path_segments(segments, segment_index + 2));
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> roots;
    roots.reserve(patch_roots.size() + repatch_roots.size());
    for (const auto &root : patch_roots)
        roots.emplace_back(root, "patch");
    for (const auto &root : repatch_roots)
        roots.emplace_back(root, "rePatch");

    return roots;
}

static const IOState::ArchiveMount::Entry *find_archive_entry(const IOState::ArchiveMount &mount, const std::string &relative_path) {
    const auto normalized = normalize_archive_path(relative_path);
    if (normalized.empty()) {
        static const IOState::ArchiveMount::Entry root_entry{ {}, 0, true };
        return &root_entry;
    }

    const auto exact = mount.entries.find(normalized);
    if (exact != mount.entries.end())
        return &exact->second;

    const auto lower = mount.lower_to_path.find(string_utils::tolower(normalized));
    if (lower == mount.lower_to_path.end())
        return nullptr;

    const auto entry = mount.entries.find(lower->second);
    return entry != mount.entries.end() ? &entry->second : nullptr;
}

static bool read_archive_file(const IOState::ArchiveMount &mount, const std::string &relative_path, FileBuffer &buf) {
    const auto entry = find_archive_entry(mount, relative_path);
    if (!entry || entry->directory)
        return false;

    FILE *archive_file = FOPEN(mount.archive_path.c_str(), "rb");
    if (!archive_file) {
        LOG_ERROR("Failed to open archive {}", mount.archive_path);
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, archive_file, 0, 0)) {
        LOG_ERROR("miniz error reading archive {}: {}", mount.archive_path, miniz_get_error(&zip));
        fclose(archive_file);
        return false;
    }

    buf.clear();
    const bool extracted = mz_zip_reader_extract_file_to_callback(&zip, entry->archive_name.c_str(), &write_to_buffer, &buf, 0);
    if (!extracted)
        LOG_ERROR("miniz error extracting {} from {}: {}", entry->archive_name, mount.archive_path, miniz_get_error(&zip));

    mz_zip_reader_end(&zip);
    fclose(archive_file);
    return extracted;
}

static fs::path archive_cache_path(const fs::path &pref_path, const IOState &io, const std::string &relative_path) {
    fs::path safe_relative_path;
    std::string segment;
    std::stringstream stream(relative_path);
    while (std::getline(stream, segment, '/')) {
        if (segment.empty() || (segment == "."))
            continue;
        if (segment == "..")
            return {};

        safe_relative_path /= fs_utils::utf8_to_path(segment);
    }

    if (safe_relative_path.empty())
        return {};

    const auto key_source = io.app0_archive.archive_path.generic_string() + "|" + io.app0_archive.content_root;
    const auto archive_key = fmt::format("{:016X}", static_cast<std::uint64_t>(std::hash<std::string>{}(key_source)));
    const auto title_id = io.title_id.empty() ? std::string("unknown-title") : io.title_id;
    return pref_path / "cache" / "cartridge_archive" / title_id / archive_key / safe_relative_path;
}

static bool cached_archive_file_ready(const fs::path &cache_path, const std::uint64_t expected_size) {
    boost::system::error_code error;
    if (!fs::exists(cache_path, error) || error)
        return false;

    return fs::file_size(cache_path, error) == expected_size && !error;
}

static bool extract_archive_file_to_cache(const IOState::ArchiveMount &mount, const IOState::ArchiveMount::Entry &entry, const fs::path &cache_path) {
    static std::mutex cache_mutex;
    const std::lock_guard<std::mutex> guard(cache_mutex);

    if (cached_archive_file_ready(cache_path, entry.size))
        return true;

    boost::system::error_code error;
    fs::create_directories(cache_path.parent_path(), error);
    if (error) {
        LOG_ERROR("Failed to create cartridge cache directory {}: {}", cache_path.parent_path(), error.message());
        return false;
    }

    const auto temp_path = fs_utils::path_concat(cache_path, ".tmp");
    fs::remove(temp_path, error);

    FILE *archive_file = FOPEN(mount.archive_path.c_str(), "rb");
    if (!archive_file) {
        LOG_ERROR("Failed to open archive {}", mount.archive_path);
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, archive_file, 0, 0)) {
        LOG_ERROR("miniz error reading archive {}: {}", mount.archive_path, miniz_get_error(&zip));
        fclose(archive_file);
        return false;
    }

    LOG_INFO("Extracting large archive member {} ({} MiB) to cartridge cache {}", entry.archive_name, entry.size / (1024 * 1024), cache_path);
    const bool extracted = mz_zip_reader_extract_file_to_file(&zip, entry.archive_name.c_str(), fs_utils::path_to_utf8(temp_path).c_str(), 0);
    if (!extracted)
        LOG_ERROR("miniz error extracting {} from {} to {}: {}", entry.archive_name, mount.archive_path, cache_path, miniz_get_error(&zip));

    mz_zip_reader_end(&zip);
    fclose(archive_file);

    if (!extracted) {
        fs::remove(temp_path, error);
        return false;
    }

    if (!cached_archive_file_ready(temp_path, entry.size)) {
        LOG_ERROR("Cartridge cache file {} has unexpected size after extracting {}", temp_path, entry.archive_name);
        fs::remove(temp_path, error);
        return false;
    }

    fs::remove(cache_path, error);
    error.clear();
    fs::rename(temp_path, cache_path, error);
    if (error) {
        LOG_ERROR("Failed to move cartridge cache file {} to {}: {}", temp_path, cache_path, error.message());
        fs::remove(temp_path, error);
        return false;
    }

    return true;
}

static std::vector<std::string> list_archive_dir(const IOState::ArchiveMount &mount, const std::string &relative_path) {
    const auto normalized = normalize_archive_path(relative_path);
    std::vector<std::string> entries;

    const auto dir = mount.dir_children.find(normalized);
    if (dir == mount.dir_children.end()) {
        const auto lower = mount.lower_to_path.find(string_utils::tolower(normalized));
        if (lower == mount.lower_to_path.end())
            return entries;

        const auto lower_dir = mount.dir_children.find(lower->second);
        if (lower_dir == mount.dir_children.end())
            return entries;

        entries.assign(lower_dir->second.begin(), lower_dir->second.end());
        return entries;
    }

    entries.assign(dir->second.begin(), dir->second.end());
    return entries;
}

static fs::path get_app_file_path(const IOState &io, const fs::path &pref_path, const fs::path &vfs_file_path) {
    if (!io.app0_host_path.empty())
        return (io.app0_host_path / vfs_file_path).generic_path();

    return (pref_path / "ux0/app" / io.app_path / vfs_file_path).generic_path();
}

bool read_file(const VitaIoDevice device, FileBuffer &buf, const fs::path &pref_path, const fs::path &vfs_file_path) {
    const auto host_file_path = device::construct_emulated_path(device, vfs_file_path, pref_path).generic_path();
    return fs_utils::read_data(host_file_path, buf);
}

bool read_app_file(FileBuffer &buf, const fs::path &pref_path, const std::string &app_path, const fs::path &vfs_file_path) {
    return read_file(VitaIoDevice::ux0, buf, pref_path, fs::path("app") / app_path / vfs_file_path);
}

bool read_current_app_file(FileBuffer &buf, const IOState &io, const fs::path &pref_path, const fs::path &vfs_file_path) {
    if (io.app0_archive.mounted())
        return read_archive_file(io.app0_archive, normalize_archive_path(vfs_file_path), buf);

    return fs_utils::read_data(get_app_file_path(io, pref_path, vfs_file_path), buf);
}

bool mount_current_app_archive(IOState &io, const fs::path &archive_path, const std::string &content_root, const std::string &title_id) {
    FILE *archive_file = FOPEN(archive_path.c_str(), "rb");
    if (!archive_file) {
        LOG_ERROR("Failed to open current app archive {}", archive_path);
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, archive_file, 0, 0)) {
        LOG_ERROR("miniz error reading current app archive {}: {}", archive_path, miniz_get_error(&zip));
        fclose(archive_file);
        return false;
    }

    IOState::ArchiveMount mount;
    mount.archive_path = archive_path;
    mount.content_root = content_root;
    add_archive_directory(mount, "");

    const auto base_entries = mount_archive_root_entries(zip, mount, content_root, true);
    size_t overlay_roots = 0;
    size_t overlay_entries = 0;
    const auto normalized_content_root = string_utils::tolower(normalize_archive_path(content_root));

    for (const auto &[overlay_root, overlay_kind] : find_archive_overlay_roots(zip, title_id)) {
        if (string_utils::tolower(normalize_archive_path(overlay_root)) == normalized_content_root)
            continue;

        const auto mounted_entries = mount_archive_root_entries(zip, mount, overlay_root, true);
        if (mounted_entries == 0)
            continue;

        overlay_roots++;
        overlay_entries += mounted_entries;
        LOG_INFO("Applied {} archive overlay root {} for {} with {} entries", overlay_kind, overlay_root, title_id, mounted_entries);
    }

    mz_zip_reader_end(&zip);
    fclose(archive_file);

    if (!find_archive_entry(mount, "sce_sys/param.sfo")) {
        LOG_ERROR("Current app archive {} has no sce_sys/param.sfo under {}", archive_path, content_root);
        return false;
    }

    io.app0_archive = std::move(mount);
    io.app0_host_path.clear();
    LOG_INFO("Mounted app0 directly from archive {} root {} base_entries={} overlay_roots={} overlay_entries={}", archive_path, content_root, base_entries, overlay_roots, overlay_entries);
    return true;
}

void unmount_current_app_archive(IOState &io) {
    io.app0_archive.clear();
}

bool current_app_archive_mounted(const IOState &io) {
    return io.app0_archive.mounted();
}

bool current_app_file_exists(const IOState &io, const fs::path &vfs_file_path) {
    if (io.app0_archive.mounted()) {
        const auto entry = find_archive_entry(io.app0_archive, normalize_archive_path(vfs_file_path));
        return entry && !entry->directory;
    }

    if (!io.app0_host_path.empty())
        return fs::is_regular_file((io.app0_host_path / vfs_file_path).generic_path());

    return false;
}

bool current_app_directory_exists(const IOState &io, const fs::path &vfs_dir_path) {
    if (io.app0_archive.mounted()) {
        const auto entry = find_archive_entry(io.app0_archive, normalize_archive_path(vfs_dir_path));
        return entry && entry->directory;
    }

    if (!io.app0_host_path.empty())
        return fs::is_directory((io.app0_host_path / vfs_dir_path).generic_path());

    return false;
}

SceOff current_app_file_size(const IOState &io, const fs::path &vfs_file_path) {
    if (io.app0_archive.mounted()) {
        const auto entry = find_archive_entry(io.app0_archive, normalize_archive_path(vfs_file_path));
        return entry && !entry->directory ? static_cast<SceOff>(entry->size) : 0;
    }

    if (!io.app0_host_path.empty()) {
        const auto file_path = (io.app0_host_path / vfs_file_path).generic_path();
        if (fs::is_regular_file(file_path))
            return static_cast<SceOff>(fs::file_size(file_path));
    }

    return 0;
}

std::vector<std::string> list_current_app_directory(const IOState &io, const fs::path &vfs_dir_path) {
    if (io.app0_archive.mounted())
        return list_archive_dir(io.app0_archive, normalize_archive_path(vfs_dir_path));

    std::vector<std::string> entries;
    if (!io.app0_host_path.empty()) {
        const auto dir_path = (io.app0_host_path / vfs_dir_path).generic_path();
        if (fs::is_directory(dir_path)) {
            for (const auto &entry : fs::directory_iterator(dir_path))
                entries.push_back(entry.path().filename().generic_string());
        }
    }

    return entries;
}

SceSize get_directory_used_size(const VitaIoDevice device, const std::string &vfs_path, const fs::path &pref_path) {
    const auto emuenv_path = device::construct_emulated_path(device, vfs_path, pref_path);

    SceSize total_size = 0;
    for (const auto &entry : fs::recursive_directory_iterator(emuenv_path)) {
        if (fs::is_regular_file(entry.path()))
            total_size += fs::file_size(entry.path());
    }

    return total_size;
}

} // namespace vfs

// ****************************
// * End utility functions *
// ****************************

bool init(IOState &io, const fs::path &cache_path, const fs::path &log_path, const fs::path &pref_path, bool redirect_stdio) {
    // Iterate through the entire list of devices and create the subdirectories if they do not exist
    for (auto i : VitaIoDevice::_names()) {
        if (!device::is_valid_output_path(i))
            continue;
        fs::create_directories(pref_path / i);
    }

    const fs::path ux0{ pref_path / (+VitaIoDevice::ux0)._to_string() };
    const fs::path uma0{ pref_path / (+VitaIoDevice::uma0)._to_string() };
    const fs::path vd0{ pref_path / (+VitaIoDevice::vd0)._to_string() };

    fs::create_directories(ux0 / "data");
    fs::create_directories(ux0 / "app");
    fs::create_directories(ux0 / "music");
    fs::create_directories(ux0 / "picture");
    fs::create_directories(ux0 / "theme");
    fs::create_directories(ux0 / "video");
    fs::create_directories(ux0 / "user");
    fs::create_directories(uma0 / "data");
    fs::create_directories(vd0 / "registry");
    fs::create_directories(vd0 / "network");

    fs::create_directories(cache_path / "shaders");
    fs::create_directory(log_path / "shaderlog");
    fs::create_directory(log_path / "texturelog");

    io.redirect_stdio = redirect_stdio;

#ifndef _WIN32
    io.case_isens_find_enabled = true;
#endif

    return true;
}

void init_device_paths(IOState &io) {
    io.device_paths.savedata0 = "user/" + io.user_id + "/savedata/" + io.savedata;
    io.device_paths.app0 = "app/" + io.app_path;
    io.device_paths.addcont0 = "addcont/" + io.addcont;
}

bool init_savedata_app_path(IOState &io, const fs::path &pref_path) {
    const fs::path user_id_path{ pref_path / (+VitaIoDevice::ux0)._to_string() / "user" / io.user_id };
    const fs::path savedata_path{ user_id_path / "savedata" };
    const fs::path savedata_game_path{ savedata_path / io.savedata };

    fs::create_directories(user_id_path);
    fs::create_directories(savedata_path);
    fs::create_directories(savedata_game_path);

    return true;
}

bool find_case_isens_path(IOState &io, VitaIoDevice &device, const fs::path &translated_path, const fs::path &system_path) {
    std::string final_path{};

    switch (device) {
    case +VitaIoDevice::app0: {
        std::string app_id = translated_path.string().substr(0, 14);
        final_path = system_path.string().substr(0, system_path.string().find(app_id)) + app_id;
        break;
    }
    case +VitaIoDevice::addcont0: {
        std::string addcont_id = translated_path.string().substr(0, 18);
        final_path = system_path.string().substr(0, system_path.string().find(addcont_id)) + addcont_id;
        break;
    }
    case +VitaIoDevice::vs0: {
        // This only works if ALL the parent folders of the path are the correct case or are in a case insensitive fs
        // Only the file's name is searched for, not the parent folders
        final_path = system_path.string().substr(0, system_path.string().find_last_of('/'));
        break;
    }
    default: {
        return false;
    }
    }

    if (!fs::exists(final_path))
        return false;

    for (const auto &file : fs::recursive_directory_iterator(final_path)) {
        io.cachemap.emplace(string_utils::tolower(file.path().string()), file.path().string());
    }

    return true;
}

fs::path find_in_cache(IOState &io, const std::string &system_path) {
    const auto find_path = io.cachemap.find(system_path);

    if (find_path != io.cachemap.end()) {
        return fs::path{ find_path->second.c_str() };
    } else {
        return fs::path{};
    }
}

static bool is_current_app_path(const IOState &io, const VitaIoDevice device, const fs::path &translated_path) {
    if ((io.app0_host_path.empty() && !vfs::current_app_archive_mounted(io)) || device != VitaIoDevice::ux0)
        return false;

    const auto translated = translated_path.generic_path().string();
    const auto app0 = fs::path(io.device_paths.app0).generic_path().string();
    return translated == app0 || translated.starts_with(app0 + "/");
}

static std::optional<fs::path> current_app_relative_path(const IOState &io, const VitaIoDevice device, const fs::path &translated_path) {
    if (!is_current_app_path(io, device, translated_path))
        return std::nullopt;

    const auto translated = translated_path.generic_path().string();
    const auto app0 = fs::path(io.device_paths.app0).generic_path().string();
    if (translated == app0)
        return fs::path{};

    return fs::path(translated.substr(app0.size() + 1)).generic_path();
}

static fs::path construct_io_path(const IOState &io, const VitaIoDevice device, const fs::path &translated_path, const fs::path &pref_path) {
    if (is_current_app_path(io, device, translated_path)) {
        const auto translated = translated_path.generic_path().string();
        const auto app0 = fs::path(io.device_paths.app0).generic_path().string();
        if (vfs::current_app_archive_mounted(io))
            return fs::path(io.app0_archive.archive_path.generic_string() + "#" + translated.substr(std::min(translated.size(), app0.size() + 1))).generic_path();

        if (translated == app0)
            return io.app0_host_path.generic_path();

        return (io.app0_host_path / translated.substr(app0.size() + 1)).generic_path();
    }

    return device::construct_emulated_path(device, translated_path, pref_path, io.redirect_stdio);
}

static void fill_virtual_stat(SceIoStat *statp, bool directory, SceOff size);

std::string translate_path(const char *path, VitaIoDevice &device, const IOState::DevicePaths &device_paths) {
    auto relative_path = device::remove_duplicate_device(path, device);

    // replace invalid slashes with proper forward slash
    string_utils::replace(relative_path, "\\", "/");
    string_utils::replace(relative_path, "/./", "/");
    string_utils::replace(relative_path, "//", "/");
    // TODO: Handle dot-dot paths

    switch (device) {
    case +VitaIoDevice::savedata0: // Redirect savedata0: to ux0:user/00/savedata/<title_id>
    case +VitaIoDevice::savedata1: {
        relative_path = device::remove_device_from_path(relative_path, device, device_paths.savedata0);
        device = VitaIoDevice::ux0;
        break;
    }
    case +VitaIoDevice::app0: { // Redirect app0: to ux0:app/<title_id>
        relative_path = device::remove_device_from_path(relative_path, device, device_paths.app0);
        device = VitaIoDevice::ux0;
        break;
    }
    case +VitaIoDevice::addcont0: { // Redirect addcont0: to ux0:addcont/<title_id>
        relative_path = device::remove_device_from_path(relative_path, device, device_paths.addcont0);
        device = VitaIoDevice::ux0;
        break;
    }
    case +VitaIoDevice::music0: { // Redirect music0: to ux0:music
        relative_path = device::remove_device_from_path(relative_path, device, "music");
        device = VitaIoDevice::ux0;
        break;
    }
    case +VitaIoDevice::photo0: { // Redirect photo0: to ux0:picture
        relative_path = device::remove_device_from_path(relative_path, device, "picture");
        device = VitaIoDevice::ux0;
        break;
    }
    case +VitaIoDevice::video0: { // Redirect video0: to ux0:video
        relative_path = device::remove_device_from_path(relative_path, device, "video");
        device = VitaIoDevice::ux0;
        break;
    }

    case +VitaIoDevice::host0:
    case +VitaIoDevice::gro0:
    case +VitaIoDevice::grw0:
    case +VitaIoDevice::imc0:
    case +VitaIoDevice::os0:
    case +VitaIoDevice::pd0:
    case +VitaIoDevice::sa0:
    case +VitaIoDevice::sd0:
    case +VitaIoDevice::tm0:
    case +VitaIoDevice::ud0:
    case +VitaIoDevice::uma0:
    case +VitaIoDevice::ur0:
    case +VitaIoDevice::ux0:
    case +VitaIoDevice::vd0:
    case +VitaIoDevice::vs0:
    case +VitaIoDevice::xmc0: {
        relative_path = device::remove_device_from_path(relative_path, device);
        break;
    }
    case +VitaIoDevice::tty0:
    case +VitaIoDevice::tty1:
    case +VitaIoDevice::tty2:
    case +VitaIoDevice::tty3: {
        return std::string{};
    }
    default: {
        LOG_CRITICAL_IF(relative_path.find(':') != std::string::npos, "Unknown device with path {} used. Report this to the developers!", relative_path);
        return std::string{};
    }
    }

    // If the path is empty, the request is the device itself
    if (relative_path.empty())
        return std::string{};

    if (relative_path.front() == '/' || relative_path.front() == '\\')
        relative_path.erase(0, 1);

    return relative_path;
}

fs::path expand_path(IOState &io, const char *path, const fs::path &pref_path) {
    auto device = device::get_device(path);

    const auto translated_path = translate_path(path, device, io.device_paths);
    return construct_io_path(io, device, translated_path, pref_path).string();
}

SceUID open_file(IOState &io, const char *path, const int flags, const fs::path &pref_path, const char *export_name) {
    auto device = device::get_device(path);
    auto device_for_icase = device;
    if (device == VitaIoDevice::_INVALID) {
        LOG_ERROR("Cannot find device for path: {}", path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    if ((device == VitaIoDevice::tty0) || (device == VitaIoDevice::tty1) || (device == VitaIoDevice::tty2) || (device == VitaIoDevice::tty3)) {
        assert(flags >= 0);

        auto tty_type = TTY_UNKNOWN;
        if (flags & SCE_O_RDONLY)
            tty_type |= TTY_IN;
        if (flags & SCE_O_WRONLY)
            tty_type |= TTY_OUT;

        const auto fd = io.next_fd++;
        io.tty_files.emplace(fd, tty_type);

        LOG_TRACE_IF(log_file_op, "{}: Opening terminal {}:", export_name, device._to_string());
        return fd;
    }

    const auto translated_path = translate_path(path, device, io.device_paths);
    if (translated_path.empty()) {
        LOG_ERROR("Cannot translate path: {}", path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    if (is_current_app_path(io, device, translated_path) && can_write(flags)) {
        LOG_ERROR("Cannot open read-only cartridge path for writing: {}", path);
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);
    }

    const auto app_relative = current_app_relative_path(io, device, translated_path);
    if (app_relative.has_value() && vfs::current_app_archive_mounted(io)) {
        const auto archive_relative = vfs::normalize_archive_path(*app_relative);
        const auto archive_entry = vfs::find_archive_entry(io.app0_archive, archive_relative);
        if (!archive_entry || archive_entry->directory) {
            if (vfs::current_app_directory_exists(io, *app_relative))
                LOG_ERROR("Cannot open archive directory as file: {}", path);
            else
                LOG_ERROR("Missing file in archive app0: {} ({})", path, app_relative->generic_string());
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }

        const auto normalized_path = device::construct_normalized_path(device, translated_path);
        if (archive_entry->size > archive_memory_file_limit) {
            const auto cache_path = vfs::archive_cache_path(pref_path, io, archive_relative);
            if (cache_path.empty() || !vfs::extract_archive_file_to_cache(io.app0_archive, *archive_entry, cache_path))
                return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);

            FileStats f{ path, normalized_path, cache_path, flags };
            const auto fd = io.next_fd++;
            io.std_files.emplace(fd, f);

            LOG_TRACE_IF(log_file_op, "{}: Opening cached archive file {} ({}) from {}, fd: {}", export_name, path, normalized_path, cache_path, log_hex(fd));
            return fd;
        }

        vfs::FileBuffer buffer;
        if (!vfs::read_current_app_file(buffer, io, pref_path, *app_relative))
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);

        FileStats f{ path, normalized_path, construct_io_path(io, device, translated_path, pref_path), flags, buffer };
        const auto fd = io.next_fd++;
        io.std_files.emplace(fd, f);

        LOG_TRACE_IF(log_file_op, "{}: Opening archive file {} ({}), fd: {}", export_name, path, normalized_path, log_hex(fd));
        return fd;
    }

    auto system_path = construct_io_path(io, device, translated_path, pref_path);
    if (fs::is_directory(system_path)) {
        LOG_ERROR("Cannot open directory: {}", system_path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    // Do not allow any new files if they do not have a write flag.
    if (!fs::exists(system_path)) {
        if (!(flags & SCE_O_CREAT)) {
            if (io.case_isens_find_enabled) {
                // Attempt a case-insensitive file search.
                const auto original_system_path = system_path;
                const auto cached_path = find_in_cache(io, string_utils::tolower(system_path.string()));
                if (!cached_path.empty()) {
                    system_path = cached_path;
                    LOG_TRACE("Found cached filepath at {}", system_path);
                } else {
                    const bool path_found = find_case_isens_path(io, device_for_icase, translated_path, system_path);
                    system_path = find_in_cache(io, string_utils::tolower(system_path.string()));
                    if (!system_path.empty() && path_found) {
                        LOG_TRACE("Found file on case-sensitive filesystem at {}", system_path);
                    } else {
                        LOG_ERROR("Missing file at {} (target path: {})", original_system_path, path);
                        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
                    }
                }
            } else {
                LOG_ERROR("Missing file at {} (target path: {})", system_path, path);
                return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
            }
        } else {
            if (!fs::exists(system_path.parent_path())) {
                fs::create_directories(system_path.parent_path());
            }
            fs::ofstream file(system_path);
        }
    }

    const auto normalized_path = device::construct_normalized_path(device, translated_path);

    FileStats f{ path, normalized_path, system_path, flags };
    const auto fd = io.next_fd++;
    io.std_files.emplace(fd, f);

    LOG_TRACE_IF(log_file_op, "{}: Opening file {} ({}), fd: {}", export_name, path, normalized_path, log_hex(fd));
    return fd;
}

int read_file(void *data, IOState &io, const SceUID fd, const SceSize size, const char *export_name) {
    assert(data != nullptr);
    assert(size >= 0);

    const auto file = io.std_files.find(fd);
    if (file != io.std_files.end()) {
        const auto read = file->second.read(data, 1, size);
        LOG_TRACE_IF(log_file_op && log_file_read, "{}: Reading {} bytes of fd {}", export_name, read, log_hex(fd));
        return static_cast<int>(read);
    }

    const auto tty_file = io.tty_files.find(fd);
    if (tty_file != io.tty_files.end()) {
        if (tty_file->second == TTY_IN) {
            std::cin.read(static_cast<char *>(data), size);
            LOG_TRACE_IF(log_file_op && log_file_read, "{}: Reading terminal fd: {}, size: {}", export_name, log_hex(fd), size);
            return size;
        }
        return IO_ERROR_UNK();
    }

    return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
}

int write_file(SceUID fd, const void *data, const SceSize size, const IOState &io, const char *export_name) {
    assert(data != nullptr);
    assert(size >= 0);

    if (fd < 0) {
        LOG_WARN("Error writing fd: {}, size: {}", log_hex(fd), size);
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
    }

    const auto tty_file = io.tty_files.find(fd);
    if (tty_file != io.tty_files.end()) {
        if (tty_file->second & TTY_OUT) {
            std::string s(static_cast<char const *>(data), size);

            // trim newline
            if (io.redirect_stdio) {
                std::cout << s;
            } else {
                if (s.back() == '\n')
                    s.pop_back();
                LOG_TRACE_IF(log_file_op, "*** TTY: {}", s);
            }

            return size;
        }
        return IO_ERROR_UNK();
    }

    const auto file = io.std_files.find(fd);
    if (file == io.std_files.end())
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

    if (!fs::is_directory(file->second.get_system_location().parent_path())) {
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT); // TODO: Is it the right error code?
    }

    if (file->second.can_write_file()) {
        const auto written = file->second.write(data, 1, size);
        LOG_TRACE_IF(log_file_op, "{}: Writing to fd: {}, size: {}", export_name, log_hex(fd), size);
        return static_cast<int>(written);
    }

    return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
}

int truncate_file(const SceUID fd, unsigned long long length, const IOState &io, const char *export_name) {
    if (fd < 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

    const auto file = io.std_files.find(fd);
    if (file == io.std_files.end())
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
    auto trunc = file->second.truncate(length);
    LOG_TRACE_IF(log_file_op, "{}: Truncating fd: {}, to size: {}", export_name, log_hex(fd), length);
    return trunc;
}

SceOff seek_file(const SceUID fd, const SceOff offset, const SceIoSeekMode whence, IOState &io, const char *export_name) {
    if (!(whence == SCE_SEEK_SET || whence == SCE_SEEK_CUR || whence == SCE_SEEK_END))
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);

    if (fd < 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

    const auto file = io.std_files.find(fd);
    if (file == io.std_files.end())
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
    if (!file->second.seek(offset, whence))
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

    const auto log_mode = [](const SceIoSeekMode whence) -> const char * {
        switch (whence) {
            STR_CASE(SCE_SEEK_SET);
            STR_CASE(SCE_SEEK_CUR);
            STR_CASE(SCE_SEEK_END);
        default:
            return "INVALID";
        }
    };

    LOG_TRACE_IF(log_file_op && log_file_seek, "{}: Seeking fd: {}, offset: {}, whence: {}", export_name, log_hex(fd), log_hex(offset), log_mode(whence));
    return file->second.tell();
}

SceOff tell_file(IOState &io, const SceUID fd, const char *export_name) {
    if (fd < 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EMFILE);

    const auto std_file = io.std_files.find(fd);

    if (std_file == io.std_files.end()) {
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
    }

    return std_file->second.tell();
}

int stat_file(IOState &io, const char *file, SceIoStat *statp, const fs::path &pref_path, const char *export_name, const SceUID fd) {
    assert(statp != nullptr);

    memset(statp, '\0', sizeof(SceIoStat));

    fs::path file_path = "";
    if (fd == invalid_fd) {
        auto device = device::get_device(file);
        auto device_for_icase = device;
        if (device == VitaIoDevice::_INVALID) {
            LOG_ERROR("Cannot find device for path: {}", file);
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }

        const auto translated_path = translate_path(file, device, io.device_paths);
        const auto app_relative = current_app_relative_path(io, device, translated_path);
        if (app_relative.has_value() && vfs::current_app_archive_mounted(io)) {
            if (vfs::current_app_file_exists(io, *app_relative)) {
                fill_virtual_stat(statp, false, vfs::current_app_file_size(io, *app_relative));
                LOG_TRACE_IF(log_file_op && log_file_stat, "{}: Statting archive file: {} ({})", export_name, file, device::construct_normalized_path(device, translated_path));
                return 0;
            }

            if (vfs::current_app_directory_exists(io, *app_relative)) {
                fill_virtual_stat(statp, true, 0);
                LOG_TRACE_IF(log_file_op && log_file_stat, "{}: Statting archive directory: {} ({})", export_name, file, device::construct_normalized_path(device, translated_path));
                return 0;
            }

            LOG_ERROR("Missing archive path {} (target path: {})", app_relative->generic_string(), file);
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }

        file_path = construct_io_path(io, device, translated_path, pref_path);

        if (!fs::exists(file_path)) {
            if (io.case_isens_find_enabled) {
                // Attempt a case-insensitive file search.
                const auto original_file_path = file_path;
                const auto cached_path = find_in_cache(io, string_utils::tolower(file_path.string()));
                if (!cached_path.empty()) {
                    file_path = cached_path;
                    LOG_TRACE("Found cached filepath at {}", file_path);
                } else {
                    const bool path_found = find_case_isens_path(io, device_for_icase, translated_path, file_path);
                    file_path = find_in_cache(io, string_utils::tolower(file_path.string()));
                    if (!file_path.empty() && path_found) {
                        LOG_TRACE("Found file on case-sensitive filesystem at {}", file_path);
                    } else {
                        LOG_ERROR("Missing file at {} (target path: {})", original_file_path, file);
                        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
                    }
                }
            } else {
                LOG_ERROR("Missing file at {} (target path: {})", file_path, file);
                return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
            }
        }
        LOG_TRACE_IF(log_file_op && log_file_stat, "{}: Statting file: {} ({})", export_name, file, device::construct_normalized_path(device, translated_path));
    } else { // We have previously opened and defined the location
        const auto fd_file = io.std_files.find(fd);
        if (fd_file == io.std_files.end())
            return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

        if (fd_file->second.is_memory_file()) {
            fill_virtual_stat(statp, false, fd_file->second.size());
            LOG_TRACE_IF(log_file_op && log_file_stat, "{}: Statting archive fd: {}", export_name, log_hex(fd));
            return 0;
        }

        file_path = fd_file->second.get_system_location();
        LOG_TRACE_IF(log_file_op && log_file_stat, "{}: Statting fd: {}", export_name, log_hex(fd));

        statp->st_attr = fd_file->second.get_file_mode();
    }

    std::uint64_t last_access_time_ticks;
    std::uint64_t creation_time_ticks;
    std::uint64_t last_modification_time_ticks;

#ifdef _WIN32
    struct _stati64 sb;
    if (_wstati64(file_path.generic_path().wstring().c_str(), &sb) < 0)
        return IO_ERROR_UNK();
#else
    struct stat64 sb;
    if (stat64(file_path.generic_path().string().c_str(), &sb) < 0)
        return IO_ERROR_UNK();
#endif

    last_access_time_ticks = RTC_OFFSET + (uint64_t)sb.st_atime * VITA_CLOCKS_PER_SEC;
    creation_time_ticks = RTC_OFFSET + (uint64_t)sb.st_ctime * VITA_CLOCKS_PER_SEC;
    last_modification_time_ticks = RTC_OFFSET + (uint64_t)sb.st_mtime * VITA_CLOCKS_PER_SEC;

#ifndef _WIN32
#undef st_atime
#undef st_mtime
#undef st_ctime
#endif

    statp->st_mode = SCE_S_IRUSR | SCE_S_IRGRP | SCE_S_IROTH | SCE_S_IXUSR | SCE_S_IXGRP | SCE_S_IXOTH;

    if (fs::is_regular_file(file_path)) {
        statp->st_size = fs::file_size(file_path);
        statp->st_attr = SCE_SO_IFREG;
        statp->st_mode |= SCE_S_IFREG;
    }
    if (fs::is_directory(file_path)) {
        statp->st_attr = SCE_SO_IFDIR;
        statp->st_mode |= SCE_S_IFDIR;
    }

    __RtcTicksToPspTime(&statp->st_atime, last_access_time_ticks);
    __RtcTicksToPspTime(&statp->st_mtime, last_modification_time_ticks);
    __RtcTicksToPspTime(&statp->st_ctime, creation_time_ticks);

    return 0;
}

static void fill_virtual_stat(SceIoStat *statp, const bool directory, const SceOff size) {
    const auto now_ticks = RTC_OFFSET + static_cast<uint64_t>(std::time(nullptr)) * VITA_CLOCKS_PER_SEC;

    statp->st_mode = SCE_S_IRUSR | SCE_S_IRGRP | SCE_S_IROTH | SCE_S_IXUSR | SCE_S_IXGRP | SCE_S_IXOTH;
    statp->st_size = size;

    if (directory) {
        statp->st_attr = SCE_SO_IFDIR;
        statp->st_mode |= SCE_S_IFDIR;
    } else {
        statp->st_attr = SCE_SO_IFREG;
        statp->st_mode |= SCE_S_IFREG;
    }

    __RtcTicksToPspTime(&statp->st_atime, now_ticks);
    __RtcTicksToPspTime(&statp->st_mtime, now_ticks);
    __RtcTicksToPspTime(&statp->st_ctime, now_ticks);
}

int stat_file_by_fd(IOState &io, const SceUID fd, SceIoStat *statp, const fs::path &pref_path, const char *export_name) {
    assert(statp != nullptr);
    memset(statp, '\0', sizeof(SceIoStat));

    const auto std_file = io.std_files.find(fd);
    if (std_file == io.std_files.end()) {
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
    }

    return stat_file(io, std_file->second.get_vita_loc(), statp, pref_path, export_name, fd);
}

int close_file(IOState &io, const SceUID fd, const char *export_name) {
    if (fd < 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EMFILE);

    LOG_TRACE_IF(log_file_op, "{}: Closing file fd: {}", export_name, log_hex(fd));

    io.tty_files.erase(fd);
    io.std_files.erase(fd);

    return 0;
}

int remove_file(IOState &io, const char *file, const fs::path &pref_path, const char *export_name) {
    auto device = device::get_device(file);
    if (device == VitaIoDevice::_INVALID) {
        LOG_ERROR("Cannot find device for path: {}", file);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto translated_path = translate_path(file, device, io.device_paths);
    if (translated_path.empty()) {
        LOG_ERROR("Cannot translate path: {}", translated_path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    if (is_current_app_path(io, device, translated_path)) {
        LOG_ERROR("Cannot remove file from read-only cartridge path: {}", file);
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);
    }

    const auto emulated_path = construct_io_path(io, device, translated_path, pref_path);
    if (!fs::exists(emulated_path) || fs::is_directory(emulated_path)) {
        LOG_ERROR("File does not exist at path: {} (target path: {})", emulated_path, file);
    }

    LOG_TRACE_IF(log_file_op, "{}: Removing file {} ({})", export_name, file, device::construct_normalized_path(device, translated_path));

    boost::system::error_code error_code{};
    auto res = fs::detail::remove(emulated_path, &error_code);

    if (!(res && !(error_code.value()))) {
        LOG_ERROR("Cannot remove file: {} ({})", file, device::construct_normalized_path(device, translated_path));
        LOG_ERROR("Error code: {} ({})", error_code.value(), error_code.message());
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    return 0;
}

int rename(IOState &io, const char *old_name, const char *new_name, const fs::path &pref_path, const char *export_name) {
    auto device = device::get_device(old_name);
    if (device == VitaIoDevice::_INVALID) {
        LOG_ERROR("Cannot find device for path: {}", old_name);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto translated_old_path = translate_path(old_name, device, io.device_paths);
    if (translated_old_path.empty()) {
        LOG_ERROR("Cannot translate path: {}", translated_old_path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto translated_new_path = translate_path(new_name, device, io.device_paths);
    if (translated_new_path.empty()) {
        LOG_ERROR("Cannot translate path: {}", translated_new_path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    if (is_current_app_path(io, device, translated_old_path)) {
        LOG_ERROR("Cannot rename file from read-only cartridge path: {}", old_name);
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);
    }

    const auto emulated_old_path = construct_io_path(io, device, translated_old_path, pref_path);
    if (!fs::exists(emulated_old_path)) {
        LOG_ERROR("File does not exist at path: {} (target path: {})", emulated_old_path, old_name);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto emulated_new_path = construct_io_path(io, device, translated_new_path, pref_path);

    LOG_TRACE_IF(log_file_op, "{}: Renaming file {} to {} ({} to {})", export_name, old_name, new_name, emulated_old_path, emulated_new_path);

    boost::system::error_code error_code{};
    fs::rename(emulated_old_path, emulated_new_path, error_code);

    if (error_code.value()) {
        LOG_ERROR("Cannot rename file: {} to {} ({} to {})", old_name, new_name, emulated_old_path, emulated_new_path);
        LOG_ERROR("Error code: {} ({})", error_code.value(), error_code.message());
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    return 0;
}

SceUID open_dir(IOState &io, const char *path, const fs::path &pref_path, const char *export_name) {
    auto device = device::get_device(path);
    auto device_for_icase = device;
    const auto translated_path = translate_path(path, device, io.device_paths);

    const auto app_relative = current_app_relative_path(io, device, translated_path);
    if (app_relative.has_value() && vfs::current_app_archive_mounted(io)) {
        if (!vfs::current_app_directory_exists(io, *app_relative)) {
            LOG_ERROR("Archive directory does not exist at app0:{} (target path: {})", app_relative->generic_string(), path);
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }

        const auto normalized = device::construct_normalized_path(device, translated_path);
        const DirStats d{ path, normalized, construct_io_path(io, device, translated_path, pref_path), vfs::list_current_app_directory(io, *app_relative) };
        const auto fd = io.next_fd++;
        io.dir_entries.emplace(fd, d);

        LOG_TRACE_IF(log_file_op, "{}: Opening archive dir {} ({}), fd: {}", export_name, path, normalized, log_hex(fd));
        return fd;
    }

    auto dir_path = construct_io_path(io, device, translated_path, pref_path) / "";
    if (!fs::exists(dir_path)) {
        if (io.case_isens_find_enabled) {
            // Attempt a case-insensitive file search.
            const auto original_dir_path = dir_path;
            const auto cached_path = find_in_cache(io, string_utils::tolower(dir_path.string()));
            if (!cached_path.empty()) {
                dir_path = cached_path;
                LOG_TRACE("Found cached directory path at {}", dir_path);
            } else {
                const bool path_found = find_case_isens_path(io, device_for_icase, translated_path, dir_path);
                dir_path = find_in_cache(io, string_utils::tolower(dir_path.string().substr(0, dir_path.string().size() - 1)));
                if (!dir_path.empty() && path_found) {
                    LOG_TRACE("Found directory on case-sensitive filesystem at {}", dir_path);
                } else {
                    LOG_ERROR("Directory does not exist at {} (target path: {})", original_dir_path, path);
                    return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
                }
            }
        } else {
            LOG_ERROR("Directory does not exist at: {} (target path: {})", dir_path, path);
            return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }
    }

    const DirPtr opened = create_shared_dir(dir_path);
    if (!opened) {
        LOG_ERROR("Failed to open directory at: {} (target path: {})", dir_path, path);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto normalized = device::construct_normalized_path(device, translated_path);
    const DirStats d{ path, normalized, dir_path, opened };
    const auto fd = io.next_fd++;
    io.dir_entries.emplace(fd, d);

    LOG_TRACE_IF(log_file_op, "{}: Opening dir {} ({}), fd: {}", export_name, path, normalized, log_hex(fd));

    return fd;
}

SceUID read_dir(IOState &io, const SceUID fd, SceIoDirent *dent, const fs::path &pref_path, const char *export_name) {
    assert(dent != nullptr);

    memset(dent->d_name, '\0', sizeof(dent->d_name));

    const auto dir = io.dir_entries.find(fd);

    if (dir != io.dir_entries.end()) {
        // Refuse any fd that is not explicitly a directory
        if (!dir->second.is_directory())
            return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

        if (dir->second.is_memory_directory()) {
            const auto d_name_utf8 = dir->second.get_next_memory_entry();
            if (d_name_utf8.empty())
                return 0;

            strncpy(dent->d_name, d_name_utf8.c_str(), sizeof(dent->d_name));
            const auto file_path = std::string(dir->second.get_vita_loc()) + '/' + d_name_utf8;

            LOG_TRACE_IF(log_file_op, "{}: Reading archive entry {} of fd: {}", export_name, file_path, log_hex(fd));
            if (stat_file(io, file_path.c_str(), &dent->d_stat, pref_path, export_name) < 0)
                return IO_ERROR(SCE_ERROR_ERRNO_EMFILE);

            return 1;
        }

        const auto d = dir->second.get_dir_ptr();
        if (!d)
            return 0;

        const auto d_name_utf8 = get_file_in_dir(d);
        strncpy(dent->d_name, d_name_utf8.c_str(), sizeof(dent->d_name));

        const auto cur_path = dir->second.get_system_location() / d_name_utf8;
        if (!(cur_path.filename_is_dot() || cur_path.filename_is_dot_dot())) {
            const auto file_path = std::string(dir->second.get_vita_loc()) + '/' + d_name_utf8;

            LOG_TRACE_IF(log_file_op, "{}: Reading entry {} of fd: {}", export_name, file_path, log_hex(fd));
            if (stat_file(io, file_path.c_str(), &dent->d_stat, pref_path, export_name) < 0)
                return IO_ERROR(SCE_ERROR_ERRNO_EMFILE);
            else
                return 1; // move to the next file
        }
        return read_dir(io, fd, dent, pref_path, export_name);
    }

    return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);
}

bool copy_directories(const fs::path &src_path, const fs::path &dst_path) {
    try {
        fs::create_directories(dst_path);

        for (const auto &src : fs::recursive_directory_iterator(src_path)) {
            const auto dst_parent_path = dst_path / fs::relative(src, src_path).parent_path();
            const auto dst_path = dst_parent_path / src.path().filename();

            LOG_INFO("Copy {}", dst_path);

            if (fs::is_regular_file(src))
                fs::copy_file(src, dst_path, fs::copy_options::overwrite_existing);
            else
                fs::create_directories(dst_path);
        }

        return true;
    } catch (std::exception &e) {
        std::cout << e.what();
        return false;
    }
}

bool copy_path(const fs::path &src_path, const fs::path &pref_path, const std::string &app_title_id, const std::string &app_category) {
    // Check if is path
    if (app_category.find("gp") != std::string::npos) {
        const auto app_path{ pref_path / "ux0/app" / app_title_id };
        const auto result = copy_directories(src_path, app_path);

        fs::remove_all(src_path);

        return result;
    }

    return true;
}

int create_dir(IOState &io, const char *dir, int mode, const fs::path &pref_path, const char *export_name, const bool recursive) {
    auto device = device::get_device(dir);
    const auto translated_path = translate_path(dir, device, io.device_paths);
    if (translated_path.empty()) {
        LOG_ERROR("Failed to translate path: {}", dir);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    if (is_current_app_path(io, device, translated_path)) {
        LOG_ERROR("Cannot create directory in read-only cartridge path: {}", dir);
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);
    }

    const auto emulated_path = construct_io_path(io, device, translated_path, pref_path);
    if (recursive)
        return fs::create_directories(emulated_path);
    if (fs::exists(emulated_path))
        return IO_ERROR(SCE_ERROR_ERRNO_EEXIST);

    const auto parent_path = fs::path(emulated_path).remove_trailing_separator().parent_path();
    if (!fs::exists(parent_path)) // Vita cannot recursively create directories
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);

    LOG_TRACE_IF(log_file_op, "{}: Creating new dir {} ({})", export_name, dir, device::construct_normalized_path(device, translated_path));

    if (!fs::create_directory(emulated_path)) {
        LOG_ERROR("Failed to create directory at {} (target path: {})", emulated_path, dir);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    return 0;
}

int close_dir(IOState &io, const SceUID fd, const char *export_name) {
    if (fd < 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EMFILE);

    const auto erased_entries = io.dir_entries.erase(fd);

    LOG_TRACE_IF(log_file_op, "{}: Closing dir fd: {}", export_name, log_hex(fd));

    if (erased_entries == 0)
        return IO_ERROR(SCE_ERROR_ERRNO_EBADFD);

    return 0;
}

int remove_dir(IOState &io, const char *dir, const fs::path &pref_path, const char *export_name) {
    auto device = device::get_device(dir);
    if (device == VitaIoDevice::_INVALID) {
        LOG_ERROR("Cannot find device for path: {}", dir);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    const auto translated_path = translate_path(dir, device, io.device_paths);
    if (translated_path.empty()) {
        LOG_ERROR("Cannot translate path: {}", dir);
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    LOG_TRACE_IF(log_file_op, "{}: Removing dir {} ({})", export_name, dir, device::construct_normalized_path(device, translated_path));

    if (is_current_app_path(io, device, translated_path)) {
        LOG_ERROR("Cannot remove directory from read-only cartridge path: {}", dir);
        return IO_ERROR(SCE_ERROR_ERRNO_EOPNOTSUPP);
    }

    if (!fs::remove_all(construct_io_path(io, device, translated_path, pref_path))) {
        LOG_ERROR("Cannot remove dir: {} ({})", dir, device::construct_normalized_path(device, translated_path));
        return IO_ERROR(SCE_ERROR_ERRNO_ENOENT);
    }

    return 0;
}

static std::string standardize_path(std::string_view path) {
    // replace app0:... by app0:/...
    bool start_with_app0 = path.starts_with("app0:");
    if (start_with_app0 && path.size() >= 6 && path[5] != '/')
        return "app0:/" + std::string(path.substr(5));
    else
        return std::string(path);
}

SceUID create_overlay(IOState &io, SceFiosProcessOverlay *fios_overlay) {
    std::lock_guard<std::mutex> lock(io.overlay_mutex);

    FiosOverlay overlay{
        .id = io.next_overlay_id++,
        .type = fios_overlay->type,
        .order = fios_overlay->order,
        .process_id = fios_overlay->process_id,
        .dst = standardize_path(fios_overlay->dst),
        .src = standardize_path(fios_overlay->src)
    };

    // find location where to put it
    size_t overlay_index = 0;
    // lower order first and in case of equality, last one inserted first
    while (overlay_index < io.overlays.size() && overlay.order < io.overlays[overlay_index].order)
        overlay_index++;
    auto res = overlay.id;
    io.overlays.insert(io.overlays.begin() + overlay_index, std::move(overlay));

    return res;
}

std::string resolve_path(IOState &io, const char *input, const SceUInt32 min_order, const SceUInt32 max_order) {
    std::lock_guard<std::mutex> lock(io.overlay_mutex);

    std::string curr_path = input;

    size_t overlay_idx = 0;
    while (overlay_idx < io.overlays.size() && io.overlays[overlay_idx].order < min_order)
        overlay_idx++;

    while (overlay_idx < io.overlays.size()) {
        const FiosOverlay &overlay = io.overlays[overlay_idx];
        overlay_idx++;

        if (overlay.order > max_order)
            break;

        if (!curr_path.starts_with(overlay.dst))
            continue;

        // replace dst with src
        curr_path = overlay.src + curr_path.substr(overlay.dst.size());
    }

    return curr_path;
}

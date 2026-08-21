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

// Thor: virtual-cartridge discovery.
//
// Scans the configured roots (plus the usual Android storage locations) for
// .zip/.vpk archives and extracted folders, and lists them in the app grid
// without installing anything into ux0:app. Lifted out of Thor's ImGui
// frontend so both the Qt desktop UI and the Android UI can use it.

#include <app/virtual_cartridge.h>

#include <app/state.h>
#include <config/state.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <packages/sfo.h>
#include <util/cheat_paths.h>
#include <util/fs.h>
#include <util/log.h>
#include <util/string_utils.h>
#include <util/vector_utils.h>

#include <miniz.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace app {

static size_t write_archive_to_buffer(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    vfs::FileBuffer *const buffer = static_cast<vfs::FileBuffer *>(pOpaque);
    assert(file_ofs == buffer->size());
    const uint8_t *const first = static_cast<const uint8_t *>(pBuf);
    const uint8_t *const last = &first[n];
    buffer->insert(buffer->end(), first, last);

    return n;
}

// Forward declarations: the scanner and its helpers call each other freely.
static bool archive_file_exists_case_insensitive(mz_zip_archive &zip, const std::string &path);
static bool archive_root_title_id_like(const std::string &root);
static bool buffer_looks_encrypted_executable(const vfs::FileBuffer &buffer);
static bool buffer_looks_encrypted_icon(const vfs::FileBuffer &buffer);
static bool buffer_starts_with(const vfs::FileBuffer &buffer, const std::initializer_list<uint8_t> prefix);
static bool extract_archive_file_to_buffer(mz_zip_archive &zip, const std::string &path, vfs::FileBuffer &buffer);
static bool has_cheats_for_title(const EmuEnvState &emuenv, const std::string &title_id);
static bool is_game_card_category(const std::string &category);
static bool is_png_buffer(const vfs::FileBuffer &buffer);
static bool is_vita_executable_buffer(const vfs::FileBuffer &buffer);
static bool source_name_starts_with_title_id(const AppEntry &app);
static bool virtual_cartridge_archive_appears_encrypted(mz_zip_archive &zip, const std::string &root);
static bool virtual_cartridge_candidate_is_better(const AppEntry &candidate, const AppEntry &current);
static bool virtual_cartridge_directory_appears_encrypted(const fs::path &content_path);
static bool virtual_cartridge_source_unchanged(const AppEntry &app);
static fs::path virtual_cartridge_stamp_path(const fs::path &source_path);
static int64_t virtual_cartridge_source_mtime(const fs::path &source_path);
static std::optional<AppEntry> app_from_cartridge_archive(EmuEnvState &emuenv, const fs::path &archive_path);
static std::optional<AppEntry> app_from_cartridge_directory(EmuEnvState &emuenv, const fs::path &content_path);
static std::optional<AppEntry> app_from_param(const EmuEnvState &emuenv, const vfs::FileBuffer &param_sfo, const fs::path &source_path, const std::string &source_root);
static std::optional<std::string> find_archive_file_case_insensitive(mz_zip_archive &zip, const std::string &path);
static std::string normalize_archive_member_name(std::string path);
static std::string virtual_cartridge_source_key(const fs::path &source_path);
static std::string virtual_cartridge_title_key(const AppEntry &app);
static std::vector<std::string> get_archive_content_roots_for_scan(mz_zip_archive &zip);
static uint64_t virtual_cartridge_source_size(const fs::path &source_path);
static void add_virtual_cartridge_candidate(std::map<std::string, AppEntry> &candidates, const AppEntry &app);
static void refresh_cheat_badges(std::vector<AppEntry> &apps, EmuEnvState &emuenv);


static bool archive_file_exists_case_insensitive(mz_zip_archive &zip, const std::string &path) {
    return find_archive_file_case_insensitive(zip, path).has_value();
}

static std::optional<std::string> find_archive_file_case_insensitive(mz_zip_archive &zip, const std::string &path) {
    const auto normalized_path = string_utils::tolower(normalize_archive_member_name(path));
    const mz_uint num_files = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat) || mz_zip_reader_is_file_a_directory(&zip, i))
            continue;

        if (string_utils::tolower(normalize_archive_member_name(file_stat.m_filename)) == normalized_path)
            return std::string(file_stat.m_filename);
    }

    return std::nullopt;
}

static std::string normalize_archive_member_name(std::string path) {
    string_utils::replace(path, "\\", "/");

    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (path.starts_with("./"))
        path.erase(0, 2);
    while (!path.empty() && path.back() == '/')
        path.pop_back();

    return path;
}


static bool buffer_looks_encrypted_executable(const vfs::FileBuffer &buffer) {
    return !buffer.empty() && !is_vita_executable_buffer(buffer);
}

static bool is_vita_executable_buffer(const vfs::FileBuffer &buffer) {
    return buffer_starts_with(buffer, { 'S', 'C', 'E', 0x00 }) || buffer_starts_with(buffer, { 0x7F, 'E', 'L', 'F' });
}

static bool buffer_starts_with(const vfs::FileBuffer &buffer, const std::initializer_list<uint8_t> prefix) {
    if (buffer.size() < prefix.size())
        return false;

    return std::equal(prefix.begin(), prefix.end(), buffer.begin());
}

static bool archive_root_title_id_like(const std::string &root) {
    auto normalized_root = normalize_archive_member_name(root);
    const auto slash = normalized_root.find_last_of('/');
    const auto title_id = string_utils::tolower(slash == std::string::npos ? normalized_root : normalized_root.substr(slash + 1));
    if (title_id.size() != 9 || !title_id.starts_with("pcs"))
        return false;

    for (size_t i = 4; i < title_id.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(title_id[i])))
            return false;
    }

    return title_id[3] >= 'a' && title_id[3] <= 'h';
}

static bool buffer_looks_encrypted_icon(const vfs::FileBuffer &buffer) {
    return !buffer.empty() && !is_png_buffer(buffer);
}

static bool is_png_buffer(const vfs::FileBuffer &buffer) {
    return buffer_starts_with(buffer, { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A });
}

// Helpers carried over with the scanner from Thor's ImGui frontend.
static bool is_game_card_category(const std::string &category) {
    return category.find("gd") != std::string::npos || category.find("gc") != std::string::npos;
}

static bool has_cheats_for_title(const EmuEnvState &emuenv, const std::string &title_id) {
    return cheat_paths::has_vitacheat_file(emuenv.vita_fs_path, emuenv.shared_path, emuenv.vita_fs_path, title_id);
}

static std::vector<std::string> get_archive_content_roots_for_scan(mz_zip_archive &zip) {
    std::map<std::string, int> candidate_scores;
    constexpr std::string_view sfo_path = "sce_sys/param.sfo";
    constexpr std::string_view eboot_path = "eboot.bin";
    const mz_uint num_files = mz_zip_reader_get_num_files(&zip);

    const auto add_candidate = [&](std::string root, const int score) {
        root = normalize_archive_member_name(std::move(root));
        if (!root.empty())
            root += "/";
        candidate_scores[root] += score;
    };

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat))
            continue;

        const std::string normalized_filename = normalize_archive_member_name(file_stat.m_filename);
        const std::string normalized_lower = string_utils::tolower(normalized_filename);
        const auto sfo_pos = normalized_lower.rfind(sfo_path);
        if (sfo_pos != std::string::npos && sfo_pos + sfo_path.size() == normalized_lower.size()) {
            auto root = normalized_filename.substr(0, sfo_pos);
            if (root.ends_with("/"))
                root.pop_back();
            add_candidate(root, 100);
        }

        const auto eboot_pos = normalized_lower.rfind(eboot_path);
        if (eboot_pos != std::string::npos && eboot_pos + eboot_path.size() == normalized_lower.size()) {
            auto root = normalized_filename.substr(0, eboot_pos);
            if (root.ends_with("/"))
                root.pop_back();
            add_candidate(root, 50);
        }
    }

    std::vector<std::pair<std::string, int>> candidates;
    candidates.reserve(candidate_scores.size());
    for (auto &[root, score] : candidate_scores) {
        const auto lower_root = string_utils::tolower(root);
        if (archive_file_exists_case_insensitive(zip, root + "eboot.bin"))
            score += 75;
        if (archive_root_title_id_like(root))
            score += 25;
        if (lower_root.find("/app/") != std::string::npos || lower_root.starts_with("app/") || lower_root.find("/ux0/app/") != std::string::npos)
            score += 15;
        if (lower_root.find("/patch/") != std::string::npos || lower_root.starts_with("patch/") || lower_root.find("/repatch/") != std::string::npos || lower_root.starts_with("repatch/"))
            score -= 40;
        candidates.emplace_back(root, score);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.second != rhs.second)
            return lhs.second > rhs.second;
        return lhs.first.size() < rhs.first.size();
    });

    std::vector<std::string> roots;
    roots.reserve(candidates.size());
    for (const auto &candidate : candidates)
        roots.push_back(candidate.first);

    return roots;
}

static bool extract_archive_file_to_buffer(mz_zip_archive &zip, const std::string &path, vfs::FileBuffer &buffer) {
    const auto archive_name = find_archive_file_case_insensitive(zip, path);
    if (!archive_name)
        return false;

    buffer.clear();
    return mz_zip_reader_extract_file_to_callback(&zip, archive_name->c_str(), &write_archive_to_buffer, &buffer, 0);
}

static std::optional<AppEntry> app_from_param(const EmuEnvState &emuenv, const vfs::FileBuffer &param_sfo, const fs::path &source_path, const std::string &source_root) {
    sfo::SfoAppInfo app_info;
    sfo::get_param_info(app_info, param_sfo, emuenv.cfg.sys_lang);
    if (app_info.app_title_id.empty() || !is_game_card_category(app_info.app_category))
        return std::nullopt;

    AppEntry app{
        app_info.app_version,
        app_info.app_category,
        app_info.app_content_id,
        app_info.app_addcont,
        app_info.app_savedata,
        app_info.app_parental_level,
        app_info.app_short_title,
        app_info.app_title,
        app_info.app_title_id,
        fs_utils::path_to_utf8(source_path)
    };
    app.source_path = fs_utils::path_to_utf8(source_path);
    app.source_root = source_root;
    app.source_size = virtual_cartridge_source_size(source_path);
    app.source_mtime = virtual_cartridge_source_mtime(source_path);
    app.virtual_cartridge = true;
    app.cheats_available = has_cheats_for_title(emuenv, app.title_id);
    return app;
}

static fs::path virtual_cartridge_stamp_path(const fs::path &source_path) {
    boost::system::error_code error;
    if (fs::is_directory(source_path, error) && !error)
        return source_path / "sce_sys/param.sfo";

    return source_path;
}

static uint64_t virtual_cartridge_source_size(const fs::path &source_path) {
    boost::system::error_code error;
    const auto stamp_path = virtual_cartridge_stamp_path(source_path);
    if (!fs::is_regular_file(stamp_path, error) || error)
        return 0;

    const auto size = fs::file_size(stamp_path, error);
    return error ? 0 : static_cast<uint64_t>(size);
}

static int64_t virtual_cartridge_source_mtime(const fs::path &source_path) {
    boost::system::error_code error;
    const auto stamp_path = virtual_cartridge_stamp_path(source_path);
    const auto write_time = fs::last_write_time(stamp_path, error);
    return error ? 0 : static_cast<int64_t>(write_time);
}

static bool virtual_cartridge_directory_appears_encrypted(const fs::path &content_path) {
    vfs::FileBuffer buffer;
    if (fs_utils::read_data(content_path / "eboot.bin", buffer) && buffer_looks_encrypted_executable(buffer))
        return true;

    buffer.clear();
    return fs_utils::read_data(content_path / "sce_sys/icon0.png", buffer) && buffer_looks_encrypted_icon(buffer);
}

static bool virtual_cartridge_archive_appears_encrypted(mz_zip_archive &zip, const std::string &root) {
    vfs::FileBuffer buffer;
    if (extract_archive_file_to_buffer(zip, root + "eboot.bin", buffer) && buffer_looks_encrypted_executable(buffer))
        return true;

    buffer.clear();
    return extract_archive_file_to_buffer(zip, root + "sce_sys/icon0.png", buffer) && buffer_looks_encrypted_icon(buffer);
}

static std::string virtual_cartridge_source_key(const fs::path &source_path) {
    return fs_utils::path_to_utf8(source_path.generic_path());
}

static bool virtual_cartridge_source_unchanged(const AppEntry &app) {
    if (!app.virtual_cartridge || app.source_path.empty())
        return false;

    const fs::path source_path = fs_utils::utf8_to_path(app.source_path);
    return app.source_size == virtual_cartridge_source_size(source_path)
        && app.source_mtime == virtual_cartridge_source_mtime(source_path);
}

static std::string virtual_cartridge_title_key(const AppEntry &app) {
    return string_utils::toupper(app.title_id);
}

static bool source_name_starts_with_title_id(const AppEntry &app) {
    const auto source_path = fs_utils::utf8_to_path(app.source_path);
    const auto filename = string_utils::tolower(source_path.filename().generic_string());
    return !app.title_id.empty() && filename.starts_with(string_utils::tolower(app.title_id));
}

static bool virtual_cartridge_candidate_is_better(const AppEntry &candidate, const AppEntry &current) {
    if (!current.virtual_cartridge)
        return true;

    if (candidate.encrypted_content != current.encrypted_content)
        return !candidate.encrypted_content;

    const bool candidate_title_id_name = source_name_starts_with_title_id(candidate);
    const bool current_title_id_name = source_name_starts_with_title_id(current);
    if (candidate_title_id_name != current_title_id_name)
        return !candidate_title_id_name;

    if (candidate.source_size != current.source_size)
        return candidate.source_mtime > current.source_mtime;

    if (candidate.source_path.size() != current.source_path.size())
        return candidate.source_path.size() > current.source_path.size();

    return candidate.source_path < current.source_path;
}

static void add_virtual_cartridge_candidate(std::map<std::string, AppEntry> &candidates, const AppEntry &app) {
    const auto title_key = virtual_cartridge_title_key(app);
    if (title_key.empty())
        return;

    auto it = candidates.find(title_key);
    if (it == candidates.end()) {
        candidates[title_key] = app;
    } else if (virtual_cartridge_candidate_is_better(app, it->second)) {
        LOG_INFO("Replacing duplicate virtual cartridge {} source {} with {}", app.title_id, it->second.source_path, app.source_path);
        it->second = app;
    } else {
        LOG_INFO("Skipping duplicate virtual cartridge {} source {}", app.title_id, app.source_path);
    }
}


static std::optional<AppEntry> app_from_cartridge_directory(EmuEnvState &emuenv, const fs::path &content_path) {
    vfs::FileBuffer param_sfo;
    if (!fs_utils::read_data(content_path / "sce_sys/param.sfo", param_sfo))
        return std::nullopt;

    auto app = app_from_param(emuenv, param_sfo, content_path.generic_path(), {});
    if (app.has_value())
        app->encrypted_content = virtual_cartridge_directory_appears_encrypted(content_path);

    return app;
}

static std::optional<AppEntry> app_from_cartridge_archive(EmuEnvState &emuenv, const fs::path &archive_path) {
    std::unique_ptr<FILE, int (*)(FILE *)> archive_file(FOPEN(archive_path.c_str(), "rb"), fclose);
    if (!archive_file)
        return std::nullopt;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, archive_file.get(), 0, 0))
        return std::nullopt;

    for (const auto &root : get_archive_content_roots_for_scan(zip)) {
        vfs::FileBuffer param_sfo;
        if (!extract_archive_file_to_buffer(zip, root + "sce_sys/param.sfo", param_sfo))
            continue;

        auto app = app_from_param(emuenv, param_sfo, archive_path.generic_path(), root);
        if (app.has_value()) {
            app->encrypted_content = virtual_cartridge_archive_appears_encrypted(zip, root);
            if (app->encrypted_content)
                LOG_WARN("Virtual cartridge {} [{}] appears to contain encrypted app files; pure ZIP launch will not work until the content is Vita3K-readable.", app->title_id, archive_path);
            mz_zip_reader_end(&zip);
            return app;
        }
    }

    mz_zip_reader_end(&zip);
    return std::nullopt;
}

static void refresh_cheat_badges(std::vector<AppEntry> &apps, EmuEnvState &emuenv) {
    for (auto &app : apps)
        app.cheats_available = has_cheats_for_title(emuenv, app.title_id);
}

void append_virtual_cartridge_apps(std::vector<AppEntry> &apps, EmuEnvState &emuenv) {
    if (!emuenv.cfg.scan_virtual_cartridges)
        return;

    std::vector<fs::path> scan_roots;
    std::set<std::string> scan_root_keys;
    const auto add_scan_root = [&](const fs::path &scan_root) {
        auto key = scan_root.generic_string();
#ifdef __ANDROID__
        key = string_utils::tolower(key);
#endif
        if (key.empty() || scan_root_keys.contains(key))
            return;

        scan_root_keys.insert(key);
        scan_roots.push_back(scan_root);
    };

    for (const auto &dir_utf8 : emuenv.cfg.virtual_cartridge_dirs)
        add_scan_root(fs_utils::utf8_to_path(dir_utf8));

#ifdef __ANDROID__
    add_scan_root(fs_utils::utf8_to_path("/sdcard/Roms/psvita"));
    add_scan_root(fs_utils::utf8_to_path("/storage/emulated/0/Roms/psvita"));

    try {
        const fs::path storage_root{ "/storage" };
        if (fs::exists(storage_root)) {
            for (const auto &entry : fs::directory_iterator(storage_root)) {
                if (!fs::is_directory(entry.path()))
                    continue;

                const auto name = entry.path().filename().generic_string();
                if (name == "emulated" || name == "self")
                    continue;

                add_scan_root(entry.path() / "Roms/psvita");
                add_scan_root(entry.path() / "roms/psvita");
                add_scan_root(entry.path() / "Emulation/ROMs/PSVita");
                add_scan_root(entry.path() / "Emulation/roms/psvita");
            }
        }
    } catch (const fs::filesystem_error &e) {
        LOG_WARN("Could not discover external storage virtual cartridge directories: {}", e.what());
    }
#endif

    const auto normalized_scan_key = [](const fs::path &path) {
        auto key = path.lexically_normal().generic_string();
#ifdef __ANDROID__
        key = string_utils::tolower(key);
#endif
        while (key.size() > 1 && key.ends_with('/'))
            key.pop_back();
        return key;
    };

    const auto source_is_in_scan_roots = [&](const AppEntry &app) {
        if (!app.virtual_cartridge || app.source_path.empty())
            return false;

        const auto source_key = normalized_scan_key(fs_utils::utf8_to_path(app.source_path));
        for (const auto &scan_root : scan_roots) {
            const auto scan_key = normalized_scan_key(scan_root);
            if (!scan_key.empty() && (source_key == scan_key || source_key.starts_with(scan_key + "/")))
                return true;
        }
        return false;
    };

    std::map<std::string, AppEntry> virtual_candidates;
    std::vector<AppEntry> installed_apps;
    installed_apps.reserve(apps.size());

    std::set<std::string> indexed_paths;
    for (const auto &app : apps) {
        if (!app.virtual_cartridge) {
            installed_apps.push_back(app);
            continue;
        }

        if (source_is_in_scan_roots(app) && virtual_cartridge_source_unchanged(app)) {
            indexed_paths.insert(app.path);
            add_virtual_cartridge_candidate(virtual_candidates, app);
        }
    }

    apps = std::move(installed_apps);

    for (const auto &scan_root : scan_roots) {
        if (!fs::exists(scan_root))
            continue;

        try {
            const auto add_app = [&](std::optional<AppEntry> app) {
                if (!app.has_value())
                    return;

                indexed_paths.insert(app->path);
                add_virtual_cartridge_candidate(virtual_candidates, *app);
            };

            for (const auto &entry : fs::directory_iterator(scan_root)) {
                const auto path = entry.path();
                const auto path_key = virtual_cartridge_source_key(path);
                const auto extension = string_utils::tolower(path.extension().string());

                if (fs::is_regular_file(path) && ((extension == ".zip") || (extension == ".vpk"))) {
                    if (!indexed_paths.contains(path_key))
                        add_app(app_from_cartridge_archive(emuenv, path));
                } else if (fs::is_regular_file(path) && (string_utils::tolower(path.filename().string()) == "param.sfo") && (path.parent_path().filename() == "sce_sys")) {
                    const auto content_path = path.parent_path().parent_path();
                    if (!indexed_paths.contains(virtual_cartridge_source_key(content_path)))
                        add_app(app_from_cartridge_directory(emuenv, content_path));
                } else if (fs::is_directory(path)) {
                    if (!indexed_paths.contains(path_key))
                        add_app(app_from_cartridge_directory(emuenv, path));

                    for (const auto &child : fs::directory_iterator(path)) {
                        const auto child_path = child.path();
                        const auto child_key = virtual_cartridge_source_key(child_path);
                        const auto child_extension = string_utils::tolower(child_path.extension().string());
                        if (fs::is_regular_file(child_path) && ((child_extension == ".zip") || (child_extension == ".vpk")))
                            if (!indexed_paths.contains(child_key))
                                add_app(app_from_cartridge_archive(emuenv, child_path));
                    }
                }
            }
        } catch (const fs::filesystem_error &e) {
            LOG_WARN("Could not scan virtual cartridge directory {}: {}", scan_root, e.what());
        }
    }

    std::set<std::string> virtual_title_keys;
    for (const auto &[title_key, app] : virtual_candidates)
        virtual_title_keys.insert(title_key);

    apps.erase(std::remove_if(apps.begin(), apps.end(), [&](const AppEntry &app) {
        return virtual_title_keys.contains(virtual_cartridge_title_key(app));
    }), apps.end());

    for (const auto &[title_key, app] : virtual_candidates)
        apps.push_back(app);
}

} // namespace app

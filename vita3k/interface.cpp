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

#include "interface.h"

#include "module/load_module.h"

#include <app/functions.h>
#include <audio/state.h>
#include <bgm_player/functions.h>
#include <config/state.h>
#include <cpu/functions.h>
#include <ctime>
#include <ctrl/functions.h>
#include <ctrl/state.h>
#include <dialog/state.h>
#include <display/functions.h>
#include <display/state.h>
#include <gui/functions.h>
#include <gxm/state.h>
#include <io/functions.h>
#include <io/vfs.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <renderer/state.h>
#include <renderer/texture_cache.h>

#include <modules/module_parent.h>
#include <motion/event_handler.h>
#include <string>
#include <touch/functions.h>
#include <util/fs.h>
#include <util/log.h>
#include <util/string_utils.h>
#include <util/vector_utils.h>

#include <gui/imgui_impl_sdl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include <cstdlib>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <fmt/chrono.h>
#include <stb_image_write.h>

#include <gdbstub/functions.h>

#if USE_DISCORD
#include <app/discord.h>
#endif

static size_t write_to_buffer(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    vfs::FileBuffer *const buffer = static_cast<vfs::FileBuffer *>(pOpaque);
    assert(file_ofs == buffer->size());
    const uint8_t *const first = static_cast<const uint8_t *>(pBuf);
    const uint8_t *const last = &first[n];
    buffer->insert(buffer->end(), first, last);

    return n;
}

static const char *miniz_get_error(const ZipPtr &zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(zip.get()));
}

static bool is_safe_archive_relative_path(const fs::path &entry_path);

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

static bool archive_root_is_safe(const std::string &root) {
    return root.empty() || is_safe_archive_relative_path(fs_utils::utf8_to_path(root));
}

static std::optional<std::string> find_archive_file_case_insensitive(const ZipPtr &zip, const std::string &path) {
    const auto normalized_path = string_utils::tolower(normalize_archive_member_name(path));
    const mz_uint num_files = mz_zip_reader_get_num_files(zip.get());

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat))
            continue;

        if (mz_zip_reader_is_file_a_directory(zip.get(), i))
            continue;

        const auto normalized_member = string_utils::tolower(normalize_archive_member_name(file_stat.m_filename));
        if (normalized_member == normalized_path)
            return std::string(file_stat.m_filename);
    }

    return std::nullopt;
}

static bool extract_archive_file_to_buffer(const ZipPtr &zip, const std::string &path, vfs::FileBuffer &buffer) {
    const auto archive_name = find_archive_file_case_insensitive(zip, path);
    if (!archive_name)
        return false;

    buffer.clear();
    return mz_zip_reader_extract_file_to_callback(zip.get(), archive_name->c_str(), &write_to_buffer, &buffer, 0);
}

static bool archive_file_exists_case_insensitive(const ZipPtr &zip, const std::string &path) {
    return find_archive_file_case_insensitive(zip, path).has_value();
}

static bool buffer_starts_with(const vfs::FileBuffer &buffer, const std::initializer_list<uint8_t> prefix) {
    if (buffer.size() < prefix.size())
        return false;

    return std::equal(prefix.begin(), prefix.end(), buffer.begin());
}

static bool archive_buffer_is_png(const vfs::FileBuffer &buffer) {
    return buffer_starts_with(buffer, { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A });
}

static bool archive_buffer_is_vita_executable(const vfs::FileBuffer &buffer) {
    return buffer_starts_with(buffer, { 'S', 'C', 'E', 0x00 }) || buffer_starts_with(buffer, { 0x7F, 'E', 'L', 'F' });
}

static bool archive_content_appears_encrypted(const ZipPtr &zip, const std::string &content_path) {
    vfs::FileBuffer buffer;
    if (extract_archive_file_to_buffer(zip, content_path + "eboot.bin", buffer) && !buffer.empty() && !archive_buffer_is_vita_executable(buffer))
        return true;

    buffer.clear();
    return extract_archive_file_to_buffer(zip, content_path + "sce_sys/icon0.png", buffer) && !buffer.empty() && !archive_buffer_is_png(buffer);
}

static void set_theme_name(EmuEnvState &emuenv, vfs::FileBuffer &buf) {
    emuenv.app_info.app_title = gui::get_theme_title_from_buffer(buf);
    emuenv.app_info.app_title_id = string_utils::remove_special_chars(emuenv.app_info.app_title);
    const auto nospace = std::remove_if(emuenv.app_info.app_title_id.begin(), emuenv.app_info.app_title_id.end(), isspace);
    emuenv.app_info.app_title_id.erase(nospace, emuenv.app_info.app_title_id.end());
    emuenv.app_info.app_category = "theme";
    emuenv.app_info.app_content_id = emuenv.app_info.app_title_id;
    emuenv.app_info.app_title += " (Theme)";
}

static bool is_nonpdrm(EmuEnvState &emuenv, const fs::path &output_path) {
    const auto app_license_path{ emuenv.pref_path / "ux0/license" / emuenv.app_info.app_title_id / fmt::format("{}.rif", emuenv.app_info.app_content_id) };
    const auto is_patch_found_app_license = (emuenv.app_info.app_category == "gp") && fs::exists(app_license_path);
    if (fs::exists(output_path / "sce_sys/package/work.bin") || is_patch_found_app_license) {
        fs::path licpath = is_patch_found_app_license ? app_license_path : output_path / "sce_sys/package/work.bin";
        LOG_INFO("Decrypt layer: {}", output_path);
        if (!decrypt_install_nonpdrm(emuenv, licpath, output_path)) {
            LOG_ERROR("NoNpDrm installation failed, deleting data!");
            fs::remove_all(output_path);
            return false;
        }
        return true;
    }

    return false;
}

static bool set_content_path(EmuEnvState &emuenv, const bool is_theme, fs::path &dest_path) {
    const auto app_path = dest_path / "app" / emuenv.app_info.app_title_id;

    if (emuenv.app_info.app_category == "ac") {
        if (is_theme) {
            dest_path /= fs::path("theme") / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (Theme)";
        } else {
            emuenv.app_info.app_content_id = emuenv.app_info.app_content_id.substr(20);
            dest_path /= fs::path("addcont") / emuenv.app_info.app_title_id / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (DLC)";
        }
    } else if (emuenv.app_info.app_category.find("gp") != std::string::npos) {
        if (!fs::exists(app_path) || fs::is_empty(app_path)) {
            LOG_ERROR("Install app before patch");
            return false;
        }
        dest_path /= fs::path("patch") / emuenv.app_info.app_title_id;
        emuenv.app_info.app_title += " (Patch)";
    } else {
        dest_path = app_path;
        emuenv.app_info.app_title += " (App)";
    }

    return true;
}

static bool install_archive_content(EmuEnvState &emuenv, GuiState *gui, const ZipPtr &zip, const std::string &content_path, const std::function<void(ArchiveContents)> &progress_callback) {
    std::string sfo_path = "sce_sys/param.sfo";
    std::string theme_path = "theme.xml";
    vfs::FileBuffer buffer, theme;

    const auto is_theme = extract_archive_file_to_buffer(zip, content_path + theme_path, theme);

    auto output_path{ emuenv.pref_path / "ux0" };
    if (extract_archive_file_to_buffer(zip, content_path + sfo_path, buffer)) {
        sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang);
        if (!set_content_path(emuenv, is_theme, output_path))
            return false;
    } else if (is_theme) {
        set_theme_name(emuenv, theme);
        output_path /= fs::path("theme") / emuenv.app_info.app_content_id;
    } else {
        LOG_CRITICAL("miniz error: {} extracting file: {}", miniz_get_error(zip), sfo_path);
        return false;
    }

    const auto created = fs::create_directories(output_path);
    if (!created) {
        if (!gui || gui->file_menu.archive_install_dialog) {
            fs::remove_all(output_path);
        } else if (!gui->file_menu.archive_install_dialog) {
            gui::GenericDialogState status = gui::UNK_STATE;

            while (handle_events(emuenv, *gui) && (status == gui::UNK_STATE)) {
                gui::draw_begin(*gui, emuenv);
                gui::draw_ui(*gui, emuenv);
                gui::draw_reinstall_dialog(&status, *gui, emuenv);
                gui::draw_end(*gui);
                emuenv.renderer->swap_window(emuenv.window.get());
            }
            switch (status) {
            case gui::CANCEL_STATE:
                LOG_INFO("{} already installed, {}", emuenv.app_info.app_title_id, emuenv.app_info.app_category.find("gd") != std::string::npos ? "launching application..." : "Open home");
                return true;
            case gui::CONFIRM_STATE:
                fs::remove_all(output_path);
                break;
            case gui::UNK_STATE:
                exit(0);
            default: break;
            }
        }
    }

    float file_progress = 0;
    float decrypt_progress = 0;

    const auto update_progress = [&]() {
        if (progress_callback)
            progress_callback({ {}, {}, { file_progress * 0.7f + decrypt_progress * 0.3f } });
    };

    const auto normalized_content_root = string_utils::tolower(normalize_archive_member_name(content_path));
    const auto content_root_prefix = normalized_content_root.empty() ? std::string{} : normalized_content_root + "/";
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat)) {
            continue;
        }
        const std::string m_filename = file_stat.m_filename;
        const std::string normalized_filename = normalize_archive_member_name(m_filename);
        const std::string normalized_filename_lower = string_utils::tolower(normalized_filename);
        if (content_root_prefix.empty() || normalized_filename_lower.starts_with(content_root_prefix)) {
            file_progress = static_cast<float>(i) / num_files * 100.0f;
            update_progress();

            std::string replace_filename = content_root_prefix.empty() ? normalized_filename : normalized_filename.substr(content_root_prefix.size());
            const fs::path file_output = (output_path / fs_utils::utf8_to_path(replace_filename)).generic_path();
            if (mz_zip_reader_is_file_a_directory(zip.get(), i)) {
                fs::create_directories(file_output);
            } else {
                fs::create_directories(file_output.parent_path());
                LOG_INFO("Extracting {}", file_output);
                mz_zip_reader_extract_to_file(zip.get(), i, fs_utils::path_to_utf8(file_output).c_str(), 0);
            }
        }
    }

    if (fs::exists(output_path / "sce_sys/package/") && emuenv.app_info.app_title_id.starts_with("PCS")) {
        update_progress();
        if (is_nonpdrm(emuenv, output_path))
            decrypt_progress = 100.f;
        else
            return false;
    }
    if (!copy_path(output_path, emuenv.pref_path, emuenv.app_info.app_title_id, emuenv.app_info.app_category))
        return false;

    update_progress();

    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);

    if (!gui->file_menu.archive_install_dialog && (emuenv.app_info.app_category != "theme")) {
        gui::update_notice_info(*gui, emuenv, "content");
        if ((emuenv.app_info.app_category.find("gd") != std::string::npos) || (emuenv.app_info.app_category.find("gp") != std::string::npos)) {
            gui::init_user_app(*gui, emuenv, emuenv.app_info.app_title_id);
            gui::save_apps_cache(*gui, emuenv);
        }
    }

    return true;
}

static bool is_safe_archive_relative_path(const fs::path &entry_path) {
    if (entry_path.empty() || entry_path.is_absolute())
        return false;

    for (const auto &part : entry_path) {
        if (part.string() == "..")
            return false;
    }

    return true;
}

static bool is_game_card_category(const std::string &category) {
    return category.find("gd") != std::string::npos || category.find("gc") != std::string::npos;
}

static bool extract_archive_content_to_path(const ZipPtr &zip, const std::string &content_path, const fs::path &output_path, const std::function<void(ArchiveContents)> &progress_callback) {
    float file_progress = 0;

    const auto update_progress = [&]() {
        if (progress_callback)
            progress_callback({ {}, {}, file_progress });
    };

    const auto normalized_content_root = string_utils::tolower(normalize_archive_member_name(content_path));
    const auto content_root_prefix = normalized_content_root.empty() ? std::string{} : normalized_content_root + "/";
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat))
            continue;

        const std::string filename = file_stat.m_filename;
        const std::string normalized_filename = normalize_archive_member_name(filename);
        const std::string normalized_filename_lower = string_utils::tolower(normalized_filename);
        if (!content_root_prefix.empty() && !normalized_filename_lower.starts_with(content_root_prefix))
            continue;

        file_progress = static_cast<float>(i) / num_files * 100.0f;
        update_progress();

        const fs::path relative_path = fs_utils::utf8_to_path(content_root_prefix.empty() ? normalized_filename : normalized_filename.substr(content_root_prefix.size()));
        if (!is_safe_archive_relative_path(relative_path)) {
            LOG_WARN("Skipping unsafe cartridge archive entry {}", filename);
            continue;
        }

        const fs::path file_output = (output_path / relative_path).generic_path();
        if (mz_zip_reader_is_file_a_directory(zip.get(), i)) {
            fs::create_directories(file_output);
        } else {
            fs::create_directories(file_output.parent_path());
            LOG_INFO("Cartridge extract {}", file_output);
            if (!mz_zip_reader_extract_to_file(zip.get(), i, fs_utils::path_to_utf8(file_output).c_str(), 0)) {
                LOG_ERROR("miniz error extracting {}: {}", filename, miniz_get_error(zip));
                return false;
            }
        }
    }

    file_progress = 100.f;
    update_progress();
    return true;
}

static bool mount_archive_content_as_cartridge(EmuEnvState &emuenv, const ZipPtr &zip, const fs::path &archive_path, const std::string &content_path, const std::function<void(ArchiveContents)> &progress_callback) {
    vfs::FileBuffer param_sfo;
    if (!extract_archive_file_to_buffer(zip, content_path + "sce_sys/param.sfo", param_sfo)) {
        LOG_ERROR("Cartridge archive content has no sce_sys/param.sfo: {}", content_path);
        return false;
    }

    sfo::get_param_info(emuenv.app_info, param_sfo, emuenv.cfg.sys_lang);
    if (!is_game_card_category(emuenv.app_info.app_category)) {
        LOG_ERROR("Cartridge mode only supports game app content, got category {}", emuenv.app_info.app_category);
        return false;
    }

    if (emuenv.app_info.app_title_id.find_first_of("/\\") != std::string::npos || !is_safe_archive_relative_path(fs::path(emuenv.app_info.app_title_id))) {
        LOG_ERROR("Unsafe cartridge title id {}", emuenv.app_info.app_title_id);
        return false;
    }

    if (archive_content_appears_encrypted(zip, content_path)) {
        LOG_ERROR("Cartridge archive {} root {} appears to contain encrypted app files. Direct ZIP mode needs Vita3K-readable app files from legally dumped content.", archive_path, content_path);
        return false;
    }

    if (!vfs::mount_current_app_archive(emuenv.io, archive_path, content_path, emuenv.app_info.app_title_id))
        return false;

    if (progress_callback)
        progress_callback({ {}, {}, 100.f });

    if (vfs::current_app_file_exists(emuenv.io, "sce_sys/package/work.bin") && emuenv.app_info.app_title_id.starts_with("PCS"))
        LOG_WARN("Direct archive cartridge mode found NoNpDrm package metadata. If this title needs install-time decryption, direct ZIP launch may fail.");

    LOG_INFO("{} [{}] mounted directly from archive {} root {}", emuenv.app_info.app_title, emuenv.app_info.app_title_id, archive_path, content_path);
    return true;
}

static std::vector<std::string> get_archive_contents_path(const ZipPtr &zip) {
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    std::map<std::string, int> candidate_scores;
    constexpr std::string_view sfo_path = "sce_sys/param.sfo";
    constexpr std::string_view theme_path = "theme.xml";
    constexpr std::string_view eboot_path = "eboot.bin";

    const auto add_candidate = [&](std::string root, int score, const std::string &source) {
        root = normalize_archive_member_name(std::move(root));
        if (!root.empty())
            root += "/";
        if (!archive_root_is_safe(root)) {
            LOG_WARN("Skipping unsafe archive content root {} from {}", root, source);
            return;
        }

        candidate_scores[root] += score;
        LOG_INFO("Archive introspection candidate root '{}' score_delta={} source={}", root, score, source);
    };

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat))
            continue;

        const std::string m_filename = std::string(file_stat.m_filename);
        const std::string normalized_filename = normalize_archive_member_name(m_filename);
        const std::string normalized_lower = string_utils::tolower(normalized_filename);
        if (normalized_lower.find("sce_module/steroid.suprx") != std::string::npos) {
            LOG_CRITICAL("A Vitamin dump was detected, aborting installation...");
#ifdef __ANDROID__
            SDL_ShowAndroidToast("Vitamin dumps are not supported!", 1, -1, 0, 0);
#endif
            candidate_scores.clear();
            return {};
        }

        const auto sfo_pos = normalized_lower.rfind(sfo_path);
        if (sfo_pos != std::string::npos && sfo_pos + sfo_path.size() == normalized_lower.size()) {
            auto root = normalized_filename.substr(0, sfo_pos);
            if (root.ends_with("/"))
                root.pop_back();
            add_candidate(root, 100, normalized_filename);
        }

        const auto theme_pos = normalized_lower.rfind(theme_path);
        if (theme_pos != std::string::npos && theme_pos + theme_path.size() == normalized_lower.size()) {
            auto root = normalized_filename.substr(0, theme_pos);
            if (root.ends_with("/"))
                root.pop_back();
            add_candidate(root, 20, normalized_filename);
        }

        const auto eboot_pos = normalized_lower.rfind(eboot_path);
        if (eboot_pos != std::string::npos && eboot_pos + eboot_path.size() == normalized_lower.size()) {
            auto root = normalized_filename.substr(0, eboot_pos);
            if (root.ends_with("/"))
                root.pop_back();
            add_candidate(root, 50, normalized_filename);
        }
    }

    std::vector<std::pair<std::string, int>> candidates;
    candidates.reserve(candidate_scores.size());
    for (auto &[root, score] : candidate_scores) {
        const auto lower_root = string_utils::tolower(root);
        if (archive_file_exists_case_insensitive(zip, root + "eboot.bin"))
            score += 75;
        if (std::regex_search(lower_root, std::regex("(^|/)(pcsa|pcsb|pcsc|pcsd|pcse|pcsf|pcsg|pcsh)[0-9]{5}/?$")))
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

    std::vector<std::string> content_path;
    content_path.reserve(candidates.size());
    for (const auto &[root, score] : candidates) {
        LOG_INFO("Archive introspection selected root '{}' score={}", root, score);
        content_path.push_back(root);
    }

    return content_path;
}

std::vector<ContentInfo> install_archive(EmuEnvState &emuenv, GuiState *gui, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback) {
    FILE *vpk_fp = FOPEN(archive_path.c_str(), "rb");
    if (!vpk_fp) {
        LOG_CRITICAL("Failed to load archive file in path: {}", fs_utils::path_to_utf8(archive_path));
        return {};
    }

    const ZipPtr zip(new mz_zip_archive, delete_zip);
    std::memset(zip.get(), 0, sizeof(*zip));

    if (!mz_zip_reader_init_cfile(zip.get(), vpk_fp, 0, 0)) {
        LOG_CRITICAL("miniz error reading archive: {}", miniz_get_error(zip));
        fclose(vpk_fp);
        return {};
    }

    const auto content_path = get_archive_contents_path(zip);
    if (content_path.empty()) {
        fclose(vpk_fp);
        return {};
    }

    const auto count = static_cast<float>(content_path.size());
    float current = 0.f;
    const auto update_progress = [&]() {
        if (progress_callback)
            progress_callback({ count, current, {} });
    };
    update_progress();

    std::vector<ContentInfo> content_installed{};
    for (auto &path : content_path) {
        current++;
        update_progress();
        bool state = install_archive_content(emuenv, gui, zip, path, progress_callback);
        // Can't use emplace_back due to Clang 15 for macos
        content_installed.push_back({ emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, path, state });
    }

    fclose(vpk_fp);
    return content_installed;
}

ContentInfo mount_archive_as_cartridge(EmuEnvState &emuenv, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback) {
    FILE *vpk_fp = FOPEN(archive_path.c_str(), "rb");
    if (!vpk_fp) {
        LOG_CRITICAL("Failed to load cartridge archive file in path: {}", fs_utils::path_to_utf8(archive_path));
        return {};
    }

    const ZipPtr zip(new mz_zip_archive, delete_zip);
    std::memset(zip.get(), 0, sizeof(*zip));

    if (!mz_zip_reader_init_cfile(zip.get(), vpk_fp, 0, 0)) {
        LOG_CRITICAL("miniz error reading cartridge archive: {}", miniz_get_error(zip));
        fclose(vpk_fp);
        return {};
    }

    const auto content_paths = get_archive_contents_path(zip);
    if (content_paths.empty()) {
        fclose(vpk_fp);
        return {};
    }

    for (const auto &path : content_paths) {
        if (!mount_archive_content_as_cartridge(emuenv, zip, archive_path, path, progress_callback))
            continue;

        fclose(vpk_fp);
        return { emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, path, true };
    }

    fclose(vpk_fp);
    return {};
}

ContentInfo mount_directory_as_cartridge(EmuEnvState &emuenv, const fs::path &content_path) {
    const auto param_sfo_path = content_path / "sce_sys/param.sfo";
    vfs::FileBuffer param_sfo;
    if (!fs_utils::read_data(param_sfo_path, param_sfo)) {
        LOG_ERROR("Cartridge directory has no sce_sys/param.sfo: {}", content_path);
        return {};
    }

    sfo::get_param_info(emuenv.app_info, param_sfo, emuenv.cfg.sys_lang);
    if (!is_game_card_category(emuenv.app_info.app_category)) {
        LOG_ERROR("Cartridge mode only supports game app content, got category {}", emuenv.app_info.app_category);
        return {};
    }

    if (emuenv.app_info.app_title_id.find_first_of("/\\") != std::string::npos || !is_safe_archive_relative_path(fs::path(emuenv.app_info.app_title_id))) {
        LOG_ERROR("Unsafe cartridge title id {}", emuenv.app_info.app_title_id);
        return {};
    }

    vfs::unmount_current_app_archive(emuenv.io);
    emuenv.io.app0_host_path = content_path.generic_path();

    LOG_INFO("{} [{}] mounted directly from directory {}", emuenv.app_info.app_title, emuenv.app_info.app_title_id, content_path);
    return { emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, content_path.generic_string(), true };
}

static std::vector<fs::path> get_contents_path(const fs::path &path) {
    std::vector<fs::path> contents_path;

    for (const auto &p : fs::recursive_directory_iterator(path)) {
        auto filename = p.path().filename();
        const auto is_content = (filename == "param.sfo") || (filename == "theme.xml");
        if (is_content) {
            auto parent_path = p.path().parent_path();
            const auto content_path = (filename == "param.sfo") ? parent_path.parent_path() : parent_path;
            vector_utils::push_if_not_exists(contents_path, content_path);
        }
    }

    return contents_path;
}

static bool install_content(EmuEnvState &emuenv, GuiState *gui, const fs::path &content_path) {
    const auto sfo_path{ content_path / "sce_sys/param.sfo" };
    const auto theme_path{ content_path / "theme.xml" };
    vfs::FileBuffer buffer;

    const auto is_theme = fs::exists(theme_path);
    auto dst_path{ emuenv.pref_path / "ux0" };
    if (fs_utils::read_data(sfo_path, buffer)) {
        sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang);
        if (!set_content_path(emuenv, is_theme, dst_path))
            return false;

        if (exists(dst_path))
            fs::remove_all(dst_path);

    } else if (fs_utils::read_data(theme_path, buffer)) {
        set_theme_name(emuenv, buffer);
        dst_path /= fs::path("theme") / fs_utils::utf8_to_path(emuenv.app_info.app_title_id);
    } else {
        LOG_ERROR("Param.sfo file is missing in path", sfo_path);
        return false;
    }

    if (!copy_directories(content_path, dst_path)) {
        LOG_ERROR("Failed to copy directory to: {}", dst_path);
        return false;
    }

    if (fs::exists(dst_path / "sce_sys/package/") && !is_nonpdrm(emuenv, dst_path))
        return false;

    if (!copy_path(dst_path, emuenv.pref_path, emuenv.app_info.app_title_id, emuenv.app_info.app_category))
        return false;

    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);

    if ((emuenv.app_info.app_category.find("gd") != std::string::npos) || (emuenv.app_info.app_category.find("gp") != std::string::npos)) {
        gui::init_user_app(*gui, emuenv, emuenv.app_info.app_title_id);
        gui::save_apps_cache(*gui, emuenv);
    }

    if (emuenv.app_info.app_category != "theme")
        gui::update_notice_info(*gui, emuenv, "content");

    return true;
}

uint32_t install_contents(EmuEnvState &emuenv, GuiState *gui, const fs::path &path) {
    const auto src_path = get_contents_path(path);

    LOG_WARN_IF(src_path.empty(), "No found any content compatible on this path: {}", path);

    uint32_t installed = 0;
    for (const auto &src : src_path) {
        if (install_content(emuenv, gui, src))
            ++installed;
    }

    if (installed) {
        gui::save_apps_cache(*gui, emuenv);
        LOG_INFO("Successfully installed {} content!", installed);
    }

    return installed;
}

static ExitCode load_app_impl(SceUID &main_module_id, EmuEnvState &emuenv) {
    const auto call_import = [&emuenv](CPUState &cpu, uint32_t nid, SceUID thread_id) {
        ::call_import(emuenv, cpu, nid, thread_id);
    };
    if (!emuenv.kernel.init(emuenv.mem, call_import, emuenv.kernel.cpu_opt)) {
        LOG_WARN("Failed to init kernel!");
        return KernelInitFailed;
    }

    if (emuenv.cfg.archive_log) {
        const fs::path log_directory{ emuenv.log_path / "logs" };
        fs::create_directory(log_directory);
        const auto log_path{ log_directory / fs_utils::utf8_to_path(emuenv.io.title_id + " - [" + string_utils::remove_special_chars(emuenv.current_app_title) + "].log") };
        if (logging::add_sink(log_path) != Success)
            return InitConfigFailed;
        logging::set_level(static_cast<spdlog::level::level_enum>(emuenv.cfg.log_level));
    }

    LOG_INFO("CPU Optimisation state: {}", emuenv.cfg.current_config.cpu_opt);
    LOG_INFO("ngs state: {}", emuenv.cfg.current_config.ngs_enable);
    LOG_INFO("Resolution multiplier: {}", emuenv.cfg.resolution_multiplier);

    // Set controller overlay state
    if (emuenv.cfg.enable_gamepad_overlay) {
        gui::set_controller_overlay_state(gui::get_overlay_display_mask(emuenv.cfg));
        refresh_controllers(emuenv.ctrl, emuenv);
    }

    if (emuenv.ctrl.controllers_num) {
        LOG_INFO("{} Controllers Connected", emuenv.ctrl.controllers_num);
        for (auto controller_it = emuenv.ctrl.controllers.begin(); controller_it != emuenv.ctrl.controllers.end(); ++controller_it) {
            LOG_INFO("Controller {}: {}", controller_it->second.port, controller_it->second.name);
        }
        if (emuenv.ctrl.has_motion_support)
            LOG_INFO("Controller has motion support");
    }
    constexpr std::array modules_mode_names{ "Automatic", "Auto & Manual", "Manual" };
    LOG_INFO("modules mode: {}", modules_mode_names.at(emuenv.cfg.current_config.modules_mode));
    if ((emuenv.cfg.current_config.modules_mode != ModulesMode::AUTOMATIC) && !emuenv.cfg.current_config.lle_modules.empty()) {
        std::string modules;
        for (const auto &mod : emuenv.cfg.current_config.lle_modules) {
            modules += mod + ",";
        }
        modules.pop_back();
        LOG_INFO("lle-modules: {}", modules);
    }

    LOG_INFO("Title: {}", emuenv.current_app_title);
    LOG_INFO("Serial: {}", emuenv.io.title_id);
    LOG_INFO("Version: {}", emuenv.app_info.app_version);
    LOG_INFO("Category: {}", emuenv.app_info.app_category);

    init_device_paths(emuenv.io);
    init_savedata_app_path(emuenv.io, emuenv.pref_path);

    // Load param.sfo
    vfs::FileBuffer param_sfo;
    if (vfs::read_current_app_file(param_sfo, emuenv.io, emuenv.pref_path, "sce_sys/param.sfo"))
        sfo::load(emuenv.sfo_handle, param_sfo);

    init_exported_vars(emuenv);

    // Load main executable
    emuenv.self_path = !emuenv.cfg.self_path.empty() ? emuenv.cfg.self_path : EBOOT_PATH;
    main_module_id = load_module(emuenv, "app0:" + emuenv.self_path);
    if (main_module_id >= 0) {
        const auto module = emuenv.kernel.loaded_modules[main_module_id];
        LOG_INFO("Main executable {} ({}) loaded", module->info.module_name, emuenv.self_path);
    } else
        return FileNotFound;
    // Set self name from self path, can contain folder, get file name only
    emuenv.self_name = fs::path(emuenv.self_path).filename().string();

    // get list of preload modules
    SceUInt32 process_preload_disabled = 0;
    auto process_param = emuenv.kernel.process_param.get(emuenv.mem);
    if (process_param) {
        auto preload_disabled_ptr = Ptr<SceUInt32>(process_param->process_preload_disabled);
        if (preload_disabled_ptr) {
            process_preload_disabled = *preload_disabled_ptr.get(emuenv.mem);
        }
    }
    std::vector<std::string> lib_load_list = {};
    // todo: check if module is imported
    auto add_preload_module = [&](uint32_t code, SceSysmoduleModuleId module_id, const std::string &name, bool load_from_app) {
        if ((process_preload_disabled & code) == 0) {
            if (is_lle_module(name, emuenv)) {
                const auto module_name_file = fmt::format("{}.suprx", name);
                const auto module_relative_path = fs::path("sce_module") / module_name_file;
                const auto module_app_path = (emuenv.io.app0_host_path.empty() ? emuenv.pref_path / "ux0/app" / emuenv.io.app_path : emuenv.io.app0_host_path) / module_relative_path;
                if (load_from_app && (vfs::current_app_file_exists(emuenv.io, module_relative_path) || fs::exists(module_app_path)))
                    lib_load_list.emplace_back(fmt::format("app0:sce_module/{}", module_name_file));
                else if (fs::exists(emuenv.pref_path / "vs0/sys/external" / module_name_file))
                    lib_load_list.emplace_back(fmt::format("vs0:sys/external/{}", module_name_file));
            }

            if (module_id != SCE_SYSMODULE_INVALID)
                emuenv.kernel.loaded_sysmodules[module_id] = {};
        }
    };
    add_preload_module(0x00010000, SCE_SYSMODULE_INVALID, "libc", true);
    add_preload_module(0x00020000, SCE_SYSMODULE_DBG, "libdbg", false);
    add_preload_module(0x00080000, SCE_SYSMODULE_INVALID, "libshellsvc", false);
    add_preload_module(0x00100000, SCE_SYSMODULE_INVALID, "libcdlg", false);
    add_preload_module(0x00200000, SCE_SYSMODULE_FIOS2, "libfios2", true);
    add_preload_module(0x00400000, SCE_SYSMODULE_APPUTIL, "apputil", false);
    add_preload_module(0x00800000, SCE_SYSMODULE_INVALID, "libSceFt2", false);
    add_preload_module(0x01000000, SCE_SYSMODULE_INVALID, "libpvf", false);
    add_preload_module(0x02000000, SCE_SYSMODULE_PERF, "libperf", false); // if DEVELOPMENT_MODE dipsw is set

    for (const auto &module_path : lib_load_list) {
        auto res = load_module(emuenv, module_path);
        if (res < 0)
            return FileNotFound;
    }

    // Load taiHEN plugins configured for this title
    load_taihen_plugins_for_title(emuenv, emuenv.io.title_id);

    return Success;
}

static void switch_full_screen(EmuEnvState &emuenv) {
    emuenv.display.fullscreen = !emuenv.display.fullscreen;
    emuenv.renderer->set_fullscreen(emuenv.display.fullscreen);

    SDL_SetWindowFullscreen(emuenv.window.get(), emuenv.display.fullscreen.load());

    // Refresh Viewport Size
    app::update_viewport(emuenv);
}

static void toggle_texture_replacement(EmuEnvState &emuenv) {
    emuenv.cfg.current_config.import_textures = !emuenv.cfg.current_config.import_textures;
    emuenv.renderer->get_texture_cache()->set_replacement_state(emuenv.cfg.current_config.import_textures, emuenv.cfg.current_config.export_textures, emuenv.cfg.current_config.export_as_png);
}

static std::vector<uint32_t> get_current_app_frame(EmuEnvState &emuenv, uint32_t &width, uint32_t &height) {
    // Dump the current frame from the emulator display
    std::vector<uint32_t> frame = emuenv.renderer->dump_frame(emuenv.display, width, height);
    if (frame.empty() || (frame.size() != (width * height))) {
        return {};
    }

    // Force alpha channel to 255 (fully opaque) for every pixel
    for (uint32_t &pixel : frame) {
        pixel |= 0xFF000000;
    }

    return frame;
}

static void update_live_area_last_app_frame(EmuEnvState &emuenv, GuiState &gui) {
    uint32_t width, height;

    // Capture the current frame of the app
    auto frame = get_current_app_frame(emuenv, width, height);
    if (frame.empty()) {
        LOG_ERROR("Failed to dump current app frame for live area");
        return;
    }

    // Create and assign a new texture with the captured frame
    gui.live_area_last_app_frame = ImGui_Texture(gui.imgui_state.get(), frame.data(), width, height);
}

void runtime_take_screenshot(EmuEnvState &emuenv) {
    if (emuenv.cfg.screenshot_format == None)
        return;

    if (emuenv.io.title_id.empty()) {
        LOG_ERROR("Trying to take a screenshot while not ingame");
        return;
    }

    uint32_t width, height;
    auto frame = get_current_app_frame(emuenv, width, height);
    if (frame.empty()) {
        LOG_ERROR("Failed to take screenshot");
        return;
    }

    const fs::path save_folder = emuenv.shared_path / "screenshots" / fmt::format("{}", string_utils::remove_special_chars(emuenv.current_app_title));
    fs::create_directories(save_folder);

    auto t = std::time(nullptr);
    struct tm localtime;
#ifdef _WIN32
    localtime_s(&localtime, &t);
#else
    localtime_r(&t, &localtime);
#endif

    const auto img_format = emuenv.cfg.screenshot_format == JPEG ? ".jpg" : ".png";
    const fs::path save_file = save_folder / fmt::format("{}_{:%Y-%m-%d-%H%M%OS}{}", string_utils::remove_special_chars(emuenv.current_app_title), localtime, img_format);
    constexpr int quality = 85; // google recommended value
    if (emuenv.cfg.screenshot_format == JPEG) {
        if (stbi_write_jpg(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(), quality) == 1)
            LOG_INFO("Successfully saved screenshot to {}", save_file);
        else
            LOG_INFO("Failed to save screenshot");
    } else {
        if (stbi_write_png(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(), width * 4) == 1)
            LOG_INFO("Successfully saved screenshot to {}", save_file);
        else
            LOG_INFO("Failed to save screenshot");
    }
}

namespace {

constexpr uint32_t QUICKSTATE_PAGE_SIZE = 4096;

struct QuickStateMemoryPage {
    Address address = 0;
    std::vector<uint8_t> bytes;
};

struct QuickStateThreadContext {
    SceUID id = 0;
    std::string name;
    Address entry_point = 0;
    Address stack_address = 0;
    uint32_t stack_size = 0;
    Address tls_address = 0;
    int priority = 0;
    SceInt32 affinity_mask = 0;
    CPUContext context;
};

struct QuickStateSection {
    std::string tag;
    uint32_t version = 1;
    std::vector<uint8_t> bytes;
};

struct QuickStateSlot {
    bool valid = false;
    bool restore_requires_same_pause = true;
    std::string title_id;
    std::string title;
    std::vector<QuickStateThreadContext> thread_contexts;
    std::vector<QuickStateMemoryPage> memory_pages;
    std::vector<uint32_t> allocator_words;
    std::vector<uint32_t> allocation_pages;
    std::vector<QuickStateSection> sections;
    uint64_t byte_count = 0;
    uint64_t pause_epoch = 0;
};

static QuickStateSlot quick_state_slot0;
static uint64_t quick_state_pause_epoch = 1;
static bool runtime_osd_open = false;
static bool runtime_osd_auto_paused = false;
static bool android_back_key_down = false;
static bool android_back_chord_used = false;
static bool android_back_fast_forward_latched = false;
static bool android_back_long_press_used = false;
static uint64_t android_back_down_ticks = 0;
constexpr uint64_t ANDROID_BACK_HOME_LONG_PRESS_MS = 400;
constexpr char QUICKSTATE_MAGIC[] = { 'V', '3', 'K', 'T', 'H', 'O', 'R', 'S', 'T', 'A', 'T', 'E' };
constexpr uint32_t QUICKSTATE_VERSION = 5;
constexpr uint32_t QUICKSTATE_MAX_STRING_BYTES = 4096;
constexpr uint32_t QUICKSTATE_MAX_SECTION_COUNT = 64;
constexpr uint64_t QUICKSTATE_MAX_SECTION_BYTES = 16 * 1024 * 1024;
constexpr uint32_t QUICKSTATE_COMPRESSION_NONE = 0;
constexpr uint32_t QUICKSTATE_COMPRESSION_MINIZ = 1;
constexpr auto QUICKSTATE_PAUSE_TIMEOUT = std::chrono::milliseconds(3000);

struct RuntimeControlFileState {
    bool initialized = false;
    fs::path path;
    std::time_t last_write = 0;
    uintmax_t last_size = 0;
    std::string last_action_id;
};

static RuntimeControlFileState runtime_control_file_state;

#ifdef __ANDROID__
struct RuntimeControlAndroidState {
    uint64_t poll_counter = 0;
    std::string last_action_id;
};

static RuntimeControlAndroidState runtime_control_android_state;
#endif

struct QuickStateDiskHeader {
    uint32_t version = 0;
    std::string title_id;
    std::string title;
    uint64_t byte_count = 0;
    uint32_t thread_count = 0;
    uint64_t memory_page_count = 0;
    uint32_t compression_level = 0;
    uint32_t allocator_word_count = 0;
    uint32_t allocation_page_count = 0;
};

struct QuickStateKernelObjectCounts {
    size_t threads = 0;
    size_t cpu_threads = 0;
    size_t timers = 0;
    size_t semaphores = 0;
    size_t condvars = 0;
    size_t lwcondvars = 0;
    size_t mutexes = 0;
    size_t lwmutexes = 0;
    size_t rwlocks = 0;
    size_t eventflags = 0;
    size_t msgpipes = 0;
    size_t callbacks = 0;
    size_t simple_events = 0;
    size_t loaded_modules = 0;
};

struct QuickStateIOCounts {
    size_t tty_files = 0;
    size_t std_files = 0;
    size_t dir_entries = 0;
    size_t overlays = 0;
    size_t archive_entries = 0;
    size_t archive_dirs = 0;
};

struct QuickStateTimingSnapshot {
    bool valid = false;
    uint64_t kernel_start_tick = 0;
    uint64_t kernel_base_tick = 0;
    uint64_t kernel_guest_tick = 0;
    uint64_t kernel_process_time = 0;
    uint32_t kernel_speed_percent = 100;
    uint64_t speed_anchor_host_process_us = 0;
    uint64_t speed_anchor_guest_process_us = 0;
};

struct QuickStateThreadMetadata {
    SceUID id = 0;
    std::string name;
    std::string status;
    Address entry_point = 0;
    Address stack_address = 0;
    uint32_t stack_size = 0;
    Address tls_address = 0;
    int priority = 0;
    SceInt32 affinity_mask = 0;
    uint64_t start_tick = 0;
    uint64_t last_vblank_waited = 0;
    uint32_t returned_value = 0;
    bool is_processing_callbacks = false;
    uint32_t callback_count = 0;
    uint32_t waiting_thread_count = 0;
};

struct QuickStateIOFilePosition {
    SceUID fd = 0;
    std::string vita_path;
    std::string translated_path;
    fs::path host_path;
    int open_mode = 0;
    bool memory_file = false;
    SceOff offset = 0;
};

struct QuickStateSyncSimpleEvent {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    uint32_t pattern = 0;
    uint64_t last_user_data = 0;
    bool auto_reset = false;
    bool cb_wakeup_only = false;
    uint32_t waiting_count = 0;
};

struct QuickStateSyncTimer {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    bool is_started = false;
    bool is_repeat = false;
    bool is_pulse = false;
    bool event_set = false;
    uint64_t time = 0;
    uint64_t next_event = 0;
    uint64_t next_event_delta_us = std::numeric_limits<uint64_t>::max();
    uint64_t event_interval = 0;
    uint32_t waiting_count = 0;
};

struct QuickStateSyncSemaphore {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    int32_t init_val = 0;
    int32_t value = 0;
    int32_t max = 0;
    uint32_t waiting_count = 0;
};

struct QuickStateSyncMutex {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    int32_t init_count = 0;
    int32_t lock_count = 0;
    SceUID owner = 0;
    Address workarea = 0;
    uint32_t waiting_count = 0;
    bool lightweight = false;
};

struct QuickStateSyncEventFlag {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    int32_t flags = 0;
    uint32_t waiting_count = 0;
};

struct QuickStateSyncCondvar {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    SceUID associated_mutex = 0;
    uint32_t waiting_count = 0;
    bool lightweight = false;
};

struct QuickStateSyncRWLock {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    RWLockState state = RWLockState::Unlocked;
    std::map<SceUID, int32_t> owners;
    uint32_t waiting_count = 0;
};

struct QuickStateSyncMsgPipe {
    SceUID id = 0;
    std::string name;
    uint32_t attr = 0;
    uint32_t sender_count = 0;
    uint32_t receiver_count = 0;
    uint64_t capacity = 0;
    uint64_t used = 0;
    bool being_deleted = false;
};

struct QuickStateSyncWaitQueueEntry {
    std::string kind;
    SceUID object_id = 0;
    uint32_t index = 0;
    SceUID thread_id = 0;
    int32_t priority = 0;
    int32_t lock_count = 0;
    bool is_write = false;
    int32_t signal = 0;
    int32_t pattern = 0;
    Address result_pattern = 0;
    Address user_data = 0;
    int32_t wait = 0;
    int32_t flags = 0;
    Address out_bits = 0;
    uint32_t request_size = 0;
    std::string cancel_source;
};

struct QuickStateSyncSnapshot {
    bool valid = false;
    std::map<SceUID, QuickStateSyncSimpleEvent> simple_events;
    std::map<SceUID, QuickStateSyncTimer> timers;
    std::map<SceUID, QuickStateSyncSemaphore> semaphores;
    std::map<SceUID, QuickStateSyncMutex> mutexes;
    std::map<SceUID, QuickStateSyncMutex> lwmutexes;
    std::map<SceUID, QuickStateSyncEventFlag> eventflags;
    std::map<SceUID, QuickStateSyncCondvar> condvars;
    std::map<SceUID, QuickStateSyncCondvar> lwcondvars;
    std::map<SceUID, QuickStateSyncRWLock> rwlocks;
    std::map<SceUID, QuickStateSyncMsgPipe> msgpipes;
    std::vector<QuickStateSyncWaitQueueEntry> wait_queue_entries;

    size_t total_waiting_threads() const {
        size_t total = 0;
        for (const auto &[_, item] : simple_events)
            total += item.waiting_count;
        for (const auto &[_, item] : timers)
            total += item.waiting_count;
        for (const auto &[_, item] : semaphores)
            total += item.waiting_count;
        for (const auto &[_, item] : mutexes)
            total += item.waiting_count;
        for (const auto &[_, item] : lwmutexes)
            total += item.waiting_count;
        for (const auto &[_, item] : eventflags)
            total += item.waiting_count;
        for (const auto &[_, item] : condvars)
            total += item.waiting_count;
        for (const auto &[_, item] : lwcondvars)
            total += item.waiting_count;
        for (const auto &[_, item] : rwlocks)
            total += item.waiting_count;
        for (const auto &[_, item] : msgpipes)
            total += item.sender_count + item.receiver_count;
        return total;
    }

    size_t msgpipe_buffer_bytes() const {
        size_t total = 0;
        for (const auto &[_, item] : msgpipes)
            total += static_cast<size_t>(std::min<uint64_t>(item.used, static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        return total;
    }

    size_t wait_queue_entry_count(const std::string_view kind, const SceUID object_id) const {
        return static_cast<size_t>(std::count_if(wait_queue_entries.begin(), wait_queue_entries.end(), [kind, object_id](const QuickStateSyncWaitQueueEntry &entry) {
            return entry.object_id == object_id && entry.kind == kind;
        }));
    }

    bool wait_queue_metadata_complete() const {
        for (const auto &[uid, item] : simple_events) {
            if (wait_queue_entry_count("simple_event", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : timers) {
            if (wait_queue_entry_count("timer", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : semaphores) {
            if (wait_queue_entry_count("semaphore", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : mutexes) {
            if (wait_queue_entry_count("mutex", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : lwmutexes) {
            if (wait_queue_entry_count("lwmutex", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : eventflags) {
            if (wait_queue_entry_count("eventflag", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : condvars) {
            if (wait_queue_entry_count("condvar", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : lwcondvars) {
            if (wait_queue_entry_count("lwcondvar", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : rwlocks) {
            if (wait_queue_entry_count("rwlock", uid) != item.waiting_count)
                return false;
        }
        for (const auto &[uid, item] : msgpipes) {
            if (wait_queue_entry_count("msgpipe_sender", uid) != item.sender_count
                || wait_queue_entry_count("msgpipe_receiver", uid) != item.receiver_count) {
                return false;
            }
        }
        return wait_queue_entries.size() == total_waiting_threads();
    }
};

struct QuickStateDisplayFrameSnapshot {
    Address base = 0;
    uint32_t pitch = 0;
    uint32_t pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    int32_t width = 0;
    int32_t height = 0;
};

struct QuickStatePredictedDisplayFrameSnapshot {
    QuickStateDisplayFrameSnapshot frame;
    Address sync_object = 0;
};

struct QuickStateDisplayVBlankWaitSnapshot {
    SceUID thread_id = 0;
    uint64_t target_vcount = 0;
};

struct QuickStateDisplaySnapshot {
    bool valid = false;
    uint32_t speed_percent = 100;
    uint64_t vblank_count = 0;
    uint64_t last_setframe_vblank_count = 0;
    Address current_sync_object = 0;
    QuickStateDisplayFrameSnapshot sce_frame;
    QuickStateDisplayFrameSnapshot next_rendered_frame;
    bool fps_hack = false;
    std::vector<QuickStatePredictedDisplayFrameSnapshot> predicted_frames;
    uint32_t predicted_frame_position = 0;
    uint32_t predicted_cycles_seen = 0;
    bool predicting = false;
    uint32_t vblank_wait_info_count = 0;
    bool vblank_waits_complete = false;
    std::vector<QuickStateDisplayVBlankWaitSnapshot> vblank_waits;
    uint32_t vblank_callback_count = 0;
};

struct QuickStateAudioPortSnapshot {
    int32_t id = 0;
    int32_t type = 0;
    int32_t len = 0;
    int32_t freq = 0;
    int32_t mode = 0;
    int32_t len_bytes = 0;
    uint64_t len_microseconds = 0;
    uint64_t last_output = 0;
    int32_t left_channel_volume = SCE_AUDIO_VOLUME_0DB;
    int32_t right_channel_volume = SCE_AUDIO_VOLUME_0DB;
    float volume = 1.0f;
};

struct QuickStateAudioSnapshot {
    bool valid = false;
    uint32_t speed_percent = 100;
    std::string backend;
    float global_volume = 1.0f;
    int32_t next_port_id = 1;
    std::map<int32_t, QuickStateAudioPortSnapshot> out_ports;
    bool audio_in_running = false;
    int32_t audio_in_len_bytes = 0;
};

struct QuickStateRestoreManifest {
    uint32_t format_version = QUICKSTATE_VERSION;
    uint64_t guest_memory_bytes = 0;
    size_t memory_pages = 0;
    size_t allocator_words = 0;
    size_t allocation_pages = 0;
    size_t thread_contexts = 0;
    size_t avplayer_threads = 0;
    std::vector<std::string> captured_sections;
    bool timing_restorable = false;
    bool thread_metadata_restorable = false;
    bool io_file_positions_restorable = false;
    bool io_file_handles_restorable = false;
    bool sync_primitives_restorable = false;
    bool sync_wait_queue_metadata_complete = false;
    bool display_state_restorable = false;
    bool display_vblank_waits_restorable = false;
    bool audio_state_restorable = false;
    size_t sync_waiting_threads = 0;
    size_t sync_wait_queue_entries = 0;
    size_t msgpipe_buffer_bytes = 0;
    size_t io_memory_file_handles = 0;
    size_t display_wait_entries = 0;
    size_t display_callback_entries = 0;
    QuickStateKernelObjectCounts kernel;
    QuickStateIOCounts io;
    std::vector<std::string> missing_serializers;
    bool restore_enabled = false;
    std::string block_reason;
};

static fs::path quick_state_root(EmuEnvState &emuenv) {
    if (emuenv.cfg.save_state_dir.empty())
        return emuenv.shared_path / "states";

    fs::path configured = fs_utils::utf8_to_path(emuenv.cfg.save_state_dir);
    if (configured.is_relative())
        configured = emuenv.shared_path / configured;

    return configured;
}

static fs::path quick_state_dir(EmuEnvState &emuenv, const std::string &title_id) {
    return quick_state_root(emuenv) / title_id;
}

static fs::path quick_state_file(EmuEnvState &emuenv, const std::string &title_id) {
    return quick_state_dir(emuenv, title_id) / "slot0.thorstate";
}

static fs::path quick_state_marker_file(EmuEnvState &emuenv, const std::string &title_id) {
    return quick_state_dir(emuenv, title_id) / "slot0.thorstate.txt";
}

template <typename T>
static bool quick_state_write_value(std::ostream &out, const T &value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
    return out.good();
}

static bool quick_state_write_bytes(std::ostream &out, const void *data, const std::streamsize size) {
    out.write(reinterpret_cast<const char *>(data), size);
    return out.good();
}

static bool quick_state_write_string(std::ostream &out, const std::string &value) {
    if (value.size() > QUICKSTATE_MAX_STRING_BYTES)
        return false;

    const auto size = static_cast<uint32_t>(value.size());
    return quick_state_write_value(out, size) && quick_state_write_bytes(out, value.data(), size);
}

template <typename T>
static bool quick_state_read_value(std::istream &in, T &value) {
    in.read(reinterpret_cast<char *>(&value), sizeof(T));
    return in.good();
}

static bool quick_state_read_bytes(std::istream &in, void *data, const std::streamsize size) {
    in.read(reinterpret_cast<char *>(data), size);
    return in.good();
}

static bool quick_state_read_string(std::istream &in, std::string &value) {
    uint32_t size = 0;
    if (!quick_state_read_value(in, size) || (size > QUICKSTATE_MAX_STRING_BYTES))
        return false;

    value.resize(size);
    return size == 0 || quick_state_read_bytes(in, value.data(), size);
}

static bool quick_state_write_cpu_context(std::ostream &out, const CPUContext &context) {
    return quick_state_write_bytes(out, context.cpu_registers.data(), static_cast<std::streamsize>(context.cpu_registers.size() * sizeof(context.cpu_registers[0])))
        && quick_state_write_bytes(out, context.fpu_registers.data(), static_cast<std::streamsize>(context.fpu_registers.size() * sizeof(context.fpu_registers[0])))
        && quick_state_write_value(out, context.cpsr)
        && quick_state_write_value(out, context.fpscr);
}

static bool quick_state_read_cpu_context(std::istream &in, CPUContext &context) {
    return quick_state_read_bytes(in, context.cpu_registers.data(), static_cast<std::streamsize>(context.cpu_registers.size() * sizeof(context.cpu_registers[0])))
        && quick_state_read_bytes(in, context.fpu_registers.data(), static_cast<std::streamsize>(context.fpu_registers.size() * sizeof(context.fpu_registers[0])))
        && quick_state_read_value(in, context.cpsr)
        && quick_state_read_value(in, context.fpscr);
}

static bool quick_state_read_header(std::istream &in, QuickStateDiskHeader &header) {
    char magic[sizeof(QUICKSTATE_MAGIC)]{};
    uint32_t version = 0;
    uint32_t page_size = 0;

    if (!quick_state_read_bytes(in, magic, sizeof(magic)) || std::memcmp(magic, QUICKSTATE_MAGIC, sizeof(QUICKSTATE_MAGIC)) != 0)
        return false;
    if (!quick_state_read_value(in, version) || (version == 0) || (version > QUICKSTATE_VERSION))
        return false;
    if (!quick_state_read_value(in, page_size) || (page_size != QUICKSTATE_PAGE_SIZE))
        return false;
    if (version >= 2 && !quick_state_read_value(in, header.compression_level))
        return false;
    if (!quick_state_read_string(in, header.title_id) || !quick_state_read_string(in, header.title))
        return false;
    if (!quick_state_read_value(in, header.byte_count))
        return false;
    if (!quick_state_read_value(in, header.thread_count))
        return false;
    if (!quick_state_read_value(in, header.memory_page_count))
        return false;
    if (version >= 3) {
        if (!quick_state_read_value(in, header.allocator_word_count))
            return false;
        if (!quick_state_read_value(in, header.allocation_page_count))
            return false;
    }

    header.version = version;
    return true;
}

static uint32_t pack_quick_state_alloc_page(const AllocMemPage &page) {
    return (static_cast<uint32_t>(page.allocated) << 28) | (page.size & 0x0FFFFFFFU);
}

static void unpack_quick_state_alloc_page(const uint32_t packed, AllocMemPage &page) {
    page.allocated = (packed >> 28) & 0xFU;
    page.size = packed & 0x0FFFFFFFU;
}

static uint64_t quick_state_file_size(EmuEnvState &emuenv, const std::string &title_id) {
    const fs::path state_file = quick_state_file(emuenv, title_id);
    boost::system::error_code ec;
    const auto size = fs::file_size(state_file, ec);
    return ec ? 0 : size;
}

static bool quick_state_compress_page(const std::vector<uint8_t> &input, std::vector<uint8_t> &output, const int compression_level) {
    if (compression_level <= 0)
        return false;

    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(input.size()));
    output.resize(static_cast<size_t>(compressed_size));
    const int result = mz_compress2(output.data(), &compressed_size, input.data(), static_cast<mz_ulong>(input.size()), std::clamp(compression_level, 1, 9));
    if (result != MZ_OK || compressed_size >= input.size())
        return false;

    output.resize(static_cast<size_t>(compressed_size));
    return true;
}

static bool quick_state_decompress_page(const std::vector<uint8_t> &input, std::vector<uint8_t> &output, const uint32_t raw_size) {
    output.resize(raw_size);
    mz_ulong output_size = static_cast<mz_ulong>(raw_size);
    const int result = mz_uncompress(output.data(), &output_size, input.data(), static_cast<mz_ulong>(input.size()));
    return result == MZ_OK && output_size == raw_size;
}

static uint32_t quick_state_crc32(const std::vector<uint8_t> &input) {
    return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, input.data(), input.size()));
}

static bool quick_state_write_section(std::ostream &out, const QuickStateSection &section) {
    if (section.tag.empty() || (section.bytes.size() > QUICKSTATE_MAX_SECTION_BYTES))
        return false;

    const auto section_size = static_cast<uint64_t>(section.bytes.size());
    const uint32_t section_crc32 = quick_state_crc32(section.bytes);
    return quick_state_write_string(out, section.tag)
        && quick_state_write_value(out, section.version)
        && quick_state_write_value(out, section_size)
        && quick_state_write_value(out, section_crc32)
        && (section.bytes.empty() || quick_state_write_bytes(out, section.bytes.data(), static_cast<std::streamsize>(section.bytes.size())));
}

static bool quick_state_read_section(std::istream &in, QuickStateSection &section) {
    uint64_t section_size = 0;
    uint32_t section_crc32 = 0;
    if (!quick_state_read_string(in, section.tag)
        || section.tag.empty()
        || !quick_state_read_value(in, section.version)
        || !quick_state_read_value(in, section_size)
        || (section_size > QUICKSTATE_MAX_SECTION_BYTES)
        || !quick_state_read_value(in, section_crc32)) {
        return false;
    }

    section.bytes.resize(static_cast<size_t>(section_size));
    if (section_size > 0 && !quick_state_read_bytes(in, section.bytes.data(), static_cast<std::streamsize>(section.bytes.size())))
        return false;

    return quick_state_crc32(section.bytes) == section_crc32;
}

static bool memory_page_is_allocated(const MemState &mem, const uint32_t page) {
    const uint32_t word = mem.allocator.words[page >> 5];
    return (word & (1U << (page & 31))) == 0;
}

static bool valid_quick_state_page(const MemState &mem, const Address address, const uint32_t size) {
    if (address > (std::numeric_limits<Address>::max() - size))
        return false;

    return is_valid_addr_range(mem, address, address + size);
}

static bool wait_for_guest_threads_paused(KernelState &kernel, std::string_view operation) {
    const auto deadline = std::chrono::steady_clock::now() + QUICKSTATE_PAUSE_TIMEOUT;
    uint32_t stop_pulses = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        bool all_paused = true;
        {
            const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
            for (const auto &[_, thread] : kernel.threads) {
                const std::lock_guard<std::mutex> thread_lock(thread->mutex);
                if (thread->status == ThreadStatus::run) {
                    all_paused = false;
                    if (thread->cpu)
                        stop(*thread->cpu);
                    break;
                }
            }
        }

        if (all_paused)
            return true;

        stop_pulses++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<std::string> running_threads;
    {
        const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
        for (const auto &[thread_id, thread] : kernel.threads) {
            const std::lock_guard<std::mutex> thread_lock(thread->mutex);
            if (thread->status != ThreadStatus::run)
                continue;

            running_threads.push_back(fmt::format("{}:{}", thread_id, thread->name));
            if (running_threads.size() >= 8)
                break;
        }
    }

    LOG_WARN("Quickstate {} timed out after {} ms and {} stop pulse(s); still-running guest threads: {}",
        operation,
        QUICKSTATE_PAUSE_TIMEOUT.count(),
        stop_pulses,
        running_threads.empty() ? "none listed" : fmt::format("{}", fmt::join(running_threads, ", ")));
    return false;
}

static bool pause_for_quick_state(EmuEnvState &emuenv, const std::string_view operation, bool &already_paused) {
    already_paused = emuenv.kernel.is_threads_paused();
    if (!already_paused)
        app::switch_state(emuenv, true);

    if (wait_for_guest_threads_paused(emuenv.kernel, operation))
        return true;

    if (!already_paused)
        app::switch_state(emuenv, false);
    return false;
}

static void resume_after_quick_state(EmuEnvState &emuenv, const bool already_paused) {
    if (!already_paused) {
        app::switch_state(emuenv, false);
        quick_state_pause_epoch++;
    }
}

static bool capture_quick_state(EmuEnvState &emuenv, QuickStateSlot &slot) {
    if (emuenv.io.title_id.empty())
        return false;

    bool already_paused = false;
    if (!pause_for_quick_state(emuenv, "capture", already_paused)) {
        LOG_WARN("Failed to capture quickstate for {} because guest threads did not pause in time.", emuenv.io.title_id);
        return false;
    }

    QuickStateSlot captured;
    captured.valid = true;
    captured.restore_requires_same_pause = true;
    captured.title_id = emuenv.io.title_id;
    captured.title = emuenv.current_app_title;
    captured.pause_epoch = quick_state_pause_epoch;

    {
        const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);
        for (const auto &[thread_id, thread] : emuenv.kernel.threads) {
            if (!thread->cpu)
                continue;

            QuickStateThreadContext thread_context;
            thread_context.id = thread_id;
            thread_context.name = thread->name;
            thread_context.entry_point = thread->entry_point;
            thread_context.stack_address = thread->stack.get();
            thread_context.stack_size = static_cast<uint32_t>(thread->stack_size);
            thread_context.tls_address = thread->tls.get();
            thread_context.priority = thread->priority;
            thread_context.affinity_mask = thread->affinity_mask;
            thread_context.context = save_context(*thread->cpu);
            captured.thread_contexts.push_back(std::move(thread_context));
        }
    }

    const auto page_count = static_cast<uint32_t>(emuenv.mem.allocator.max_offset);
    {
        const std::lock_guard<std::mutex> mem_lock(emuenv.mem.generation_mutex);
        captured.allocator_words = emuenv.mem.allocator.words;
        captured.allocation_pages.reserve(page_count);
        for (uint32_t page = 0; page < page_count; page++)
            captured.allocation_pages.push_back(pack_quick_state_alloc_page(emuenv.mem.alloc_table[page]));
    }

    captured.memory_pages.reserve(page_count / 8);
    for (uint32_t page = 1; page < page_count; page++) {
        if (!memory_page_is_allocated(emuenv.mem, page))
            continue;

        const Address address = page * QUICKSTATE_PAGE_SIZE;
        if (!valid_quick_state_page(emuenv.mem, address, QUICKSTATE_PAGE_SIZE))
            continue;

        QuickStateMemoryPage snapshot_page;
        snapshot_page.address = address;
        snapshot_page.bytes.resize(QUICKSTATE_PAGE_SIZE);
        std::memcpy(snapshot_page.bytes.data(), Ptr<uint8_t>(address).get(emuenv.mem), QUICKSTATE_PAGE_SIZE);
        captured.byte_count += snapshot_page.bytes.size();
        captured.memory_pages.push_back(std::move(snapshot_page));
    }

    slot = std::move(captured);
    resume_after_quick_state(emuenv, already_paused);

    return true;
}

static bool save_quick_state_to_disk(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    if (!slot.valid || slot.title_id.empty())
        return false;

    const fs::path state_dir = quick_state_dir(emuenv, slot.title_id);
    const fs::path state_file = quick_state_file(emuenv, slot.title_id);
    const fs::path tmp_file = state_file.string() + ".tmp";
    const fs::path backup_file = state_file.string() + ".bak";
    fs::create_directories(state_dir);

    {
        fs::ofstream out(tmp_file, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        const uint32_t compression_level = static_cast<uint32_t>(std::clamp(emuenv.cfg.save_state_compression_level, 0, 9));
        const uint32_t thread_count = static_cast<uint32_t>(slot.thread_contexts.size());
        const uint64_t memory_page_count = static_cast<uint64_t>(slot.memory_pages.size());
        const uint32_t allocator_word_count = static_cast<uint32_t>(slot.allocator_words.size());
        const uint32_t allocation_page_count = static_cast<uint32_t>(slot.allocation_pages.size());
        if (slot.sections.size() > QUICKSTATE_MAX_SECTION_COUNT)
            return false;

        if (!quick_state_write_bytes(out, QUICKSTATE_MAGIC, sizeof(QUICKSTATE_MAGIC))
            || !quick_state_write_value(out, QUICKSTATE_VERSION)
            || !quick_state_write_value(out, QUICKSTATE_PAGE_SIZE)
            || !quick_state_write_value(out, compression_level)
            || !quick_state_write_string(out, slot.title_id)
            || !quick_state_write_string(out, slot.title)
            || !quick_state_write_value(out, slot.byte_count)
            || !quick_state_write_value(out, thread_count)
            || !quick_state_write_value(out, memory_page_count)
            || !quick_state_write_value(out, allocator_word_count)
            || !quick_state_write_value(out, allocation_page_count)) {
            return false;
        }

        for (const auto word : slot.allocator_words) {
            if (!quick_state_write_value(out, word))
                return false;
        }

        for (const auto page : slot.allocation_pages) {
            if (!quick_state_write_value(out, page))
                return false;
        }

        for (const auto &thread_context : slot.thread_contexts) {
            if (!quick_state_write_value(out, thread_context.id)
                || !quick_state_write_string(out, thread_context.name)
                || !quick_state_write_value(out, thread_context.entry_point)
                || !quick_state_write_value(out, thread_context.stack_address)
                || !quick_state_write_value(out, thread_context.stack_size)
                || !quick_state_write_value(out, thread_context.tls_address)
                || !quick_state_write_value(out, thread_context.priority)
                || !quick_state_write_value(out, thread_context.affinity_mask)
                || !quick_state_write_cpu_context(out, thread_context.context)) {
                return false;
            }
        }

        for (const auto &page : slot.memory_pages) {
            const uint32_t raw_page_size = static_cast<uint32_t>(page.bytes.size());
            uint32_t compression_method = QUICKSTATE_COMPRESSION_NONE;
            std::vector<uint8_t> compressed_page;
            const std::vector<uint8_t> *page_bytes = &page.bytes;
            const uint32_t raw_crc32 = quick_state_crc32(page.bytes);
            if (quick_state_compress_page(page.bytes, compressed_page, static_cast<int>(compression_level))) {
                compression_method = QUICKSTATE_COMPRESSION_MINIZ;
                page_bytes = &compressed_page;
            }

            const uint32_t stored_page_size = static_cast<uint32_t>(page_bytes->size());
            if (!quick_state_write_value(out, page.address)
                || !quick_state_write_value(out, raw_page_size)
                || !quick_state_write_value(out, stored_page_size)
                || !quick_state_write_value(out, compression_method)
                || !quick_state_write_value(out, raw_crc32)
                || !quick_state_write_bytes(out, page_bytes->data(), stored_page_size)) {
                return false;
            }
        }

        const uint32_t section_count = static_cast<uint32_t>(slot.sections.size());
        if (!quick_state_write_value(out, section_count))
            return false;
        for (const auto &section : slot.sections) {
            if (!quick_state_write_section(out, section))
                return false;
        }
    }

    boost::system::error_code ec;
    fs::remove(backup_file, ec);
    ec.clear();
    if (fs::exists(state_file, ec)) {
        ec.clear();
        fs::rename(state_file, backup_file, ec);
        if (ec) {
            fs::remove(tmp_file);
            LOG_WARN("Failed to stage old quickstate backup {}: {}", backup_file, ec.message());
            return false;
        }
    }
    ec.clear();
    fs::rename(tmp_file, state_file, ec);
    if (ec) {
        fs::remove(tmp_file);
        boost::system::error_code restore_ec;
        if (fs::exists(backup_file, restore_ec)) {
            restore_ec.clear();
            fs::rename(backup_file, state_file, restore_ec);
        }
        LOG_WARN("Failed to finalize durable quickstate {}: {}", state_file, ec.message());
        return false;
    }
    fs::remove(backup_file, ec);

    return true;
}

static bool read_quick_state_disk_header(EmuEnvState &emuenv, const std::string &title_id, QuickStateDiskHeader &header) {
    const fs::path state_file = quick_state_file(emuenv, title_id);
    fs::ifstream in(state_file, std::ios::binary);
    if (!in)
        return false;

    return quick_state_read_header(in, header) && (header.title_id == title_id);
}

static bool load_quick_state_from_disk(EmuEnvState &emuenv, const std::string &title_id, QuickStateSlot &slot) {
    const fs::path state_file = quick_state_file(emuenv, title_id);
    fs::ifstream in(state_file, std::ios::binary);
    if (!in)
        return false;

    QuickStateDiskHeader header;
    if (!quick_state_read_header(in, header) || (header.title_id != title_id))
        return false;
    if (header.thread_count > 1024 || header.memory_page_count > emuenv.mem.allocator.max_offset)
        return false;
    if ((header.allocator_word_count > emuenv.mem.allocator.words.size()) || (header.allocation_page_count > emuenv.mem.allocator.max_offset))
        return false;

    QuickStateSlot loaded;
    loaded.valid = true;
    loaded.restore_requires_same_pause = true;
    loaded.title_id = header.title_id;
    loaded.title = header.title;
    loaded.byte_count = header.byte_count;
    loaded.thread_contexts.reserve(header.thread_count);
    loaded.memory_pages.reserve(static_cast<size_t>(header.memory_page_count));
    loaded.allocator_words.reserve(header.allocator_word_count);
    loaded.allocation_pages.reserve(header.allocation_page_count);

    for (uint32_t i = 0; i < header.allocator_word_count; i++) {
        uint32_t word = 0;
        if (!quick_state_read_value(in, word))
            return false;
        loaded.allocator_words.push_back(word);
    }

    for (uint32_t i = 0; i < header.allocation_page_count; i++) {
        uint32_t page = 0;
        if (!quick_state_read_value(in, page))
            return false;
        loaded.allocation_pages.push_back(page);
    }

    for (uint32_t i = 0; i < header.thread_count; i++) {
        QuickStateThreadContext thread_context;
        if (!quick_state_read_value(in, thread_context.id)
            || !quick_state_read_string(in, thread_context.name)
            || !quick_state_read_value(in, thread_context.entry_point)
            || !quick_state_read_value(in, thread_context.stack_address)
            || !quick_state_read_value(in, thread_context.stack_size)
            || !quick_state_read_value(in, thread_context.tls_address)
            || !quick_state_read_value(in, thread_context.priority)
            || !quick_state_read_value(in, thread_context.affinity_mask)
            || !quick_state_read_cpu_context(in, thread_context.context)) {
            return false;
        }
        loaded.thread_contexts.push_back(std::move(thread_context));
    }

    uint64_t byte_count = 0;
    for (uint64_t i = 0; i < header.memory_page_count; i++) {
        QuickStateMemoryPage page;
        uint32_t raw_page_size = 0;
        uint32_t stored_page_size = 0;
        uint32_t compression_method = QUICKSTATE_COMPRESSION_NONE;
        uint32_t raw_crc32 = 0;
        if (!quick_state_read_value(in, page.address) || !quick_state_read_value(in, raw_page_size))
            return false;
        if (header.version >= 2) {
            if (!quick_state_read_value(in, stored_page_size) || !quick_state_read_value(in, compression_method))
                return false;
        } else {
            stored_page_size = raw_page_size;
        }

        if ((page.address % QUICKSTATE_PAGE_SIZE) != 0 || (raw_page_size != QUICKSTATE_PAGE_SIZE) || (stored_page_size == 0))
            return false;
        if (header.version >= 4 && !quick_state_read_value(in, raw_crc32))
            return false;
        if ((compression_method == QUICKSTATE_COMPRESSION_MINIZ) && (stored_page_size > mz_compressBound(raw_page_size)))
            return false;

        std::vector<uint8_t> stored_page(stored_page_size);
        if (!quick_state_read_bytes(in, stored_page.data(), stored_page_size))
            return false;

        if (compression_method == QUICKSTATE_COMPRESSION_NONE) {
            if (stored_page_size != raw_page_size)
                return false;
            page.bytes = std::move(stored_page);
        } else if (compression_method == QUICKSTATE_COMPRESSION_MINIZ) {
            if (!quick_state_decompress_page(stored_page, page.bytes, raw_page_size))
                return false;
        } else {
            return false;
        }

        if (header.version >= 4 && quick_state_crc32(page.bytes) != raw_crc32)
            return false;

        byte_count += raw_page_size;
        loaded.memory_pages.push_back(std::move(page));
    }

    if (byte_count != header.byte_count)
        return false;
    if (header.version >= 5) {
        uint32_t section_count = 0;
        if (!quick_state_read_value(in, section_count) || (section_count > QUICKSTATE_MAX_SECTION_COUNT))
            return false;
        loaded.sections.reserve(section_count);
        for (uint32_t i = 0; i < section_count; i++) {
            QuickStateSection section;
            if (!quick_state_read_section(in, section))
                return false;
            loaded.sections.push_back(std::move(section));
        }
    }
    in.peek();
    if (!in.eof())
        return false;

    slot = std::move(loaded);
    return true;
}

static ThreadStatePtr find_quick_state_restore_thread(KernelState &kernel, const QuickStateThreadContext &saved_thread, std::set<SceUID> &matched_threads) {
    const auto exact_thread = kernel.threads.find(saved_thread.id);
    if ((exact_thread != kernel.threads.end()) && exact_thread->second->cpu && !matched_threads.contains(exact_thread->first))
        return exact_thread->second;

    for (const auto &[thread_id, thread] : kernel.threads) {
        if (matched_threads.contains(thread_id) || !thread->cpu)
            continue;
        if ((thread->name == saved_thread.name) && (thread->entry_point == saved_thread.entry_point))
            return thread;
    }

    for (const auto &[thread_id, thread] : kernel.threads) {
        if (matched_threads.contains(thread_id) || !thread->cpu)
            continue;
        if (thread->name == saved_thread.name)
            return thread;
    }

    return nullptr;
}

static bool preflight_quick_state_restore_threads(KernelState &kernel, const QuickStateSlot &slot, std::vector<ThreadStatePtr> &matched_threads) {
    matched_threads.clear();

    std::set<SceUID> matched_thread_ids;
    uint32_t current_cpu_thread_count = 0;
    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    for (const auto &[_, thread] : kernel.threads) {
        if (thread->cpu)
            current_cpu_thread_count++;
    }

    if (current_cpu_thread_count != slot.thread_contexts.size()) {
        LOG_WARN("Refused quickstate restore for {} because current CPU thread count {} does not match saved count {}.",
            slot.title_id, current_cpu_thread_count, slot.thread_contexts.size());
        return false;
    }

    matched_threads.reserve(slot.thread_contexts.size());
    for (const auto &thread_context : slot.thread_contexts) {
        const auto thread = find_quick_state_restore_thread(kernel, thread_context, matched_thread_ids);
        if (!thread || !thread->cpu) {
            LOG_WARN("Refused quickstate restore for {} because saved thread {} ({}) could not be matched before touching guest memory.",
                slot.title_id, thread_context.id, thread_context.name);
            return false;
        }

        matched_thread_ids.insert(thread->id);
        matched_threads.push_back(thread);
    }

    return true;
}

static bool quick_state_alloc_page_is_allocated(const uint32_t packed) {
    return ((packed >> 28) & 0xFU) != 0;
}

static uint32_t quick_state_alloc_page_size(const uint32_t packed) {
    return packed & 0x0FFFFFFFU;
}

static bool quick_state_commit_guest_pages(MemState &mem, const uint32_t start_page, const uint32_t end_page) {
    if (end_page <= start_page)
        return true;

    const Address address = start_page * QUICKSTATE_PAGE_SIZE;
    const uint32_t size = (end_page - start_page) * QUICKSTATE_PAGE_SIZE;
    return commit_range(mem, address, size);
}

static bool restore_quick_state_allocation_map(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    if (slot.allocator_words.empty() || slot.allocation_pages.empty())
        return true;

    MemState &mem = emuenv.mem;
    if ((slot.allocator_words.size() != mem.allocator.words.size()) || (slot.allocation_pages.size() != mem.allocator.max_offset)) {
        LOG_WARN("Refused quickstate restore for {} because the saved allocation map does not match this emulator memory layout.", slot.title_id);
        return false;
    }

    for (uint32_t page = 1; page < slot.allocation_pages.size();) {
        const uint32_t packed = slot.allocation_pages[page];
        if (!quick_state_alloc_page_is_allocated(packed)) {
            page++;
            continue;
        }

        const uint32_t block_pages = std::max(quick_state_alloc_page_size(packed), 1U);
        const uint32_t end_page = std::min<uint32_t>(page + block_pages, static_cast<uint32_t>(slot.allocation_pages.size()));
        if (!quick_state_commit_guest_pages(mem, page, end_page)) {
            LOG_WARN("Refused quickstate restore for {} because guest memory pages {}-{} could not be committed.", slot.title_id, page, end_page);
            return false;
        }
        page = end_page;
    }

    {
        const std::lock_guard<std::mutex> mem_lock(mem.generation_mutex);
        mem.allocator.words = slot.allocator_words;
        for (uint32_t page = 0; page < slot.allocation_pages.size(); page++)
            unpack_quick_state_alloc_page(slot.allocation_pages[page], mem.alloc_table[page]);
    }

    {
        const std::lock_guard<std::mutex> protect_lock(mem.protect_mutex);
        mem.protect_tree.clear();
    }

    for (uint32_t page = 1; page < slot.allocation_pages.size();) {
        const uint32_t packed = slot.allocation_pages[page];
        if (!quick_state_alloc_page_is_allocated(packed)) {
            page++;
            continue;
        }

        const uint32_t block_pages = std::max(quick_state_alloc_page_size(packed), 1U);
        const Address address = page * QUICKSTATE_PAGE_SIZE;
        unprotect_inner(mem, address, block_pages * QUICKSTATE_PAGE_SIZE);
        page = std::min<uint32_t>(page + block_pages, static_cast<uint32_t>(slot.allocation_pages.size()));
    }

    protect_inner(mem, 0, mem.host_page_size, MemPerm::None);

    return true;
}

static bool quick_state_page_can_restore(const MemState &mem, const QuickStateMemoryPage &page, uint32_t &missing_pages) {
    const auto page_size = static_cast<uint32_t>(page.bytes.size());
    if (valid_quick_state_page(mem, page.address, page_size))
        return true;

    missing_pages++;
    return true;
}

static void reset_quick_state_runtime_render_state(EmuEnvState &emuenv) {
    emuenv.gxm.display_queue.reset();

    if (!emuenv.renderer)
        return;

    emuenv.renderer->command_buffer_queue.reset();
    emuenv.renderer->last_scene_id = 0;
    emuenv.renderer->should_display = false;
    if (auto texture_cache = emuenv.renderer->get_texture_cache())
        texture_cache->reset_runtime_cache();
}

static std::string quick_state_join_strings(const std::vector<std::string> &values) {
    if (values.empty())
        return "none";

    std::string joined = values.front();
    for (size_t i = 1; i < values.size(); i++) {
        joined += ", ";
        joined += values[i];
    }
    return joined;
}

static std::vector<std::string> quick_state_section_tags(const QuickStateSlot &slot) {
    std::vector<std::string> tags;
    tags.reserve(slot.sections.size());
    for (const auto &section : slot.sections)
        tags.push_back(fmt::format("{}:v{}:{}B", section.tag, section.version, section.bytes.size()));
    return tags;
}

static const QuickStateSection *quick_state_find_section(const QuickStateSlot &slot, const std::string_view tag) {
    const auto section = std::find_if(slot.sections.begin(), slot.sections.end(), [tag](const QuickStateSection &candidate) {
        return candidate.tag == tag;
    });
    return section == slot.sections.end() ? nullptr : &*section;
}

static std::string quick_state_section_text(const QuickStateSection &section) {
    return std::string(section.bytes.begin(), section.bytes.end());
}

static std::map<std::string, std::string> quick_state_parse_text_section(const QuickStateSection &section) {
    std::map<std::string, std::string> values;
    std::istringstream text(quick_state_section_text(section));
    std::string line;
    while (std::getline(text, line)) {
        if (line.empty() || line.front() == '#')
            continue;

        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;

        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}

static bool quick_state_parse_u64(const std::map<std::string, std::string> &values, const std::string &key, uint64_t &out) {
    const auto value = values.find(key);
    if (value == values.end() || value->second.empty())
        return false;

    size_t consumed = 0;
    try {
        out = std::stoull(value->second, &consumed, 10);
    } catch (...) {
        return false;
    }
    return consumed == value->second.size();
}

static bool quick_state_parse_u32(const std::map<std::string, std::string> &values, const std::string &key, uint32_t &out) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64(values, key, parsed) || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

static bool quick_state_parse_i32_text(const std::string &text, int32_t &out) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    long parsed = 0;
    try {
        parsed = std::stol(text, &consumed, 10);
    } catch (...) {
        return false;
    }
    if (consumed != text.size() || parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
        return false;
    out = static_cast<int32_t>(parsed);
    return true;
}

static bool quick_state_parse_u32_text(const std::string &text, uint32_t &out) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(text, &consumed, 10);
    } catch (...) {
        return false;
    }
    if (consumed != text.size() || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

static bool quick_state_parse_u64_text(const std::string &text, uint64_t &out, const int base = 10) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    try {
        out = std::stoull(text, &consumed, base);
    } catch (...) {
        return false;
    }
    return consumed == text.size();
}

static bool quick_state_parse_bool_text(const std::string &text, bool &out) {
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    return false;
}

static bool quick_state_parse_float_text(const std::string &text, float &out) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    try {
        out = std::stof(text, &consumed);
    } catch (...) {
        return false;
    }
    return consumed == text.size() && std::isfinite(out);
}

static bool quick_state_parse_size(const std::map<std::string, std::string> &values, const std::string &key, size_t &out) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64(values, key, parsed) || parsed > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return false;
    out = static_cast<size_t>(parsed);
    return true;
}

static std::map<std::string, std::string> quick_state_parse_semicolon_fields(const std::string &text) {
    std::map<std::string, std::string> fields;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(';', start);
        const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t separator = item.find('=');
        if (separator != std::string::npos)
            fields[item.substr(0, separator)] = item.substr(separator + 1);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return fields;
}

static bool quick_state_parse_uid_from_key(const std::string &key, const std::string_view prefix, const std::string_view suffix, SceUID &uid) {
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size() + suffix.size())
        return false;
    if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;

    const std::string id_text = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    int32_t parsed_id = 0;
    if (!quick_state_parse_i32_text(id_text, parsed_id) || parsed_id <= 0)
        return false;
    uid = static_cast<SceUID>(parsed_id);
    return true;
}

static bool quick_state_parse_named_common_fields(const std::map<std::string, std::string> &fields, std::string &name, uint32_t &attr) {
    if (!fields.contains("name") || !fields.contains("attr"))
        return false;

    name = fields.at("name");
    return quick_state_parse_u32_text(fields.at("attr"), attr);
}

static bool quick_state_parse_waiting_field(const std::map<std::string, std::string> &fields, uint32_t &waiting_count) {
    return fields.contains("waiting") && quick_state_parse_u32_text(fields.at("waiting"), waiting_count);
}

static bool quick_state_parse_rwlock_owners_text(const std::string &text, std::map<SceUID, int32_t> &owners) {
    owners.clear();
    if (text.empty() || text == "none")
        return true;

    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(',', start);
        const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t separator = item.find(':');
        if (separator == std::string::npos)
            return false;

        int32_t parsed_id = 0;
        int32_t parsed_count = 0;
        if (!quick_state_parse_i32_text(item.substr(0, separator), parsed_id)
            || !quick_state_parse_i32_text(item.substr(separator + 1), parsed_count)
            || parsed_id <= 0
            || parsed_count <= 0) {
            return false;
        }
        owners[static_cast<SceUID>(parsed_id)] = parsed_count;

        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    return true;
}

static bool quick_state_parse_timing_snapshot(const QuickStateSlot &slot, QuickStateTimingSnapshot &timing) {
    timing = {};
    const QuickStateSection *section = quick_state_find_section(slot, "thor.timing");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.timing.v1")
        return false;

    if (!quick_state_parse_u64(values, "kernel_start_tick", timing.kernel_start_tick)
        || !quick_state_parse_u64(values, "kernel_base_tick", timing.kernel_base_tick)
        || !quick_state_parse_u64(values, "kernel_guest_tick", timing.kernel_guest_tick)
        || !quick_state_parse_u64(values, "kernel_process_time", timing.kernel_process_time)
        || !quick_state_parse_u32(values, "kernel_speed_percent", timing.kernel_speed_percent)
        || !quick_state_parse_u64(values, "speed_anchor_host_process_us", timing.speed_anchor_host_process_us)
        || !quick_state_parse_u64(values, "speed_anchor_guest_process_us", timing.speed_anchor_guest_process_us)) {
        return false;
    }

    if (timing.kernel_speed_percent == 0 || timing.kernel_speed_percent > 1000)
        return false;

    timing.valid = true;
    return true;
}

static bool quick_state_parse_thread_id_from_key(const std::string &key, SceUID &thread_id) {
    constexpr std::string_view prefix = "thread.";
    constexpr std::string_view suffix = ".name";
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size() + suffix.size())
        return false;
    if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;

    const std::string id_text = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    int32_t parsed_id = 0;
    if (!quick_state_parse_i32_text(id_text, parsed_id) || parsed_id <= 0)
        return false;
    thread_id = static_cast<SceUID>(parsed_id);
    return true;
}

static bool quick_state_parse_thread_metadata(const std::map<std::string, std::string> &values, const std::string &key, const std::string &value, QuickStateThreadMetadata &metadata) {
    if (!quick_state_parse_thread_id_from_key(key, metadata.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    const auto required_fields = {
        "name",
        "status",
        "entry",
        "stack",
        "stack_size",
        "tls",
        "priority",
        "affinity",
        "start_tick",
        "last_vblank_waited",
        "returned_value",
        "processing_callbacks",
        "callbacks",
        "waiting_threads",
    };
    for (const auto required : required_fields) {
        if (!fields.contains(required))
            return false;
    }

    metadata.name = fields.at("name");
    metadata.status = fields.at("status");
    uint64_t parsed_hex = 0;
    uint32_t parsed_u32 = 0;
    int32_t parsed_i32 = 0;
    if (!quick_state_parse_u64_text(fields.at("entry"), parsed_hex, 0))
        return false;
    metadata.entry_point = static_cast<Address>(parsed_hex);
    if (!quick_state_parse_u64_text(fields.at("stack"), parsed_hex, 0))
        return false;
    metadata.stack_address = static_cast<Address>(parsed_hex);
    if (!quick_state_parse_u32_text(fields.at("stack_size"), metadata.stack_size))
        return false;
    if (!quick_state_parse_u64_text(fields.at("tls"), parsed_hex, 0))
        return false;
    metadata.tls_address = static_cast<Address>(parsed_hex);
    if (!quick_state_parse_i32_text(fields.at("priority"), parsed_i32))
        return false;
    metadata.priority = parsed_i32;
    if (!quick_state_parse_i32_text(fields.at("affinity"), parsed_i32))
        return false;
    metadata.affinity_mask = parsed_i32;
    if (!quick_state_parse_u64_text(fields.at("start_tick"), metadata.start_tick)
        || !quick_state_parse_u64_text(fields.at("last_vblank_waited"), metadata.last_vblank_waited)
        || !quick_state_parse_u32_text(fields.at("returned_value"), metadata.returned_value)
        || !quick_state_parse_bool_text(fields.at("processing_callbacks"), metadata.is_processing_callbacks)
        || !quick_state_parse_u32_text(fields.at("callbacks"), metadata.callback_count)
        || !quick_state_parse_u32_text(fields.at("waiting_threads"), metadata.waiting_thread_count)) {
        return false;
    }

    return true;
}

static bool quick_state_parse_thread_metadata_section(const QuickStateSlot &slot, std::map<SceUID, QuickStateThreadMetadata> &metadata_by_id) {
    metadata_by_id.clear();
    const QuickStateSection *section = quick_state_find_section(slot, "thor.kernel.objects");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.kernel.objects.v1")
        return false;

    for (const auto &[key, value] : values) {
        if (key.rfind("thread.", 0) != 0)
            continue;

        QuickStateThreadMetadata metadata;
        if (!quick_state_parse_thread_metadata(values, key, value, metadata))
            return false;
        metadata_by_id[metadata.id] = std::move(metadata);
    }

    return metadata_by_id.size() == slot.thread_contexts.size();
}

static bool quick_state_parse_fd_from_key(const std::string &key, SceUID &fd) {
    constexpr std::string_view prefix = "file.";
    constexpr std::string_view suffix = ".vita";
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size() + suffix.size())
        return false;
    if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;

    const std::string fd_text = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    int32_t parsed_fd = 0;
    if (!quick_state_parse_i32_text(fd_text, parsed_fd) || parsed_fd < 0)
        return false;
    fd = static_cast<SceUID>(parsed_fd);
    return true;
}

static bool quick_state_parse_io_file_position(const std::string &key, const std::string &value, QuickStateIOFilePosition &position) {
    if (!quick_state_parse_fd_from_key(key, position.fd))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("vita=" + value);
    const auto required_fields = {
        "vita",
        "translated",
        "host",
        "open_mode",
        "memory",
        "offset",
    };
    for (const auto required : required_fields) {
        if (!fields.contains(required))
            return false;
    }

    int32_t parsed_i32 = 0;
    uint64_t parsed_u64 = 0;
    bool parsed_bool = false;
    position.vita_path = fields.at("vita");
    position.translated_path = fields.at("translated");
    position.host_path = fs_utils::utf8_to_path(fields.at("host"));
    if (!quick_state_parse_i32_text(fields.at("open_mode"), parsed_i32))
        return false;
    position.open_mode = parsed_i32;
    if (!quick_state_parse_bool_text(fields.at("memory"), parsed_bool))
        return false;
    position.memory_file = parsed_bool;
    if (!quick_state_parse_u64_text(fields.at("offset"), parsed_u64) || parsed_u64 > static_cast<uint64_t>(std::numeric_limits<SceOff>::max()))
        return false;
    position.offset = static_cast<SceOff>(parsed_u64);
    return true;
}

static bool quick_state_parse_io_file_positions_section(const QuickStateSlot &slot, std::map<SceUID, QuickStateIOFilePosition> &positions_by_fd, SceUID *next_fd_out = nullptr) {
    positions_by_fd.clear();
    const QuickStateSection *section = quick_state_find_section(slot, "thor.io.vfs");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.io.vfs.v1")
        return false;

    uint64_t expected_file_count = 0;
    if (!quick_state_parse_u64(values, "std_files", expected_file_count) || expected_file_count > 1024)
        return false;
    int32_t parsed_next_fd = 0;
    if (!values.contains("next_fd") || !quick_state_parse_i32_text(values.at("next_fd"), parsed_next_fd) || parsed_next_fd < 0)
        return false;

    for (const auto &[key, value] : values) {
        if (key.rfind("file.", 0) != 0)
            continue;

        QuickStateIOFilePosition position;
        if (!quick_state_parse_io_file_position(key, value, position))
            return false;
        positions_by_fd[position.fd] = std::move(position);
    }

    if (next_fd_out)
        *next_fd_out = static_cast<SceUID>(parsed_next_fd);
    return positions_by_fd.size() == expected_file_count;
}

static bool quick_state_parse_sync_simple_event(const std::string &key, const std::string &value, QuickStateSyncSimpleEvent &event) {
    if (!quick_state_parse_uid_from_key(key, "simple_event.", ".name", event.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    bool parsed_bool = false;
    if (!quick_state_parse_named_common_fields(fields, event.name, event.attr)
        || !fields.contains("pattern")
        || !fields.contains("last_user_data")
        || !fields.contains("auto_reset")
        || !fields.contains("cb_wakeup_only")
        || !quick_state_parse_u32_text(fields.at("pattern"), event.pattern)
        || !quick_state_parse_u64_text(fields.at("last_user_data"), event.last_user_data)
        || !quick_state_parse_bool_text(fields.at("auto_reset"), parsed_bool)) {
        return false;
    }
    event.auto_reset = parsed_bool;
    if (!quick_state_parse_bool_text(fields.at("cb_wakeup_only"), parsed_bool)
        || !quick_state_parse_waiting_field(fields, event.waiting_count)) {
        return false;
    }
    event.cb_wakeup_only = parsed_bool;
    return true;
}

static bool quick_state_parse_sync_timer(const std::string &key, const std::string &value, QuickStateSyncTimer &timer) {
    if (!quick_state_parse_uid_from_key(key, "timer.", ".name", timer.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    bool parsed_bool = false;
    if (!quick_state_parse_named_common_fields(fields, timer.name, timer.attr)
        || !fields.contains("started")
        || !fields.contains("repeat")
        || !fields.contains("pulse")
        || !fields.contains("event_set")
        || !fields.contains("time")
        || !fields.contains("next_event")
        || !fields.contains("next_event_delta_us")
        || !fields.contains("interval")
        || !quick_state_parse_bool_text(fields.at("started"), parsed_bool)) {
        return false;
    }
    timer.is_started = parsed_bool;
    if (!quick_state_parse_bool_text(fields.at("repeat"), parsed_bool))
        return false;
    timer.is_repeat = parsed_bool;
    if (!quick_state_parse_bool_text(fields.at("pulse"), parsed_bool))
        return false;
    timer.is_pulse = parsed_bool;
    if (!quick_state_parse_bool_text(fields.at("event_set"), parsed_bool)
        || !quick_state_parse_u64_text(fields.at("time"), timer.time)
        || !quick_state_parse_u64_text(fields.at("next_event"), timer.next_event)
        || !quick_state_parse_u64_text(fields.at("next_event_delta_us"), timer.next_event_delta_us)
        || !quick_state_parse_u64_text(fields.at("interval"), timer.event_interval)
        || !quick_state_parse_waiting_field(fields, timer.waiting_count)) {
        return false;
    }
    timer.event_set = parsed_bool;
    return true;
}

static bool quick_state_parse_sync_semaphore(const std::string &key, const std::string &value, QuickStateSyncSemaphore &semaphore) {
    if (!quick_state_parse_uid_from_key(key, "semaphore.", ".name", semaphore.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    if (!quick_state_parse_named_common_fields(fields, semaphore.name, semaphore.attr)
        || !fields.contains("init")
        || !fields.contains("value")
        || !fields.contains("max")
        || !quick_state_parse_i32_text(fields.at("init"), semaphore.init_val)
        || !quick_state_parse_i32_text(fields.at("value"), semaphore.value)
        || !quick_state_parse_i32_text(fields.at("max"), semaphore.max)
        || !quick_state_parse_waiting_field(fields, semaphore.waiting_count)) {
        return false;
    }
    return semaphore.max >= 0 && semaphore.init_val >= 0 && semaphore.value >= 0 && semaphore.value <= semaphore.max;
}

static bool quick_state_parse_sync_mutex(const std::string &key, const std::string &value, const bool lightweight, QuickStateSyncMutex &mutex) {
    if (!quick_state_parse_uid_from_key(key, lightweight ? "lwmutex." : "mutex.", ".name", mutex.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    int32_t parsed_owner = 0;
    uint64_t parsed_workarea = 0;
    if (!quick_state_parse_named_common_fields(fields, mutex.name, mutex.attr)
        || !fields.contains("init")
        || !fields.contains("lock_count")
        || !fields.contains("owner")
        || !quick_state_parse_i32_text(fields.at("init"), mutex.init_count)
        || !quick_state_parse_i32_text(fields.at("lock_count"), mutex.lock_count)
        || !quick_state_parse_i32_text(fields.at("owner"), parsed_owner)
        || !quick_state_parse_waiting_field(fields, mutex.waiting_count)) {
        return false;
    }

    if (lightweight) {
        if (!fields.contains("workarea") || !quick_state_parse_u64_text(fields.at("workarea"), parsed_workarea, 0))
            return false;
        mutex.workarea = static_cast<Address>(parsed_workarea);
    }

    if (parsed_owner < 0 || mutex.init_count < 0 || mutex.lock_count < 0)
        return false;
    mutex.owner = static_cast<SceUID>(parsed_owner);
    mutex.lightweight = lightweight;
    return true;
}

static bool quick_state_parse_sync_eventflag(const std::string &key, const std::string &value, QuickStateSyncEventFlag &eventflag) {
    if (!quick_state_parse_uid_from_key(key, "eventflag.", ".name", eventflag.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    return quick_state_parse_named_common_fields(fields, eventflag.name, eventflag.attr)
        && fields.contains("flags")
        && quick_state_parse_i32_text(fields.at("flags"), eventflag.flags)
        && quick_state_parse_waiting_field(fields, eventflag.waiting_count);
}

static bool quick_state_parse_sync_condvar(const std::string &key, const std::string &value, const bool lightweight, QuickStateSyncCondvar &condvar) {
    if (!quick_state_parse_uid_from_key(key, lightweight ? "lwcondvar." : "condvar.", ".name", condvar.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    int32_t parsed_mutex = 0;
    if (!quick_state_parse_named_common_fields(fields, condvar.name, condvar.attr)
        || !fields.contains("associated_mutex")
        || !quick_state_parse_i32_text(fields.at("associated_mutex"), parsed_mutex)
        || !quick_state_parse_waiting_field(fields, condvar.waiting_count)
        || parsed_mutex < 0) {
        return false;
    }
    condvar.associated_mutex = static_cast<SceUID>(parsed_mutex);
    condvar.lightweight = lightweight;
    return true;
}

static bool quick_state_parse_sync_rwlock(const std::string &key, const std::string &value, QuickStateSyncRWLock &rwlock) {
    if (!quick_state_parse_uid_from_key(key, "rwlock.", ".name", rwlock.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    int32_t parsed_state = 0;
    if (!quick_state_parse_named_common_fields(fields, rwlock.name, rwlock.attr)
        || !fields.contains("state")
        || !fields.contains("owners_detail")
        || !quick_state_parse_i32_text(fields.at("state"), parsed_state)
        || parsed_state < static_cast<int32_t>(RWLockState::Unlocked)
        || parsed_state > static_cast<int32_t>(RWLockState::WriteLocked)
        || !quick_state_parse_rwlock_owners_text(fields.at("owners_detail"), rwlock.owners)
        || !quick_state_parse_waiting_field(fields, rwlock.waiting_count)) {
        return false;
    }
    rwlock.state = static_cast<RWLockState>(parsed_state);
    return true;
}

static bool quick_state_parse_sync_msgpipe(const std::string &key, const std::string &value, QuickStateSyncMsgPipe &msgpipe) {
    if (!quick_state_parse_uid_from_key(key, "msgpipe.", ".name", msgpipe.id))
        return false;

    const auto fields = quick_state_parse_semicolon_fields("name=" + value);
    bool parsed_bool = false;
    if (!quick_state_parse_named_common_fields(fields, msgpipe.name, msgpipe.attr)
        || !fields.contains("senders")
        || !fields.contains("receivers")
        || !fields.contains("capacity")
        || !fields.contains("used")
        || !fields.contains("being_deleted")
        || !quick_state_parse_u32_text(fields.at("senders"), msgpipe.sender_count)
        || !quick_state_parse_u32_text(fields.at("receivers"), msgpipe.receiver_count)
        || !quick_state_parse_u64_text(fields.at("capacity"), msgpipe.capacity)
        || !quick_state_parse_u64_text(fields.at("used"), msgpipe.used)
        || !quick_state_parse_bool_text(fields.at("being_deleted"), parsed_bool)) {
        return false;
    }
    msgpipe.being_deleted = parsed_bool;
    return msgpipe.used <= msgpipe.capacity;
}

static bool quick_state_parse_sync_wait_queue_key(const std::string &key, QuickStateSyncWaitQueueEntry &entry) {
    constexpr std::string_view prefix = "wait.";
    if (key.rfind(prefix, 0) != 0)
        return false;

    const std::string rest = key.substr(prefix.size());
    const size_t kind_end = rest.find('.');
    if (kind_end == std::string::npos || kind_end == 0)
        return false;
    const size_t id_end = rest.find('.', kind_end + 1);
    if (id_end == std::string::npos || id_end == kind_end + 1 || id_end + 1 >= rest.size())
        return false;

    int32_t parsed_id = 0;
    uint32_t parsed_index = 0;
    if (!quick_state_parse_i32_text(rest.substr(kind_end + 1, id_end - kind_end - 1), parsed_id)
        || parsed_id <= 0
        || !quick_state_parse_u32_text(rest.substr(id_end + 1), parsed_index)) {
        return false;
    }

    entry.kind = rest.substr(0, kind_end);
    entry.object_id = static_cast<SceUID>(parsed_id);
    entry.index = parsed_index;
    return true;
}

static bool quick_state_parse_sync_wait_queue_entry(const std::string &key, const std::string &value, QuickStateSyncWaitQueueEntry &entry) {
    entry = {};
    if (!quick_state_parse_sync_wait_queue_key(key, entry))
        return false;

    const auto fields = quick_state_parse_semicolon_fields(value);
    int32_t parsed_thread_id = 0;
    if (!fields.contains("thread")
        || !fields.contains("priority")
        || !quick_state_parse_i32_text(fields.at("thread"), parsed_thread_id)
        || !quick_state_parse_i32_text(fields.at("priority"), entry.priority)
        || parsed_thread_id <= 0) {
        return false;
    }
    entry.thread_id = static_cast<SceUID>(parsed_thread_id);

    const auto parse_address = [&](const char *name, Address &out) {
        uint64_t parsed = 0;
        if (!fields.contains(name)
            || !quick_state_parse_u64_text(fields.at(name), parsed, 0)
            || parsed > static_cast<uint64_t>(std::numeric_limits<Address>::max())) {
            return false;
        }
        out = static_cast<Address>(parsed);
        return true;
    };
    const auto parse_optional_cancel_source = [&]() {
        entry.cancel_source = fields.contains("cancel") ? fields.at("cancel") : "none";
        return true;
    };

    if (entry.kind == "simple_event") {
        return fields.contains("pattern")
            && quick_state_parse_i32_text(fields.at("pattern"), entry.pattern)
            && parse_address("result_pattern", entry.result_pattern)
            && parse_address("user_data", entry.user_data);
    }
    if (entry.kind == "timer") {
        return parse_address("result_pattern", entry.result_pattern)
            && parse_address("user_data", entry.user_data);
    }
    if (entry.kind == "semaphore") {
        return fields.contains("signal")
            && quick_state_parse_i32_text(fields.at("signal"), entry.signal)
            && parse_optional_cancel_source();
    }
    if (entry.kind == "mutex" || entry.kind == "lwmutex") {
        return fields.contains("lock_count")
            && quick_state_parse_i32_text(fields.at("lock_count"), entry.lock_count);
    }
    if (entry.kind == "eventflag") {
        return fields.contains("wait")
            && fields.contains("flags")
            && quick_state_parse_i32_text(fields.at("wait"), entry.wait)
            && quick_state_parse_i32_text(fields.at("flags"), entry.flags)
            && parse_address("out_bits", entry.out_bits)
            && parse_optional_cancel_source();
    }
    if (entry.kind == "condvar" || entry.kind == "lwcondvar") {
        return true;
    }
    if (entry.kind == "rwlock") {
        return fields.contains("is_write")
            && quick_state_parse_bool_text(fields.at("is_write"), entry.is_write);
    }
    if (entry.kind == "msgpipe_sender" || entry.kind == "msgpipe_receiver") {
        return fields.contains("request_size")
            && quick_state_parse_u32_text(fields.at("request_size"), entry.request_size);
    }
    return false;
}

static bool quick_state_parse_sync_primitives_section(const QuickStateSlot &slot, QuickStateSyncSnapshot &snapshot) {
    snapshot = {};
    const QuickStateSection *section = quick_state_find_section(slot, "thor.kernel.objects");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.kernel.objects.v1")
        return false;

    size_t expected_simple_events = 0;
    size_t expected_timers = 0;
    size_t expected_semaphores = 0;
    size_t expected_mutexes = 0;
    size_t expected_lwmutexes = 0;
    size_t expected_eventflags = 0;
    size_t expected_condvars = 0;
    size_t expected_lwcondvars = 0;
    size_t expected_rwlocks = 0;
    size_t expected_msgpipes = 0;
    size_t expected_wait_queue_entries = 0;
    if (!quick_state_parse_size(values, "simple_events", expected_simple_events)
        || !quick_state_parse_size(values, "timers", expected_timers)
        || !quick_state_parse_size(values, "semaphores", expected_semaphores)
        || !quick_state_parse_size(values, "mutexes", expected_mutexes)
        || !quick_state_parse_size(values, "lwmutexes", expected_lwmutexes)
        || !quick_state_parse_size(values, "eventflags", expected_eventflags)
        || !quick_state_parse_size(values, "condvars", expected_condvars)
        || !quick_state_parse_size(values, "lwcondvars", expected_lwcondvars)
        || !quick_state_parse_size(values, "rwlocks", expected_rwlocks)
        || !quick_state_parse_size(values, "msgpipes", expected_msgpipes)
        || !quick_state_parse_size(values, "wait_queue_entries", expected_wait_queue_entries)) {
        return false;
    }

    for (const auto &[key, value] : values) {
        if (key.rfind("simple_event.", 0) == 0) {
            QuickStateSyncSimpleEvent event;
            if (!quick_state_parse_sync_simple_event(key, value, event))
                return false;
            snapshot.simple_events[event.id] = std::move(event);
        } else if (key.rfind("timer.", 0) == 0) {
            QuickStateSyncTimer timer;
            if (!quick_state_parse_sync_timer(key, value, timer))
                return false;
            snapshot.timers[timer.id] = std::move(timer);
        } else if (key.rfind("semaphore.", 0) == 0) {
            QuickStateSyncSemaphore semaphore;
            if (!quick_state_parse_sync_semaphore(key, value, semaphore))
                return false;
            snapshot.semaphores[semaphore.id] = std::move(semaphore);
        } else if (key.rfind("mutex.", 0) == 0) {
            QuickStateSyncMutex mutex;
            if (!quick_state_parse_sync_mutex(key, value, false, mutex))
                return false;
            snapshot.mutexes[mutex.id] = std::move(mutex);
        } else if (key.rfind("lwmutex.", 0) == 0) {
            QuickStateSyncMutex mutex;
            if (!quick_state_parse_sync_mutex(key, value, true, mutex))
                return false;
            snapshot.lwmutexes[mutex.id] = std::move(mutex);
        } else if (key.rfind("eventflag.", 0) == 0) {
            QuickStateSyncEventFlag eventflag;
            if (!quick_state_parse_sync_eventflag(key, value, eventflag))
                return false;
            snapshot.eventflags[eventflag.id] = std::move(eventflag);
        } else if (key.rfind("condvar.", 0) == 0) {
            QuickStateSyncCondvar condvar;
            if (!quick_state_parse_sync_condvar(key, value, false, condvar))
                return false;
            snapshot.condvars[condvar.id] = std::move(condvar);
        } else if (key.rfind("lwcondvar.", 0) == 0) {
            QuickStateSyncCondvar condvar;
            if (!quick_state_parse_sync_condvar(key, value, true, condvar))
                return false;
            snapshot.lwcondvars[condvar.id] = std::move(condvar);
        } else if (key.rfind("rwlock.", 0) == 0) {
            QuickStateSyncRWLock rwlock;
            if (!quick_state_parse_sync_rwlock(key, value, rwlock))
                return false;
            snapshot.rwlocks[rwlock.id] = std::move(rwlock);
        } else if (key.rfind("msgpipe.", 0) == 0) {
            QuickStateSyncMsgPipe msgpipe;
            if (!quick_state_parse_sync_msgpipe(key, value, msgpipe))
                return false;
            snapshot.msgpipes[msgpipe.id] = std::move(msgpipe);
        } else if (key.rfind("wait.", 0) == 0) {
            QuickStateSyncWaitQueueEntry wait_entry;
            if (!quick_state_parse_sync_wait_queue_entry(key, value, wait_entry))
                return false;
            snapshot.wait_queue_entries.push_back(std::move(wait_entry));
        }
    }

    snapshot.valid = snapshot.simple_events.size() == expected_simple_events
        && snapshot.timers.size() == expected_timers
        && snapshot.semaphores.size() == expected_semaphores
        && snapshot.mutexes.size() == expected_mutexes
        && snapshot.lwmutexes.size() == expected_lwmutexes
        && snapshot.eventflags.size() == expected_eventflags
        && snapshot.condvars.size() == expected_condvars
        && snapshot.lwcondvars.size() == expected_lwcondvars
        && snapshot.rwlocks.size() == expected_rwlocks
        && snapshot.msgpipes.size() == expected_msgpipes
        && snapshot.wait_queue_entries.size() == expected_wait_queue_entries;
    return snapshot.valid;
}

static bool quick_state_parse_display_frame_snapshot(const std::string &text, QuickStateDisplayFrameSnapshot &frame) {
    const auto fields = quick_state_parse_semicolon_fields(text);
    if (!fields.contains("base")
        || !fields.contains("pitch")
        || !fields.contains("format")
        || !fields.contains("width")
        || !fields.contains("height")) {
        return false;
    }

    uint64_t parsed_base = 0;
    uint32_t parsed_u32 = 0;
    int32_t parsed_i32 = 0;
    if (!quick_state_parse_u64_text(fields.at("base"), parsed_base, 0)
        || !quick_state_parse_u32_text(fields.at("pitch"), frame.pitch)
        || !quick_state_parse_u32_text(fields.at("format"), frame.pixelformat)
        || !quick_state_parse_i32_text(fields.at("width"), parsed_i32)
        || parsed_i32 < 0) {
        return false;
    }
    frame.base = static_cast<Address>(parsed_base);
    frame.width = parsed_i32;
    if (!quick_state_parse_i32_text(fields.at("height"), parsed_i32) || parsed_i32 < 0)
        return false;
    frame.height = parsed_i32;
    return frame.pitch <= 8192 && frame.width <= 8192 && frame.height <= 8192;
}

static bool quick_state_parse_predicted_display_frame_key(const std::string &key, uint32_t &index) {
    constexpr std::string_view prefix = "predicted_frame.";
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size())
        return false;
    return quick_state_parse_u32_text(key.substr(prefix.size()), index);
}

static bool quick_state_parse_display_vblank_wait_key(const std::string &key, uint32_t &index) {
    constexpr std::string_view prefix = "vblank_wait.";
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size())
        return false;
    return quick_state_parse_u32_text(key.substr(prefix.size()), index);
}

static bool quick_state_parse_display_vblank_wait_snapshot(const std::string &text, QuickStateDisplayVBlankWaitSnapshot &wait) {
    const auto fields = quick_state_parse_semicolon_fields(text);
    if (!fields.contains("thread") || !fields.contains("target"))
        return false;

    int32_t parsed_thread_id = 0;
    if (!quick_state_parse_i32_text(fields.at("thread"), parsed_thread_id) || parsed_thread_id <= 0)
        return false;
    wait.thread_id = static_cast<SceUID>(parsed_thread_id);
    return quick_state_parse_u64_text(fields.at("target"), wait.target_vcount);
}

static bool quick_state_parse_display_snapshot_section(const QuickStateSlot &slot, QuickStateDisplaySnapshot &snapshot) {
    snapshot = {};
    const QuickStateSection *section = quick_state_find_section(slot, "thor.display");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.display.v1")
        return false;

    uint64_t parsed_hex = 0;
    size_t predicted_frame_count = 0;
    bool parsed_bool = false;
    if (!quick_state_parse_u32(values, "speed_percent", snapshot.speed_percent)
        || !quick_state_parse_u64(values, "vblank_count", snapshot.vblank_count)
        || !quick_state_parse_u64(values, "last_setframe_vblank_count", snapshot.last_setframe_vblank_count)
        || !values.contains("current_sync_object")
        || !quick_state_parse_u64_text(values.at("current_sync_object"), parsed_hex, 0)
        || !values.contains("sce_frame")
        || !quick_state_parse_display_frame_snapshot(values.at("sce_frame"), snapshot.sce_frame)
        || !values.contains("next_rendered_frame")
        || !quick_state_parse_display_frame_snapshot(values.at("next_rendered_frame"), snapshot.next_rendered_frame)
        || !values.contains("fps_hack")
        || !quick_state_parse_bool_text(values.at("fps_hack"), parsed_bool)
        || !quick_state_parse_size(values, "predicted_frames", predicted_frame_count)
        || !quick_state_parse_u32(values, "predicted_frame_position", snapshot.predicted_frame_position)
        || !quick_state_parse_u32(values, "predicted_cycles_seen", snapshot.predicted_cycles_seen)
        || !values.contains("predicting")
        || !quick_state_parse_bool_text(values.at("predicting"), snapshot.predicting)
        || !quick_state_parse_u32(values, "vblank_wait_infos", snapshot.vblank_wait_info_count)
        || !quick_state_parse_u32(values, "vblank_callbacks", snapshot.vblank_callback_count)) {
        return false;
    }
    snapshot.current_sync_object = static_cast<Address>(parsed_hex);
    snapshot.fps_hack = parsed_bool;

    snapshot.predicted_frames.resize(predicted_frame_count);
    std::vector<bool> seen(predicted_frame_count, false);
    for (const auto &[key, value] : values) {
        uint32_t index = 0;
        if (!quick_state_parse_predicted_display_frame_key(key, index))
            continue;
        if (index >= predicted_frame_count)
            return false;

        const auto fields = quick_state_parse_semicolon_fields(value);
        if (!fields.contains("sync"))
            return false;
        QuickStatePredictedDisplayFrameSnapshot predicted;
        if (!quick_state_parse_display_frame_snapshot(value, predicted.frame)
            || !quick_state_parse_u64_text(fields.at("sync"), parsed_hex, 0)) {
            return false;
        }
        predicted.sync_object = static_cast<Address>(parsed_hex);
        snapshot.predicted_frames[index] = predicted;
        seen[index] = true;
    }
    if (std::any_of(seen.begin(), seen.end(), [](const bool value) { return !value; }))
        return false;
    if (predicted_frame_count > 0 && snapshot.predicted_frame_position >= predicted_frame_count)
        return false;
    if (snapshot.speed_percent == 0 || snapshot.speed_percent > 1000)
        return false;

    snapshot.vblank_waits.resize(snapshot.vblank_wait_info_count);
    std::vector<bool> seen_waits(snapshot.vblank_wait_info_count, false);
    for (const auto &[key, value] : values) {
        uint32_t index = 0;
        if (!quick_state_parse_display_vblank_wait_key(key, index))
            continue;
        if (index >= snapshot.vblank_wait_info_count)
            return false;

        QuickStateDisplayVBlankWaitSnapshot wait;
        if (!quick_state_parse_display_vblank_wait_snapshot(value, wait))
            return false;
        snapshot.vblank_waits[index] = wait;
        seen_waits[index] = true;
    }
    snapshot.vblank_waits_complete = !std::any_of(seen_waits.begin(), seen_waits.end(), [](const bool value) { return !value; });

    snapshot.valid = true;
    return true;
}

static bool quick_state_parse_audio_port_snapshot(const std::string &key, const std::string &value, QuickStateAudioPortSnapshot &port) {
    SceUID port_id = 0;
    if (!quick_state_parse_uid_from_key(key, "port.", ".type", port_id))
        return false;
    port.id = port_id;

    const auto fields = quick_state_parse_semicolon_fields("type=" + value);
    if (!fields.contains("type")
        || !fields.contains("len")
        || !fields.contains("freq")
        || !fields.contains("mode")
        || !fields.contains("len_bytes")
        || !fields.contains("len_us")
        || !fields.contains("last_output")
        || !fields.contains("left")
        || !fields.contains("right")
        || !fields.contains("volume")
        || !quick_state_parse_i32_text(fields.at("type"), port.type)
        || !quick_state_parse_i32_text(fields.at("len"), port.len)
        || !quick_state_parse_i32_text(fields.at("freq"), port.freq)
        || !quick_state_parse_i32_text(fields.at("mode"), port.mode)
        || !quick_state_parse_i32_text(fields.at("len_bytes"), port.len_bytes)
        || !quick_state_parse_u64_text(fields.at("len_us"), port.len_microseconds)
        || !quick_state_parse_u64_text(fields.at("last_output"), port.last_output)
        || !quick_state_parse_i32_text(fields.at("left"), port.left_channel_volume)
        || !quick_state_parse_i32_text(fields.at("right"), port.right_channel_volume)
        || !quick_state_parse_float_text(fields.at("volume"), port.volume)) {
        return false;
    }

    return port.id > 0 && port.len >= 0 && port.freq >= 0 && port.len_bytes >= 0 && port.volume >= 0.0f && port.volume <= 4.0f;
}

static bool quick_state_parse_audio_snapshot_section(const QuickStateSlot &slot, QuickStateAudioSnapshot &snapshot) {
    snapshot = {};
    const QuickStateSection *section = quick_state_find_section(slot, "thor.audio");
    if (!section || section->version != 1)
        return false;

    const auto values = quick_state_parse_text_section(*section);
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.audio.v1")
        return false;

    size_t expected_ports = 0;
    int32_t parsed_i32 = 0;
    bool parsed_bool = false;
    if (!quick_state_parse_u32(values, "speed_percent", snapshot.speed_percent)
        || !values.contains("backend")
        || !values.contains("global_volume")
        || !values.contains("next_port_id")
        || !values.contains("audio_in_len_bytes")
        || !quick_state_parse_float_text(values.at("global_volume"), snapshot.global_volume)
        || !quick_state_parse_i32_text(values.at("next_port_id"), snapshot.next_port_id)
        || !quick_state_parse_size(values, "out_ports", expected_ports)
        || !values.contains("audio_in_running")
        || !quick_state_parse_bool_text(values.at("audio_in_running"), parsed_bool)
        || !quick_state_parse_i32_text(values.at("audio_in_len_bytes"), parsed_i32)) {
        return false;
    }
    snapshot.backend = values.at("backend");
    snapshot.audio_in_running = parsed_bool;
    snapshot.audio_in_len_bytes = parsed_i32;

    for (const auto &[key, value] : values) {
        if (key.rfind("port.", 0) != 0)
            continue;
        QuickStateAudioPortSnapshot port;
        if (!quick_state_parse_audio_port_snapshot(key, value, port))
            return false;
        snapshot.out_ports[port.id] = std::move(port);
    }

    snapshot.valid = snapshot.speed_percent > 0
        && snapshot.speed_percent <= 1000
        && snapshot.global_volume >= 0.0f
        && snapshot.global_volume <= 4.0f
        && snapshot.next_port_id > 0
        && snapshot.audio_in_len_bytes >= 0
        && snapshot.out_ports.size() == expected_ports;
    return snapshot.valid;
}

static std::string quick_state_thread_status_name(const ThreadStatus status) {
    switch (status) {
    case ThreadStatus::run:
        return "run";
    case ThreadStatus::dormant:
        return "dormant";
    case ThreadStatus::suspend:
        return "suspend";
    case ThreadStatus::wait:
        return "wait";
    }
    return "unknown";
}

static bool quick_state_thread_status_from_name(const std::string &name, ThreadStatus &status) {
    if (name == "run") {
        status = ThreadStatus::run;
        return true;
    }
    if (name == "dormant") {
        status = ThreadStatus::dormant;
        return true;
    }
    if (name == "suspend") {
        status = ThreadStatus::suspend;
        return true;
    }
    if (name == "wait") {
        status = ThreadStatus::wait;
        return true;
    }
    return false;
}

static size_t quick_state_count_avplayer_threads(const QuickStateSlot &slot) {
    return static_cast<size_t>(std::count_if(slot.thread_contexts.begin(), slot.thread_contexts.end(), [](const QuickStateThreadContext &thread) {
        return thread.name.rfind("avPlayer ", 0) == 0;
    }));
}

static QuickStateKernelObjectCounts quick_state_count_kernel_objects(EmuEnvState &emuenv) {
    QuickStateKernelObjectCounts counts;
    const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);

    counts.threads = emuenv.kernel.threads.size();
    for (const auto &[_, thread] : emuenv.kernel.threads) {
        if (thread->cpu)
            counts.cpu_threads++;
    }
    counts.timers = emuenv.kernel.timers.size();
    counts.semaphores = emuenv.kernel.semaphores.size();
    counts.condvars = emuenv.kernel.condvars.size();
    counts.lwcondvars = emuenv.kernel.lwcondvars.size();
    counts.mutexes = emuenv.kernel.mutexes.size();
    counts.lwmutexes = emuenv.kernel.lwmutexes.size();
    counts.rwlocks = emuenv.kernel.rwlocks.size();
    counts.eventflags = emuenv.kernel.eventflags.size();
    counts.msgpipes = emuenv.kernel.msgpipes.size();
    counts.callbacks = emuenv.kernel.callbacks.size();
    counts.simple_events = emuenv.kernel.simple_events.size();
    counts.loaded_modules = emuenv.kernel.loaded_modules.size();

    return counts;
}

static QuickStateIOCounts quick_state_count_io_objects(EmuEnvState &emuenv) {
    QuickStateIOCounts counts;
    counts.tty_files = emuenv.io.tty_files.size();
    counts.std_files = emuenv.io.std_files.size();
    counts.dir_entries = emuenv.io.dir_entries.size();
    counts.archive_entries = emuenv.io.app0_archive.entries.size();
    for (const auto &[_, entry] : emuenv.io.app0_archive.entries) {
        if (entry.directory)
            counts.archive_dirs++;
    }
    {
        const std::lock_guard<std::mutex> overlay_lock(emuenv.io.overlay_mutex);
        counts.overlays = emuenv.io.overlays.size();
    }
    return counts;
}

static QuickStateRestoreManifest build_quick_state_restore_manifest(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    QuickStateRestoreManifest manifest;
    manifest.guest_memory_bytes = slot.byte_count;
    manifest.memory_pages = slot.memory_pages.size();
    manifest.allocator_words = slot.allocator_words.size();
    manifest.allocation_pages = slot.allocation_pages.size();
    manifest.thread_contexts = slot.thread_contexts.size();
    manifest.avplayer_threads = quick_state_count_avplayer_threads(slot);
    manifest.captured_sections = quick_state_section_tags(slot);
    QuickStateTimingSnapshot timing;
    manifest.timing_restorable = quick_state_parse_timing_snapshot(slot, timing);
    std::map<SceUID, QuickStateThreadMetadata> thread_metadata;
    manifest.thread_metadata_restorable = quick_state_parse_thread_metadata_section(slot, thread_metadata);
    std::map<SceUID, QuickStateIOFilePosition> io_file_positions;
    manifest.io_file_positions_restorable = quick_state_parse_io_file_positions_section(slot, io_file_positions);
    if (manifest.io_file_positions_restorable) {
        manifest.io_memory_file_handles = static_cast<size_t>(std::count_if(io_file_positions.begin(), io_file_positions.end(), [](const auto &item) {
            return item.second.memory_file;
        }));
        manifest.io_file_handles_restorable = true;
    }
    QuickStateSyncSnapshot sync_snapshot;
    manifest.sync_primitives_restorable = quick_state_parse_sync_primitives_section(slot, sync_snapshot);
    if (manifest.sync_primitives_restorable) {
        manifest.sync_waiting_threads = sync_snapshot.total_waiting_threads();
        manifest.sync_wait_queue_entries = sync_snapshot.wait_queue_entries.size();
        manifest.sync_wait_queue_metadata_complete = sync_snapshot.wait_queue_metadata_complete();
        manifest.msgpipe_buffer_bytes = sync_snapshot.msgpipe_buffer_bytes();
    }
    QuickStateDisplaySnapshot display_snapshot;
    manifest.display_state_restorable = quick_state_parse_display_snapshot_section(slot, display_snapshot);
    if (manifest.display_state_restorable) {
        manifest.display_wait_entries = display_snapshot.vblank_wait_info_count;
        manifest.display_callback_entries = display_snapshot.vblank_callback_count;
        manifest.display_vblank_waits_restorable = display_snapshot.vblank_waits_complete;
    }
    QuickStateAudioSnapshot audio_snapshot;
    manifest.audio_state_restorable = quick_state_parse_audio_snapshot_section(slot, audio_snapshot);
    manifest.kernel = quick_state_count_kernel_objects(emuenv);
    manifest.io = quick_state_count_io_objects(emuenv);

    manifest.missing_serializers = {
        "kernel-thread-lifecycle",
        "host-syscall-state",
        "renderer-resources",
    };
    if (!manifest.sync_primitives_restorable) {
        manifest.missing_serializers.push_back("kernel-sync-primitives");
    } else {
        if (manifest.sync_waiting_threads > 0) {
            if (!manifest.sync_wait_queue_metadata_complete)
                manifest.missing_serializers.push_back("kernel-wait-queue-metadata");
            manifest.missing_serializers.push_back("kernel-wait-queues");
        }
        if (manifest.msgpipe_buffer_bytes > 0)
            manifest.missing_serializers.push_back("kernel-msgpipe-buffers");
    }
    if (!manifest.thread_metadata_restorable)
        manifest.missing_serializers.push_back("kernel-thread-metadata");
    if (!manifest.display_state_restorable) {
        manifest.missing_serializers.push_back("display-state");
    } else {
        if (manifest.display_wait_entries > 0 && !manifest.display_vblank_waits_restorable)
            manifest.missing_serializers.push_back("display-vblank-waits");
        if (manifest.display_callback_entries > 0)
            manifest.missing_serializers.push_back("display-vblank-callbacks");
    }
    if (!manifest.audio_state_restorable)
        manifest.missing_serializers.push_back("audio-state");
    if (!manifest.io_file_positions_restorable) {
        manifest.missing_serializers.push_back("io-file-positions");
    } else {
        if (manifest.io.dir_entries > 0)
            manifest.missing_serializers.push_back("io-directory-handles");
        if (manifest.io.overlays > 0)
            manifest.missing_serializers.push_back("io-overlays");
    }
    if (!manifest.timing_restorable)
        manifest.missing_serializers.push_back("timing-clocks");
    if (manifest.avplayer_threads > 0)
        manifest.missing_serializers.push_back("avplayer-movie-state");

    manifest.restore_enabled = manifest.missing_serializers.empty() && !slot.restore_requires_same_pause;
    if (manifest.restore_enabled) {
        manifest.block_reason = "all mandatory quickstate serializers are present";
    } else {
        manifest.block_reason = fmt::format("restore disabled by Windows stability gate; missing serializers: {}",
            quick_state_join_strings(manifest.missing_serializers));
    }

    return manifest;
}

static QuickStateSection quick_state_make_text_section(std::string tag, std::ostringstream &text) {
    const std::string payload = text.str();
    QuickStateSection section;
    section.tag = std::move(tag);
    section.version = 1;
    section.bytes.assign(payload.begin(), payload.end());
    return section;
}

static size_t quick_state_waiting_count(const WaitingThreadQueuePtr &waiting_threads) {
    return waiting_threads ? waiting_threads->size() : 0;
}

static Address quick_state_guest_address_from_host_pointer(const MemState &mem, const void *pointer) {
    if (!pointer || !mem.memory)
        return 0;

    const auto host_address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base_address = reinterpret_cast<std::uintptr_t>(mem.memory.get());
    constexpr uint64_t guest_memory_bytes = 1ull << 32;
    if (host_address >= base_address && host_address < base_address + guest_memory_bytes) {
        const auto guest_address = static_cast<Address>(host_address - base_address);
        return is_valid_addr(mem, guest_address) ? guest_address : 0;
    }

    if (mem.use_page_table) {
        const auto mapped = mem.external_mapping.lower_bound(static_cast<uint64_t>(host_address));
        if (mapped != mem.external_mapping.end() && host_address < mapped->first + mapped->second.size) {
            const auto guest_address = static_cast<Address>(host_address - mapped->first + mapped->second.address);
            return is_valid_addr(mem, guest_address) ? guest_address : 0;
        }
    }

    return 0;
}

static const char *quick_state_wait_queue_cancel_source(const WaitingThreadData &data) {
    return data.was_canceled ? "host-stack" : "none";
}

template <typename FieldWriter>
static size_t quick_state_write_wait_queue_entries(std::ostringstream &text, const char *kind, const SceUID uid, const WaitingThreadQueuePtr &queue, FieldWriter write_fields) {
    if (!queue)
        return 0;

    size_t index = 0;
    for (auto it = queue->begin(); it != queue->end(); ++it, ++index) {
        const WaitingThreadData data = *it;
        text << "wait." << kind << "." << uid << "." << index
             << "=thread=" << (data.thread ? data.thread->id : 0)
             << ";priority=" << data.priority;
        write_fields(text, data);
        text << "\n";
    }
    return index;
}

static uint64_t quick_state_host_time_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
            .count());
}

static uint64_t quick_state_timer_next_event_delta_us(const uint64_t next_event) {
    if (next_event == std::numeric_limits<uint64_t>::max())
        return std::numeric_limits<uint64_t>::max();

    const uint64_t now = quick_state_host_time_us();
    return next_event > now ? next_event - now : 0;
}

static std::string quick_state_display_frame_text(const DisplayFrameInfo &frame) {
    return fmt::format("base=0x{:X};pitch={};format={};width={};height={}",
        frame.base.address(),
        frame.pitch,
        frame.pixelformat,
        frame.image_size.x,
        frame.image_size.y);
}

static std::string quick_state_rwlock_owners_detail(const RWLockOwners &owners) {
    if (owners.empty())
        return "none";

    std::vector<std::string> entries;
    entries.reserve(owners.size());
    for (const auto &[thread, lock_count] : owners)
        entries.push_back(fmt::format("{}:{}", thread ? thread->id : 0, lock_count));

    std::string joined = entries.front();
    for (size_t i = 1; i < entries.size(); i++) {
        joined += ",";
        joined += entries[i];
    }
    return joined;
}

static std::vector<QuickStateSection> build_quick_state_capture_sections(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    const std::vector<std::string> planned_sections = {
        "thor.quickstate.manifest",
        "thor.timing",
        "thor.kernel.objects",
        "thor.io.vfs",
        "thor.display",
        "thor.audio",
    };

    std::vector<QuickStateSection> sections;
    sections.reserve(planned_sections.size());

    {
        const auto manifest = build_quick_state_restore_manifest(emuenv, slot);
        std::ostringstream text;
        text << "schema=thor.quickstate.manifest.v1\n";
        text << "format_version=" << QUICKSTATE_VERSION << "\n";
        text << "title_id=" << slot.title_id << "\n";
        text << "title=" << slot.title << "\n";
        text << "guest_memory_bytes=" << slot.byte_count << "\n";
        text << "guest_memory_pages=" << slot.memory_pages.size() << "\n";
        text << "thread_contexts=" << slot.thread_contexts.size() << "\n";
        text << "avplayer_threads=" << manifest.avplayer_threads << "\n";
        text << "allocator_words=" << slot.allocator_words.size() << "\n";
        text << "allocation_pages=" << slot.allocation_pages.size() << "\n";
        text << "capture_sections=" << quick_state_join_strings(planned_sections) << "\n";
        text << "restore_enabled=0\n";
        text << "restore_gate=windows-stability\n";
        text << "missing_serializers=" << quick_state_join_strings(manifest.missing_serializers) << "\n";
        sections.push_back(quick_state_make_text_section("thor.quickstate.manifest", text));
    }

    {
        std::ostringstream text;
        text << "schema=thor.timing.v1\n";
        text << "kernel_start_tick=" << emuenv.kernel.start_tick << "\n";
        text << "kernel_base_tick=" << emuenv.kernel.base_tick.tick << "\n";
        text << "kernel_guest_tick=" << emuenv.kernel.get_guest_tick() << "\n";
        text << "kernel_process_time=" << emuenv.kernel.get_process_time() << "\n";
        text << "kernel_speed_percent=" << emuenv.kernel.speed_percent.load() << "\n";
        {
            const std::lock_guard<std::mutex> speed_lock(emuenv.kernel.speed_mutex);
            text << "speed_anchor_host_process_us=" << emuenv.kernel.speed_anchor_host_process_us << "\n";
            text << "speed_anchor_guest_process_us=" << emuenv.kernel.speed_anchor_guest_process_us << "\n";
        }
        sections.push_back(quick_state_make_text_section("thor.timing", text));
    }

    {
        std::ostringstream text;
        text << "schema=thor.kernel.objects.v1\n";
        const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);
        size_t wait_queue_entries = 0;
        text << "threads=" << emuenv.kernel.threads.size() << "\n";
        for (const auto &[thread_id, thread] : emuenv.kernel.threads) {
            const std::lock_guard<std::mutex> thread_lock(thread->mutex);
            const ThreadStatus snapshot_status = emuenv.kernel.snapshot_thread_status_unlocked(thread_id, thread->status);
            text << "thread." << thread_id
                 << ".name=" << thread->name
                 << ";status=" << quick_state_thread_status_name(snapshot_status)
                 << ";entry=0x" << std::hex << thread->entry_point << std::dec
                 << ";stack=0x" << std::hex << thread->stack.get() << std::dec
                 << ";stack_size=" << thread->stack_size
                 << ";tls=0x" << std::hex << thread->tls.get() << std::dec
                 << ";priority=" << thread->priority
                 << ";affinity=" << thread->affinity_mask
                 << ";start_tick=" << thread->start_tick
                 << ";last_vblank_waited=" << thread->last_vblank_waited
                 << ";returned_value=" << thread->returned_value
                 << ";processing_callbacks=" << thread->is_processing_callbacks
                 << ";callbacks=" << thread->callbacks.size()
                 << ";waiting_threads=" << thread->waiting_threads.size()
                 << "\n";
        }

        text << "timers=" << emuenv.kernel.timers.size() << "\n";
        for (const auto &[uid, timer] : emuenv.kernel.timers) {
            const std::lock_guard<std::mutex> timer_lock(timer->mutex);
            text << "timer." << uid
                 << ".name=" << timer->name
                 << ";attr=" << timer->attr
                 << ";started=" << timer->is_started
                 << ";repeat=" << timer->is_repeat
                 << ";pulse=" << timer->is_pulse
                 << ";event_set=" << timer->event_set
                 << ";time=" << timer->time
                 << ";next_event=" << timer->next_event
                 << ";next_event_delta_us=" << quick_state_timer_next_event_delta_us(timer->next_event)
                 << ";interval=" << timer->event_interval
                 << ";waiting=" << quick_state_waiting_count(timer->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "timer", uid, timer->waiting_threads, [&emuenv](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";result_pattern=0x" << std::hex << quick_state_guest_address_from_host_pointer(emuenv.mem, data.result_pattern)
                           << ";user_data=0x" << quick_state_guest_address_from_host_pointer(emuenv.mem, data.user_data) << std::dec;
            });
        }

        text << "semaphores=" << emuenv.kernel.semaphores.size() << "\n";
        for (const auto &[uid, semaphore] : emuenv.kernel.semaphores) {
            const std::lock_guard<std::mutex> semaphore_lock(semaphore->mutex);
            text << "semaphore." << uid
                 << ".name=" << semaphore->name
                 << ";attr=" << semaphore->attr
                 << ";init=" << semaphore->init_val
                 << ";value=" << semaphore->val
                 << ";max=" << semaphore->max
                 << ";waiting=" << quick_state_waiting_count(semaphore->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "semaphore", uid, semaphore->waiting_threads, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";signal=" << data.signal
                           << ";cancel=" << quick_state_wait_queue_cancel_source(data);
            });
        }

        text << "mutexes=" << emuenv.kernel.mutexes.size() << "\n";
        for (const auto &[uid, mutex] : emuenv.kernel.mutexes) {
            const std::lock_guard<std::mutex> mutex_lock(mutex->mutex);
            text << "mutex." << uid
                 << ".name=" << mutex->name
                 << ";attr=" << mutex->attr
                 << ";init=" << mutex->init_count
                 << ";lock_count=" << mutex->lock_count
                 << ";owner=" << (mutex->owner ? mutex->owner->id : 0)
                 << ";waiting=" << quick_state_waiting_count(mutex->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "mutex", uid, mutex->waiting_threads, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";lock_count=" << data.lock_count;
            });
        }

        text << "lwmutexes=" << emuenv.kernel.lwmutexes.size() << "\n";
        for (const auto &[uid, mutex] : emuenv.kernel.lwmutexes) {
            const std::lock_guard<std::mutex> mutex_lock(mutex->mutex);
            text << "lwmutex." << uid
                 << ".name=" << mutex->name
                 << ";attr=" << mutex->attr
                 << ";init=" << mutex->init_count
                 << ";lock_count=" << mutex->lock_count
                 << ";owner=" << (mutex->owner ? mutex->owner->id : 0)
                 << ";workarea=0x" << std::hex << mutex->workarea.address() << std::dec
                 << ";waiting=" << quick_state_waiting_count(mutex->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "lwmutex", uid, mutex->waiting_threads, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";lock_count=" << data.lock_count;
            });
        }

        text << "eventflags=" << emuenv.kernel.eventflags.size() << "\n";
        for (const auto &[uid, eventflag] : emuenv.kernel.eventflags) {
            const std::lock_guard<std::mutex> event_lock(eventflag->mutex);
            text << "eventflag." << uid
                 << ".name=" << eventflag->name
                 << ";attr=" << eventflag->attr
                 << ";flags=" << eventflag->flags
                 << ";waiting=" << quick_state_waiting_count(eventflag->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "eventflag", uid, eventflag->waiting_threads, [&emuenv](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";wait=" << data.wait
                           << ";flags=" << data.flags
                           << ";out_bits=0x" << std::hex << quick_state_guest_address_from_host_pointer(emuenv.mem, data.outBits) << std::dec
                           << ";cancel=" << quick_state_wait_queue_cancel_source(data);
            });
        }

        text << "condvars=" << emuenv.kernel.condvars.size() << "\n";
        for (const auto &[uid, condvar] : emuenv.kernel.condvars) {
            const std::lock_guard<std::mutex> condvar_lock(condvar->mutex);
            text << "condvar." << uid
                 << ".name=" << condvar->name
                 << ";attr=" << condvar->attr
                 << ";associated_mutex=" << (condvar->associated_mutex ? condvar->associated_mutex->uid : 0)
                 << ";waiting=" << quick_state_waiting_count(condvar->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "condvar", uid, condvar->waiting_threads, [](std::ostringstream &, const WaitingThreadData &) {});
        }

        text << "lwcondvars=" << emuenv.kernel.lwcondvars.size() << "\n";
        for (const auto &[uid, condvar] : emuenv.kernel.lwcondvars) {
            const std::lock_guard<std::mutex> condvar_lock(condvar->mutex);
            text << "lwcondvar." << uid
                 << ".name=" << condvar->name
                 << ";attr=" << condvar->attr
                 << ";associated_mutex=" << (condvar->associated_mutex ? condvar->associated_mutex->uid : 0)
                 << ";waiting=" << quick_state_waiting_count(condvar->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "lwcondvar", uid, condvar->waiting_threads, [](std::ostringstream &, const WaitingThreadData &) {});
        }

        text << "rwlocks=" << emuenv.kernel.rwlocks.size() << "\n";
        for (const auto &[uid, rwlock] : emuenv.kernel.rwlocks) {
            const std::lock_guard<std::mutex> rwlock_lock(rwlock->mutex);
            text << "rwlock." << uid
                 << ".name=" << rwlock->name
                 << ";attr=" << rwlock->attr
                 << ";state=" << static_cast<int>(rwlock->state)
                 << ";owners=" << rwlock->owners.size()
                 << ";owners_detail=" << quick_state_rwlock_owners_detail(rwlock->owners)
                 << ";waiting=" << quick_state_waiting_count(rwlock->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "rwlock", uid, rwlock->waiting_threads, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";is_write=" << data.is_write;
            });
        }

        text << "msgpipes=" << emuenv.kernel.msgpipes.size() << "\n";
        for (const auto &[uid, msgpipe] : emuenv.kernel.msgpipes) {
            const std::lock_guard<std::mutex> msgpipe_lock(msgpipe->mutex);
            text << "msgpipe." << uid
                 << ".name=" << msgpipe->name
                 << ";attr=" << msgpipe->attr
                 << ";senders=" << quick_state_waiting_count(msgpipe->senders)
                 << ";receivers=" << quick_state_waiting_count(msgpipe->receivers)
                 << ";capacity=" << msgpipe->data_buffer.Capacity()
                 << ";used=" << msgpipe->data_buffer.Used()
                 << ";being_deleted=" << msgpipe->beingDeleted
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "msgpipe_sender", uid, msgpipe->senders, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";request_size=" << data.mp.request_size;
            });
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "msgpipe_receiver", uid, msgpipe->receivers, [](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";request_size=" << data.mp.request_size;
            });
        }

        text << "callbacks=" << emuenv.kernel.callbacks.size() << "\n";
        text << "simple_events=" << emuenv.kernel.simple_events.size() << "\n";
        for (const auto &[uid, event] : emuenv.kernel.simple_events) {
            const std::lock_guard<std::mutex> event_lock(event->mutex);
            text << "simple_event." << uid
                 << ".name=" << event->name
                 << ";attr=" << event->attr
                 << ";pattern=" << event->pattern
                 << ";last_user_data=" << event->last_user_data
                 << ";auto_reset=" << event->auto_reset
                 << ";cb_wakeup_only=" << event->cb_wakeup_only
                 << ";waiting=" << quick_state_waiting_count(event->waiting_threads)
                 << "\n";
            wait_queue_entries += quick_state_write_wait_queue_entries(text, "simple_event", uid, event->waiting_threads, [&emuenv](std::ostringstream &entry_text, const WaitingThreadData &data) {
                entry_text << ";pattern=" << data.pattern
                           << ";result_pattern=0x" << std::hex << quick_state_guest_address_from_host_pointer(emuenv.mem, data.result_pattern)
                           << ";user_data=0x" << quick_state_guest_address_from_host_pointer(emuenv.mem, data.user_data) << std::dec;
            });
        }
        text << "wait_queue_entries=" << wait_queue_entries << "\n";
        text << "loaded_modules=" << emuenv.kernel.loaded_modules.size() << "\n";
        sections.push_back(quick_state_make_text_section("thor.kernel.objects", text));
    }

    {
        std::ostringstream text;
        text << "schema=thor.io.vfs.v1\n";
        text << "title_id=" << emuenv.io.title_id << "\n";
        text << "content_id=" << emuenv.io.content_id << "\n";
        text << "app_path=" << emuenv.io.app_path << "\n";
        text << "app0_host_path=" << emuenv.io.app0_host_path << "\n";
        text << "archive_path=" << emuenv.io.app0_archive.archive_path << "\n";
        text << "archive_content_root=" << emuenv.io.app0_archive.content_root << "\n";
        text << "archive_entries=" << emuenv.io.app0_archive.entries.size() << "\n";
        text << "archive_dirs=" << quick_state_count_io_objects(emuenv).archive_dirs << "\n";
        text << "next_fd=" << emuenv.io.next_fd << "\n";
        text << "tty_files=" << emuenv.io.tty_files.size() << "\n";
        text << "std_files=" << emuenv.io.std_files.size() << "\n";
        for (const auto &[fd, file] : emuenv.io.std_files) {
            text << "file." << fd
                 << ".vita=" << file.get_vita_loc()
                 << ";translated=" << file.get_translated_path()
                 << ";host=" << file.get_system_location()
                 << ";open_mode=" << file.get_open_mode()
                 << ";memory=" << file.is_memory_file()
                 << ";offset=" << std::max<SceOff>(file.tell(), 0)
                 << "\n";
        }
        text << "dir_entries=" << emuenv.io.dir_entries.size() << "\n";
        for (const auto &[fd, dir] : emuenv.io.dir_entries) {
            text << "dir." << fd
                 << ".vita=" << dir.get_vita_loc()
                 << ";translated=" << dir.get_translated_path()
                 << ";host=" << dir.get_system_location()
                 << ";memory=" << dir.is_memory_directory()
                 << "\n";
        }
        {
            const std::lock_guard<std::mutex> overlay_lock(emuenv.io.overlay_mutex);
            text << "overlays=" << emuenv.io.overlays.size() << "\n";
        }
        sections.push_back(quick_state_make_text_section("thor.io.vfs", text));
    }

    {
        std::ostringstream text;
        text << "schema=thor.display.v1\n";
        text << "speed_percent=" << emuenv.display.speed_percent.load() << "\n";
        text << "vblank_count=" << emuenv.display.vblank_count.load() << "\n";
        text << "last_setframe_vblank_count=" << emuenv.display.last_setframe_vblank_count.load() << "\n";
        text << "current_sync_object=0x" << std::hex << emuenv.display.current_sync_object.load() << std::dec << "\n";
        {
            const std::lock_guard<std::mutex> display_info_lock(emuenv.display.display_info_mutex);
            text << "sce_frame=" << quick_state_display_frame_text(emuenv.display.sce_frame) << "\n";
            text << "next_rendered_frame=" << quick_state_display_frame_text(emuenv.display.next_rendered_frame) << "\n";
            text << "predicted_frames=" << emuenv.display.predicted_frames.size() << "\n";
            for (size_t i = 0; i < emuenv.display.predicted_frames.size(); i++) {
                const auto &predicted_frame = emuenv.display.predicted_frames[i];
                text << "predicted_frame." << i
                     << "=" << quick_state_display_frame_text(predicted_frame.frame_info)
                     << ";sync=0x" << std::hex << predicted_frame.sync_object << std::dec
                     << "\n";
            }
            text << "predicted_frame_position=" << emuenv.display.predicted_frame_position << "\n";
            text << "predicted_cycles_seen=" << emuenv.display.predicted_cycles_seen << "\n";
            text << "predicting=" << emuenv.display.predicting.load() << "\n";
        }
        text << "fps_hack=" << emuenv.display.fps_hack << "\n";
        {
            const std::lock_guard<std::mutex> display_lock(emuenv.display.mutex);
            text << "vblank_wait_infos=" << emuenv.display.vblank_wait_infos.size() << "\n";
            for (size_t i = 0; i < emuenv.display.vblank_wait_infos.size(); i++) {
                const auto &vblank_wait = emuenv.display.vblank_wait_infos[i];
                const SceUID thread_id = vblank_wait.target_thread ? vblank_wait.target_thread->id : 0;
                text << "vblank_wait." << i
                     << "=thread=" << thread_id
                     << ";target=" << vblank_wait.target_vcount
                     << "\n";
            }
            text << "vblank_callbacks=" << emuenv.display.vblank_callbacks.size() << "\n";
        }
        sections.push_back(quick_state_make_text_section("thor.display", text));
    }

    {
        std::ostringstream text;
        text << "schema=thor.audio.v1\n";
        text << "speed_percent=" << emuenv.audio.speed_percent.load() << "\n";
        text << "backend=" << emuenv.audio.audio_backend << "\n";
        text << "global_volume=" << emuenv.audio.global_volume << "\n";
        const std::lock_guard<std::mutex> audio_lock(emuenv.audio.mutex);
        text << "next_port_id=" << emuenv.audio.next_port_id << "\n";
        text << "out_ports=" << emuenv.audio.out_ports.size() << "\n";
        for (const auto &[port_id, port] : emuenv.audio.out_ports) {
            text << "port." << port_id
                 << ".type=" << port->type
                 << ";len=" << port->len
                 << ";freq=" << port->freq
                 << ";mode=" << port->mode
                 << ";len_bytes=" << port->len_bytes
                 << ";len_us=" << port->len_microseconds
                 << ";last_output=" << port->last_output
                 << ";left=" << port->left_channel_volume
                 << ";right=" << port->right_channel_volume
                 << ";volume=" << port->volume
                 << "\n";
        }
        text << "audio_in_running=" << emuenv.audio.in_port.running << "\n";
        text << "audio_in_len_bytes=" << emuenv.audio.in_port.len_bytes << "\n";
        sections.push_back(quick_state_make_text_section("thor.audio", text));
    }

    return sections;
}

static bool restore_quick_state_timing_state(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    QuickStateTimingSnapshot timing;
    if (!quick_state_parse_timing_snapshot(slot, timing)) {
        LOG_WARN("Refused timing restore for {} because the thor.timing section is missing or invalid.", slot.title_id);
        return false;
    }

    {
        const std::lock_guard<std::mutex> speed_lock(emuenv.kernel.speed_mutex);
        emuenv.kernel.start_tick = timing.kernel_start_tick;
        emuenv.kernel.base_tick = { timing.kernel_base_tick };
        emuenv.kernel.speed_anchor_host_process_us = timing.speed_anchor_host_process_us;
        emuenv.kernel.speed_anchor_guest_process_us = timing.speed_anchor_guest_process_us;
        emuenv.kernel.speed_percent.store(timing.kernel_speed_percent);
    }
    emuenv.display.speed_percent.store(timing.kernel_speed_percent);
    emuenv.audio.speed_percent.store(timing.kernel_speed_percent);

    LOG_INFO("Restored timing snapshot for {}: guest_tick={} process_time={} speed={}%",
        slot.title_id,
        timing.kernel_guest_tick,
        timing.kernel_process_time,
        timing.kernel_speed_percent);
    return true;
}

static bool restore_quick_state_thread_metadata(EmuEnvState &emuenv, const QuickStateSlot &slot, const std::vector<ThreadStatePtr> &matched_threads) {
    std::map<SceUID, QuickStateThreadMetadata> metadata_by_id;
    if (!quick_state_parse_thread_metadata_section(slot, metadata_by_id)) {
        LOG_WARN("Refused thread metadata restore for {} because the thor.kernel.objects thread metadata is missing or invalid.", slot.title_id);
        return false;
    }

    if (matched_threads.size() != slot.thread_contexts.size()) {
        LOG_WARN("Refused thread metadata restore for {} because matched thread count {} does not match saved context count {}.",
            slot.title_id, matched_threads.size(), slot.thread_contexts.size());
        return false;
    }

    for (size_t i = 0; i < slot.thread_contexts.size(); i++) {
        const auto &thread_context = slot.thread_contexts[i];
        const auto metadata = metadata_by_id.find(thread_context.id);
        if (metadata == metadata_by_id.end()) {
            LOG_WARN("Refused thread metadata restore for {} because saved thread {} ({}) has no metadata row.",
                slot.title_id, thread_context.id, thread_context.name);
            return false;
        }

        const QuickStateThreadMetadata &thread_metadata = metadata->second;
        if ((thread_metadata.name != thread_context.name)
            || (thread_metadata.entry_point != thread_context.entry_point)
            || (thread_metadata.stack_address != thread_context.stack_address)
            || (thread_metadata.stack_size != thread_context.stack_size)
            || (thread_metadata.tls_address != thread_context.tls_address)) {
            LOG_WARN("Refused thread metadata restore for {} because saved thread {} metadata does not match its CPU context record.",
                slot.title_id, thread_context.id);
            return false;
        }
    }

    for (size_t i = 0; i < slot.thread_contexts.size(); i++) {
        const auto &thread_context = slot.thread_contexts[i];
        const QuickStateThreadMetadata &thread_metadata = metadata_by_id.at(thread_context.id);
        const auto &thread = matched_threads[i];
        ThreadStatus saved_status = ThreadStatus::dormant;
        if (!quick_state_thread_status_from_name(thread_metadata.status, saved_status)) {
            LOG_WARN("Refused thread metadata restore for {} because saved thread {} has unknown status '{}'.",
                slot.title_id, thread_context.id, thread_metadata.status);
            return false;
        }

        {
            const std::lock_guard<std::mutex> thread_lock(thread->mutex);
            thread->priority = thread_metadata.priority;
            thread->affinity_mask = thread_metadata.affinity_mask;
            thread->start_tick = thread_metadata.start_tick;
            thread->last_vblank_waited = thread_metadata.last_vblank_waited;
            thread->returned_value = thread_metadata.returned_value;
            thread->is_processing_callbacks = thread_metadata.is_processing_callbacks;
            thread->status = saved_status == ThreadStatus::run ? ThreadStatus::suspend : saved_status;
            thread->status_cond.notify_all();
        }
        emuenv.kernel.set_paused_thread_status_for_restore(thread->id, saved_status);
    }

    LOG_INFO("Restored thread metadata snapshot for {} ({} matched thread(s)).", slot.title_id, matched_threads.size());
    return true;
}

static std::map<SceUID, ThreadStatePtr> quick_state_matched_threads_by_saved_id(const QuickStateSlot &slot, const std::vector<ThreadStatePtr> &matched_threads) {
    std::map<SceUID, ThreadStatePtr> threads_by_saved_id;
    const size_t count = std::min(slot.thread_contexts.size(), matched_threads.size());
    for (size_t i = 0; i < count; i++)
        threads_by_saved_id[slot.thread_contexts[i].id] = matched_threads[i];
    return threads_by_saved_id;
}

static bool quick_state_sync_identity_matches(const SyncPrimitive &object, const std::string &name, const uint32_t attr) {
    return std::string(object.name) == name && object.attr == attr;
}

static uint64_t quick_state_timer_restore_next_event(const QuickStateSyncTimer &saved_timer) {
    if (saved_timer.next_event_delta_us == std::numeric_limits<uint64_t>::max())
        return std::numeric_limits<uint64_t>::max();
    return quick_state_host_time_us() + saved_timer.next_event_delta_us;
}

static bool restore_quick_state_sync_primitives(EmuEnvState &emuenv, const QuickStateSlot &slot, const std::vector<ThreadStatePtr> &matched_threads) {
    QuickStateSyncSnapshot snapshot;
    if (!quick_state_parse_sync_primitives_section(slot, snapshot)) {
        LOG_WARN("Refused sync primitive restore for {} because thor.kernel.objects sync metadata is missing or invalid.", slot.title_id);
        return false;
    }

    if (snapshot.total_waiting_threads() > 0) {
        LOG_WARN("Refused sync primitive restore for {} because {} waiting thread queue entry/entries still need queue serialization.",
            slot.title_id, snapshot.total_waiting_threads());
        return false;
    }

    if (snapshot.msgpipe_buffer_bytes() > 0) {
        LOG_WARN("Refused sync primitive restore for {} because {} message-pipe buffer byte(s) still need payload serialization.",
            slot.title_id, snapshot.msgpipe_buffer_bytes());
        return false;
    }

    const auto threads_by_saved_id = quick_state_matched_threads_by_saved_id(slot, matched_threads);
    const auto resolve_owner = [&threads_by_saved_id](const SceUID saved_thread_id) -> ThreadStatePtr {
        if (saved_thread_id == 0)
            return nullptr;
        const auto thread = threads_by_saved_id.find(saved_thread_id);
        return thread == threads_by_saved_id.end() ? nullptr : thread->second;
    };

    const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);
    if (emuenv.kernel.simple_events.size() != snapshot.simple_events.size()
        || emuenv.kernel.timers.size() != snapshot.timers.size()
        || emuenv.kernel.semaphores.size() != snapshot.semaphores.size()
        || emuenv.kernel.mutexes.size() != snapshot.mutexes.size()
        || emuenv.kernel.lwmutexes.size() != snapshot.lwmutexes.size()
        || emuenv.kernel.eventflags.size() != snapshot.eventflags.size()
        || emuenv.kernel.condvars.size() != snapshot.condvars.size()
        || emuenv.kernel.lwcondvars.size() != snapshot.lwcondvars.size()
        || emuenv.kernel.rwlocks.size() != snapshot.rwlocks.size()
        || emuenv.kernel.msgpipes.size() != snapshot.msgpipes.size()) {
        LOG_WARN("Refused sync primitive restore for {} because current kernel object counts do not match the saved snapshot.", slot.title_id);
        return false;
    }

    for (const auto &[uid, saved_event] : snapshot.simple_events) {
        const auto current = emuenv.kernel.simple_events.find(uid);
        if (current == emuenv.kernel.simple_events.end())
            return false;
        const std::lock_guard<std::mutex> event_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_event.name, saved_event.attr)
            || quick_state_waiting_count(current->second->waiting_threads) != saved_event.waiting_count) {
            LOG_WARN("Refused sync primitive restore for {} because simple event {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
    }

    for (const auto &[uid, saved_timer] : snapshot.timers) {
        const auto current = emuenv.kernel.timers.find(uid);
        if (current == emuenv.kernel.timers.end())
            return false;
        const std::lock_guard<std::mutex> timer_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_timer.name, saved_timer.attr)
            || quick_state_waiting_count(current->second->waiting_threads) != saved_timer.waiting_count) {
            LOG_WARN("Refused sync primitive restore for {} because timer {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
    }

    for (const auto &[uid, saved_semaphore] : snapshot.semaphores) {
        const auto current = emuenv.kernel.semaphores.find(uid);
        if (current == emuenv.kernel.semaphores.end())
            return false;
        const std::lock_guard<std::mutex> semaphore_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_semaphore.name, saved_semaphore.attr)
            || quick_state_waiting_count(current->second->waiting_threads) != saved_semaphore.waiting_count) {
            LOG_WARN("Refused sync primitive restore for {} because semaphore {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
    }

    const auto validate_mutex_map = [&](const MutexPtrs &current_map, const std::map<SceUID, QuickStateSyncMutex> &saved_map, const char *kind) {
        for (const auto &[uid, saved_mutex] : saved_map) {
            const auto current = current_map.find(uid);
            if (current == current_map.end())
                return false;
            const std::lock_guard<std::mutex> mutex_lock(current->second->mutex);
            if (!quick_state_sync_identity_matches(*current->second, saved_mutex.name, saved_mutex.attr)
                || quick_state_waiting_count(current->second->waiting_threads) != saved_mutex.waiting_count
                || (saved_mutex.lightweight && current->second->workarea.address() != saved_mutex.workarea)) {
                LOG_WARN("Refused sync primitive restore for {} because {} {} does not match the saved identity.", slot.title_id, kind, uid);
                return false;
            }
            if (saved_mutex.owner != 0 && !resolve_owner(saved_mutex.owner)) {
                LOG_WARN("Refused sync primitive restore for {} because {} {} owner thread {} was not matched.", slot.title_id, kind, uid, saved_mutex.owner);
                return false;
            }
        }
        return true;
    };
    if (!validate_mutex_map(emuenv.kernel.mutexes, snapshot.mutexes, "mutex")
        || !validate_mutex_map(emuenv.kernel.lwmutexes, snapshot.lwmutexes, "lwmutex")) {
        return false;
    }

    for (const auto &[uid, saved_eventflag] : snapshot.eventflags) {
        const auto current = emuenv.kernel.eventflags.find(uid);
        if (current == emuenv.kernel.eventflags.end())
            return false;
        const std::lock_guard<std::mutex> eventflag_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_eventflag.name, saved_eventflag.attr)
            || quick_state_waiting_count(current->second->waiting_threads) != saved_eventflag.waiting_count) {
            LOG_WARN("Refused sync primitive restore for {} because event flag {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
    }

    const auto validate_condvar_map = [&](const CondvarPtrs &current_map, const MutexPtrs &associated_mutexes, const std::map<SceUID, QuickStateSyncCondvar> &saved_map, const char *kind) {
        for (const auto &[uid, saved_condvar] : saved_map) {
            const auto current = current_map.find(uid);
            if (current == current_map.end())
                return false;
            const auto associated = saved_condvar.associated_mutex == 0 ? associated_mutexes.end() : associated_mutexes.find(saved_condvar.associated_mutex);
            const std::lock_guard<std::mutex> condvar_lock(current->second->mutex);
            if (!quick_state_sync_identity_matches(*current->second, saved_condvar.name, saved_condvar.attr)
                || quick_state_waiting_count(current->second->waiting_threads) != saved_condvar.waiting_count
                || (saved_condvar.associated_mutex != 0 && associated == associated_mutexes.end())
                || (saved_condvar.associated_mutex == 0 && current->second->associated_mutex)
                || (saved_condvar.associated_mutex != 0 && !current->second->associated_mutex)
                || (saved_condvar.associated_mutex != 0 && current->second->associated_mutex && current->second->associated_mutex->uid != saved_condvar.associated_mutex)) {
                LOG_WARN("Refused sync primitive restore for {} because {} {} does not match the saved identity.", slot.title_id, kind, uid);
                return false;
            }
        }
        return true;
    };
    if (!validate_condvar_map(emuenv.kernel.condvars, emuenv.kernel.mutexes, snapshot.condvars, "condvar")
        || !validate_condvar_map(emuenv.kernel.lwcondvars, emuenv.kernel.lwmutexes, snapshot.lwcondvars, "lwcondvar")) {
        return false;
    }

    for (const auto &[uid, saved_rwlock] : snapshot.rwlocks) {
        const auto current = emuenv.kernel.rwlocks.find(uid);
        if (current == emuenv.kernel.rwlocks.end())
            return false;
        const std::lock_guard<std::mutex> rwlock_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_rwlock.name, saved_rwlock.attr)
            || quick_state_waiting_count(current->second->waiting_threads) != saved_rwlock.waiting_count) {
            LOG_WARN("Refused sync primitive restore for {} because rwlock {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
        for (const auto &[owner_id, _] : saved_rwlock.owners) {
            if (!resolve_owner(owner_id)) {
                LOG_WARN("Refused sync primitive restore for {} because rwlock {} owner thread {} was not matched.", slot.title_id, uid, owner_id);
                return false;
            }
        }
    }

    for (const auto &[uid, saved_msgpipe] : snapshot.msgpipes) {
        const auto current = emuenv.kernel.msgpipes.find(uid);
        if (current == emuenv.kernel.msgpipes.end())
            return false;
        const std::lock_guard<std::mutex> msgpipe_lock(current->second->mutex);
        if (!quick_state_sync_identity_matches(*current->second, saved_msgpipe.name, saved_msgpipe.attr)
            || quick_state_waiting_count(current->second->senders) != saved_msgpipe.sender_count
            || quick_state_waiting_count(current->second->receivers) != saved_msgpipe.receiver_count
            || current->second->data_buffer.Capacity() != saved_msgpipe.capacity
            || current->second->data_buffer.Used() != saved_msgpipe.used) {
            LOG_WARN("Refused sync primitive restore for {} because msgpipe {} does not match the saved identity.", slot.title_id, uid);
            return false;
        }
    }

    for (const auto &[uid, saved_event] : snapshot.simple_events) {
        auto &event = emuenv.kernel.simple_events.at(uid);
        const std::lock_guard<std::mutex> event_lock(event->mutex);
        event->pattern = saved_event.pattern;
        event->last_user_data = saved_event.last_user_data;
        event->auto_reset = saved_event.auto_reset;
        event->cb_wakeup_only = saved_event.cb_wakeup_only;
    }

    for (const auto &[uid, saved_timer] : snapshot.timers) {
        auto &timer = emuenv.kernel.timers.at(uid);
        const std::lock_guard<std::mutex> timer_lock(timer->mutex);
        timer->is_started = saved_timer.is_started;
        timer->is_repeat = saved_timer.is_repeat;
        timer->is_pulse = saved_timer.is_pulse;
        timer->event_set = saved_timer.event_set;
        timer->time = saved_timer.time;
        timer->next_event = quick_state_timer_restore_next_event(saved_timer);
        timer->event_interval = saved_timer.event_interval;
        timer->condvar.notify_all();
    }

    for (const auto &[uid, saved_semaphore] : snapshot.semaphores) {
        auto &semaphore = emuenv.kernel.semaphores.at(uid);
        const std::lock_guard<std::mutex> semaphore_lock(semaphore->mutex);
        semaphore->init_val = saved_semaphore.init_val;
        semaphore->val = saved_semaphore.value;
        semaphore->max = saved_semaphore.max;
    }

    const auto restore_mutex_map = [&](MutexPtrs &current_map, const std::map<SceUID, QuickStateSyncMutex> &saved_map) {
        for (const auto &[uid, saved_mutex] : saved_map) {
            auto &mutex = current_map.at(uid);
            const std::lock_guard<std::mutex> mutex_lock(mutex->mutex);
            mutex->init_count = saved_mutex.init_count;
            mutex->lock_count = saved_mutex.lock_count;
            mutex->owner = resolve_owner(saved_mutex.owner);
        }
    };
    restore_mutex_map(emuenv.kernel.mutexes, snapshot.mutexes);
    restore_mutex_map(emuenv.kernel.lwmutexes, snapshot.lwmutexes);

    for (const auto &[uid, saved_eventflag] : snapshot.eventflags) {
        auto &eventflag = emuenv.kernel.eventflags.at(uid);
        const std::lock_guard<std::mutex> eventflag_lock(eventflag->mutex);
        eventflag->flags = saved_eventflag.flags;
    }

    const auto restore_condvar_map = [&](CondvarPtrs &current_map, MutexPtrs &associated_mutexes, const std::map<SceUID, QuickStateSyncCondvar> &saved_map) {
        for (const auto &[uid, saved_condvar] : saved_map) {
            auto &condvar = current_map.at(uid);
            const std::lock_guard<std::mutex> condvar_lock(condvar->mutex);
            condvar->associated_mutex = saved_condvar.associated_mutex == 0 ? nullptr : associated_mutexes.at(saved_condvar.associated_mutex);
        }
    };
    restore_condvar_map(emuenv.kernel.condvars, emuenv.kernel.mutexes, snapshot.condvars);
    restore_condvar_map(emuenv.kernel.lwcondvars, emuenv.kernel.lwmutexes, snapshot.lwcondvars);

    for (const auto &[uid, saved_rwlock] : snapshot.rwlocks) {
        auto &rwlock = emuenv.kernel.rwlocks.at(uid);
        const std::lock_guard<std::mutex> rwlock_lock(rwlock->mutex);
        rwlock->state = saved_rwlock.state;
        rwlock->owners.clear();
        for (const auto &[owner_id, count] : saved_rwlock.owners)
            rwlock->owners[threads_by_saved_id.at(owner_id)] = count;
    }

    for (const auto &[uid, saved_msgpipe] : snapshot.msgpipes) {
        auto &msgpipe = emuenv.kernel.msgpipes.at(uid);
        const std::lock_guard<std::mutex> msgpipe_lock(msgpipe->mutex);
        msgpipe->beingDeleted = saved_msgpipe.being_deleted;
    }

    LOG_INFO("Restored sync primitive scalar snapshot for {} (timers={}, semaphores={}, mutexes={}, lwmutexes={}, eventflags={}, condvars={}, lwcondvars={}, rwlocks={}, simple_events={}, msgpipes={}).",
        slot.title_id,
        snapshot.timers.size(),
        snapshot.semaphores.size(),
        snapshot.mutexes.size(),
        snapshot.lwmutexes.size(),
        snapshot.eventflags.size(),
        snapshot.condvars.size(),
        snapshot.lwcondvars.size(),
        snapshot.rwlocks.size(),
        snapshot.simple_events.size(),
        snapshot.msgpipes.size());
    return true;
}

static DisplayFrameInfo quick_state_to_display_frame_info(const QuickStateDisplayFrameSnapshot &snapshot) {
    DisplayFrameInfo frame;
    frame.base = Ptr<const void>(snapshot.base);
    frame.pitch = snapshot.pitch;
    frame.pixelformat = snapshot.pixelformat;
    frame.image_size = { snapshot.width, snapshot.height };
    return frame;
}

static bool restore_quick_state_display_state(EmuEnvState &emuenv, const QuickStateSlot &slot, const std::vector<ThreadStatePtr> &matched_threads) {
    QuickStateDisplaySnapshot snapshot;
    if (!quick_state_parse_display_snapshot_section(slot, snapshot)) {
        LOG_WARN("Refused display restore for {} because the thor.display section is missing or invalid.", slot.title_id);
        return false;
    }

    if (!snapshot.vblank_waits_complete) {
        LOG_WARN("Refused display restore for {} because vblank wait target metadata is incomplete (waits={}).",
            slot.title_id, snapshot.vblank_wait_info_count);
        return false;
    }

    if (snapshot.vblank_callback_count > 0) {
        LOG_WARN("Refused display restore for {} because vblank callbacks still need callback lifecycle serialization (callbacks={}).",
            slot.title_id, snapshot.vblank_callback_count);
        return false;
    }

    const auto threads_by_saved_id = quick_state_matched_threads_by_saved_id(slot, matched_threads);
    std::vector<DisplayStateVBlankWaitInfo> restored_vblank_waits;
    restored_vblank_waits.reserve(snapshot.vblank_waits.size());
    for (const auto &saved_wait : snapshot.vblank_waits) {
        const auto thread = threads_by_saved_id.find(saved_wait.thread_id);
        if (thread == threads_by_saved_id.end() || !thread->second) {
            LOG_WARN("Refused display restore for {} because vblank wait thread {} was not matched.",
                slot.title_id, saved_wait.thread_id);
            return false;
        }
        if (saved_wait.target_vcount <= snapshot.vblank_count) {
            LOG_WARN("Refused display restore for {} because vblank wait thread {} has stale target vcount {} at saved vblank {}.",
                slot.title_id, saved_wait.thread_id, saved_wait.target_vcount, snapshot.vblank_count);
            return false;
        }
        restored_vblank_waits.push_back({ thread->second, saved_wait.target_vcount });
    }

    {
        const std::lock_guard<std::mutex> display_lock(emuenv.display.mutex);
        if (emuenv.display.vblank_callbacks.size() != snapshot.vblank_callback_count) {
            LOG_WARN("Refused display restore for {} because current vblank callback count does not match the saved snapshot.", slot.title_id);
            return false;
        }
        emuenv.display.vblank_wait_infos = std::move(restored_vblank_waits);
    }

    emuenv.display.speed_percent.store(snapshot.speed_percent);
    emuenv.display.vblank_count.store(snapshot.vblank_count);
    emuenv.display.last_setframe_vblank_count.store(snapshot.last_setframe_vblank_count);
    emuenv.display.current_sync_object.store(snapshot.current_sync_object);
    emuenv.display.fps_hack = snapshot.fps_hack;
    emuenv.display.predicting.store(snapshot.predicting);
    {
        const std::lock_guard<std::mutex> display_info_lock(emuenv.display.display_info_mutex);
        emuenv.display.sce_frame = quick_state_to_display_frame_info(snapshot.sce_frame);
        emuenv.display.next_rendered_frame = quick_state_to_display_frame_info(snapshot.next_rendered_frame);
        emuenv.display.predicted_frames.clear();
        emuenv.display.predicted_frames.reserve(snapshot.predicted_frames.size());
        for (const auto &saved_predicted_frame : snapshot.predicted_frames) {
            PredictedDisplayFrame frame;
            frame.frame_info = quick_state_to_display_frame_info(saved_predicted_frame.frame);
            frame.sync_object = saved_predicted_frame.sync_object;
            emuenv.display.predicted_frames.push_back(frame);
        }
        emuenv.display.predicted_frame_position = snapshot.predicted_frame_position;
        emuenv.display.predicted_cycles_seen = snapshot.predicted_cycles_seen;
    }

    LOG_INFO("Restored display scalar snapshot for {} (vblank={}, waits={}, predicted_frames={}, speed={}%).",
        slot.title_id,
        snapshot.vblank_count,
        snapshot.vblank_wait_info_count,
        snapshot.predicted_frames.size(),
        snapshot.speed_percent);
    return true;
}

static bool restore_quick_state_audio_state(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    QuickStateAudioSnapshot snapshot;
    if (!quick_state_parse_audio_snapshot_section(slot, snapshot)) {
        LOG_WARN("Refused audio restore for {} because the thor.audio section is missing or invalid.", slot.title_id);
        return false;
    }

    const std::lock_guard<std::mutex> audio_lock(emuenv.audio.mutex);
    if (emuenv.audio.audio_backend != snapshot.backend || emuenv.audio.out_ports.size() != snapshot.out_ports.size()) {
        LOG_WARN("Refused audio restore for {} because current backend/port count does not match the saved snapshot.", slot.title_id);
        return false;
    }

    for (const auto &[port_id, saved_port] : snapshot.out_ports) {
        const auto current = emuenv.audio.out_ports.find(port_id);
        if (current == emuenv.audio.out_ports.end()) {
            LOG_WARN("Refused audio restore for {} because audio port {} is not open in the current session.", slot.title_id, port_id);
            return false;
        }
        if (!current->second) {
            LOG_WARN("Refused audio restore for {} because audio port {} is null.", slot.title_id, port_id);
            return false;
        }
    }

    emuenv.audio.speed_percent.store(snapshot.speed_percent);
    emuenv.audio.global_volume = snapshot.global_volume;
    emuenv.audio.next_port_id = snapshot.next_port_id;
    emuenv.audio.in_port.running = snapshot.audio_in_running;
    emuenv.audio.in_port.len_bytes = snapshot.audio_in_len_bytes;
    for (const auto &[port_id, saved_port] : snapshot.out_ports) {
        auto &port = *emuenv.audio.out_ports.at(port_id);
        port.type = saved_port.type;
        port.len = saved_port.len;
        port.freq = saved_port.freq;
        port.mode = saved_port.mode;
        port.len_bytes = saved_port.len_bytes;
        port.len_microseconds = saved_port.len_microseconds;
        port.last_output = saved_port.last_output;
        port.left_channel_volume = saved_port.left_channel_volume;
        port.right_channel_volume = saved_port.right_channel_volume;
        port.volume = saved_port.volume;
        if (emuenv.audio.adapter)
            emuenv.audio.adapter->set_volume(port, saved_port.volume * snapshot.global_volume);
    }

    LOG_INFO("Restored audio scalar snapshot for {} (ports={}, backend={}, speed={}%).",
        slot.title_id,
        snapshot.out_ports.size(),
        snapshot.backend,
        snapshot.speed_percent);
    return true;
}

static std::optional<fs::path> quick_state_app0_relative_path(const std::string &vita_path) {
    std::string normalized = vita_path;
    string_utils::replace(normalized, "\\", "/");
    const std::string lower = string_utils::tolower(normalized);
    constexpr std::string_view app0_prefix = "app0:";
    if (lower.rfind(app0_prefix, 0) != 0)
        return std::nullopt;

    normalized.erase(0, app0_prefix.size());
    while (!normalized.empty() && normalized.front() == '/')
        normalized.erase(normalized.begin());
    if (normalized.empty())
        return std::nullopt;

    return fs_utils::utf8_to_path(normalized);
}

static std::optional<FileStats> quick_state_recreate_file_stats(EmuEnvState &emuenv, const QuickStateIOFilePosition &position) {
    if (position.memory_file) {
        const auto relative_path = quick_state_app0_relative_path(position.vita_path);
        if (!relative_path.has_value())
            return std::nullopt;

        vfs::FileBuffer buffer;
        if (!vfs::read_current_app_file(buffer, emuenv.io, emuenv.pref_path, *relative_path))
            return std::nullopt;

        return FileStats(position.vita_path.c_str(), position.translated_path, position.host_path, position.open_mode, buffer);
    }

    if (!fs::exists(position.host_path))
        return std::nullopt;

    FileStats file_stats(position.vita_path.c_str(), position.translated_path, position.host_path, position.open_mode);
    if (!file_stats.get_file_pointer())
        return std::nullopt;
    return file_stats;
}

static bool restore_quick_state_io_file_positions(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    std::map<SceUID, QuickStateIOFilePosition> positions_by_fd;
    SceUID saved_next_fd = 0;
    if (!quick_state_parse_io_file_positions_section(slot, positions_by_fd, &saved_next_fd)) {
        LOG_WARN("Refused IO file-position restore for {} because the thor.io.vfs file metadata is missing or invalid.", slot.title_id);
        return false;
    }

    if (positions_by_fd.size() != emuenv.io.std_files.size()) {
        for (const auto &[fd, position] : positions_by_fd) {
            if (emuenv.io.tty_files.contains(fd) || emuenv.io.dir_entries.contains(fd)) {
                LOG_WARN("Refused IO file-handle restore for {} because saved fd {} collides with an existing tty or directory handle.",
                    slot.title_id, fd);
                return false;
            }
        }

        StdFiles rebuilt_files;
        SceUID highest_fd = saved_next_fd;
        for (const auto &[fd, position] : positions_by_fd) {
            auto file = quick_state_recreate_file_stats(emuenv, position);
            if (!file.has_value()) {
                const std::string source = position.memory_file ? std::string("archive memory file") : fs_utils::path_to_utf8(position.host_path);
                LOG_WARN("Refused IO file-handle restore for {} because saved fd {} could not be recreated from {}.",
                    slot.title_id, fd, source);
                return false;
            }
            if (!file->seek(position.offset, SCE_SEEK_SET)) {
                LOG_WARN("Refused IO file-handle restore for {} because recreated fd {} could not seek to {}.",
                    slot.title_id, fd, position.offset);
                return false;
            }
            rebuilt_files.emplace(fd, std::move(*file));
            highest_fd = std::max<SceUID>(highest_fd, fd + 1);
        }

        emuenv.io.std_files = std::move(rebuilt_files);
        emuenv.io.next_fd = std::max(saved_next_fd, highest_fd);
        LOG_INFO("Recreated IO file handles for {} ({} open file(s), next_fd={}).",
            slot.title_id, positions_by_fd.size(), emuenv.io.next_fd);
        return true;
    }

    for (const auto &[fd, position] : positions_by_fd) {
        const auto file = emuenv.io.std_files.find(fd);
        if (file == emuenv.io.std_files.end()) {
            LOG_WARN("Refused IO file-position restore for {} because fd {} is not open in the current session.", slot.title_id, fd);
            return false;
        }

        const FileStats &current_file = file->second;
        if (position.vita_path != current_file.get_vita_loc()
            || position.translated_path != current_file.get_translated_path()
            || position.host_path != current_file.get_system_location()
            || position.open_mode != current_file.get_open_mode()
            || position.memory_file != current_file.is_memory_file()) {
            LOG_WARN("Refused IO file-position restore for {} because fd {} does not match the saved file identity.", slot.title_id, fd);
            return false;
        }
    }

    for (const auto &[fd, position] : positions_by_fd) {
        auto &file = emuenv.io.std_files.at(fd);
        if (!file.seek(position.offset, SCE_SEEK_SET)) {
            LOG_WARN("Refused IO file-position restore for {} because fd {} could not seek to {}.", slot.title_id, fd, position.offset);
            return false;
        }
    }
    emuenv.io.next_fd = std::max(emuenv.io.next_fd, saved_next_fd);

    LOG_INFO("Restored IO file positions for {} ({} open file(s)).", slot.title_id, positions_by_fd.size());
    return true;
}

static bool restore_quick_state(EmuEnvState &emuenv, QuickStateSlot &slot) {
    if (!slot.valid || (slot.title_id != emuenv.io.title_id))
        return false;

    if (slot.restore_requires_same_pause) {
        const auto manifest = build_quick_state_restore_manifest(emuenv, slot);
        LOG_WARN("Refused quickstate restore for {} because {}.", slot.title_id, manifest.block_reason);
        return false;
    }

    bool already_paused = false;
    if (!pause_for_quick_state(emuenv, "restore", already_paused)) {
        LOG_WARN("Failed to restore quickstate for {} because guest threads did not pause in time.", slot.title_id);
        return false;
    }

    std::vector<ThreadStatePtr> matched_threads;
    if (!preflight_quick_state_restore_threads(emuenv.kernel, slot, matched_threads)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_allocation_map(emuenv, slot)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    uint32_t missing_pages = 0;
    for (const auto &page : slot.memory_pages) {
        if (!quick_state_page_can_restore(emuenv.mem, page, missing_pages)) {
            resume_after_quick_state(emuenv, already_paused);
            return false;
        }
    }

    if (missing_pages > 0) {
        resume_after_quick_state(emuenv, already_paused);
        LOG_WARN("Refused quickstate restore for {} because {} guest memory page(s) are not allocated in the current session. Restart/load-state support needs full kernel object and allocation-map serialization before this can be restored safely.",
            slot.title_id, missing_pages);
        return false;
    }

    if (!restore_quick_state_timing_state(emuenv, slot)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_thread_metadata(emuenv, slot, matched_threads)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_sync_primitives(emuenv, slot, matched_threads)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_io_file_positions(emuenv, slot)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_display_state(emuenv, slot, matched_threads)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    if (!restore_quick_state_audio_state(emuenv, slot)) {
        resume_after_quick_state(emuenv, already_paused);
        return false;
    }

    for (const auto &page : slot.memory_pages)
        std::memcpy(Ptr<uint8_t>(page.address).get(emuenv.mem), page.bytes.data(), page.bytes.size());

    {
        for (size_t i = 0; i < slot.thread_contexts.size(); i++) {
            const auto &thread_context = slot.thread_contexts[i];
            const auto &thread = matched_threads[i];
            const std::lock_guard<std::mutex> thread_lock(thread->mutex);
            load_context(*thread->cpu, thread_context.context);
        }
    }

    reset_quick_state_runtime_render_state(emuenv);
    emuenv.kernel.invalidate_jit_cache(0, std::numeric_limits<Address>::max());
    resume_after_quick_state(emuenv, already_paused);

    return true;
}

static std::string quick_state_wait_queue_entry_detail(const QuickStateSyncWaitQueueEntry &entry) {
    std::ostringstream detail;
    detail << "#" << entry.index
           << " thread=" << entry.thread_id
           << " priority=" << entry.priority;
    if (entry.kind == "simple_event") {
        detail << " pattern=" << entry.pattern
               << " result_pattern=0x" << std::hex << entry.result_pattern
               << " user_data=0x" << entry.user_data << std::dec;
    } else if (entry.kind == "timer") {
        detail << " result_pattern=0x" << std::hex << entry.result_pattern
               << " user_data=0x" << entry.user_data << std::dec;
    } else if (entry.kind == "semaphore") {
        detail << " signal=" << entry.signal
               << " cancel=" << entry.cancel_source;
    } else if (entry.kind == "mutex" || entry.kind == "lwmutex") {
        detail << " lock_count=" << entry.lock_count;
    } else if (entry.kind == "eventflag") {
        detail << " wait=" << entry.wait
               << " flags=" << entry.flags
               << " out_bits=0x" << std::hex << entry.out_bits << std::dec
               << " cancel=" << entry.cancel_source;
    } else if (entry.kind == "rwlock") {
        detail << " is_write=" << entry.is_write;
    } else if (entry.kind == "msgpipe_sender" || entry.kind == "msgpipe_receiver") {
        detail << " request_size=" << entry.request_size;
    }
    return detail.str();
}

static void write_quick_state_wait_queue_marker(std::ostream &marker, const QuickStateSlot &slot) {
    QuickStateSyncSnapshot snapshot;
    if (!quick_state_parse_sync_primitives_section(slot, snapshot))
        return;

    marker << "\nKernel wait queue snapshot\n";
    const auto write_wait = [&marker, &snapshot](const char *kind, const SceUID uid, const std::string &name, const uint32_t count) {
        if (count > 0) {
            marker << kind << " " << uid << " (" << name << "): " << count << "\n";
            for (const QuickStateSyncWaitQueueEntry &entry : snapshot.wait_queue_entries) {
                if (entry.kind == kind && entry.object_id == uid)
                    marker << "  " << quick_state_wait_queue_entry_detail(entry) << "\n";
            }
        }
    };
    for (const auto &[uid, item] : snapshot.semaphores)
        write_wait("semaphore", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.eventflags)
        write_wait("eventflag", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.condvars)
        write_wait("condvar", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.lwcondvars)
        write_wait("lwcondvar", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.mutexes)
        write_wait("mutex", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.lwmutexes)
        write_wait("lwmutex", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.rwlocks)
        write_wait("rwlock", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.timers)
        write_wait("timer", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.simple_events)
        write_wait("simple_event", uid, item.name, item.waiting_count);
    for (const auto &[uid, item] : snapshot.msgpipes) {
        if (item.sender_count > 0) {
            marker << "msgpipe " << uid << " (" << item.name << ") senders: " << item.sender_count << "\n";
            for (const QuickStateSyncWaitQueueEntry &entry : snapshot.wait_queue_entries) {
                if (entry.kind == "msgpipe_sender" && entry.object_id == uid)
                    marker << "  " << quick_state_wait_queue_entry_detail(entry) << "\n";
            }
        }
        if (item.receiver_count > 0) {
            marker << "msgpipe " << uid << " (" << item.name << ") receivers: " << item.receiver_count << "\n";
            for (const QuickStateSyncWaitQueueEntry &entry : snapshot.wait_queue_entries) {
                if (entry.kind == "msgpipe_receiver" && entry.object_id == uid)
                    marker << "  " << quick_state_wait_queue_entry_detail(entry) << "\n";
            }
        }
    }
}

static void write_quick_state_marker(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    const fs::path state_dir = quick_state_dir(emuenv, slot.title_id);
    fs::create_directories(state_dir);
    const auto manifest = build_quick_state_restore_manifest(emuenv, slot);
    fs::ofstream marker(quick_state_marker_file(emuenv, slot.title_id));
    marker << "Vita3K Thor quickstate capture\n";
    marker << "Title ID: " << slot.title_id << "\n";
    marker << "Title: " << slot.title << "\n";
    marker << "Format version: " << manifest.format_version << "\n";
    marker << "Guest memory bytes: " << slot.byte_count << "\n";
    marker << "Guest memory pages: " << manifest.memory_pages << "\n";
    marker << "Thread contexts: " << slot.thread_contexts.size() << "\n";
    marker << "AVPlayer threads: " << manifest.avplayer_threads << "\n";
    marker << "Allocator words: " << manifest.allocator_words << "\n";
    marker << "Allocation pages: " << manifest.allocation_pages << "\n";
    marker << "Captured sections: " << quick_state_join_strings(manifest.captured_sections) << "\n";
    marker << "Compression level: " << std::clamp(emuenv.cfg.save_state_compression_level, 0, 9) << "\n";
    marker << "State file bytes: " << quick_state_file_size(emuenv, slot.title_id) << "\n";
    marker << "State root: " << quick_state_root(emuenv) << "\n";
    marker << "State file: " << quick_state_file(emuenv, slot.title_id) << "\n";
    marker << "\nRestore readiness\n";
    marker << "Restore enabled: " << (manifest.restore_enabled ? "yes" : "no") << "\n";
    marker << "Timing restore layer: " << (manifest.timing_restorable ? "ready" : "missing") << "\n";
    marker << "Thread metadata restore layer: " << (manifest.thread_metadata_restorable ? "ready" : "missing") << "\n";
    marker << "Sync primitive scalar restore layer: " << (manifest.sync_primitives_restorable ? "ready" : "missing") << "\n";
    marker << "Sync wait queue entries: " << manifest.sync_waiting_threads << "\n";
    marker << "Sync wait queue metadata: " << (manifest.sync_wait_queue_metadata_complete ? "ready" : "missing")
           << " (" << manifest.sync_wait_queue_entries << " serialized)\n";
    marker << "Message-pipe buffered bytes: " << manifest.msgpipe_buffer_bytes << "\n";
    marker << "IO file-position restore layer: " << (manifest.io_file_positions_restorable ? "ready" : "missing") << "\n";
    marker << "IO file-handle restore layer: " << (manifest.io_file_handles_restorable ? "ready" : "missing") << "\n";
    marker << "IO memory-backed file handles: " << manifest.io_memory_file_handles << "\n";
    marker << "Display scalar restore layer: " << (manifest.display_state_restorable ? "ready" : "missing") << "\n";
    marker << "Display vblank wait restore layer: " << (manifest.display_vblank_waits_restorable ? "ready" : "missing") << "\n";
    marker << "Display vblank waits: " << manifest.display_wait_entries << "\n";
    marker << "Display vblank callbacks: " << manifest.display_callback_entries << "\n";
    marker << "Audio scalar restore layer: " << (manifest.audio_state_restorable ? "ready" : "missing") << "\n";
    marker << "Block reason: " << manifest.block_reason << "\n";
    marker << "Missing serializers: " << quick_state_join_strings(manifest.missing_serializers) << "\n";
    marker << "\nKernel object snapshot at manifest time\n";
    marker << "Threads: " << manifest.kernel.threads << "\n";
    marker << "CPU threads: " << manifest.kernel.cpu_threads << "\n";
    marker << "Timers: " << manifest.kernel.timers << "\n";
    marker << "Semaphores: " << manifest.kernel.semaphores << "\n";
    marker << "Condvars: " << manifest.kernel.condvars << "\n";
    marker << "Lightweight condvars: " << manifest.kernel.lwcondvars << "\n";
    marker << "Mutexes: " << manifest.kernel.mutexes << "\n";
    marker << "Lightweight mutexes: " << manifest.kernel.lwmutexes << "\n";
    marker << "RW locks: " << manifest.kernel.rwlocks << "\n";
    marker << "Event flags: " << manifest.kernel.eventflags << "\n";
    marker << "Message pipes: " << manifest.kernel.msgpipes << "\n";
    marker << "Callbacks: " << manifest.kernel.callbacks << "\n";
    marker << "Simple events: " << manifest.kernel.simple_events << "\n";
    marker << "Loaded modules: " << manifest.kernel.loaded_modules << "\n";
    write_quick_state_wait_queue_marker(marker, slot);
    marker << "\nIO/VFS snapshot at manifest time\n";
    marker << "TTY files: " << manifest.io.tty_files << "\n";
    marker << "Open std files: " << manifest.io.std_files << "\n";
    marker << "Open directories: " << manifest.io.dir_entries << "\n";
    marker << "Active overlays: " << manifest.io.overlays << "\n";
    marker << "Archive entries: " << manifest.io.archive_entries << "\n";
    marker << "Archive directories: " << manifest.io.archive_dirs << "\n";
    marker << "\nNote: this is an experimental disk-backed capture. It is not PPSSPP-style load reliability yet; restore stays gated until the missing serializer list is actually implemented and proven stable on Windows.\n";
}

} // namespace

bool runtime_osd_is_open() {
    return runtime_osd_open;
}

bool runtime_quick_state_slot_valid(const EmuEnvState &emuenv) {
    return quick_state_slot0.valid && (quick_state_slot0.title_id == emuenv.io.title_id);
}

uint64_t runtime_quick_state_slot_bytes() {
    return quick_state_slot0.byte_count;
}

std::string runtime_quick_state_slot_status(EmuEnvState &emuenv) {
    const auto title_id = emuenv.io.title_id.empty() ? std::string("unknown-title") : emuenv.io.title_id;
    if (quick_state_slot0.valid && (quick_state_slot0.title_id == title_id)) {
        QuickStateDiskHeader header;
        const bool has_disk_state = read_quick_state_disk_header(emuenv, title_id, header);
        if (has_disk_state) {
            const uint64_t disk_size = quick_state_file_size(emuenv, title_id);
            return fmt::format("capture {} MiB, load gated ({} MiB raw)", disk_size / (1024 * 1024), quick_state_slot0.byte_count / (1024 * 1024));
        }
        return fmt::format("RAM capture {} MiB, load gated", quick_state_slot0.byte_count / (1024 * 1024));
    }

    QuickStateDiskHeader header;
    if (read_quick_state_disk_header(emuenv, title_id, header)) {
        const uint64_t disk_size = quick_state_file_size(emuenv, title_id);
        return fmt::format("disk capture {} MiB, load gated ({} MiB raw)", disk_size / (1024 * 1024), header.byte_count / (1024 * 1024));
    }

    return "empty";
}

void runtime_osd_set_open(EmuEnvState &emuenv, bool open) {
    if (open == runtime_osd_open)
        return;

    runtime_osd_open = open;
    if (open) {
        runtime_osd_auto_paused = false;
        if (!emuenv.kernel.is_threads_paused()) {
            app::switch_state(emuenv, true);
            runtime_osd_auto_paused = true;
        }
        LOG_INFO("Runtime OSD opened{}", runtime_osd_auto_paused ? " and paused emulation" : "");
        return;
    }

    if (runtime_osd_auto_paused)
        app::switch_state(emuenv, false);
    if (runtime_osd_auto_paused)
        quick_state_pause_epoch++;
    runtime_osd_auto_paused = false;
    LOG_INFO("Runtime OSD closed");
}

void runtime_set_speed_percent(EmuEnvState &emuenv, uint32_t speed_percent) {
    speed_percent = std::clamp(speed_percent, 100u, 1000u);
    emuenv.display.speed_percent.store(speed_percent);
    emuenv.kernel.set_speed_percent(speed_percent);
    emuenv.audio.speed_percent.store(speed_percent);
    {
        const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);
        for (auto &[_, timer] : emuenv.kernel.timers) {
            const std::lock_guard<std::mutex> timer_lock(timer->mutex);
            timer->condvar.notify_all();
        }
    }
    LOG_INFO("Runtime speed set to {}%", speed_percent);
}

void runtime_toggle_fast_forward(EmuEnvState &emuenv) {
    const bool enable = emuenv.display.speed_percent.load() == 100;
    const uint32_t configured_speed = static_cast<uint32_t>(std::clamp(emuenv.cfg.fast_forward_speed_percent, 101, 1000));
    const uint32_t speed_percent = enable ? configured_speed : 100;
    runtime_set_speed_percent(emuenv, speed_percent);
    LOG_INFO("Fast forward {}", enable ? fmt::format("{}%", configured_speed) : "off");
}

void runtime_request_save_state(EmuEnvState &emuenv) {
    const auto title_id = emuenv.io.title_id.empty() ? std::string("unknown-title") : emuenv.io.title_id;
    const fs::path state_dir = quick_state_dir(emuenv, title_id);
    fs::create_directories(state_dir);
    if (capture_quick_state(emuenv, quick_state_slot0)) {
        quick_state_slot0.sections = build_quick_state_capture_sections(emuenv, quick_state_slot0);
        const bool saved_to_disk = save_quick_state_to_disk(emuenv, quick_state_slot0);
        if (saved_to_disk)
            write_quick_state_marker(emuenv, quick_state_slot0);

        LOG_INFO("Captured {} quickstate slot 0 for {} at {} ({} bytes, {} threads)",
            saved_to_disk ? "disk-backed" : "RAM-only",
            title_id,
            saved_to_disk ? quick_state_file(emuenv, title_id) : state_dir,
            quick_state_slot0.byte_count,
            quick_state_slot0.thread_contexts.size());
        if (!emuenv.kernel.is_threads_paused() || (quick_state_slot0.pause_epoch != quick_state_pause_epoch)) {
            LOG_WARN("Quickstate slot 0 for {} was captured and then emulation resumed; restore is refused until full kernel/GPU/audio state serialization makes post-resume loads safe.", title_id);
        }
        if (!saved_to_disk)
            LOG_WARN("Failed to write disk-backed quickstate slot 0 for {}", title_id);
    } else {
        LOG_WARN("Failed to capture same-session quickstate slot 0 for {}", title_id);
    }
}

void runtime_request_load_state(EmuEnvState &emuenv) {
    const auto title_id = emuenv.io.title_id.empty() ? std::string("unknown-title") : emuenv.io.title_id;
    bool loaded_from_disk = false;
    if (!quick_state_slot0.valid || (quick_state_slot0.title_id != title_id)) {
        loaded_from_disk = load_quick_state_from_disk(emuenv, title_id, quick_state_slot0);
        if (loaded_from_disk) {
            LOG_INFO("Loaded disk-backed quickstate slot 0 for {} from {}", title_id, quick_state_file(emuenv, title_id));
        }
    }

    if (!quick_state_slot0.valid || (quick_state_slot0.title_id != title_id)) {
        LOG_WARN("No quickstate slot 0 is available for {}", title_id);
        return;
    }

    const auto manifest = build_quick_state_restore_manifest(emuenv, quick_state_slot0);
    if (!manifest.restore_enabled) {
        LOG_WARN("Refused quickstate slot 0 restore for {} because {}.", title_id, manifest.block_reason);
        LOG_INFO("Quickstate slot 0 manifest for {}: guest={} MiB, pages={}, saved_threads={}, sections={}, timing_restorable={}, thread_metadata_restorable={}, sync_primitives_restorable={}, sync_wait_entries={}, msgpipe_buffer_bytes={}, io_file_positions_restorable={}, io_file_handles_restorable={}, io_memory_file_handles={}, display_state_restorable={}, display_vblank_waits_restorable={}, display_wait_entries={}, display_callback_entries={}, audio_state_restorable={}, current_cpu_threads={}, current_kernel_objects timers={} semaphores={} condvars={} lwcondvars={} mutexes={} lwmutexes={} eventflags={} msgpipes={} callbacks={}, io_files={} io_dirs={} overlays={} archive_entries={}, avplayer_threads={}",
            title_id,
            manifest.guest_memory_bytes / (1024 * 1024),
            manifest.memory_pages,
            manifest.thread_contexts,
            quick_state_join_strings(manifest.captured_sections),
            manifest.timing_restorable,
            manifest.thread_metadata_restorable,
            manifest.sync_primitives_restorable,
            manifest.sync_waiting_threads,
            manifest.msgpipe_buffer_bytes,
            manifest.io_file_positions_restorable,
            manifest.io_file_handles_restorable,
            manifest.io_memory_file_handles,
            manifest.display_state_restorable,
            manifest.display_vblank_waits_restorable,
            manifest.display_wait_entries,
            manifest.display_callback_entries,
            manifest.audio_state_restorable,
            manifest.kernel.cpu_threads,
            manifest.kernel.timers,
            manifest.kernel.semaphores,
            manifest.kernel.condvars,
            manifest.kernel.lwcondvars,
            manifest.kernel.mutexes,
            manifest.kernel.lwmutexes,
            manifest.kernel.eventflags,
            manifest.kernel.msgpipes,
            manifest.kernel.callbacks,
            manifest.io.std_files,
            manifest.io.dir_entries,
            manifest.io.overlays,
            manifest.io.archive_entries,
            manifest.avplayer_threads);
        return;
    }

    if (restore_quick_state(emuenv, quick_state_slot0)) {
        LOG_INFO("Restored {} quickstate slot 0 for {} from {}",
            loaded_from_disk ? "durable" : "same-session",
            title_id,
            loaded_from_disk ? quick_state_file(emuenv, title_id) : quick_state_marker_file(emuenv, title_id));
    } else {
        LOG_WARN("Failed to restore quickstate slot 0 for {}", title_id);
    }
}

static std::string runtime_control_trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

static std::string runtime_control_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

static fs::path runtime_control_file_path() {
    const char *path = std::getenv("VITA3K_RUNTIME_CONTROL_FILE");
    if (path == nullptr || path[0] == '\0')
        path = std::getenv("VITA3K_RENDER_CONTROL_FILE");
    if (path == nullptr || path[0] == '\0')
        return {};

    return fs_utils::utf8_to_path(path);
}

static std::map<std::string, std::string> read_runtime_control_file(const fs::path &path) {
    std::map<std::string, std::string> values;
    fs::ifstream in(path);
    if (!in.good())
        return values;

    std::string line;
    while (std::getline(in, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);

        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;

        std::string key = runtime_control_lower(runtime_control_trim(std::string_view(line).substr(0, equals)));
        std::string value = runtime_control_trim(std::string_view(line).substr(equals + 1));
        if (!key.empty())
            values[std::move(key)] = std::move(value);
    }

    return values;
}

static void runtime_set_pause_state(EmuEnvState &emuenv, const bool pause) {
    const bool already_paused = emuenv.kernel.is_threads_paused();
    if (pause == already_paused)
        return;

    if (!pause) {
        runtime_osd_auto_paused = false;
        quick_state_pause_epoch++;
    }
    app::switch_state(emuenv, pause);
    LOG_INFO("Runtime control {}", pause ? "paused emulation" : "resumed emulation");
}

static void apply_runtime_control_action(EmuEnvState &emuenv, const std::string &raw_action) {
    std::string action = runtime_control_lower(runtime_control_trim(raw_action));
    std::replace(action.begin(), action.end(), '-', '_');
    if (action.empty() || action == "none" || action == "clear")
        return;

    if (emuenv.io.title_id.empty()) {
        LOG_WARN("Runtime control action '{}' ignored because no title is running", raw_action);
        return;
    }

    if (action == "save" || action == "save_state" || action == "save_state_0" || action == "quick_save" || action == "quicksave") {
        LOG_INFO("Runtime control action: save_state");
        runtime_request_save_state(emuenv);
    } else if (action == "load" || action == "load_state" || action == "load_state_0" || action == "quick_load" || action == "quickload") {
        LOG_INFO("Runtime control action: load_state");
        runtime_request_load_state(emuenv);
    } else if (action == "pause") {
        runtime_set_pause_state(emuenv, true);
    } else if (action == "resume" || action == "unpause") {
        runtime_set_pause_state(emuenv, false);
    } else if (action == "toggle_pause") {
        runtime_set_pause_state(emuenv, !emuenv.kernel.is_threads_paused());
    } else if (action == "open_osd") {
        runtime_osd_set_open(emuenv, true);
    } else if (action == "close_osd") {
        runtime_osd_set_open(emuenv, false);
    } else {
        LOG_WARN("Unknown runtime control action '{}'", raw_action);
    }
}

#ifdef __ANDROID__
static std::string android_runtime_control_property(const char *name) {
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get(name, value) <= 0)
        return {};
    return value;
}

static void runtime_poll_control_android_properties(EmuEnvState &emuenv) {
    RuntimeControlAndroidState &state = runtime_control_android_state;
    ++state.poll_counter;

    const std::string raw_action = android_runtime_control_property("debug.vita3k.runtime_action");
    std::string action = runtime_control_lower(runtime_control_trim(raw_action));
    if (action.empty() || action == "0" || action == "none" || action == "clear")
        return;

    std::string action_id = runtime_control_trim(android_runtime_control_property("debug.vita3k.runtime_action_id"));
    if (action_id.empty())
        action_id = action;
    if (action_id == state.last_action_id)
        return;

    state.last_action_id = action_id;
    LOG_INFO("Runtime control android-prop action={} action_id={}", raw_action, action_id);
    apply_runtime_control_action(emuenv, raw_action);
}
#endif

void runtime_poll_control_file(EmuEnvState &emuenv) {
#ifdef __ANDROID__
    runtime_poll_control_android_properties(emuenv);
#endif

    const fs::path path = runtime_control_file_path();
    if (path.empty())
        return;

    boost::system::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return;

    const std::time_t last_write = fs::last_write_time(path, ec);
    if (ec)
        return;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec)
        return;

    RuntimeControlFileState &state = runtime_control_file_state;
    if (state.initialized && state.path == path && state.last_write == last_write && state.last_size == size)
        return;

    state.initialized = true;
    state.path = path;
    state.last_write = last_write;
    state.last_size = size;

    const auto values = read_runtime_control_file(path);
    const auto action_it = values.find("action");
    if (action_it == values.end())
        return;

    const auto action_id_it = values.find("action_id");
    const std::string action_id = action_id_it != values.end() ? action_id_it->second : fmt::format("{}:{}:{}", fs_utils::path_to_utf8(path), last_write, size);
    if (!action_id.empty() && action_id == state.last_action_id)
        return;

    state.last_action_id = action_id;
    apply_runtime_control_action(emuenv, action_it->second);
}

static SDL_GamepadButton runtime_configured_button(const EmuEnvState &emuenv, const SDL_GamepadButton default_button) {
    const auto index = static_cast<size_t>(default_button);
    if (index < emuenv.cfg.controller_binds.size())
        return static_cast<SDL_GamepadButton>(emuenv.cfg.controller_binds[index]);
    return default_button;
}

static bool runtime_button_matches(const SDL_GamepadButton button, const SDL_GamepadButton configured_button, const SDL_GamepadButton default_button) {
    return (button == configured_button) || (button == default_button);
}

static bool runtime_gamepad_button_down(SDL_Gamepad *gamepad, const SDL_GamepadButton configured_button, const SDL_GamepadButton default_button) {
    return SDL_GetGamepadButton(gamepad, configured_button)
        || ((configured_button != default_button) && SDL_GetGamepadButton(gamepad, default_button));
}

static bool runtime_any_gamepad_button_down(EmuEnvState &emuenv, const SDL_GamepadButton configured_button, const SDL_GamepadButton default_button) {
    const std::lock_guard<std::mutex> guard(emuenv.ctrl.mutex);
    for (const auto &[_, controller] : emuenv.ctrl.controllers) {
        if (controller.controller && runtime_gamepad_button_down(controller.controller.get(), configured_button, default_button))
            return true;
    }
    return false;
}

static bool handle_runtime_gamepad_hotkey(EmuEnvState &emuenv, const SDL_Event &event) {
    static bool osd_chord_latched = false;
    static bool fast_forward_latched = false;
    static bool save_state_latched = false;
    static bool load_state_latched = false;
    const SDL_GamepadButton l3_button = runtime_configured_button(emuenv, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    const SDL_GamepadButton r3_button = runtime_configured_button(emuenv, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    const SDL_GamepadButton select_button = runtime_configured_button(emuenv, SDL_GAMEPAD_BUTTON_BACK);
    const SDL_GamepadButton r1_button = runtime_configured_button(emuenv, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if (runtime_button_matches(static_cast<SDL_GamepadButton>(event.gbutton.button), l3_button, SDL_GAMEPAD_BUTTON_LEFT_STICK)
            || runtime_button_matches(static_cast<SDL_GamepadButton>(event.gbutton.button), r3_button, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
            osd_chord_latched = false;
        }
        if (runtime_button_matches(static_cast<SDL_GamepadButton>(event.gbutton.button), r1_button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
            fast_forward_latched = false;
        if (runtime_button_matches(static_cast<SDL_GamepadButton>(event.gbutton.button), select_button, SDL_GAMEPAD_BUTTON_BACK)) {
            fast_forward_latched = false;
        }
        return false;
    }

    SDL_Gamepad *gamepad = nullptr;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        gamepad = SDL_GetGamepadFromID(event.gbutton.which);
    else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
        gamepad = SDL_GetGamepadFromID(event.gaxis.which);

    if (!gamepad)
        return false;

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        const bool l3_event = runtime_button_matches(button, l3_button, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        const bool r3_event = runtime_button_matches(button, r3_button, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        if ((l3_event || r3_event) && !emuenv.io.title_id.empty()) {
            const bool l3_down = runtime_gamepad_button_down(gamepad, l3_button, SDL_GAMEPAD_BUTTON_LEFT_STICK);
            const bool r3_down = runtime_gamepad_button_down(gamepad, r3_button, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
            if (l3_down && r3_down && !osd_chord_latched) {
                osd_chord_latched = true;
                LOG_INFO("L3+R3 chord: toggling runtime OSD");
                runtime_osd_set_open(emuenv, !runtime_osd_is_open());
                return true;
            }
        }
    }

    const bool select_down = runtime_gamepad_button_down(gamepad, select_button, SDL_GAMEPAD_BUTTON_BACK) || android_back_key_down;
    const bool r1_down = runtime_gamepad_button_down(gamepad, r1_button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    if ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) && runtime_button_matches(static_cast<SDL_GamepadButton>(event.gbutton.button), select_button, SDL_GAMEPAD_BUTTON_BACK)) {
        if (emuenv.io.title_id.empty())
            return false;
        if (r1_down && !fast_forward_latched) {
            fast_forward_latched = true;
            android_back_chord_used = true;
            runtime_toggle_fast_forward(emuenv);
            return true;
        }
        return false;
    }

    if (!select_down) {
        save_state_latched = false;
        load_state_latched = false;
        return false;
    }

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (r1_down && !fast_forward_latched) {
            fast_forward_latched = true;
            android_back_chord_used = true;
            runtime_toggle_fast_forward(emuenv);
            return true;
        }
    }

    if ((event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) && (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY)) {
        constexpr Sint16 axis_threshold = 16000;
        constexpr Sint16 axis_release = 8000;
        if (event.gaxis.value > axis_threshold && !save_state_latched) {
            save_state_latched = true;
            android_back_chord_used = true;
            runtime_request_save_state(emuenv);
            return true;
        }
        if (event.gaxis.value < -axis_threshold && !load_state_latched) {
            load_state_latched = true;
            android_back_chord_used = true;
            runtime_request_load_state(emuenv);
            return true;
        }
        if (std::abs(event.gaxis.value) < axis_release) {
            save_state_latched = false;
            load_state_latched = false;
        }
    }

    return false;
}

bool handle_events(EmuEnvState &emuenv, GuiState &gui) {
    const auto allow_switch_state = !emuenv.io.title_id.empty() && !gui.vita_area.app_close && !gui.vita_area.home_screen && !gui.vita_area.user_management && !gui.configuration_menu.custom_settings_dialog && !gui.configuration_menu.settings_dialog && !gui.controls_menu.controls_dialog && gui::get_sys_apps_state(gui);

    const auto ui_navigation = [&emuenv, &gui, allow_switch_state](const uint32_t sce_ctrl_btn) {
        switch (sce_ctrl_btn) {
        case SCE_CTRL_CROSS:
        case SCE_CTRL_CIRCLE:
            gui.is_key_locked = true;
            if (gui.vita_area.start_screen)
                gui::close_start_screen(gui, emuenv);
            break;
        case SCE_CTRL_PSBUTTON:
            gui.is_key_locked = true;
            if (allow_switch_state) {
                const auto running_app_path = gui::get_app_index(gui, emuenv.io.app_path) ? emuenv.io.app_path : emuenv.app_path;
                // Show/Hide Live Area during app running
                const auto live_area_app_index = gui::get_live_area_current_open_apps_list_index(gui, running_app_path);
                if (live_area_app_index == gui.live_area_current_open_apps_list.end())
                    gui::open_live_area(gui, emuenv, running_app_path);
                else {
                    // If current live area app open is not the current app running, set it as current
                    if ((gui.live_area_app_current_open < 0) || (gui.live_area_current_open_apps_list[gui.live_area_app_current_open] != running_app_path))
                        gui.live_area_app_current_open = static_cast<int32_t>(std::distance(live_area_app_index, gui.live_area_current_open_apps_list.end()) - 1);

                    // Switch Live Area state
                    if (!gui.vita_area.live_area_screen) {
                        gui.vita_area.information_bar = true;
                        gui.vita_area.live_area_screen = true;
                    }
                }

                if (!emuenv.kernel.is_threads_paused()) {
                    // Update the last app frame for live area
                    update_live_area_last_app_frame(emuenv, gui);
                    gui.gate_animation.start(GateAnimationState::ReturnApp);
                    app::switch_state(emuenv, true);
                    bgm_player::switch_bgm_state(false);
                } else {
                    gui.gate_animation.start(GateAnimationState::EnterApp);
                }
            } else if (!gui::get_sys_apps_state(gui))
                gui::close_system_app(gui, emuenv);
            break;
        default: break;
        }

        if (gui.vita_area.app_close) {
            const auto cancel = [&gui]() {
                gui.vita_area.app_close = false;
            };
            const auto confirm = [&gui, &emuenv]() {
                const auto app_path = gui.vita_area.live_area_screen ? gui.live_area_current_open_apps_list[gui.live_area_app_current_open] : emuenv.app_path;
                gui::close_and_run_new_app(emuenv, app_path);
            };
            switch (sce_ctrl_btn) {
            case SCE_CTRL_CIRCLE:
                if (emuenv.cfg.sys_button == 1)
                    cancel();
                else
                    confirm();
                break;
            case SCE_CTRL_CROSS:
                if (emuenv.cfg.sys_button == 1)
                    confirm();
                else
                    cancel();
                break;
            default: break;
            }
        } else if (gui.vita_area.user_management)
            gui::browse_users_management(gui, emuenv, sce_ctrl_btn);
        else if (gui.vita_area.manual)
            gui::browse_pages_manual(gui, emuenv, sce_ctrl_btn);
        else if (gui.vita_area.home_screen)
            gui::browse_home_apps_list(gui, emuenv, sce_ctrl_btn);
        else if (gui.vita_area.live_area_screen)
            gui::browse_live_area_apps_list(gui, emuenv, sce_ctrl_btn);
        else if (emuenv.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            switch (emuenv.common_dialog.type) {
            case SAVEDATA_DIALOG:
                gui::browse_save_data_dialog(gui, emuenv, sce_ctrl_btn);
                break;

            default: break;
            }
        }
    };

#ifdef __ANDROID__
    const auto android_back_held_ms = []() -> uint64_t {
        if (!android_back_key_down || (android_back_down_ticks == 0))
            return 0;

        const uint64_t now = SDL_GetTicks();
        return now >= android_back_down_ticks ? now - android_back_down_ticks : 0;
    };

    const auto route_android_back_to_vita_home = [&](const bool from_key_up) {
        if (android_back_long_press_used || android_back_chord_used || emuenv.io.title_id.empty())
            return;

        android_back_long_press_used = true;
        android_back_chord_used = true;
        if (runtime_osd_is_open())
            runtime_osd_set_open(emuenv, false);

        if (emuenv.display.speed_percent.load() != 100) {
            LOG_INFO("Android Back long press: disabling fast forward before Vita PS/Home");
            runtime_set_speed_percent(emuenv, 100);
        }

        LOG_INFO("Android Back long press: routing to Vita PS/Home");
        ui_navigation(SCE_CTRL_PSBUTTON);
        if (from_key_up)
            gui.is_key_locked = false;
    };
#endif

    // Check if any settings or controls dialog is open and drop inputs on this case
    emuenv.drop_inputs = gui.configuration_menu.settings_dialog || gui.configuration_menu.custom_settings_dialog || gui.controls_menu.controllers_dialog || gui.controls_menu.controls_dialog;

    // A set to store the last pressed buttons to prevent duplicate inputs from the controller.
    std::set<uint32_t> last_buttons;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSdl_ProcessEvent(gui.imgui_state.get(), &event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
            bgm_player::destroy_bgm_player();
            if (!emuenv.io.app_path.empty())
                gui::update_time_app_used(gui, emuenv, gui::get_app_index(gui, emuenv.io.app_path) ? emuenv.io.app_path : emuenv.app_path);
            if (emuenv.audio.adapter)
                emuenv.audio.switch_state(true);
            emuenv.kernel.exit_delete_all_threads();
            emuenv.gxm.display_queue.abort();
            emuenv.display.abort = true;
            emuenv.renderer->notification_ready.notify_all();
            if (emuenv.display.vblank_thread) {
                emuenv.display.vblank_thread->join();
            }
            return false;

        case SDL_EVENT_KEY_DOWN: {
            const auto get_sce_ctrl_btn_from_scancode = [&emuenv](const SDL_Scancode scancode) {
                if (scancode == emuenv.cfg.keyboard_button_up || scancode == emuenv.cfg.keyboard_button_up_alt)
                    return SCE_CTRL_UP;
                else if (scancode == emuenv.cfg.keyboard_button_right || scancode == emuenv.cfg.keyboard_button_right_alt)
                    return SCE_CTRL_RIGHT;
                else if (scancode == emuenv.cfg.keyboard_button_down || scancode == emuenv.cfg.keyboard_button_down_alt)
                    return SCE_CTRL_DOWN;
                else if (scancode == emuenv.cfg.keyboard_button_left || scancode == emuenv.cfg.keyboard_button_left_alt)
                    return SCE_CTRL_LEFT;
                else if (scancode == emuenv.cfg.keyboard_button_l1 || scancode == emuenv.cfg.keyboard_button_l1_alt)
                    return SCE_CTRL_L1;
                else if (scancode == emuenv.cfg.keyboard_button_r1 || scancode == emuenv.cfg.keyboard_button_r1_alt)
                    return SCE_CTRL_R1;
                else if (scancode == emuenv.cfg.keyboard_button_triangle || scancode == emuenv.cfg.keyboard_button_triangle_alt)
                    return SCE_CTRL_TRIANGLE;
                else if (scancode == emuenv.cfg.keyboard_button_circle || scancode == emuenv.cfg.keyboard_button_circle_alt)
                    return SCE_CTRL_CIRCLE;
                else if (scancode == emuenv.cfg.keyboard_button_cross || scancode == emuenv.cfg.keyboard_button_cross_alt)
                    return SCE_CTRL_CROSS;
                else if (scancode == emuenv.cfg.keyboard_button_psbutton || scancode == emuenv.cfg.keyboard_button_psbutton_alt)
                    return SCE_CTRL_PSBUTTON;
                else
                    return static_cast<SceCtrlButtons>(0);
            };

            // Get Sce Ctrl button from key
            auto sce_ctrl_btn = get_sce_ctrl_btn_from_scancode(event.key.scancode);

            if (gui.is_capturing_keys && event.key.scancode) {
                gui.is_key_capture_dropped = false;
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    LOG_ERROR("Key is reserved!");
                    gui.captured_key = gui.old_captured_key;
                    gui.is_key_capture_dropped = true;
                } else {
                    gui.captured_key = static_cast<int>(event.key.scancode);
                }
                gui.is_capturing_keys = false;
            }

            if (ImGui::GetIO().WantTextInput || gui.is_key_locked || emuenv.drop_inputs || gui.gate_animation.state != GateAnimationState::None)
                continue;
#ifdef __ANDROID__
            if (event.key.scancode == SDL_SCANCODE_AC_BACK) {
                if (!emuenv.io.title_id.empty()) {
                    if (!android_back_key_down) {
                        android_back_key_down = true;
                        android_back_chord_used = false;
                        android_back_fast_forward_latched = false;
                        android_back_long_press_used = false;
                        android_back_down_ticks = SDL_GetTicks();
                    } else if (android_back_held_ms() >= ANDROID_BACK_HOME_LONG_PRESS_MS)
                        route_android_back_to_vita_home(false);

                    const SDL_GamepadButton r1_button = runtime_configured_button(emuenv, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
                    if (!android_back_chord_used && !android_back_fast_forward_latched && runtime_any_gamepad_button_down(emuenv, r1_button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                        android_back_fast_forward_latched = true;
                        android_back_chord_used = true;
                        runtime_toggle_fast_forward(emuenv);
                    }
                    continue;
                }
                sce_ctrl_btn = SCE_CTRL_PSBUTTON;
            }
#else
            // toggle gui state
            if (allow_switch_state && (event.key.scancode == emuenv.cfg.keyboard_gui_toggle_gui || event.key.scancode == emuenv.cfg.keyboard_gui_toggle_gui_alt))
                emuenv.display.imgui_render = !emuenv.display.imgui_render;
            if ((event.key.scancode == emuenv.cfg.keyboard_gui_toggle_touch || event.key.scancode == emuenv.cfg.keyboard_gui_toggle_touch_alt) && !gui.is_key_capture_dropped)
                toggle_touchscreen();
            if ((event.key.scancode == emuenv.cfg.keyboard_gui_fullscreen || event.key.scancode == emuenv.cfg.keyboard_gui_fullscreen_alt) && !gui.is_key_capture_dropped)
                switch_full_screen(emuenv);
            if ((event.key.scancode == emuenv.cfg.keyboard_toggle_texture_replacement || event.key.scancode == emuenv.cfg.keyboard_toggle_texture_replacement_alt) && !gui.is_key_capture_dropped)
                toggle_texture_replacement(emuenv);
            if ((event.key.scancode == emuenv.cfg.keyboard_take_screenshot || event.key.scancode == emuenv.cfg.keyboard_take_screenshot_alt) && !gui.is_key_capture_dropped)
                runtime_take_screenshot(emuenv);
            if ((event.key.scancode == emuenv.cfg.keyboard_pinch_modifier || event.key.scancode == emuenv.cfg.keyboard_pinch_modifier_alt || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in_alt || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out_alt) && !gui.is_key_capture_dropped)
                pinch_modifier(true);

            const float pinch_amount = 0.5;
            if ((event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in_alt) && !gui.is_key_capture_dropped)
                pinch_automove(pinch_amount * -1);
            if ((event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out_alt) && !gui.is_key_capture_dropped)
                pinch_automove(pinch_amount);
#endif
            if (runtime_osd_is_open())
                continue;
            if (sce_ctrl_btn != 0) {
                if (last_buttons.contains(sce_ctrl_btn)) {
                    continue;
                }
                last_buttons.insert(sce_ctrl_btn);
                ui_navigation(sce_ctrl_btn);
            }

            break;
        }
        case SDL_EVENT_KEY_UP:
            gui.is_key_locked = false;
#ifdef __ANDROID__
            if (event.key.scancode == SDL_SCANCODE_AC_BACK) {
                if (android_back_key_down && !android_back_chord_used && !emuenv.io.title_id.empty()) {
                    if (android_back_held_ms() >= ANDROID_BACK_HOME_LONG_PRESS_MS)
                        route_android_back_to_vita_home(true);
                    else {
                        LOG_INFO("Android Back short press: toggling runtime OSD");
                        runtime_osd_set_open(emuenv, !runtime_osd_is_open());
                    }
                }
                android_back_key_down = false;
                android_back_chord_used = false;
                android_back_fast_forward_latched = false;
                android_back_long_press_used = false;
                android_back_down_ticks = 0;
                continue;
            }
#endif
            if (runtime_osd_is_open())
                continue;
            if (event.key.scancode == emuenv.cfg.keyboard_pinch_modifier || event.key.scancode == emuenv.cfg.keyboard_pinch_modifier_alt || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_in_alt || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out || event.key.scancode == emuenv.cfg.keyboard_alternate_pinch_out_alt) {
                pinch_modifier(false);
                pinch_automove(0);
            }

            break;

        case SDL_EVENT_MOUSE_WHEEL:
            pinch_move(event.wheel.y);
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            gui.is_nav_button = false;
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (handle_runtime_gamepad_hotkey(emuenv, event))
                continue;
            if (runtime_osd_is_open())
                continue;

            if (!emuenv.kernel.is_threads_paused() && (event.gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD))
                toggle_touchscreen();

            if (ImGui::GetIO().WantTextInput || gui.is_key_locked || emuenv.drop_inputs || (gui.gate_animation.state != GateAnimationState::None))
                continue;

            for (const auto &binding : get_controller_bindings_ext(emuenv)) {
                if (event.gbutton.button == binding.controller) {
                    if (last_buttons.contains(binding.button)) {
                        continue;
                    }
                    last_buttons.insert(binding.button);
                    ui_navigation(binding.button);

                    break;
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            handle_runtime_gamepad_hotkey(emuenv, event);
            if (runtime_osd_is_open())
                continue;
            gui.is_key_locked = false;
            break;

        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (handle_runtime_gamepad_hotkey(emuenv, event))
                continue;
            if (runtime_osd_is_open())
                continue;
            break;

        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
            handle_touchpad_event(event.gtouchpad);
            break;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            handle_motion_event(emuenv, event.gsensor.sensor, event.gsensor);
            break;
        case SDL_EVENT_SENSOR_UPDATE:
            handle_motion_event(emuenv, SDL_GetSensorTypeForID(event.sensor.which), event.sensor);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            refresh_controllers(emuenv.ctrl, emuenv);
            break;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            app::update_viewport(emuenv);
            break;

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
            handle_touch_event(event.tfinger);
            break;
        case SDL_EVENT_DROP_FILE: {
            const auto drop_file = fs_utils::utf8_to_path(event.drop.data);
            const auto extension = string_utils::tolower(drop_file.extension().string());
            if (extension == ".pup") {
                const std::string fw_version = install_pup(emuenv.pref_path, drop_file);
                if (!fw_version.empty()) {
                    LOG_INFO("Firmware {} installed successfully!", fw_version);
                    gui::get_modules_list(gui, emuenv);
                    if (emuenv.cfg.initial_setup)
                        gui::init_theme(gui, emuenv, gui.users[emuenv.cfg.user_id].theme_id);
                }
            } else if ((extension == ".vpk") || (extension == ".zip"))
                install_archive(emuenv, &gui, drop_file);
            else if ((extension == ".rif") || (drop_file.filename() == "work.bin"))
                copy_license(emuenv, drop_file);
            else if (fs::is_directory(drop_file))
                install_contents(emuenv, &gui, drop_file);
            else if (drop_file.filename() == "theme.xml")
                install_content(emuenv, &gui, drop_file.parent_path());
            else
                LOG_ERROR("File dropped: [{}] is not supported.", drop_file.filename());
            break;
        }
        }
    }

    return true;
}

ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv) {
    if (load_app_impl(main_module_id, emuenv) != Success) {
        std::string message = fmt::format(fmt::runtime(emuenv.common_dialog.lang.message["load_app_failed"]), emuenv.pref_path / "ux0/app" / emuenv.io.app_path / emuenv.self_path);
        app::error_dialog(message, emuenv.window.get());
        return ModuleLoadFailed;
    }

    if (!emuenv.cfg.show_gui)
        emuenv.display.imgui_render = false;

    if (emuenv.cfg.gdbstub) {
        emuenv.kernel.debugger.wait_for_debugger = true;
        server_open(emuenv);
    }

#if USE_DISCORD
    if (emuenv.cfg.discord_rich_presence)
        discordrpc::update_presence(emuenv.io.title_id, emuenv.current_app_title);
#endif

    return Success;
}

static std::vector<std::string> split(const std::string &input, const std::string &regex) {
    std::regex re(regex);
    std::sregex_token_iterator
        first{ input.begin(), input.end(), re, -1 },
        last;
    return { first, last };
}

ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id) {
    auto entry_point = emuenv.kernel.loaded_modules[main_module_id]->info.start_entry;
    auto process_param = emuenv.kernel.process_param.get(emuenv.mem);

    SceInt32 priority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
    SceInt32 stack_size = SCE_KERNEL_STACK_SIZE_USER_MAIN;
    SceInt32 affinity = SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT;
    if (process_param) {
        auto priority_ptr = Ptr<int32_t>(process_param->main_thread_priority);
        if (priority_ptr) {
            priority = *priority_ptr.get(emuenv.mem);
        }

        auto stack_size_ptr = Ptr<int32_t>(process_param->main_thread_stacksize);
        if (stack_size_ptr) {
            stack_size = *stack_size_ptr.get(emuenv.mem);
        }

        auto affinity_ptr = Ptr<SceInt32>(process_param->main_thread_cpu_affinity_mask);
        if (affinity_ptr) {
            affinity = *affinity_ptr.get(emuenv.mem);
        }
    }
    const ThreadStatePtr main_thread = emuenv.kernel.create_thread(emuenv.mem, emuenv.io.title_id.c_str(), entry_point, priority, affinity, stack_size, nullptr);
    if (!main_thread) {
        app::error_dialog("Failed to init main thread.", emuenv.window.get());
        return InitThreadFailed;
    }
    emuenv.main_thread_id = main_thread->id;

    // Run `module_start` export (entry point) of loaded libraries
    for (auto &[_, module] : emuenv.kernel.loaded_modules) {
        if (module->info.modid != main_module_id)
            start_module(emuenv, module->info);
    }

    SceKernelThreadOptParam param{ 0, 0 };
    if (!emuenv.cfg.app_args.empty()) {
        auto args = split(emuenv.cfg.app_args, ",\\s+");
        // why is this flipped
        std::vector<uint8_t> buf;
        for (auto &arg : args)
            buf.insert(buf.end(), arg.c_str(), arg.c_str() + arg.size() + 1);
        auto arr = Ptr<uint8_t>(alloc(emuenv.mem, static_cast<uint32_t>(buf.size()), "arg"));
        memcpy(arr.get(emuenv.mem), buf.data(), buf.size());
        param.size = static_cast<SceSize>(buf.size());
        param.attr = arr.address();
    }
    if (main_thread->start(param.size, Ptr<void>(param.attr), true) < 0) {
        app::error_dialog("Failed to run main thread.", emuenv.window.get());
        return RunThreadFailed;
    }

    start_sync_thread(emuenv);

    if (emuenv.cfg.boot_apps_full_screen && !emuenv.display.fullscreen.load())
        switch_full_screen(emuenv);

    return Success;
}

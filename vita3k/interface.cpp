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
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <string_view>
#include <thread>
#include <vector>

#include <cstdlib>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

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

struct QuickStateSlot {
    bool valid = false;
    std::string title_id;
    std::string title;
    std::vector<QuickStateThreadContext> thread_contexts;
    std::vector<QuickStateMemoryPage> memory_pages;
    std::vector<uint32_t> allocator_words;
    std::vector<uint32_t> allocation_pages;
    uint64_t byte_count = 0;
};

static QuickStateSlot quick_state_slot0;
static bool runtime_osd_open = false;
static bool runtime_osd_auto_paused = false;
static bool android_back_key_down = false;
static bool android_back_chord_used = false;
static bool android_back_fast_forward_latched = false;
static bool android_back_long_press_used = false;
static uint64_t android_back_down_ticks = 0;
constexpr uint64_t ANDROID_BACK_HOME_LONG_PRESS_MS = 400;
constexpr char QUICKSTATE_MAGIC[] = { 'V', '3', 'K', 'T', 'H', 'O', 'R', 'S', 'T', 'A', 'T', 'E' };
constexpr uint32_t QUICKSTATE_VERSION = 3;
constexpr uint32_t QUICKSTATE_MAX_STRING_BYTES = 4096;
constexpr uint32_t QUICKSTATE_COMPRESSION_NONE = 0;
constexpr uint32_t QUICKSTATE_COMPRESSION_MINIZ = 1;

struct RuntimeControlFileState {
    bool initialized = false;
    fs::path path;
    std::time_t last_write = 0;
    uintmax_t last_size = 0;
    std::string last_action_id;
};

static RuntimeControlFileState runtime_control_file_state;

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

static bool memory_page_is_allocated(const MemState &mem, const uint32_t page) {
    const uint32_t word = mem.allocator.words[page >> 5];
    return (word & (1U << (page & 31))) == 0;
}

static bool valid_quick_state_page(const MemState &mem, const Address address, const uint32_t size) {
    if (address > (std::numeric_limits<Address>::max() - size))
        return false;

    return is_valid_addr_range(mem, address, address + size);
}

static bool wait_for_guest_threads_paused(KernelState &kernel) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
    while (std::chrono::steady_clock::now() < deadline) {
        bool all_paused = true;
        {
            const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
            for (const auto &[_, thread] : kernel.threads) {
                const std::lock_guard<std::mutex> thread_lock(thread->mutex);
                if (thread->status == ThreadStatus::run) {
                    all_paused = false;
                    break;
                }
            }
        }

        if (all_paused)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

static bool capture_quick_state(EmuEnvState &emuenv, QuickStateSlot &slot) {
    if (emuenv.io.title_id.empty())
        return false;

    const bool already_paused = emuenv.kernel.is_threads_paused();
    if (!already_paused)
        emuenv.kernel.pause_threads();

    if (!wait_for_guest_threads_paused(emuenv.kernel)) {
        if (!already_paused)
            emuenv.kernel.resume_threads();
        LOG_WARN("Failed to capture quickstate for {} because guest threads did not pause in time.", emuenv.io.title_id);
        return false;
    }

    QuickStateSlot captured;
    captured.valid = true;
    captured.title_id = emuenv.io.title_id;
    captured.title = emuenv.current_app_title;

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
    if (!already_paused)
        emuenv.kernel.resume_threads();

    return true;
}

static bool save_quick_state_to_disk(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    if (!slot.valid || slot.title_id.empty())
        return false;

    const fs::path state_dir = quick_state_dir(emuenv, slot.title_id);
    const fs::path state_file = quick_state_file(emuenv, slot.title_id);
    const fs::path tmp_file = state_file.string() + ".tmp";
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
            if (quick_state_compress_page(page.bytes, compressed_page, static_cast<int>(compression_level))) {
                compression_method = QUICKSTATE_COMPRESSION_MINIZ;
                page_bytes = &compressed_page;
            }

            const uint32_t stored_page_size = static_cast<uint32_t>(page_bytes->size());
            if (!quick_state_write_value(out, page.address)
                || !quick_state_write_value(out, raw_page_size)
                || !quick_state_write_value(out, stored_page_size)
                || !quick_state_write_value(out, compression_method)
                || !quick_state_write_bytes(out, page_bytes->data(), stored_page_size)) {
                return false;
            }
        }
    }

    boost::system::error_code ec;
    fs::remove(state_file, ec);
    ec.clear();
    fs::rename(tmp_file, state_file, ec);
    if (ec) {
        fs::remove(tmp_file);
        LOG_WARN("Failed to finalize durable quickstate {}: {}", state_file, ec.message());
        return false;
    }

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

        byte_count += raw_page_size;
        loaded.memory_pages.push_back(std::move(page));
    }

    if (byte_count != header.byte_count)
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

static bool restore_quick_state(EmuEnvState &emuenv, QuickStateSlot &slot) {
    if (!slot.valid || (slot.title_id != emuenv.io.title_id))
        return false;

    const bool already_paused = emuenv.kernel.is_threads_paused();
    if (!already_paused)
        emuenv.kernel.pause_threads();

    if (!wait_for_guest_threads_paused(emuenv.kernel)) {
        if (!already_paused)
            emuenv.kernel.resume_threads();
        LOG_WARN("Failed to restore quickstate for {} because guest threads did not pause in time.", slot.title_id);
        return false;
    }

    if (!restore_quick_state_allocation_map(emuenv, slot)) {
        if (!already_paused)
            emuenv.kernel.resume_threads();
        return false;
    }

    uint32_t missing_pages = 0;
    for (const auto &page : slot.memory_pages) {
        if (!quick_state_page_can_restore(emuenv.mem, page, missing_pages)) {
            if (!already_paused)
                emuenv.kernel.resume_threads();
            return false;
        }
    }

    if (missing_pages > 0) {
        if (!already_paused)
            emuenv.kernel.resume_threads();
        LOG_WARN("Refused quickstate restore for {} because {} guest memory page(s) are not allocated in the current session. Restart/load-state support needs full kernel object and allocation-map serialization before this can be restored safely.",
            slot.title_id, missing_pages);
        return false;
    }

    for (const auto &page : slot.memory_pages)
        std::memcpy(Ptr<uint8_t>(page.address).get(emuenv.mem), page.bytes.data(), page.bytes.size());

    {
        const std::lock_guard<std::mutex> kernel_lock(emuenv.kernel.mutex);
        std::set<SceUID> matched_threads;
        for (const auto &thread_context : slot.thread_contexts) {
            const auto thread = find_quick_state_restore_thread(emuenv.kernel, thread_context, matched_threads);
            if (!thread || !thread->cpu) {
                if (!already_paused)
                    emuenv.kernel.resume_threads();
                LOG_WARN("Failed to match quickstate thread {} ({}) for restore", thread_context.id, thread_context.name);
                return false;
            }

            matched_threads.insert(thread->id);
            load_context(*thread->cpu, thread_context.context);
        }
    }

    reset_quick_state_runtime_render_state(emuenv);
    emuenv.kernel.invalidate_jit_cache(0, std::numeric_limits<Address>::max());
    if (!already_paused)
        emuenv.kernel.resume_threads();

    return true;
}

static bool quick_state_has_avplayer_threads(const QuickStateSlot &slot) {
    return std::any_of(slot.thread_contexts.begin(), slot.thread_contexts.end(), [](const QuickStateThreadContext &thread) {
        return thread.name.rfind("avPlayer ", 0) == 0;
    });
}

static void write_quick_state_marker(EmuEnvState &emuenv, const QuickStateSlot &slot) {
    const fs::path state_dir = quick_state_dir(emuenv, slot.title_id);
    fs::create_directories(state_dir);
    fs::ofstream marker(quick_state_marker_file(emuenv, slot.title_id));
    marker << "Vita3K Thor durable quickstate\n";
    marker << "Title ID: " << slot.title_id << "\n";
    marker << "Title: " << slot.title << "\n";
    marker << "Guest memory bytes: " << slot.byte_count << "\n";
    marker << "Thread contexts: " << slot.thread_contexts.size() << "\n";
    marker << "Compression level: " << std::clamp(emuenv.cfg.save_state_compression_level, 0, 9) << "\n";
    marker << "State file bytes: " << quick_state_file_size(emuenv, slot.title_id) << "\n";
    marker << "State root: " << quick_state_root(emuenv) << "\n";
    marker << "State file: " << quick_state_file(emuenv, slot.title_id) << "\n";
    marker << "Note: this is an experimental durable state and still needs full GPU/audio/IO/kernel-object serialization for emulator-perfect resumes.\n";
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
            return fmt::format("durable {} MiB ({} MiB raw)", disk_size / (1024 * 1024), quick_state_slot0.byte_count / (1024 * 1024));
        }
        return fmt::format("RAM {} MiB", quick_state_slot0.byte_count / (1024 * 1024));
    }

    QuickStateDiskHeader header;
    if (read_quick_state_disk_header(emuenv, title_id, header)) {
        const uint64_t disk_size = quick_state_file_size(emuenv, title_id);
        return fmt::format("disk {} MiB ({} MiB raw)", disk_size / (1024 * 1024), header.byte_count / (1024 * 1024));
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
        const bool saved_to_disk = save_quick_state_to_disk(emuenv, quick_state_slot0);
        if (saved_to_disk)
            write_quick_state_marker(emuenv, quick_state_slot0);

        LOG_INFO("Captured {} quickstate slot 0 for {} at {} ({} bytes, {} threads)",
            saved_to_disk ? "durable" : "RAM-only",
            title_id,
            saved_to_disk ? quick_state_file(emuenv, title_id) : state_dir,
            quick_state_slot0.byte_count,
            quick_state_slot0.thread_contexts.size());
        if (!saved_to_disk)
            LOG_WARN("Failed to write durable quickstate slot 0 for {}", title_id);
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
            LOG_INFO("Loaded durable quickstate slot 0 for {} from {}", title_id, quick_state_file(emuenv, title_id));
        }
    }

    if (loaded_from_disk && quick_state_has_avplayer_threads(quick_state_slot0)) {
        LOG_WARN("Refused durable quickstate restore for {} because this state contains AVPlayer movie/audio threads. Same-session AVPlayer states can load, but app-restart restores need AVPlayer host object serialization before they are safe.", title_id);
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

    if (!pause)
        runtime_osd_auto_paused = false;
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

void runtime_poll_control_file(EmuEnvState &emuenv) {
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

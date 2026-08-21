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

#include <app/functions.h>

#include <config/state.h>
#include <config/version.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <util/log.h>

#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <host/dialog/filesystem.h>
#include <miniz.h>
#endif

namespace app {

void error_dialog(const std::string &message, SDL_Window *window) {
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message.c_str(), window)) {
        LOG_ERROR("SDL Error: {}", message);
    }
}

static constexpr uint32_t frames_size = 20;
void calculate_fps(EmuEnvState &emuenv) {
    const uint32_t sdl_ticks_now = SDL_GetTicks();
    const uint32_t ms = sdl_ticks_now - emuenv.sdl_ticks;

    if (ms >= 1000 && emuenv.frame_count > 0) {
        // Set Current FPS
        // round division to nearest integer instead of truncating
        const uint32_t frame_count = static_cast<std::uint32_t>(emuenv.frame_count);
        emuenv.fps = (frame_count * 1000 + ms / 2) / ms;
        emuenv.ms_per_frame = (ms + frame_count / 2) / frame_count;
        emuenv.sdl_ticks = sdl_ticks_now;
        emuenv.frame_count = 0;
        set_window_title(emuenv);

        // Set FPS Statistics
        emuenv.fps_values[emuenv.current_fps_offset] = static_cast<float>(emuenv.fps);
        emuenv.current_fps_offset = (emuenv.current_fps_offset + 1) % frames_size;
        float avg_fps = 0;
        for (uint32_t i = 0; i < frames_size; i++)
            avg_fps += emuenv.fps_values[i];
        emuenv.avg_fps = static_cast<uint32_t>(avg_fps) / frames_size;
        emuenv.min_fps = static_cast<uint32_t>(*std::min_element(emuenv.fps_values, std::next(emuenv.fps_values, frames_size)));
        emuenv.max_fps = static_cast<uint32_t>(*std::max_element(emuenv.fps_values, std::next(emuenv.fps_values, frames_size)));
    }
}

void set_window_title(EmuEnvState &emuenv) {
    const auto af = emuenv.cfg.current_config.anisotropic_filtering > 1 ? fmt ::format(" | AF {}x", emuenv.cfg.current_config.anisotropic_filtering) : "";
    const auto x = emuenv.display.next_rendered_frame.image_size.x * emuenv.cfg.current_config.resolution_multiplier;
    const auto y = emuenv.display.next_rendered_frame.image_size.y * emuenv.cfg.current_config.resolution_multiplier;
    const std::string title_to_set = fmt::format("{} | {} ({}) | {} | {} FPS ({} ms) | {}x{}{} | {}",
        window_title,
        emuenv.current_app_title, emuenv.io.title_id,
        emuenv.cfg.current_config.backend_renderer,
        emuenv.fps, emuenv.ms_per_frame,
        x, y, af, emuenv.cfg.current_config.screen_filter);

    SDL_SetWindowTitle(emuenv.window.get(), title_to_set.c_str());
}

#ifdef __ANDROID__
static bool is_safe_zip_entry_path(const fs::path &entry_path) {
    if (entry_path.is_absolute())
        return false;

    for (const auto &part : entry_path) {
        if (part.string() == "..")
            return false;
    }

    return true;
}

static std::string install_custom_driver_archive(const fs::path &file_path) {
    // remove the .zip extension
    std::string driver = file_path.filename().stem().string();

    fs::path driver_path = fs::path(SDL_GetAndroidInternalStoragePath()) / "driver" / driver;

    if (fs::exists(driver_path) && !fs::is_empty(driver_path)) {
        LOG_INFO("Driver {} already exists, selecting installed copy.", driver);
        return driver;
    }

    fs::create_directories(driver_path);

    std::unique_ptr<FILE, int (*)(FILE *)> zip_file(FOPEN(file_path.c_str(), "rb"), fclose);
    if (!zip_file) {
        LOG_CRITICAL("Could not open custom driver archive {}", file_path);
        fs::remove_all(driver_path);
        return {};
    }

    std::string driver_so = "";

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(mz_zip_archive));
    if (!mz_zip_reader_init_cfile(&zip, zip_file.get(), 0, 0)) {
        LOG_CRITICAL("miniz error reading archive: {}", mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        fs::remove_all(driver_path);
        return {};
    }

    bool extract_failed = false;
    const int num_files = mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
            continue;
        }
        const std::string m_filename = file_stat.m_filename;
        if (m_filename.ends_with("/")) {
            // directory
            continue;
        }

        const fs::path entry_path(m_filename);
        if (!is_safe_zip_entry_path(entry_path)) {
            LOG_WARN("Skipping unsafe custom driver archive entry {}", m_filename);
            continue;
        }

        // assume only the driver would contain vulkan in its name
        if (m_filename.find("vulkan") != std::string_view::npos) {
            if (!driver_so.empty())
                LOG_WARN("Two possible files can be used as the main vulkan driver : {} {}", driver_so, m_filename);

            driver_so = m_filename;
        }

        LOG_INFO("Extracting {}", m_filename);
        const fs::path file_output = driver_path / entry_path;
        if (!fs::exists(file_output.parent_path()))
            fs::create_directories(file_output.parent_path());

        if (!mz_zip_reader_extract_to_file(&zip, i, file_output.c_str(), 0)) {
            LOG_CRITICAL("miniz error extracting {}: {}", m_filename, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
            extract_failed = true;
            break;
        }
    }

    mz_zip_reader_end(&zip);

<<<<<<< HEAD
    if (extract_failed || driver_so.empty()) {
        LOG_ERROR("Could not locate main driver file!");
        fs::remove_all(driver_path);
        return {};
=======
FirmwareState get_firmware_state(const EmuEnvState &emuenv) {
    return FirmwareState{
        .preinstalled_package = has_installed_firmware_content(emuenv.vita_fs_path / "pd0"),
        .main_firmware = has_installed_firmware_content(emuenv.vita_fs_path / "vs0"),
        .font_package = has_installed_firmware_content(emuenv.vita_fs_path / "sa0"),
    };
}

bool has_firmware_installed(const EmuEnvState &emuenv) {
    return has_installed_firmware_content(emuenv.vita_fs_path / "vs0");
}

bool ensure_current_user(EmuEnvState &emuenv) {
    const auto &users = emuenv.app.user_list.users;

    if (users.empty()) {
        const std::string id = create_user(emuenv, "Vita3K");
        if (id.empty() || !activate_user(emuenv, id))
            return false;

        emuenv.cfg.user_id = id;
        config::serialize_config(emuenv.cfg, emuenv.cfg.config_path);
        return true;
>>>>>>> upstream/master
    }

    // last thing to do: create a driver_name.txt file with the name of the main so
    fs::ofstream driver_name_file(driver_path / "driver_name.txt", std::ios_base::out);
    driver_name_file << driver_so << '\n';
    driver_name_file.close();

    LOG_INFO("Successfully installed driver {}!", driver);
    return driver;
}

<<<<<<< HEAD
std::string add_custom_driver(EmuEnvState &emuenv) {
    fs::path file_path{};
    host::dialog::filesystem::Result result = host::dialog::filesystem::open_file(file_path);

    if (result != host::dialog::filesystem::SUCCESS)
        return {};

    return install_custom_driver_archive(file_path);
=======
bool switch_emulator_path(EmuEnvState &emuenv, const fs::path &vita_fs_path) {
    const fs::path normalized_vita_fs_path = vita_fs_path / "";
    if (normalized_vita_fs_path.empty())
        return false;

    emuenv.vita_fs_path = normalized_vita_fs_path;
    emuenv.cfg.set_vita_fs_path(emuenv.vita_fs_path);

    ::io_deinit(emuenv.io);
    if (!::init(emuenv.io, emuenv.cache_path, emuenv.log_path, emuenv.vita_fs_path, emuenv.cfg.console)) {
        LOG_ERROR("Failed to initialize file system for switched emulator path '{}'.", emuenv.vita_fs_path);
        return false;
    }

    load_users(emuenv);
    emuenv.io.user_id.clear();
    emuenv.io.user_name.clear();

    const auto &users = emuenv.app.user_list.users;
    if (users.empty()) {
        const std::string id = create_user(emuenv, "Vita3K");
        if (id.empty() || !activate_user(emuenv, id))
            return false;
        emuenv.cfg.user_id = id;
    } else {
        const auto &first = users.begin();
        if (!activate_user(emuenv, first->first))
            return false;
        emuenv.cfg.user_id = first->first;
    }

    load_app_times(emuenv);
    if (!scan_apps(emuenv)) {
        LOG_ERROR("Failed to scan apps after switching emulator path to '{}'.", emuenv.vita_fs_path);
        return false;
    }

    set_current_config(emuenv, "");
    return true;
>>>>>>> upstream/master
}

std::string add_custom_driver_from_path(const std::string &file_path) {
    return install_custom_driver_archive(fs::path(file_path));
}

void remove_custom_driver(EmuEnvState &emuenv, const std::string &driver) {
    fs::path driver_path = fs::path(SDL_GetAndroidInternalStoragePath()) / "driver" / driver;

    if (!fs::exists(driver_path)) {
        LOG_ERROR("Path {} does not exist", driver_path.c_str());
        return;
<<<<<<< HEAD
=======

    auto &renderer = *emuenv.renderer;
    renderer.precompile_bg_path.clear();
    renderer.precompile_queue.clear();
    renderer.precompile_total = 0;
    renderer.precompile_progress = 0;
    renderer.precompile_requested = false;
    renderer.precompile_complete.store(false, std::memory_order_relaxed);

    const auto bg_path = emuenv.vita_fs_path / "ux0/app" / emuenv.io.app_path / "sce_sys/pic0.png";
    if (fs::exists(bg_path))
        renderer.precompile_bg_path = fs_utils::path_to_utf8(bg_path);

    if (renderer::get_shaders_cache_hashs(renderer) && emuenv.cfg.shader_cache) {
        renderer.precompile_queue = renderer.shaders_cache_hashs;
        renderer.precompile_progress = 0;
        renderer.precompile_complete.store(false, std::memory_order_relaxed);
        renderer.precompile_requested = true;
    }
}

bool update_runtime_metrics(EmuEnvState &emuenv, LaunchRuntimeMetrics &metrics) {
    sync_perf_overlay_config(emuenv);

    if (emuenv.frame_count == 0)
        return false;

    if (!metrics.tracking_started) {
        metrics.tracking_started = true;
        metrics.last_fps_time = std::chrono::steady_clock::now();
        emuenv.frame_count = 0;
        return false;
>>>>>>> upstream/master
    }

    fs::remove_all(driver_path);
    LOG_INFO("Driver {} was successfully removed!", driver);
}
#endif

} // namespace app

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

#include <audio/state.h>
#include <config/functions.h>
#include <config/state.h>
#include <config/version.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <gui/functions.h>
#include <gui/imgui_impl_sdl.h>
#include <gui/state.h>
#include <io/functions.h>
#include <kernel/state.h>
#include <motion/state.h>
#include <ngs/state.h>
#include <renderer/state.h>

#include <renderer/functions.h>
#include <util/fs.h>
#include <util/log.h>
#include <util/string_utils.h>

#if USE_DISCORD
#include <app/discord.h>
#endif

#include <gdbstub/functions.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

#ifdef _WIN32
#include <dwmapi.h>
#endif

#ifdef __ANDROID__
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_system.h>
#endif

<<<<<<< HEAD
=======
#ifdef __linux__
#include <pwd.h>
#endif

#include <algorithm>
#include <fstream>

>>>>>>> upstream/master
namespace app {
void update_viewport(EmuEnvState &state) {
    int w = 0;
    int h = 0;

    SDL_GetWindowSize(state.window.get(), &w, &h);
    state.window_size.x = w;
    state.window_size.y = h;

    SDL_GetWindowSizeInPixels(state.window.get(), &w, &h);
    state.drawable_size.x = w;
    state.drawable_size.y = h;

    state.system_dpi_scale = SDL_GetWindowPixelDensity(state.window.get());
    state.manual_dpi_scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(state.window.get()));
    ImGui::GetIO().FontGlobalScale = 1.f * state.manual_dpi_scale;

    if (h > 0) {
        const float window_aspect = static_cast<float>(w) / h;
        const float vita_aspect = static_cast<float>(DEFAULT_RES_WIDTH) / DEFAULT_RES_HEIGHT;
        const bool fullscreen_hd_res_pixel_perfect_en = state.cfg.fullscreen_hd_res_pixel_perfect && state.display.fullscreen && !(w % DEFAULT_RES_WIDTH) && !(h % (DEFAULT_RES_HEIGHT - 4));
        if (state.cfg.stretch_the_display_area && !fullscreen_hd_res_pixel_perfect_en) {
            // Match the aspect ratio to the screen size.
            state.logical_viewport_size.x = static_cast<SceFloat>(state.window_size.x);
            state.logical_viewport_size.y = static_cast<SceFloat>(state.window_size.y);
            state.logical_viewport_pos.x = 0;
            state.logical_viewport_pos.y = 0;

            state.drawable_viewport_size.x = static_cast<SceFloat>(state.drawable_size.x);
            state.drawable_viewport_size.y = static_cast<SceFloat>(state.drawable_size.y);
            state.drawable_viewport_pos.x = 0;
            state.drawable_viewport_pos.y = 0;
        } else if ((window_aspect > vita_aspect) && !fullscreen_hd_res_pixel_perfect_en) {
            // Window is wide. Pin top and bottom.
            state.logical_viewport_size.x = state.window_size.y * vita_aspect;
            state.logical_viewport_size.y = static_cast<SceFloat>(state.window_size.y);
            state.logical_viewport_pos.x = (state.window_size.x - state.logical_viewport_size.x) / 2;
            state.logical_viewport_pos.y = 0;

            state.drawable_viewport_size.x = state.drawable_size.y * vita_aspect;
            state.drawable_viewport_size.y = static_cast<SceFloat>(state.drawable_size.y);
            state.drawable_viewport_pos.x = (state.drawable_size.x - state.drawable_viewport_size.x) / 2;
            state.drawable_viewport_pos.y = 0;
        } else {
            // Window is tall. Pin left and right.
            state.logical_viewport_size.x = static_cast<SceFloat>(state.window_size.x);
            state.logical_viewport_size.y = state.window_size.x / vita_aspect;
            state.logical_viewport_pos.x = 0;
            state.logical_viewport_pos.y = (state.window_size.y - state.logical_viewport_size.y) / 2;

            state.drawable_viewport_size.x = static_cast<SceFloat>(state.drawable_size.x);
            state.drawable_viewport_size.y = state.drawable_size.x / vita_aspect;
            state.drawable_viewport_pos.x = 0;
            state.drawable_viewport_pos.y = (state.drawable_size.y - state.drawable_viewport_size.y) / 2;
        }

        state.gui_scale.x = state.logical_viewport_size.x / static_cast<float>(DEFAULT_RES_WIDTH) / state.manual_dpi_scale;
        state.gui_scale.y = state.logical_viewport_size.y / static_cast<float>(DEFAULT_RES_HEIGHT) / state.manual_dpi_scale;
    } else {
        state.logical_viewport_pos.x = 0;
        state.logical_viewport_pos.y = 0;
        state.logical_viewport_size.x = 0;
        state.logical_viewport_size.y = 0;

        state.drawable_viewport_pos.x = 0;
        state.drawable_viewport_pos.y = 0;
        state.drawable_viewport_size.x = 0;
        state.drawable_viewport_size.y = 0;
    }

    // Update nearest font level
    float scale = state.gui_scale.y * state.system_dpi_scale * state.manual_dpi_scale;
    state.current_font_level = 0;
    for (int i = 0; i <= state.max_font_level; i++) {
        if (i == state.max_font_level || scale <= FontScaleCandidates[i]) {
            state.current_font_level = i;
            break;
        }
        if (FontScaleCandidates[i] / scale > scale / FontScaleCandidates[i + 1]) {
            state.current_font_level = i;
            break;
        }
    }
}

// Initializes paths to their respective defaults, to be changed later by settings or CLI
// Returns true if in portable mode, false otherwise
bool init_paths(Root &root_paths) {
    bool portable = false;
#ifdef __ANDROID__
    fs::path storage_path = fs::path(SDL_GetAndroidExternalStoragePath()) / "";
    fs::path vita_storage_path = storage_path / "vita/";

<<<<<<< HEAD
    root_paths.set_base_path(storage_path);

    // On Android, static assets are bundled inside the APK and accessed via SDL_IOFromFile.
    root_paths.set_static_assets_path({});

    root_paths.set_pref_path(vita_storage_path);
    root_paths.set_log_path(storage_path);
    root_paths.set_config_path(storage_path);
    root_paths.set_shared_path(storage_path);
    root_paths.set_cache_path(storage_path / "cache" / "");
    root_paths.set_patch_path(storage_path / "patch" / "");
=======
    // On Android, static assets are bundled inside the APK and accessed via SDL_IOFromFile.
    root_paths.set_static_assets_path({});

    root_paths.set_vita_fs_path(vita_storage_path);
    root_paths.set_log_path(internal_storage_path);
    root_paths.set_config_path(internal_storage_path);
    root_paths.set_shared_path(internal_storage_path);
    root_paths.set_cache_path(internal_storage_path / "cache" / "");
    root_paths.set_patch_path(internal_storage_path / "patch" / "");
>>>>>>> upstream/master
#else
    auto sdl_exe_path = SDL_GetBasePath();
    auto exe_path = fs_utils::utf8_to_path(sdl_exe_path);

    root_paths.set_static_assets_path(exe_path);

#ifdef _WIN32
    auto portable_path = exe_path / "portable" / "";
#elif defined(__APPLE__)
    // On Apple platforms, exe_path is "Contents/Resources/" inside the app bundle.
    // An extra parent_path is apparently needed because of the trailing slash.
    auto portable_path = exe_path.parent_path().parent_path().parent_path().parent_path() / "portable" / "";
#elif defined(__linux__)
    fs::path portable_path = "";
    auto APPIMAGE = getenv("APPIMAGE"); // Used in AppImage
    if (APPIMAGE) {
        fs::path appimage_path = fs::path(APPIMAGE).remove_filename() / "";
        portable_path = appimage_path / "portable" / "";
    } else {
        portable_path = exe_path / "portable" / "";
    }
#endif

    if (fs::is_directory(portable_path)) {
        portable = true;
        // If a portable directory exists, use it for everything else.
        // Note that vita_fs_path should not be the same as the other paths.
        root_paths.set_vita_fs_path(portable_path / "fs" / "");
        root_paths.set_log_path(portable_path);
        root_paths.set_config_path(portable_path);
        root_paths.set_shared_path(portable_path);
        root_paths.set_cache_path(portable_path / "cache" / "");
        root_paths.set_patch_path(portable_path / "patch" / "");
    } else {
        // SDL_GetPrefPath is deferred as it creates the directory.
        // When using a portable directory, it is not needed.
        auto sdl_vita_fs_path = SDL_GetPrefPath(org_name, app_name);
        auto vita_fs_path = fs_utils::utf8_to_path(sdl_vita_fs_path);
        SDL_free(sdl_vita_fs_path);

#if defined(__APPLE__)
        // Store other data in the user-wide path. Otherwise we may end up dumping
        // files into the "/Applications/" install directory or the app bundle.
        // This will typically be "~/Library/Application Support/Vita3K/Vita3K/".
        // Check for config.yml first, though, to maintain backwards compatibility,
        // even though storing user data inside the app bundle is not a good idea.
        auto existing_config = exe_path / "config.yml";
        if (!fs::exists(existing_config)) {
            exe_path = vita_fs_path;
        }

        // vita_fs_path should not be the same as the other paths.
        // For backwards compatibility, though, check if ux0 exists first.
        auto existing_ux0 = vita_fs_path / "ux0";
        if (!fs::is_directory(existing_ux0)) {
            vita_fs_path = vita_fs_path / "fs" / "";
        }
#endif

        root_paths.set_vita_fs_path(vita_fs_path);
        root_paths.set_log_path(exe_path);
        root_paths.set_config_path(exe_path);
        root_paths.set_shared_path(exe_path);
        root_paths.set_cache_path(exe_path / "cache" / "");
        root_paths.set_patch_path(exe_path / "patch" / "");

#if defined(__linux__)
        // XDG Data Dirs.
        char home_path[PATH_MAX] = {};
        auto env_home = getenv("HOME");
        if (env_home != NULL)
            strncpy(home_path, env_home, PATH_MAX - 1);
        else {
            struct passwd *pw = getpwuid(getuid());
            if (pw) {
                strncpy(home_path, pw->pw_dir, PATH_MAX - 1);
            } else {
                LOG_CRITICAL("Failed to get home directory path");
            }
        }

        auto XDG_DATA_HOME = getenv("XDG_DATA_HOME");
        auto XDG_CACHE_HOME = getenv("XDG_CACHE_HOME");
        auto XDG_CONFIG_HOME = getenv("XDG_CONFIG_HOME");
        auto APPDIR = getenv("APPDIR"); // Used by AppImage

        // Config and game-specific configs
        if (XDG_CONFIG_HOME != NULL)
            root_paths.set_config_path(fs::path(XDG_CONFIG_HOME) / app_name / "");
        else if (home_path[0] != '\0')
            root_paths.set_config_path(fs::path(home_path) / ".config" / app_name / "");

        // Logs, cache and dumps
        if (XDG_CACHE_HOME != NULL) {
            root_paths.set_cache_path(fs::path(XDG_CACHE_HOME) / app_name / "");
            root_paths.set_log_path(fs::path(XDG_CACHE_HOME) / app_name / "");
        } else if (home_path[0] != '\0') {
            root_paths.set_cache_path(fs::path(home_path) / ".cache" / app_name / "");
            root_paths.set_log_path(fs::path(home_path) / ".cache" / app_name / "");
        }

<<<<<<< HEAD
        // Don't assume that base_path is portable.
        if (fs::exists(root_paths.get_base_path() / "data") && fs::exists(root_paths.get_base_path() / "lang") && fs::exists(root_paths.get_base_path() / "shaders-builtin"))
            root_paths.set_static_assets_path(root_paths.get_base_path());
        else if (env_home != NULL)
            root_paths.set_static_assets_path(fs::path(env_home) / ".local/share" / app_name / "");
=======
        const constexpr char *static_asset_paths[] = {
            "/usr/local/share/Vita3K",
            "/usr/share/Vita3K",
            "/app/share/Vita3K",
        };
>>>>>>> upstream/master

        // Check both normal case and all lowercase paths
        for (const auto &path : static_asset_paths) {
            if (fs::exists(path)) {
                root_paths.set_static_assets_path(path);
                break;
            }
            if (fs::exists(string_utils::tolower(path))) {
                root_paths.set_static_assets_path(string_utils::tolower(path));
                break;
            }
<<<<<<< HEAD
        } else if (XDG_DATA_HOME != NULL) {
            if (fs::exists(fs::path(XDG_DATA_HOME) / app_name / "data") && fs::exists(fs::path(XDG_DATA_HOME) / app_name / "lang") && fs::exists(fs::path(XDG_DATA_HOME) / app_name / "shaders-builtin"))
                root_paths.set_static_assets_path(fs::path(XDG_DATA_HOME) / app_name / "");
=======
>>>>>>> upstream/master
        }

        // Only really used in standalone (rare) or development
        if (fs::exists(exe_path / "shaders-builtin"))
            root_paths.set_static_assets_path(exe_path);

        // AppImage root
        if (APPDIR != NULL && fs::exists(fs::path(APPDIR) / "usr/share/Vita3K"))
            root_paths.set_static_assets_path(fs::path(APPDIR) / "usr/share/Vita3K");

        // shared path
        if (XDG_DATA_HOME != NULL)
            root_paths.set_shared_path(fs::path(XDG_DATA_HOME) / app_name / "");
        else if (home_path[0] != '\0')
            root_paths.set_shared_path(fs::path(home_path) / ".local/share" / app_name / "");

        // These default to being in shared path
        root_paths.set_vita_fs_path(root_paths.get_shared_path() / app_name / "");
        root_paths.set_patch_path(root_paths.get_shared_path() / "patch" / "");
#endif
    }
#endif

    // Create paths for safety
    fs::create_directories(root_paths.get_config_path());
    fs::create_directories(root_paths.get_cache_path());
    fs::create_directories(root_paths.get_log_path() / "shaderlog");
    fs::create_directories(root_paths.get_log_path() / "texturelog");
    fs::create_directories(root_paths.get_patch_path());

    const auto gui_configs_source_path = root_paths.get_static_assets_path() / "data" / "gui-configs";
    if (fs::is_directory(gui_configs_source_path)) {
        const auto gui_configs_destination_path = root_paths.get_config_path() / "gui-configs";
        if (!fs_utils::copy_directory_contents(gui_configs_source_path, gui_configs_destination_path, fs::copy_options::overwrite_existing))
            LOG_WARN("Failed to copy GUI configs from {} to {}", gui_configs_source_path, gui_configs_destination_path);
    }
    return portable;
}

bool init(EmuEnvState &state, Config &cfg, const Root &root_paths) {
    state.cfg = std::move(cfg);

    state.default_path = root_paths.get_vita_fs_path();
    state.log_path = root_paths.get_log_path();
    state.config_path = root_paths.get_config_path();
    state.cache_path = root_paths.get_cache_path();
    state.shared_path = root_paths.get_shared_path();
    state.static_assets_path = root_paths.get_static_assets_path();
    state.patch_path = root_paths.get_patch_path();

<<<<<<< HEAD
    // If configuration does not provide a preference path, use SDL's default
    if (state.cfg.pref_path == root_paths.get_pref_path() || state.cfg.pref_path.empty())
        state.pref_path = root_paths.get_pref_path();
    else {
        auto last_char = state.cfg.pref_path.back();
=======
    // If configuration does not provide a VitaFS path, use SDL's default
    if (state.cfg.vita_fs_path == root_paths.get_vita_fs_path() || state.cfg.vita_fs_path.empty()) {
        state.vita_fs_path = root_paths.get_vita_fs_path();
    } else {
        auto last_char = state.cfg.vita_fs_path.back();
>>>>>>> upstream/master
        if (last_char != fs::path::preferred_separator && last_char != '/')
            state.cfg.vita_fs_path += fs::path::preferred_separator;
        state.vita_fs_path = state.cfg.get_vita_fs_path();
    }

    // Set initall current config for current backend renderer and custom driver with run app path if provided
    gui::set_current_config(state, state.cfg.run_app_path.has_value() ? *state.cfg.run_app_path : "");
    LOG_INFO("backend-renderer: {}", state.cfg.current_config.backend_renderer);

    // Here any path can be used since they are all the same in windows/macos
    LOG_INFO("Base path: {}", state.static_assets_path);
#if defined(__linux__) && !defined(__ANDROID__)
    LOG_INFO("Static assets path: {}", state.static_assets_path);
    LOG_INFO("Shared path: {}", state.shared_path);
    LOG_INFO("Log path: {}", state.log_path);
    LOG_INFO("User config path: {}", state.config_path);
    LOG_INFO("User cache path: {}", state.cache_path);
#endif
    LOG_INFO("VitaFS path: {}", state.vita_fs_path);

<<<<<<< HEAD
    if (ImGui::GetCurrentContext() == NULL) {
        ImGui::CreateContext();
    }
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;

    int window_type = 0;
    switch (state.backend_renderer) {
    case renderer::Backend::OpenGL:
        window_type = SDL_WINDOW_OPENGL;
        break;

    case renderer::Backend::Vulkan:
        window_type = SDL_WINDOW_VULKAN;
        break;

    default:
        LOG_ERROR("Unimplemented backend renderer: {}.", state.cfg.backend_renderer);
        break;
    }

#ifdef ANDROID
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    state.display.fullscreen = true;
    window_type |= SDL_WINDOW_FULLSCREEN;
#else
    if (state.cfg.fullscreen) {
        state.display.fullscreen = true;
        window_type |= SDL_WINDOW_FULLSCREEN;
    }
#endif

    state.manual_dpi_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    state.window = WindowPtr(SDL_CreateWindow(window_title, DEFAULT_RES_WIDTH * state.manual_dpi_scale, DEFAULT_RES_HEIGHT * state.manual_dpi_scale, window_type | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
    if (!state.window) {
        LOG_ERROR("SDL failed to create window: {}", SDL_GetError());
        return false;
    }
    state.manual_dpi_scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(state.window.get()));

#ifdef _WIN32
    // Disable round corners for the game window
    const auto window_preference = DWMWCP_DONOTROUND;
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(state.window.get()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd)
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &window_preference, sizeof(window_preference));
#endif

    // initialize the renderer first because we need to know if we need a page table
    if (!state.cfg.console) {
        if (renderer::init(state.window.get(), state.renderer, state.backend_renderer, state.cfg, root_paths)) {
            update_viewport(state);
        } else {
            switch (state.backend_renderer) {
            case renderer::Backend::OpenGL:
#ifdef ANDROID
                error_dialog("Could not create OpenGL ES context!\nDoes your GPU support OpenGL ES 3.2?", nullptr);
#else
                error_dialog("Could not create OpenGL context!\nDoes your GPU at least support OpenGL 4.4?", nullptr);
#endif
                break;

            case renderer::Backend::Vulkan:
                error_dialog("Could not create Vulkan context!\nDoes your device support Vulkan?");
                break;

            default:
                error_dialog(fmt::format("Unknown backend renderer: {}.", state.cfg.backend_renderer));
                break;
            }
            return false;
        }
    }

    if (!init(state.io, state.cache_path, state.log_path, state.pref_path, state.cfg.console)) {
=======
    if (!init(state.io, state.cache_path, state.log_path, state.vita_fs_path, state.cfg.console)) {
>>>>>>> upstream/master
        LOG_ERROR("Failed to initialize file system for the emulator!");
        return false;
    }

#ifdef __ANDROID__
    state.renderer->current_custom_driver = state.cfg.current_config.custom_driver_name;
#endif

#if USE_DISCORD
    if (discordrpc::init() && state.cfg.discord_rich_presence) {
        discordrpc::update_presence();
    }
#endif

    state.motion.init();

    return true;
}

<<<<<<< HEAD
=======
void shutdown_app_runtime(EmuEnvState &state) {
    state.audio.stop_all_ports();

    gxm::shutdown(state);

    state.net.abort_all();
    state.http.shutdown_connections();

    state.kernel.process_exit();

    state.motion.reset_runtime();

    state.audio.deinit();

    state.renderer->preclose_action();
    renderer::stop_render_thread(*state.renderer);
    gxm::destroy_all_contexts(state, true);
    gxm::destroy_all_render_targets(state, true);
    state.gxm.deinit();
    state.overlay_manager.reset();

    state.display.deinit();

    state.netctl.deinit();

    ngs::deinit(state.ngs, state.mem);

    // trophy (maybe namespace this?)
    deinit(state.np);

    state.http.deinit();

    state.net.deinit();

    io_deinit(state.io);

    state.camera.deinit();

    state.common_dialog.deinit();

    state.ime.deinit();

    state.touch.reset_runtime();

    state.sfo_handle.header = {};
    state.sfo_handle.entries.clear();

    state.license_content_id.clear();
    state.license_title_id.clear();

    state.app_info = {};

    state.regmgr.system_dreg.clear();
    state.regmgr.system_dreg_path.clear();
    state.regmgr.reg_category_template.clear();
    state.regmgr.reg_template.clear();

    state.kernel.deinit(state.mem);

    state.renderer->cleanup();
    state.renderer.reset();

    deinit_mem(state.mem);
}

void reset_app_state(EmuEnvState &state) {
    app::reset_perf_metrics(state);

    state.app_path.clear();
    state.current_app_title.clear();
    state.self_name.clear();
    state.self_path.clear();
    state.main_thread_id = 0;
    state.drop_inputs = false;
    state.missing_nids.clear();
    state.clear_app_launch_request();

    state.ctrl.reset_runtime();
    set_current_config(state, "");

#if USE_DISCORD
    if (state.cfg.discord_rich_presence)
        discordrpc::update_presence();
#endif

    // re-init
    init_libraries(state);

    if (!init(state.io, state.cache_path, state.log_path, state.vita_fs_path, state.cfg.console)) {
        LOG_ERROR("Failed to re-initialize file system after deinit!");
    }
}

>>>>>>> upstream/master
bool late_init(EmuEnvState &state) {
    // note: mem is not initialized yet but that's not an issue
    // the renderer is not using it yet, just storing it for later uses
    state.renderer->late_init(state.cfg, state.app_path, state.mem);

    const bool need_page_table = state.renderer->mapping_method == MappingMethod::PageTable || state.renderer->mapping_method == MappingMethod::NativeBuffer;
    if (!init(state.mem, need_page_table)) {
        LOG_ERROR("Failed to initialize memory for emulator state!");
        return false;
    }

    if (!state.audio.init(state.cfg.current_config.audio_backend)) {
        LOG_WARN("Failed to initialize audio! Audio will not work.");
    }

    if (!ngs::init(state.ngs, state.mem)) {
        LOG_ERROR("Failed to initialize ngs.");
        return false;
    }

    return true;
}

void destroy(EmuEnvState &emuenv, ImGui_State *imgui) {
    ImGui_ImplSdl_Shutdown(imgui);

#ifdef USE_DISCORD
    discordrpc::shutdown();
#endif

<<<<<<< HEAD
=======
    if (!emuenv.overlay_manager)
        emuenv.overlay_manager = std::make_unique<overlay::display_manager>();
    r.overlay_manager = emuenv.overlay_manager.get();
    emuenv.overlay_manager->set_flip_request_callback([&r]() {
        r.async_flip_requested.store(true, std::memory_order_relaxed);
        r.command_buffer_queue.wake();
    });
    r.common_dialog = &emuenv.common_dialog;
    r.sys_date_format = cc.sys_date_format;
    r.sys_lang = cc.sys_lang;
    r.sys_button = cc.sys_button;

    r.show_compile_shaders = emuenv.cfg.show_compile_shaders;
    app::sync_perf_overlay_config(emuenv);

    // overlay input callbacks
    emuenv.overlay_manager->set_input_callbacks(
        [&emuenv]() -> overlay::button_states {
            return poll_overlay_input(emuenv);
        },
        [&emuenv](bool intercepted) {
            auto &ctrl = emuenv.ctrl;
            if (intercepted) {
                ctrl.overlay_input_intercepted.store(true, std::memory_order_relaxed);
            } else {
                ctrl.overlay_input_intercepted.store(false, std::memory_order_relaxed);
            }
        },
        [&emuenv]() -> overlay::overlay_touch_state {
            auto &mouse = emuenv.ctrl.overlay_mouse;
            overlay::overlay_touch_state state;
            state.x = mouse.x.load(std::memory_order_relaxed);
            state.y = mouse.y.load(std::memory_order_relaxed);
            state.pressed = mouse.pressed.load(std::memory_order_relaxed);
            return state;
        });

    emuenv.np.trophy_state.add_trophy_unlock_callback([&emuenv](NpTrophyUnlockCallbackData &data) {
        if (!emuenv.overlay_manager)
            return;
        auto notif = emuenv.overlay_manager->get<overlay::trophy_notification>();
        if (!notif)
            notif = emuenv.overlay_manager->create<overlay::trophy_notification>();

        std::vector<uint8_t> grade_icon_buf;
        const char *grade_name = nullptr;
        switch (data.trophy_kind) {
        case np::trophy::SceNpTrophyGrade::SCE_NP_TROPHY_GRADE_PLATINUM: grade_name = "platinum"; break;
        case np::trophy::SceNpTrophyGrade::SCE_NP_TROPHY_GRADE_GOLD: grade_name = "gold"; break;
        case np::trophy::SceNpTrophyGrade::SCE_NP_TROPHY_GRADE_SILVER: grade_name = "silver"; break;
        case np::trophy::SceNpTrophyGrade::SCE_NP_TROPHY_GRADE_BRONZE: grade_name = "bronze"; break;
        default: break;
        }
        if (grade_name) {
            const auto icon_path = emuenv.static_assets_path / "icons" / (std::string(grade_name) + ".png");
            std::ifstream file(icon_path.c_str(), std::ios::binary | std::ios::ate);
            if (file) {
                const auto size = file.tellg();
                file.seekg(0);
                grade_icon_buf.resize(static_cast<size_t>(size));
                file.read(reinterpret_cast<char *>(grade_icon_buf.data()), size);
            }
        }

        notif->enqueue(data.trophy_name, data.icon_buf, grade_icon_buf);
    });
}

void apply_runtime_settings(EmuEnvState &emuenv) {
    const auto &cc = emuenv.cfg.current_config;

    emuenv.audio.set_global_volume(cc.audio_volume / 100.f);
    lang::set_locale(cc.sys_lang);
    emuenv.compat.log_compat_warn = emuenv.cfg.log_compat_warn;

    if (!emuenv.renderer)
        return;

    auto &r = *emuenv.renderer;
    r.set_vsync_state(cc.v_sync);
    r.set_surface_sync_state(cc.disable_surface_sync);
    r.set_screen_filter(cc.screen_filter);
    r.set_anisotropic_filtering(cc.anisotropic_filtering);
    r.set_stretch_display(cc.stretch_the_display_area);
    r.stretch_hd_pixel_perfect(cc.fullscreen_hd_res_pixel_perfect);
    r.set_async_compilation(cc.async_pipeline_compilation);
    r.get_texture_cache()->set_replacement_state(cc.import_textures, cc.export_textures, cc.export_as_png);
#ifdef __ANDROID__
    if (r.support_custom_drivers())
        r.set_turbo_mode(emuenv.cfg.turbo_mode);
#endif
    emuenv.display.fps_hack = cc.fps_hack;
    r.sys_date_format = cc.sys_date_format;
    r.sys_lang = cc.sys_lang;
    r.sys_button = cc.sys_button;
    r.show_compile_shaders = emuenv.cfg.show_compile_shaders;
    app::sync_perf_overlay_config(emuenv);
}

SettingsCommitResult commit_settings(EmuEnvState &emuenv, const Config &desired_cfg, const std::string &scope_app_path) {
    SettingsCommitResult result;
    const bool scope_is_custom = !scope_app_path.empty();
    const bool active_profile = applies_to_active_profile(emuenv, scope_app_path);
    const bool running_game = !emuenv.io.app_path.empty();
    const std::string active_app_path = running_game ? emuenv.io.app_path : std::string();

    result.affected_running_game = running_game && active_profile;
    result.active_source_is_custom = scope_is_custom
        || (!active_app_path.empty() && config::has_custom_config(emuenv.config_path, active_app_path));

    const auto previous_current = emuenv.cfg.current_config;

    if (scope_is_custom) {
        const bool had_custom_config = config::has_custom_config(emuenv.config_path, scope_app_path);

        Config persisted_cfg;
        persisted_cfg = emuenv.cfg;
        persisted_cfg.current_config = desired_cfg.current_config;
        config::save_current_config(persisted_cfg, emuenv.config_path, scope_app_path, true);

        result.custom_config_created = !had_custom_config;
        result.active_source_is_custom = true;

        if (active_profile) {
            const auto effective_current = get_effective_current_config(persisted_cfg, emuenv.config_path, active_app_path);
            if (result.affected_running_game)
                result.restart_required_settings = config::get_restart_required_settings(previous_current, effective_current);
            emuenv.cfg.current_config = result.affected_running_game
                ? get_runtime_current_config_after_save(previous_current, effective_current, result.restart_required_settings)
                : effective_current;
            apply_runtime_settings(emuenv);
            result.runtime_settings_applied = true;
        }

        return result;
    }

    Config persisted_cfg;
    persisted_cfg = emuenv.cfg;
    persisted_cfg = desired_cfg;
    persisted_cfg.current_config = desired_cfg.current_config;
    config::save_current_config(persisted_cfg, emuenv.config_path, {}, false);
    emuenv.cfg = persisted_cfg;

    if (!active_profile) {
        emuenv.cfg.current_config = previous_current;
        result.active_source_is_custom = true;
        return result;
    }

    result.active_source_is_custom = false;
    const auto effective_current = get_effective_current_config(emuenv.cfg, emuenv.config_path, active_app_path);
    if (result.affected_running_game)
        result.restart_required_settings = config::get_restart_required_settings(previous_current, effective_current);
    emuenv.cfg.current_config = result.affected_running_game
        ? get_runtime_current_config_after_save(previous_current, effective_current, result.restart_required_settings)
        : effective_current;
    apply_runtime_settings(emuenv);
    result.runtime_settings_applied = true;
    return result;
}

SettingsCommitResult delete_custom_settings(EmuEnvState &emuenv, const std::string &scope_app_path) {
    SettingsCommitResult result;
    if (scope_app_path.empty())
        return result;

    if (!config::delete_custom_config(emuenv.config_path, scope_app_path))
        return result;

    if (emuenv.io.app_path != scope_app_path)
        return result;

    Config desired_cfg;
    desired_cfg = emuenv.cfg;
    config::set_current_config(desired_cfg, emuenv.config_path, {});
    return commit_settings(emuenv, desired_cfg, {});
}

// need to remove this function completely, waiting for gdb refactor
void destroy(EmuEnvState &emuenv) {
>>>>>>> upstream/master
    if (emuenv.cfg.gdbstub)
        server_close(emuenv);

    // There may be changes that made in the GUI, so we should save, again
    if (emuenv.cfg.overwrite_config)
        config::serialize_config(emuenv.cfg, emuenv.cfg.config_path);
}

void switch_state(EmuEnvState &emuenv, const bool pause) {
    if (pause) {
#ifdef __ANDROID__
        emuenv.display.imgui_render = true;
        gui::set_controller_overlay_state(0);
#endif
        emuenv.kernel.pause_threads();
    } else {
#ifdef __ANDROID__
        emuenv.display.imgui_render = false;
        gui::set_controller_overlay_state(gui::get_overlay_display_mask(emuenv.cfg));
#endif
        emuenv.kernel.resume_threads();
    }
    emuenv.audio.switch_state(pause);
}

} // namespace app

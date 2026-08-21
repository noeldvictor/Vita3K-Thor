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

#pragma once

#include <string>

struct Config;
struct EmuEnvState;
struct SDL_Window;
struct ImGui_State;
class Root;

namespace app {

/// Describes the state of the application to be run
enum class AppRunType {
    /// Run type is unknown
    Unknown,
    /// Extracted, files are as they are on console
    Extracted,
};

bool init_paths(Root &root_paths);
bool init(EmuEnvState &state, Config &cfg, const Root &root_paths);
void shutdown_app_runtime(EmuEnvState &state);
void reset_app_state(EmuEnvState &state);
bool late_init(EmuEnvState &state);
void destroy(EmuEnvState &emuenv, ImGui_State *imgui);
void update_viewport(EmuEnvState &state);
void switch_state(EmuEnvState &emuenv, const bool pause);
void error_dialog(const std::string &message, SDL_Window *window = nullptr);

#ifdef __ANDROID__
std::string add_custom_driver(EmuEnvState &emuenv);
std::string add_custom_driver_from_path(const std::string &file_path);
void remove_custom_driver(EmuEnvState &emuenv, const std::string &driver);
#endif

bool set_app_info(EmuEnvState &emuenv, const std::string &app_path);
void reset_controller_binding(EmuEnvState &emuenv);
void reset_perf_metrics(EmuEnvState &emuenv);
void sync_perf_overlay_config(EmuEnvState &emuenv);
FirmwareState get_firmware_state(const EmuEnvState &emuenv);
bool has_firmware_installed(const EmuEnvState &emuenv);
bool ensure_current_user(EmuEnvState &emuenv);
bool switch_emulator_path(EmuEnvState &emuenv, const fs::path &vita_fs_path);
bool setup_game_launch(EmuEnvState &emuenv, const std::string &app_path, bool update_last_time_used = true);
void prepare_game_launch_overlay(EmuEnvState &emuenv);
bool update_runtime_metrics(EmuEnvState &emuenv, LaunchRuntimeMetrics &metrics);
void abort_game_launch(EmuEnvState &emuenv);
void request_in_process_launch(EmuEnvState &emuenv, AppLaunchRequest request);

void load_users(EmuEnvState &emuenv);
void save_user(EmuEnvState &emuenv, const std::string &user_id);
std::string create_user(EmuEnvState &emuenv, const std::string &name);
void delete_user(EmuEnvState &emuenv, const std::string &user_id);
bool activate_user(EmuEnvState &emuenv, const std::string &user_id);

} // namespace app

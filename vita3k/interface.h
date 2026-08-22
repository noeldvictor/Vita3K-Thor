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

#include <util/exit_code.h>
#include <util/fs.h>

#include <string>
#include <vector>

struct AppLaunchRequest;
struct EmuEnvState;

#include "archive.h"

#include <functional>



ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv);
ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv, const AppLaunchRequest &launch_request);
ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id);
ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id, const AppLaunchRequest &launch_request);
void toggle_texture_replacement(EmuEnvState &emuenv);
void take_screenshot(EmuEnvState &emuenv);

// Thor: mount a .zip/.vpk or an extracted folder as a read-only virtual game
// card instead of installing it into ux0:app.
ContentInfo mount_archive_as_cartridge(EmuEnvState &emuenv, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback = nullptr);
ContentInfo mount_directory_as_cartridge(EmuEnvState &emuenv, const fs::path &content_path);

// Thor: quickstate and runtime controls, called from the frontend hotkeys.
bool runtime_quick_state_slot_valid(const EmuEnvState &emuenv);
bool runtime_quick_state_load_undo_available(EmuEnvState &emuenv);
uint64_t runtime_quick_state_slot_bytes();
std::string runtime_quick_state_slot_status(EmuEnvState &emuenv);
void runtime_set_speed_percent(EmuEnvState &emuenv, uint32_t speed_percent);
void runtime_toggle_fast_forward(EmuEnvState &emuenv);
// Current runtime speed, 100 = normal.
uint32_t runtime_speed_percent(const EmuEnvState &emuenv);
// The speed fast forward switches to, as a percentage. Persisted to config.
void runtime_set_fast_forward_speed(EmuEnvState &emuenv, uint32_t speed_percent);
bool runtime_performance_overlay_enabled(const EmuEnvState &emuenv);
void runtime_set_performance_overlay(EmuEnvState &emuenv, bool enabled);
void runtime_request_save_state(EmuEnvState &emuenv);
void runtime_request_load_state(EmuEnvState &emuenv);
void runtime_request_undo_load_state(EmuEnvState &emuenv);
void runtime_take_screenshot(EmuEnvState &emuenv);
void runtime_poll_control_file(EmuEnvState &emuenv);

// Thor: gamepad chords for the runtime controls - Select+R1 toggles fast
// forward, Select + right stick down/up save and load a quickstate. Returns
// true when the event was consumed. Call from the frontend's SDL pump.
union SDL_Event;
bool handle_runtime_gamepad_hotkey(EmuEnvState &emuenv, const SDL_Event &event);

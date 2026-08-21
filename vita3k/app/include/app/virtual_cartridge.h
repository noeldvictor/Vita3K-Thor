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
#include <vector>

struct EmuEnvState;

namespace app {

struct AppEntry;

/**
 * @brief Adds the virtual cartridges found under the configured scan roots to
 *        an app list, and drops entries whose source archive is gone.
 *
 * A no-op when scan-virtual-cartridges is off. Entries whose source archive is
 * unchanged (same path, size and mtime) are kept as-is rather than re-read.
 */
void append_virtual_cartridge_apps(std::vector<AppEntry> &apps, EmuEnvState &emuenv);

/**
 * @brief Adds a just-mounted cartridge to the in-memory app list so it can be
 *        booted by title id.
 *
 * The entry is transient on purpose: it is never written to the installed-apps
 * cache, because the content lives outside VitaFS. Replaces any existing entry
 * with the same path.
 */
void add_transient_cartridge_entry(EmuEnvState &emuenv, const std::string &title_id,
    const std::string &title, const std::string &category, const std::string &content_id,
    const std::string &source_path);

} // namespace app

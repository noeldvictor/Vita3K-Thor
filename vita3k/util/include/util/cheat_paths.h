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

#include <util/fs.h>

#include <optional>
#include <string>
#include <vector>

namespace cheat_paths {

std::vector<fs::path> get_vitacheat_roots(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path);
std::vector<fs::path> get_vitacheat_candidate_files(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id);
std::optional<fs::path> find_vitacheat_file(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id);
bool has_vitacheat_file(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id);

} // namespace cheat_paths

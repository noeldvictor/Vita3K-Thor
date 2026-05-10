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

#include <util/cheat_paths.h>

#include <algorithm>
#include <array>

namespace cheat_paths {
namespace {

static void add_unique_root(std::vector<fs::path> &roots, const fs::path &root) {
    const auto normalized = root.lexically_normal();
    if (std::find(roots.begin(), roots.end(), normalized) == roots.end())
        roots.push_back(normalized);
}

#ifdef __ANDROID__
static void add_android_storage_root(std::vector<fs::path> &storage_roots, const fs::path &root) {
    if (root.empty())
        return;

    add_unique_root(storage_roots, root);
}

static std::vector<fs::path> get_android_storage_roots() {
    std::vector<fs::path> storage_roots;
    add_android_storage_root(storage_roots, fs_utils::utf8_to_path("/sdcard"));
    add_android_storage_root(storage_roots, fs_utils::utf8_to_path("/storage/emulated/0"));

    const fs::path storage_root = fs_utils::utf8_to_path("/storage");
    boost::system::error_code error;
    if (!fs::exists(storage_root, error))
        return storage_roots;

    for (fs::directory_iterator it(storage_root, error), end; !error && it != end; it.increment(error)) {
        boost::system::error_code entry_error;
        if (!fs::is_directory(it->path(), entry_error))
            continue;

        const auto name = it->path().filename().string();
        if (name.empty() || (name == "self") || (name == "emulated"))
            continue;

        add_android_storage_root(storage_roots, it->path());
    }

    return storage_roots;
}
#endif

} // namespace

std::vector<fs::path> get_vitacheat_roots(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path) {
    std::vector<fs::path> roots;
    add_unique_root(roots, base_path / "cheats");
    add_unique_root(roots, shared_path / "cheats");
    add_unique_root(roots, pref_path / "ux0/vitacheat/db");
    add_unique_root(roots, pref_path / "ux0/vitacheat");

#ifdef __ANDROID__
    for (const auto &storage_root : get_android_storage_roots()) {
        add_unique_root(roots, storage_root / "cheats");
        add_unique_root(roots, storage_root / "cheats/psvita");
        add_unique_root(roots, storage_root / "cheats/vitacheat/db");
        add_unique_root(roots, storage_root / "cheats/vitacheat");
        add_unique_root(roots, storage_root / "VitaCheat/db");
        add_unique_root(roots, storage_root / "VitaCheat");
        add_unique_root(roots, storage_root / "vitacheat/db");
        add_unique_root(roots, storage_root / "vitacheat");
        add_unique_root(roots, storage_root / "Roms/psvita/cheats");
        add_unique_root(roots, storage_root / "Roms/psvita/vitacheat/db");
        add_unique_root(roots, storage_root / "Roms/psvita/vitacheat");
        add_unique_root(roots, storage_root / "roms/psvita/cheats");
        add_unique_root(roots, storage_root / "roms/psvita/vitacheat/db");
        add_unique_root(roots, storage_root / "roms/psvita/vitacheat");
    }
#endif

    return roots;
}

std::vector<fs::path> get_vitacheat_candidate_files(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id) {
    std::vector<fs::path> candidates;
    if (title_id.empty())
        return candidates;

    const auto filename = title_id + ".psv";
    for (const auto &root : get_vitacheat_roots(base_path, shared_path, pref_path)) {
        candidates.push_back(root / filename);
        candidates.push_back(root / "db" / filename);
    }

    return candidates;
}

std::optional<fs::path> find_vitacheat_file(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id) {
    for (const auto &candidate : get_vitacheat_candidate_files(base_path, shared_path, pref_path, title_id)) {
        boost::system::error_code error;
        if (fs::exists(candidate, error) && !error)
            return candidate;
    }

    return std::nullopt;
}

bool has_vitacheat_file(const fs::path &base_path, const fs::path &shared_path, const fs::path &pref_path, const std::string &title_id) {
    return find_vitacheat_file(base_path, shared_path, pref_path, title_id).has_value();
}

} // namespace cheat_paths

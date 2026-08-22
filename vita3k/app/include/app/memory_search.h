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

#include <mem/util.h>

#include <cstdint>
#include <string>
#include <vector>

struct EmuEnvState;

namespace app {

/**
 * @brief Guest memory search, for building cheats without a second machine.
 *
 * The usual Cheat Engine loop: scan for a value, do something in the game that
 * changes it, scan again for the new value, and repeat until few enough
 * addresses remain to be believable. Thor drives this from the runtime control
 * file, so it works identically on Windows and on the handheld, where there is
 * nowhere to attach a debugger.
 */
enum class SearchCompare {
    Equal,
    NotEqual,
    Greater, ///< greater than the value recorded by the previous scan
    Less, ///< less than the value recorded by the previous scan
    Changed, ///< differs from the previous scan
    Unchanged, ///< matches the previous scan
};

struct MemorySearchState {
    /// Addresses still matching every scan so far.
    std::vector<Address> candidates;
    /// What each candidate held at the previous scan, for the relative compares.
    std::vector<uint64_t> previous;
    /// 1, 2 or 4 bytes. Fixed once the first scan has run.
    uint32_t width = 4;
    bool started = false;

    /// Set when a scan was refused, e.g. because no game is running.
    std::string last_error;
};

/**
 * @brief Scans every allocated guest page. Discards any previous result set.
 *
 * Only width-aligned offsets are examined, which is what keeps a 4GiB address
 * space tractable. Compilers align scalars, so this finds essentially anything
 * a game stores as a normal variable - but a value packed at an odd offset
 * inside a struct will be missed, and the answer there is to search at width 1.
 */
size_t memory_search_first(EmuEnvState &emuenv, MemorySearchState &state,
    uint32_t width, SearchCompare compare, uint64_t value);

/// Filters the existing candidates. Cheap: it only re-reads the survivors.
size_t memory_search_narrow(EmuEnvState &emuenv, MemorySearchState &state,
    SearchCompare compare, uint64_t value);

void memory_search_reset(MemorySearchState &state);

/// Human-readable summary: how many candidates survive, and the first few.
std::string memory_search_report(EmuEnvState &emuenv, const MemorySearchState &state,
    size_t max_entries = 16);

bool memory_read_value(EmuEnvState &emuenv, Address address, uint32_t width, uint64_t &out);
bool memory_write_value(EmuEnvState &emuenv, Address address, uint32_t width, uint64_t value);

/**
 * @brief Emits the surviving candidates as a VitaCheat-style .psv body.
 *
 * Only sensible once the search has narrowed to a handful; writing hundreds of
 * pokes would be a way to corrupt a save, not a cheat.
 */
std::string memory_search_to_cheat(const MemorySearchState &state,
    const std::string &name, uint64_t value, size_t max_entries = 8);

/// Parses "equal", "changed", "greater"... Returns false for anything else.
bool parse_search_compare(const std::string &text, SearchCompare &out);

} // namespace app

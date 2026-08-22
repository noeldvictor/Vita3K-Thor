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

#include <app/memory_search.h>

#include <emuenv/state.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <util/log.h>
#include <util/string_utils.h>

#include <algorithm>
#include <cstring>

namespace app {

// Guest RAM is one contiguous host reservation, but only a fraction of it is
// ever committed. Scanning the lot would touch reserved-but-unmapped pages and
// fault, so every read is gated on is_valid_addr.
static constexpr uint64_t search_address_end = 0x100000000ULL;

// mem.cpp keeps STANDARD_PAGE_SIZE to itself, and is_valid_addr works in those
// same units, so mirror the value here rather than widening that header.
static constexpr uint32_t search_page_size = KiB(4);

// A first scan over ~4GiB of address space is the expensive one. Cap what we
// keep: past this many hits the result is not a search result, it is a haystack,
// and the honest answer is "narrow it further".
static constexpr size_t max_candidates = 2'000'000;

static bool read_raw(const MemState &mem, Address address, uint32_t width, uint64_t &out) {
    if (address == 0 || width == 0 || width > 4)
        return false;
    // A value must not straddle out of its page into an unmapped one.
    if (!is_valid_addr_range(mem, address, address + width - 1))
        return false;

    const uint8_t *base = mem.memory.get() + address;
    out = 0;
    std::memcpy(&out, base, width);
    return true;
}

static bool compare_value(SearchCompare compare, uint64_t current, uint64_t wanted,
    uint64_t previous, bool has_previous) {
    switch (compare) {
    case SearchCompare::Equal:
        return current == wanted;
    case SearchCompare::NotEqual:
        return current != wanted;
    case SearchCompare::Greater:
        return has_previous ? current > previous : current > wanted;
    case SearchCompare::Less:
        return has_previous ? current < previous : current < wanted;
    case SearchCompare::Changed:
        return has_previous && current != previous;
    case SearchCompare::Unchanged:
        return has_previous && current == previous;
    }
    return false;
}

bool parse_search_compare(const std::string &text, SearchCompare &out) {
    const std::string key = string_utils::tolower(text);
    if (key == "equal" || key == "eq" || key == "=") {
        out = SearchCompare::Equal;
    } else if (key == "not_equal" || key == "ne" || key == "!=") {
        out = SearchCompare::NotEqual;
    } else if (key == "greater" || key == "gt" || key == ">" || key == "increased") {
        out = SearchCompare::Greater;
    } else if (key == "less" || key == "lt" || key == "<" || key == "decreased") {
        out = SearchCompare::Less;
    } else if (key == "changed" || key == "different") {
        out = SearchCompare::Changed;
    } else if (key == "unchanged" || key == "same") {
        out = SearchCompare::Unchanged;
    } else {
        return false;
    }
    return true;
}

void memory_search_reset(MemorySearchState &state) {
    state.candidates.clear();
    state.previous.clear();
    state.started = false;
    state.last_error.clear();
}

size_t memory_search_first(EmuEnvState &emuenv, MemorySearchState &state,
    uint32_t width, SearchCompare compare, uint64_t value) {
    memory_search_reset(state);

    if (width != 1 && width != 2 && width != 4) {
        state.last_error = "width must be 1, 2 or 4";
        return 0;
    }
    // The relative compares need something to be relative to.
    if (compare != SearchCompare::Equal && compare != SearchCompare::NotEqual) {
        state.last_error = "the first scan can only use equal or not_equal";
        return 0;
    }

    state.width = width;
    state.started = true;

    const MemState &mem = emuenv.mem;
    size_t scanned_pages = 0;

    for (uint64_t page_base = search_page_size; page_base < search_address_end;
        page_base += search_page_size) {
        const Address page_addr = static_cast<Address>(page_base);
        if (!is_valid_addr(mem, page_addr))
            continue;

        scanned_pages++;
        const uint32_t last = search_page_size - width;
        for (uint32_t offset = 0; offset <= last; offset += width) {
            const Address address = page_addr + offset;
            uint64_t current = 0;
            if (!read_raw(mem, address, width, current))
                break;

            if (compare_value(compare, current, value, 0, false)) {
                state.candidates.push_back(address);
                state.previous.push_back(current);
                if (state.candidates.size() >= max_candidates) {
                    state.last_error = "hit the candidate cap; narrow with a more specific value";
                    LOG_WARN("Memory search hit the {} candidate cap", max_candidates);
                    return state.candidates.size();
                }
            }
        }
    }

    LOG_INFO("Memory search: {} candidates across {} mapped pages ({}-byte values)",
        state.candidates.size(), scanned_pages, width);
    return state.candidates.size();
}

size_t memory_search_narrow(EmuEnvState &emuenv, MemorySearchState &state,
    SearchCompare compare, uint64_t value) {
    if (!state.started) {
        state.last_error = "no search in progress; run a first scan";
        return 0;
    }

    const MemState &mem = emuenv.mem;
    std::vector<Address> kept;
    std::vector<uint64_t> kept_previous;
    kept.reserve(state.candidates.size());
    kept_previous.reserve(state.candidates.size());

    for (size_t i = 0; i < state.candidates.size(); i++) {
        uint64_t current = 0;
        // A candidate can stop being valid: the game is free to free that page.
        if (!read_raw(mem, state.candidates[i], state.width, current))
            continue;

        if (compare_value(compare, current, value, state.previous[i], true)) {
            kept.push_back(state.candidates[i]);
            kept_previous.push_back(current);
        }
    }

    state.candidates = std::move(kept);
    state.previous = std::move(kept_previous);
    state.last_error.clear();

    LOG_INFO("Memory search narrowed to {} candidates", state.candidates.size());
    return state.candidates.size();
}

std::string memory_search_report(EmuEnvState &emuenv, const MemorySearchState &state,
    size_t max_entries) {
    if (!state.started)
        return "no search in progress";

    std::string text = fmt::format("candidates={} width={}\n", state.candidates.size(), state.width);
    if (!state.last_error.empty())
        text += fmt::format("note={}\n", state.last_error);

    const size_t shown = std::min(max_entries, state.candidates.size());
    for (size_t i = 0; i < shown; i++) {
        uint64_t current = 0;
        const bool ok = read_raw(emuenv.mem, state.candidates[i], state.width, current);
        text += fmt::format("0x{:08X} = {}\n", state.candidates[i],
            ok ? std::to_string(current) : std::string("<unmapped>"));
    }
    if (state.candidates.size() > shown)
        text += fmt::format("... {} more\n", state.candidates.size() - shown);

    return text;
}

bool memory_read_value(EmuEnvState &emuenv, Address address, uint32_t width, uint64_t &out) {
    return read_raw(emuenv.mem, address, width, out);
}

bool memory_write_value(EmuEnvState &emuenv, Address address, uint32_t width, uint64_t value) {
    if (width != 1 && width != 2 && width != 4)
        return false;
    if (!is_valid_addr_range(emuenv.mem, address, address + width - 1))
        return false;

    std::memcpy(emuenv.mem.memory.get() + address, &value, width);
    LOG_INFO("Memory poke: 0x{:08X} = {} ({} bytes)", address, value, width);
    return true;
}

std::string memory_search_to_cheat(const MemorySearchState &state,
    const std::string &name, uint64_t value, size_t max_entries) {
    if (state.candidates.empty())
        return "# no candidates; narrow the search first\n";

    // VitaCheat's format is one poke per line, address then value, under a
    // named section. Anything wider than 4 bytes is not expressible.
    const char *op = state.width == 1 ? "byte" : (state.width == 2 ? "short" : "int");

    std::string text = fmt::format("# generated by Vita3K Thor memory search\n[{}]\n", name);
    const size_t shown = std::min(max_entries, state.candidates.size());
    for (size_t i = 0; i < shown; i++)
        text += fmt::format("{} 0x{:08X} {}\n", op, state.candidates[i], value);

    if (state.candidates.size() > shown) {
        text += fmt::format("# {} further candidates were left out - narrow the search\n",
            state.candidates.size() - shown);
    }
    return text;
}

} // namespace app

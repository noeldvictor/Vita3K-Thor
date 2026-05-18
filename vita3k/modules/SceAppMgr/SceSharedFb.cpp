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

#include "../SceDisplay/SceDisplay.h"
#include <module/module.h>

#include <gxm/types.h>

#include <kernel/state.h>
#include <modules/SceSharedFb/quick_state.h>

#include <util/tracy.h>

#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

TRACY_MODULE_NAME(SceSharedFB);

typedef struct SceSharedFbInfo { // size is 0x58
    Ptr<void> base1; // cdram base
    int memsize;
    Ptr<void> base2; // cdram base
    int unk_0C;
    Ptr<void> unk_10;
    int unk_14;
    int unk_18;
    int unk_1C;
    int unk_20;
    int pitch; // 960
    int width; // 960
    int height; // 544
    SceGxmColorFormat color_format; // SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR
    int curbuf;
    int unk_38;
    int unk_3C;
    int unk_40;
    int unk_44;
    int vsync;
    int unk_4C;
    int unk_50;
    int unk_54;
} SceSharedFbInfo;

struct SharedFbState {
    SceSharedFbInfo info;
};

static_assert(sizeof(SceSharedFbInfo) == 0x58, "Unexpected SceSharedFbInfo size");

namespace sce_sharedfb {

static bool quick_state_parse_u64_text(const std::string &text, uint64_t &out, const int base = 10) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    try {
        out = std::stoull(text, &consumed, base);
    } catch (...) {
        return false;
    }
    return consumed == text.size();
}

static bool quick_state_parse_u32_text(const std::string &text, uint32_t &out, const int base = 10) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64_text(text, parsed, base) || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

static bool quick_state_parse_bool_text(const std::string &text, bool &out) {
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    return false;
}

static std::string quick_state_hex_bytes(const void *data, const size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; i++)
        text << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return text.str();
}

static bool quick_state_unhex_bytes(const std::string &text, std::vector<uint8_t> &out) {
    if ((text.size() % 2) != 0)
        return false;

    auto hex_value = [](const char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    out.clear();
    out.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_value(text[i]);
        const int low = hex_value(text[i + 1]);
        if (high < 0 || low < 0)
            return false;
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

static bool quick_state_parse_info_text(const std::string &text, SceSharedFbInfo &info) {
    std::vector<uint8_t> bytes;
    if (!quick_state_unhex_bytes(text, bytes) || bytes.size() != sizeof(SceSharedFbInfo))
        return false;

    std::memcpy(&info, bytes.data(), sizeof(info));
    return true;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.sharedfb.v1\n";

    SharedFbState *state = emuenv.kernel.obj_store.get_if<SharedFbState>();
    const SceSharedFbInfo info = state ? state->info : SceSharedFbInfo {};
    const bool created = info.memsize != 0;
    text << "created=" << (created ? 1 : 0) << "\n";
    text << "memsize=" << static_cast<uint32_t>(info.memsize) << "\n";
    text << "base1=0x" << std::hex << info.base1.address() << "\n";
    text << "base2=0x" << info.base2.address() << std::dec << "\n";
    text << "info=" << quick_state_hex_bytes(&info, sizeof(info)) << "\n";
    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, bool *created, uint32_t *memsize, uint32_t *base1, uint32_t *base2, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.sharedfb.v1") {
        if (detail)
            *detail = "SharedFb section schema is invalid";
        return false;
    }

    bool parsed_created = false;
    uint32_t parsed_memsize = 0;
    uint32_t parsed_base1 = 0;
    uint32_t parsed_base2 = 0;
    SceSharedFbInfo info = {};
    if (!values.contains("created") || !quick_state_parse_bool_text(values.at("created"), parsed_created)
        || !values.contains("memsize") || !quick_state_parse_u32_text(values.at("memsize"), parsed_memsize)
        || !values.contains("base1") || !quick_state_parse_u32_text(values.at("base1"), parsed_base1, 0)
        || !values.contains("base2") || !quick_state_parse_u32_text(values.at("base2"), parsed_base2, 0)
        || !values.contains("info") || !quick_state_parse_info_text(values.at("info"), info)) {
        if (detail)
            *detail = "SharedFb section header is invalid";
        return false;
    }

    if (static_cast<uint32_t>(info.memsize) != parsed_memsize
        || info.base1.address() != parsed_base1
        || info.base2.address() != parsed_base2
        || (parsed_created != (info.memsize != 0))) {
        if (detail)
            *detail = "SharedFb scalar fields do not match raw info";
        return false;
    }

    if (parsed_created && (info.base1.address() == 0 || info.base2.address() == 0 || info.memsize <= 0 || info.pitch <= 0 || info.width <= 0 || info.height <= 0)) {
        if (detail)
            *detail = "SharedFb created state is incomplete";
        return false;
    }

    if (created)
        *created = parsed_created;
    if (memsize)
        *memsize = parsed_memsize;
    if (base1)
        *base1 = parsed_base1;
    if (base2)
        *base2 = parsed_base2;
    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    if (!quick_state_validate_snapshot_values(values, nullptr, nullptr, nullptr, nullptr, detail))
        return false;

    SharedFbState *state = emuenv.kernel.obj_store.get_if<SharedFbState>();
    if (!state) {
        emuenv.kernel.obj_store.create<SharedFbState>();
        state = emuenv.kernel.obj_store.get_if<SharedFbState>();
    }
    if (!state) {
        if (detail)
            *detail = "SharedFb state object could not be created";
        return false;
    }

    SceSharedFbInfo info = {};
    if (!quick_state_parse_info_text(values.at("info"), info)) {
        if (detail)
            *detail = "SharedFb raw info restore field is invalid";
        return false;
    }
    state->info = info;
    return true;
}

} // namespace sce_sharedfb

LIBRARY_INIT(SceSharedFb) {
    emuenv.kernel.obj_store.create<SharedFbState>();
}

DECL_EXPORT(int, sceSharedFbCreate, int smth);

EXPORT(int, _sceSharedFbOpen, int smth) {
    TRACY_FUNC(_sceSharedFbOpen, smth);
    STUBBED("sceSharedFbCreate");
    return CALL_EXPORT(sceSharedFbCreate, smth);
}

EXPORT(int, sceSharedFbBegin, int id, SceSharedFbInfo *info) {
    TRACY_FUNC(sceSharedFbBegin, id, info);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    state->info.curbuf = 1;
    *info = state->info;
    return 0;
}

EXPORT(int, sceSharedFbClose) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbCreate, int smth) {
    TRACY_FUNC(sceSharedFbCreate, smth);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    if (state->info.memsize == 0) {
        // enough memory for 2 956x544 buffers
        constexpr uint32_t alloc_size = 4 * 1024 * 512 * 2;
        Ptr<uint8_t> data = Ptr<uint8_t>(alloc(emuenv.mem, alloc_size, "sharedFB"));
        state->info = SceSharedFbInfo{
            .base1 = data,
            .memsize = alloc_size,
            .base2 = data + alloc_size / 2,
            .pitch = 960,
            .width = 960,
            .height = 544,
            .color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
            .curbuf = 1,
        };
    }
    return 1;
}

EXPORT(int, sceSharedFbDelete) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbEnd) {
    TRACY_FUNC(sceSharedFbEnd);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    Ptr<void> data = (state->info.curbuf == 0) ? state->info.base2 : state->info.base1;
    // tell the display a new buffer is ready
    SceDisplayFrameBuf frame_buf{
        .size = sizeof(SceDisplayFrameBuf),
        .base = data,
        .pitch = 960,
        .width = 960,
        .height = 544
    };
    return CALL_EXPORT(_sceDisplaySetFrameBuf, &frame_buf, SCE_DISPLAY_SETBUF_NEXTFRAME, nullptr);
}

EXPORT(int, sceSharedFbGetInfo, int id, SceSharedFbInfo *info) {
    TRACY_FUNC(sceSharedFbGetInfo);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    *info = state->info;
    return 0;
}

EXPORT(int, sceSharedFbGetRenderingInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbGetShellRenderPort) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbUpdateProcess) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbUpdateProcessBegin) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbUpdateProcessEnd) {
    return UNIMPLEMENTED();
}

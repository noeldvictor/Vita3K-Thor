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

#include "SceSysmem.h"
#include "SceSysmemForDriver.h"

#include <kernel/state.h>
#include <kernel/types.h>
#include <modules/SceSysmem/quick_state.h>
#include <modules/sysmem_state.h>

#include <packages/sfo.h>

#include <util/align.h>
#include <util/string_utils.h>

#include <util/tracy.h>
#include <fmt/format.h>
#include <cstring>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <vector>
TRACY_MODULE_NAME(SceSysmem);

std::string to_debug_str(const MemState &mem, SceKernelMemBlockType type) {
    switch (type) {
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE";
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RX: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_RX";
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_RW";
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW";
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW";
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW: return "SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW";
    }
    return std::to_string(type);
}

struct SceKernelAllocMemBlockOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 uidBaseBlock;
    const char *strBaseBlockName;
    int flags;
    int reserved[10];
};

struct SceKernelFreeMemorySizeInfo {
    int size; //!< sizeof(SceKernelFreeMemorySizeInfo)
    int size_user; //!< Free memory size for *_USER_RW memory
    int size_cdram; //!< Free memory size for USER_CDRAM_RW memory
    int size_phycont; //!< Free memory size for USER_MAIN_PHYCONT_*_RW memory
};

namespace sce_sysmem {

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

static bool quick_state_parse_i32_text(const std::string &text, int32_t &out) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    int64_t parsed = 0;
    try {
        parsed = std::stoll(text, &consumed, 10);
    } catch (...) {
        return false;
    }
    if (consumed != text.size() || parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
        return false;
    out = static_cast<int32_t>(parsed);
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

static bool quick_state_parse_address_text(const std::string &text, Address &out) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64_text(text, parsed, 0) || parsed > std::numeric_limits<Address>::max())
        return false;
    out = static_cast<Address>(parsed);
    return true;
}

static std::map<std::string, std::string> quick_state_parse_fields(const std::string &text) {
    std::map<std::string, std::string> fields;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(';', start);
        const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t separator = item.find('=');
        if (separator != std::string::npos)
            fields[item.substr(0, separator)] = item.substr(separator + 1);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return fields;
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

static bool quick_state_valid_memblock_type(const uint32_t type) {
    switch (static_cast<SceKernelMemBlockType>(type)) {
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RX:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW:
    case SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW:
        return true;
    default:
        return type == 0;
    }
}

static bool quick_state_parse_block_fields(const std::map<std::string, std::string> &fields, SceUID &uid, KernelMemBlockPtr &block, bool &vm_block, std::string *detail) {
    static constexpr const char *required_fields[] = {
        "uid",
        "base",
        "mapped_size",
        "info_size",
        "memory_type",
        "access",
        "type",
        "vm",
        "name",
    };
    for (const char *field : required_fields) {
        if (!fields.contains(field)) {
            if (detail)
                *detail = std::string("Sysmem block field is missing: ") + field;
            return false;
        }
    }

    int32_t parsed_uid = 0;
    Address mapped_base = 0;
    uint32_t mapped_size = 0;
    uint32_t info_size = 0;
    int32_t memory_type = 0;
    uint32_t access = 0;
    uint32_t type = 0;
    bool parsed_vm = false;
    std::vector<uint8_t> name_bytes;
    if (!quick_state_parse_i32_text(fields.at("uid"), parsed_uid) || parsed_uid <= 0
        || !quick_state_parse_address_text(fields.at("base"), mapped_base)
        || !quick_state_parse_u32_text(fields.at("mapped_size"), mapped_size)
        || !quick_state_parse_u32_text(fields.at("info_size"), info_size)
        || !quick_state_parse_i32_text(fields.at("memory_type"), memory_type)
        || !quick_state_parse_u32_text(fields.at("access"), access)
        || !quick_state_parse_u32_text(fields.at("type"), type)
        || !quick_state_valid_memblock_type(type)
        || !quick_state_parse_bool_text(fields.at("vm"), parsed_vm)
        || !quick_state_unhex_bytes(fields.at("name"), name_bytes)
        || name_bytes.size() != sizeof(KernelMemBlock::name)) {
        if (detail)
            *detail = "Sysmem block metadata is invalid";
        return false;
    }

    uid = parsed_uid;
    vm_block = parsed_vm;
    block = std::make_shared<KernelMemBlock>();
    block->size = info_size;
    block->mappedBase = Ptr<void>(mapped_base);
    block->mappedSize = mapped_size;
    block->memoryType = memory_type;
    block->access = access;
    block->type = static_cast<SceKernelMemBlockType>(type);
    std::memcpy(block->name, name_bytes.data(), sizeof(block->name));
    block->name[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
    return true;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.sysmem.v1\n";

    SysmemState *state = emuenv.kernel.obj_store.get_if<SysmemState>();
    if (!state) {
        text << "next_uid=1\n";
        text << "allocated_user=0\n";
        text << "allocated_cdram=0\n";
        text << "allocated_phycont=0\n";
        text << "blocks=0\n";
        text << "vm_blocks=0\n";
        return text.str();
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    text << "next_uid=" << state->next_uid << "\n";
    text << "allocated_user=" << state->allocated_user << "\n";
    text << "allocated_cdram=" << state->allocated_cdram << "\n";
    text << "allocated_phycont=" << state->allocated_phycont << "\n";
    size_t block_count = 0;
    size_t vm_block_count = 0;
    for (const auto &[uid, block] : state->blocks) {
        if (!block)
            continue;
        block_count++;
        if (state->vm_blocks.contains(uid))
            vm_block_count++;
    }
    text << "blocks=" << block_count << "\n";
    text << "vm_blocks=" << vm_block_count << "\n";

    size_t index = 0;
    for (const auto &[uid, block] : state->blocks) {
        if (!block)
            continue;
        text << "block." << index++
             << "=uid=" << uid
             << ";base=0x" << std::hex << block->mappedBase.address() << std::dec
             << ";mapped_size=" << block->mappedSize
             << ";info_size=" << block->size
             << ";memory_type=" << block->memoryType
             << ";access=" << block->access
             << ";type=" << static_cast<uint32_t>(block->type)
             << ";vm=" << (state->vm_blocks.contains(uid) ? 1 : 0)
             << ";name=" << quick_state_hex_bytes(block->name, sizeof(block->name))
             << "\n";
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *block_count, size_t *vm_block_count, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.sysmem.v1") {
        if (detail)
            *detail = "Sysmem section schema is invalid";
        return false;
    }

    uint32_t next_uid = 0;
    uint32_t allocated_user = 0;
    uint32_t allocated_cdram = 0;
    uint32_t allocated_phycont = 0;
    uint64_t parsed_blocks = 0;
    uint64_t parsed_vm_blocks = 0;
    if (!values.contains("next_uid") || !quick_state_parse_u32_text(values.at("next_uid"), next_uid) || next_uid == 0
        || !values.contains("allocated_user") || !quick_state_parse_u32_text(values.at("allocated_user"), allocated_user)
        || !values.contains("allocated_cdram") || !quick_state_parse_u32_text(values.at("allocated_cdram"), allocated_cdram)
        || !values.contains("allocated_phycont") || !quick_state_parse_u32_text(values.at("allocated_phycont"), allocated_phycont)
        || !values.contains("blocks") || !quick_state_parse_u64_text(values.at("blocks"), parsed_blocks) || parsed_blocks > 16384
        || !values.contains("vm_blocks") || !quick_state_parse_u64_text(values.at("vm_blocks"), parsed_vm_blocks) || parsed_vm_blocks > parsed_blocks) {
        if (detail)
            *detail = "Sysmem section header is invalid";
        return false;
    }

    std::set<SceUID> seen_uids;
    size_t counted_vm_blocks = 0;
    SceUID max_uid = 0;
    for (size_t index = 0; index < static_cast<size_t>(parsed_blocks); index++) {
        const auto value = values.find(fmt::format("block.{}", index));
        if (value == values.end()) {
            if (detail)
                *detail = fmt::format("Sysmem block {} is missing", index);
            return false;
        }

        const auto fields = quick_state_parse_fields(value->second);
        SceUID uid = 0;
        KernelMemBlockPtr block;
        bool vm_block = false;
        if (!quick_state_parse_block_fields(fields, uid, block, vm_block, detail))
            return false;

        if (seen_uids.contains(uid)) {
            if (detail)
                *detail = fmt::format("Sysmem block UID {} is duplicated", uid);
            return false;
        }
        seen_uids.insert(uid);
        max_uid = std::max(max_uid, uid);
        if (vm_block)
            counted_vm_blocks++;
    }

    if (counted_vm_blocks != static_cast<size_t>(parsed_vm_blocks)) {
        if (detail)
            *detail = "Sysmem VM block count does not match block entries";
        return false;
    }
    if (max_uid > 0 && next_uid <= static_cast<uint32_t>(max_uid)) {
        if (detail)
            *detail = "Sysmem next_uid does not exceed saved block UIDs";
        return false;
    }

    if (block_count)
        *block_count = static_cast<size_t>(parsed_blocks);
    if (vm_block_count)
        *vm_block_count = static_cast<size_t>(parsed_vm_blocks);
    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    size_t block_count = 0;
    size_t vm_block_count = 0;
    if (!quick_state_validate_snapshot_values(values, &block_count, &vm_block_count, detail))
        return false;

    uint32_t next_uid = 1;
    uint32_t allocated_user = 0;
    uint32_t allocated_cdram = 0;
    uint32_t allocated_phycont = 0;
    quick_state_parse_u32_text(values.at("next_uid"), next_uid);
    quick_state_parse_u32_text(values.at("allocated_user"), allocated_user);
    quick_state_parse_u32_text(values.at("allocated_cdram"), allocated_cdram);
    quick_state_parse_u32_text(values.at("allocated_phycont"), allocated_phycont);

    SysmemState *state = emuenv.kernel.obj_store.get_if<SysmemState>();
    if (!state) {
        emuenv.kernel.obj_store.create<SysmemState>();
        state = emuenv.kernel.obj_store.get_if<SysmemState>();
    }
    if (!state) {
        if (detail)
            *detail = "Sysmem state object could not be created";
        return false;
    }

    Blocks restored_blocks;
    Blocks restored_vm_blocks;
    for (size_t index = 0; index < block_count; index++) {
        const auto fields = quick_state_parse_fields(values.at(fmt::format("block.{}", index)));
        SceUID uid = 0;
        KernelMemBlockPtr block;
        bool vm_block = false;
        if (!quick_state_parse_block_fields(fields, uid, block, vm_block, detail))
            return false;

        restored_blocks.emplace(uid, block);
        if (vm_block)
            restored_vm_blocks.emplace(uid, block);
    }

    if (restored_vm_blocks.size() != vm_block_count) {
        if (detail)
            *detail = "Sysmem restored VM block count does not match header";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->blocks.swap(restored_blocks);
        state->vm_blocks.swap(restored_vm_blocks);
        state->next_uid = static_cast<SceUID>(next_uid);
        state->allocated_user = allocated_user;
        state->allocated_cdram = allocated_cdram;
        state->allocated_phycont = allocated_phycont;
    }
    return true;
}

} // namespace sce_sysmem

LIBRARY_INIT(SceSysmem) {
    emuenv.kernel.obj_store.create<SysmemState>();
}

EXPORT(SceUID, sceKernelAllocMemBlock, const char *pName, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *optp) {
    TRACY_FUNC(sceKernelAllocMemBlock, pName, type, size, optp);

    // Build kernel opts from user opts
    SceKernelAllocMemBlockKernelOpt k_opt = {};
    k_opt.size = sizeof(k_opt);
    SceKernelAllocMemBlockKernelOpt *k_opt_ptr = nullptr;

    if (optp) {
        if (optp->attr & SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT) {
            k_opt.attr |= SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
            k_opt.alignment = optp->alignment;
        }
        k_opt_ptr = &k_opt;
    }

    return CALL_EXPORT(ksceKernelAllocMemBlock, pName, type, size, k_opt_ptr);
}

EXPORT(int, sceKernelAllocMemBlockForVM, const char *pName, SceSize size) {
    TRACY_FUNC(sceKernelAllocMemBlockForVM, pName, size);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);

    MemState &mem = emuenv.mem;
    assert(pName != nullptr);

    if (size < 0x1000 || (size & 0xFFF) != 0) {
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    }

    const Ptr<void> address(alloc(mem, size, pName));
    if (!address) {
        return RET_ERROR(SCE_KERNEL_ERROR_NO_MEMORY);
    }

    const SceUID uid = state->get_next_uid();

    const KernelMemBlockPtr sceKernelMemBlock = std::make_shared<KernelMemBlock>();
    sceKernelMemBlock->mappedBase = address;
    sceKernelMemBlock->mappedSize = size;
    sceKernelMemBlock->size = sizeof(SceKernelMemBlockInfo);
    std::strncpy(sceKernelMemBlock->name, pName, KERNELOBJECT_MAX_NAME_LENGTH);
    state->blocks.emplace(uid, sceKernelMemBlock);
    state->vm_blocks.emplace(uid, sceKernelMemBlock);
    state->allocated_user += size;

    return uid;
}

EXPORT(int, sceKernelAllocUnmapMemBlock) {
    TRACY_FUNC(sceKernelAllocUnmapMemBlock);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCheckModelCapability) {
    TRACY_FUNC(sceKernelCheckModelCapability);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCloseMemBlock) {
    TRACY_FUNC(sceKernelCloseMemBlock);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCloseVMDomain) {
    TRACY_FUNC(sceKernelCloseVMDomain);
    return UNIMPLEMENTED();
}

EXPORT(SceUID, sceKernelFindMemBlockByAddr, Address addr, uint32_t size) {
    TRACY_FUNC(sceKernelFindMemBlockByAddr, addr, size);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);

    for (auto &[id, block] : state->blocks) {
        if (block->mappedBase.address() <= addr && (block->mappedBase.address() + block->mappedSize > addr)) {
            return id;
        }
    }
    return RET_ERROR(SCE_KERNEL_ERROR_BLOCK_ERROR);
}

EXPORT(int, sceKernelFreeMemBlock, SceUID uid) {
    TRACY_FUNC(sceKernelFreeMemBlock, uid);
    return CALL_EXPORT(ksceKernelFreeMemBlock, uid);
}

EXPORT(int, sceKernelFreeMemBlockForVM, SceUID uid) {
    TRACY_FUNC(sceKernelFreeMemBlockForVM, uid);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);

    assert(uid >= 0);
    const Blocks::const_iterator block = state->vm_blocks.find(uid);
    assert(block != state->vm_blocks.end());

    free(emuenv.mem, block->second->mappedBase.address());
    state->allocated_user -= block->second->size;
    state->blocks.erase(block);
    state->vm_blocks.erase(block);

    return SCE_KERNEL_OK;
}

EXPORT(int, sceKernelGetFreeMemorySize, SceKernelFreeMemorySizeInfo *info) {
    TRACY_FUNC(sceKernelGetFreeMemorySize, info);

    // Default memory configuration
    uint32_t max_user = MiB(256);

    // if DevKit then max_user = MB(512); else check sfo file for memory expansion mode
    // Fetch the "ATTRIBUTE2" key from the SFO file to check for memory expansion mode
    std::string attribute2;
    if (sfo::get_data_by_key(attribute2, emuenv.sfo_handle, "ATTRIBUTE2")) {
        // Convert the string to an unsigned 32-bit integer
        const uint32_t attr_val = string_utils::stoi_def(attribute2, 0, "memory expansion mode");

        switch (attr_val & 0x0C) {
        case 0x4: // Check for the +29MiB mode
            max_user += MiB(29);
            break;
        case 0x8: // Check for the +77MiB mode
            max_user += MiB(77);
            break;
        case 0xC: // Check for the +109MiB mode
            max_user += MiB(109);
            break;
        default: break;
        }
    } else
        LOG_WARN_ONCE("ATTRIBUTE2 key not found in SFO data.");

    // Define other memory limits
    constexpr uint32_t max_cdram = MiB(112); // Max cdram memory (112 MiB)
    constexpr uint32_t max_phycont = MiB(26); // Max physically contiguous memory (26 MiB)
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);

    // Set the free memory size info
    info->size_cdram = std::max<int>(max_cdram - state->allocated_cdram, 0);
    info->size_user = std::max<int>(max_user - state->allocated_user, 0);
    info->size_phycont = std::max<int>(max_phycont - state->allocated_phycont, 0);

    return 0;
}

EXPORT(int, sceKernelGetMemBlockBase, SceUID uid, Ptr<void> *basep) {
    TRACY_FUNC(sceKernelGetMemBlockBase, uid, basep);
    return CALL_EXPORT(ksceKernelGetMemBlockBase, uid, basep);
}

EXPORT(int, sceKernelGetMemBlockInfoByAddr, Address addr, SceKernelMemBlockInfo *info) {
    TRACY_FUNC(sceKernelGetMemBlockInfoByAddr, addr, info);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);
    assert(addr >= 0);
    assert(info != nullptr);
    for (const auto &[_, block_info] : state->blocks) {
        if (block_info->mappedBase.address() <= addr && (block_info->mappedBase.address() + block_info->mappedSize > addr)) {
            memcpy(info, block_info.get(), sizeof(SceKernelMemBlockInfo));
            return SCE_KERNEL_OK;
        }
    }

    return SCE_KERNEL_ERROR_BLOCK_ERROR;
}

EXPORT(int, sceKernelGetMemBlockInfoByRange) {
    TRACY_FUNC(sceKernelGetMemBlockInfoByRange);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetModel) {
    TRACY_FUNC(sceKernelGetModel);
    return emuenv.cfg.current_config.pstv_mode ? SCE_KERNEL_MODEL_VITATV : SCE_KERNEL_MODEL_VITA;
}

EXPORT(int, sceKernelGetModelForCDialog) {
    TRACY_FUNC(sceKernelGetModelForCDialog);
    return emuenv.cfg.current_config.pstv_mode ? SCE_KERNEL_MODEL_VITATV : SCE_KERNEL_MODEL_VITA;
}

EXPORT(int, sceKernelGetSubbudgetInfo) {
    TRACY_FUNC(sceKernelGetSubbudgetInfo);
    return UNIMPLEMENTED();
}

EXPORT(bool, sceKernelIsPSVitaTV) {
    TRACY_FUNC(sceKernelIsPSVitaTV);
    return emuenv.cfg.current_config.pstv_mode;
}

EXPORT(SceUID, sceKernelOpenMemBlock, const char *pName, int flags) {
    TRACY_FUNC(sceKernelOpenMemBlock, pName, flags);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const std::lock_guard<std::mutex> memblock_lock(state->mutex);

    const auto it = std::find_if(state->blocks.begin(), state->blocks.end(), [=](const auto &block) {
        return strncmp(block.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != state->blocks.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

EXPORT(int, sceKernelOpenVMDomain) {
    TRACY_FUNC(sceKernelOpenVMDomain);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelSyncVMDomain, SceUID block_uid, Address base, uint32_t size) {
    TRACY_FUNC(sceKernelSyncVMDomain, block_uid, base, size);
    const auto state = emuenv.kernel.obj_store.get<SysmemState>();
    const auto guard = std::lock_guard<std::mutex>(state->mutex);

    const auto it = state->vm_blocks.find(block_uid);
    if (it == state->vm_blocks.end()) {
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_BLOCK_ID);
    }

    const auto block = it->second;
    const uint32_t block_base_end = block->mappedBase.address() + block->mappedSize;
    const uint32_t base_end = base + size;
    if (block->mappedBase.address() > base_end || base > block_base_end) {
        return RET_ERROR(SCE_KERNEL_ERROR_BLOCK_ERROR);
    }
    invalidate_jit_cache(*emuenv.kernel.get_thread(thread_id)->cpu, base, size);

    return 0;
}

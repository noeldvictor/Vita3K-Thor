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

#include <module/module.h>

#include <cpu/functions.h>
#include <kernel/state.h>
#include <modules/SceFiber/quick_state.h>

#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <util/lock_and_find.h>
#include <util/log.h>
#include <vector>

#include <fmt/format.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceFiber);

#define SCE_FIBER_CONTEXT_MINIMUM_SIZE 512

enum SceFiberErrorCode : uint32_t {
    SCE_FIBER_OK = 0x00000000, //!< Success
    SCE_FIBER_ERROR_NULL = 0x80590001, //!< Some parameters are NULL.
    SCE_FIBER_ERROR_ALIGNMENT = 0x80590002, //!< Some pointer-parameters are not aligned in their proper alignments.
    SCE_FIBER_ERROR_RANGE = 0x80590003, //!< A parameter exceeds its range in the specification.
    SCE_FIBER_ERROR_INVALID = 0x80590004, //!< A parameter has an invalid value.
    SCE_FIBER_ERROR_PERMISSION = 0x80590005, //!< The function was called from the entity which does not have the permission.
    SCE_FIBER_ERROR_STATE = 0x80590006, //!< The function was applied to a fiber in the state which the function does not support.
    SCE_FIBER_ERROR_BUSY = 0x80590007, //!< The module specified by the function is busy.
    SCE_FIBER_ERROR_AGAIN = 0x80590008, //!< The function could not complete because of the situation. Please try again later.
    SCE_FIBER_ERROR_FATAL = 0x80590009, //!< The fiber caused an unrecoverable error.
};

typedef void(SceFiberEntry)(SceUInt32 argOnInitialize, SceUInt32 argOnRun);

struct SceFiberOptParam {
    char reserved[128];
};

enum class FiberStatus {
    INIT,
    SUSPEND,
    RUN
};

typedef struct SceFiber {
    Ptr<SceFiberEntry> entry;
    Address addrContext;
    SceSize sizeContext;
    char name[32];
    CPUContext *cpu;
    SceUInt32 argOnInitialize;
    Ptr<uint32_t> argOnRun;
    FiberStatus status;
} SceFiber;

static_assert(sizeof(SceFiber) <= 128, "SceFiber struct size is more than 128");

struct FiberState {
    std::mutex mutex;
    std::map<Address, SceFiber *> fibers;
    std::map<SceUID, SceFiber *> thread_fibers;
    std::map<SceUID, CPUContext> thread_contexts;
};

LIBRARY_INIT(SceFiber) {
    emuenv.kernel.obj_store.create<FiberState>();
}

constexpr bool LOG_FIBER = false;

static Address fiber_guest_address(EmuEnvState &emuenv, SceFiber *fiber) {
    if (!fiber)
        return 0;
    return Ptr<SceFiber>(fiber, emuenv.mem).address();
}

static void track_fiber_locked(EmuEnvState &emuenv, FiberState &state, SceFiber *fiber) {
    const Address address = fiber_guest_address(emuenv, fiber);
    if (address != 0)
        state.fibers[address] = fiber;
}

static void untrack_fiber_locked(EmuEnvState &emuenv, FiberState &state, SceFiber *fiber, const bool release_host_context) {
    if (!fiber)
        return;

    state.fibers.erase(fiber_guest_address(emuenv, fiber));
    for (auto it = state.thread_fibers.begin(); it != state.thread_fibers.end();) {
        if (it->second == fiber) {
            state.thread_contexts.erase(it->first);
            it = state.thread_fibers.erase(it);
        } else {
            ++it;
        }
    }

    if (release_host_context) {
        delete fiber->cpu;
        fiber->cpu = nullptr;
    }
}

static void set_thread_fiber(FiberState &state, const SceUID &tid, SceFiber *fiber) {
    if (fiber) {
        state.thread_fibers[tid] = fiber;
    } else {
        state.thread_fibers.erase(tid);
        state.thread_contexts.erase(tid);
    }
}

static SceFiber *get_thread_fiber(FiberState &state, const SceUID &tid) {
    auto fiber = state.thread_fibers.find(tid);
    if (fiber == state.thread_fibers.end()) {
        return nullptr;
    }
    return fiber->second;
}

static void set_thread_context(FiberState &state, const SceUID &tid, const CPUContext &ctx) {
    state.thread_contexts[tid] = ctx;
}

static CPUContext get_thread_context(FiberState &state, const SceUID &tid) {
    return state.thread_contexts[tid];
}

static std::string describe_fiber(FiberState &state, const ThreadStatePtr &thread, SceFiber *fiber) {
    std::string str;
    auto back_it = std::back_inserter(str);
    fmt::format_to(back_it, "Fiber (name: {})\n", fiber->name);
    fmt::format_to(back_it, "entry: 0x{:X}\n", fiber->entry.address());
    fmt::format_to(back_it, "CPU Context:\n{}", fiber->cpu->description());
    fmt::format_to(back_it, "Referenced from {}\n", thread->id);
    fmt::format_to(back_it, "CPU Context:\n{}", get_thread_context(state, thread->id).description());
    return str;
}

static void log_fiber(FiberState &state, const ThreadStatePtr &thread, SceFiber *fiber, const std::string &function_name) {
    LOG_INFO("{}\n{}", function_name, describe_fiber(state, thread, fiber));
}

static void setup_fiber_to_run(EmuEnvState &emuenv, const ThreadStatePtr &thread, SceFiber *fiber, uint32_t thread_sp, const uint32_t &argOnRunTo) {
    assert(fiber->status != FiberStatus::RUN);
    if (!fiber->addrContext) {
        fiber->cpu->set_sp(thread_sp);
        fiber->status = FiberStatus::INIT;
    }

    if (fiber->status == FiberStatus::INIT) {
        fiber->cpu->cpu_registers[0] = fiber->argOnInitialize;
        fiber->cpu->cpu_registers[1] = argOnRunTo;
        fiber->cpu->set_pc(fiber->entry.address());
    } else {
        if (fiber->argOnRun) {
            *fiber->argOnRun.get(emuenv.mem) = argOnRunTo;
        }
    }
    fiber->status = FiberStatus::RUN;
}

static void initialize_fiber(EmuEnvState &emuenv, const ThreadStatePtr &thread, SceFiber *fiber, const char *name, Ptr<SceFiberEntry> entry, SceUInt32 argOnInitialize, Ptr<void> addrContext, SceSize sizeContext, SceFiberOptParam *params) {
    fiber->entry = entry;
    strncpy(fiber->name, name, 32);
    fiber->argOnInitialize = argOnInitialize;
    fiber->argOnRun = nullptr;
    fiber->addrContext = addrContext.address();
    fiber->sizeContext = sizeContext;
    fiber->cpu = new CPUContext;
    fiber->status = FiberStatus::INIT;
    *fiber->cpu = save_context(*thread->cpu);

    if (addrContext && sizeContext > 0) {
        memset(addrContext.get(emuenv.mem), 0xCC, sizeContext);
        fiber->cpu->set_sp(addrContext.address() + sizeContext);
    }
    fiber->cpu->set_lr(0xDEADBEAF);
}

namespace sce_fiber {

static_assert(std::is_trivially_copyable_v<CPUContext>, "Fiber quickstate expects CPUContext to be raw-serializable");

struct SavedFiberSnapshot {
    Address address = 0;
    Address entry = 0;
    Address context = 0;
    uint32_t context_size = 0;
    uint32_t arg_initialize = 0;
    Address arg_run = 0;
    uint32_t status = 0;
    CPUContext cpu_context = {};
};

struct SavedActiveFiberSnapshot {
    SceUID thread_id = 0;
    Address fiber_address = 0;
    CPUContext thread_context = {};
};

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

static bool quick_state_parse_cpu_context_text(const std::string &text, CPUContext &context) {
    std::vector<uint8_t> bytes;
    if (!quick_state_unhex_bytes(text, bytes) || bytes.size() != sizeof(CPUContext))
        return false;

    std::memcpy(&context, bytes.data(), sizeof(context));
    return true;
}

static bool quick_state_valid_fiber_status(const uint32_t status) {
    switch (static_cast<FiberStatus>(status)) {
    case FiberStatus::INIT:
    case FiberStatus::SUSPEND:
    case FiberStatus::RUN:
        return true;
    default:
        return false;
    }
}

static bool quick_state_parse_fiber_fields(const std::map<std::string, std::string> &fields, SavedFiberSnapshot &fiber, std::string *detail) {
    static constexpr const char *required_fields[] = {
        "addr",
        "entry",
        "context",
        "context_size",
        "arg_init",
        "arg_run",
        "status",
        "ctx",
    };
    for (const char *field : required_fields) {
        if (!fields.contains(field)) {
            if (detail)
                *detail = std::string("Fiber field is missing: ") + field;
            return false;
        }
    }

    if (!quick_state_parse_address_text(fields.at("addr"), fiber.address)
        || fiber.address == 0
        || !quick_state_parse_address_text(fields.at("entry"), fiber.entry)
        || !quick_state_parse_address_text(fields.at("context"), fiber.context)
        || !quick_state_parse_u32_text(fields.at("context_size"), fiber.context_size)
        || !quick_state_parse_u32_text(fields.at("arg_init"), fiber.arg_initialize)
        || !quick_state_parse_address_text(fields.at("arg_run"), fiber.arg_run)
        || !quick_state_parse_u32_text(fields.at("status"), fiber.status)
        || !quick_state_valid_fiber_status(fiber.status)
        || !quick_state_parse_cpu_context_text(fields.at("ctx"), fiber.cpu_context)) {
        if (detail)
            *detail = "Fiber metadata is invalid";
        return false;
    }

    return true;
}

static bool quick_state_parse_active_fiber_fields(const std::map<std::string, std::string> &fields, SavedActiveFiberSnapshot &active, std::string *detail) {
    static constexpr const char *required_fields[] = {
        "thread",
        "fiber",
        "thread_ctx",
    };
    for (const char *field : required_fields) {
        if (!fields.contains(field)) {
            if (detail)
                *detail = std::string("Active Fiber field is missing: ") + field;
            return false;
        }
    }

    int32_t parsed_thread = 0;
    if (!quick_state_parse_i32_text(fields.at("thread"), parsed_thread)
        || parsed_thread <= 0
        || !quick_state_parse_address_text(fields.at("fiber"), active.fiber_address)
        || active.fiber_address == 0
        || !quick_state_parse_cpu_context_text(fields.at("thread_ctx"), active.thread_context)) {
        if (detail)
            *detail = "Active Fiber metadata is invalid";
        return false;
    }

    active.thread_id = static_cast<SceUID>(parsed_thread);
    return true;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.fiber.v1\n";

    FiberState *state = emuenv.kernel.obj_store.get_if<FiberState>();
    if (!state) {
        text << "fibers=0\n";
        text << "active_threads=0\n";
        return text.str();
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    std::vector<std::pair<Address, SceFiber *>> tracked_fibers;
    tracked_fibers.reserve(state->fibers.size());
    for (const auto &[address, fiber] : state->fibers) {
        if (address != 0 && fiber && fiber->cpu)
            tracked_fibers.emplace_back(address, fiber);
    }

    std::map<SceFiber *, Address> addresses_by_fiber;
    for (const auto &[address, fiber] : tracked_fibers)
        addresses_by_fiber[fiber] = address;

    std::vector<std::pair<SceUID, SceFiber *>> active_threads;
    for (const auto &[thread_id, fiber] : state->thread_fibers) {
        if (fiber && addresses_by_fiber.contains(fiber) && state->thread_contexts.contains(thread_id))
            active_threads.emplace_back(thread_id, fiber);
    }

    text << "fibers=" << tracked_fibers.size() << "\n";
    text << "active_threads=" << active_threads.size() << "\n";

    size_t fiber_index = 0;
    for (const auto &[address, fiber] : tracked_fibers) {
        text << "fiber." << fiber_index++
             << "=addr=0x" << std::hex << address
             << ";entry=0x" << fiber->entry.address()
             << ";context=0x" << fiber->addrContext
             << std::dec
             << ";context_size=" << fiber->sizeContext
             << ";arg_init=" << fiber->argOnInitialize
             << ";arg_run=0x" << std::hex << fiber->argOnRun.address()
             << std::dec
             << ";status=" << static_cast<uint32_t>(fiber->status)
             << ";ctx=" << quick_state_hex_bytes(fiber->cpu, sizeof(CPUContext))
             << "\n";
    }

    size_t active_index = 0;
    for (const auto &[thread_id, fiber] : active_threads) {
        text << "active." << active_index++
             << "=thread=" << thread_id
             << ";fiber=0x" << std::hex << addresses_by_fiber.at(fiber) << std::dec
             << ";thread_ctx=" << quick_state_hex_bytes(&state->thread_contexts.at(thread_id), sizeof(CPUContext))
             << "\n";
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *fiber_count, size_t *active_thread_count, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.fiber.v1") {
        if (detail)
            *detail = "Fiber section schema is invalid";
        return false;
    }

    uint64_t parsed_fibers = 0;
    uint64_t parsed_active_threads = 0;
    if (!values.contains("fibers") || !quick_state_parse_u64_text(values.at("fibers"), parsed_fibers) || parsed_fibers > 4096
        || !values.contains("active_threads") || !quick_state_parse_u64_text(values.at("active_threads"), parsed_active_threads) || parsed_active_threads > 4096) {
        if (detail)
            *detail = "Fiber section header is invalid";
        return false;
    }

    std::set<Address> seen_fibers;
    for (size_t index = 0; index < static_cast<size_t>(parsed_fibers); index++) {
        const auto value = values.find(fmt::format("fiber.{}", index));
        if (value == values.end()) {
            if (detail)
                *detail = fmt::format("Fiber entry {} is missing", index);
            return false;
        }

        SavedFiberSnapshot fiber;
        if (!quick_state_parse_fiber_fields(quick_state_parse_fields(value->second), fiber, detail))
            return false;
        if (seen_fibers.contains(fiber.address)) {
            if (detail)
                *detail = fmt::format("Fiber address 0x{:x} is duplicated", fiber.address);
            return false;
        }
        seen_fibers.insert(fiber.address);
    }

    std::set<SceUID> seen_threads;
    for (size_t index = 0; index < static_cast<size_t>(parsed_active_threads); index++) {
        const auto value = values.find(fmt::format("active.{}", index));
        if (value == values.end()) {
            if (detail)
                *detail = fmt::format("Active Fiber entry {} is missing", index);
            return false;
        }

        SavedActiveFiberSnapshot active;
        if (!quick_state_parse_active_fiber_fields(quick_state_parse_fields(value->second), active, detail))
            return false;
        if (!seen_fibers.contains(active.fiber_address)) {
            if (detail)
                *detail = fmt::format("Active Fiber thread {} points at unknown Fiber 0x{:x}", active.thread_id, active.fiber_address);
            return false;
        }
        if (seen_threads.contains(active.thread_id)) {
            if (detail)
                *detail = fmt::format("Active Fiber thread {} is duplicated", active.thread_id);
            return false;
        }
        seen_threads.insert(active.thread_id);
    }

    if (fiber_count)
        *fiber_count = static_cast<size_t>(parsed_fibers);
    if (active_thread_count)
        *active_thread_count = static_cast<size_t>(parsed_active_threads);
    return true;
}

void quick_state_discard_live_host_state(EmuEnvState &emuenv) {
    FiberState *state = emuenv.kernel.obj_store.get_if<FiberState>();
    if (!state)
        return;

    std::lock_guard<std::mutex> lock(state->mutex);
    std::set<SceFiber *> released_fibers;
    for (const auto &[address, fiber] : state->fibers) {
        if (!fiber || released_fibers.contains(fiber))
            continue;
        released_fibers.insert(fiber);
        delete fiber->cpu;
        fiber->cpu = nullptr;
    }
    state->fibers.clear();
    state->thread_fibers.clear();
    state->thread_contexts.clear();
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    size_t fiber_count = 0;
    size_t active_thread_count = 0;
    if (!quick_state_validate_snapshot_values(values, &fiber_count, &active_thread_count, detail))
        return false;

    FiberState *state = emuenv.kernel.obj_store.get_if<FiberState>();
    if (!state) {
        emuenv.kernel.obj_store.create<FiberState>();
        state = emuenv.kernel.obj_store.get_if<FiberState>();
    }
    if (!state) {
        if (detail)
            *detail = "Fiber state object could not be created";
        return false;
    }

    std::vector<SavedFiberSnapshot> saved_fibers;
    saved_fibers.reserve(fiber_count);
    std::map<Address, SceFiber *> restored_fibers;
    for (size_t index = 0; index < fiber_count; index++) {
        SavedFiberSnapshot saved;
        if (!quick_state_parse_fiber_fields(quick_state_parse_fields(values.at(fmt::format("fiber.{}", index))), saved, detail))
            return false;

        Ptr<SceFiber> fiber_ptr(saved.address);
        if (!fiber_ptr.valid(emuenv.mem)) {
            if (detail)
                *detail = fmt::format("Fiber guest address 0x{:x} is not mapped", saved.address);
            return false;
        }

        SceFiber *fiber = fiber_ptr.get(emuenv.mem);
        if (!fiber
            || fiber->entry.address() != saved.entry
            || fiber->addrContext != saved.context
            || fiber->sizeContext != saved.context_size
            || fiber->argOnInitialize != saved.arg_initialize
            || fiber->argOnRun.address() != saved.arg_run
            || static_cast<uint32_t>(fiber->status) != saved.status) {
            if (detail)
                *detail = fmt::format("Fiber guest metadata at 0x{:x} does not match snapshot", saved.address);
            return false;
        }

        saved_fibers.push_back(saved);
        restored_fibers.emplace(saved.address, fiber);
    }

    std::map<SceUID, SceFiber *> restored_thread_fibers;
    std::map<SceUID, CPUContext> restored_thread_contexts;
    for (size_t index = 0; index < active_thread_count; index++) {
        SavedActiveFiberSnapshot active;
        if (!quick_state_parse_active_fiber_fields(quick_state_parse_fields(values.at(fmt::format("active.{}", index))), active, detail))
            return false;

        const auto restored_fiber = restored_fibers.find(active.fiber_address);
        if (restored_fiber == restored_fibers.end()) {
            if (detail)
                *detail = fmt::format("Active Fiber thread {} points at unknown Fiber 0x{:x}", active.thread_id, active.fiber_address);
            return false;
        }
        if (!emuenv.kernel.get_thread(active.thread_id)) {
            if (detail)
                *detail = fmt::format("Active Fiber thread {} is not present after thread restore", active.thread_id);
            return false;
        }

        restored_thread_fibers.emplace(active.thread_id, restored_fiber->second);
        restored_thread_contexts.emplace(active.thread_id, active.thread_context);
    }

    for (const SavedFiberSnapshot &saved : saved_fibers) {
        SceFiber *fiber = restored_fibers.at(saved.address);
        fiber->cpu = new CPUContext(saved.cpu_context);
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->fibers.swap(restored_fibers);
        state->thread_fibers.swap(restored_thread_fibers);
        state->thread_contexts.swap(restored_thread_contexts);
    }
    return true;
}

} // namespace sce_fiber

EXPORT(int, _sceFiberAttachContextAndRun, SceFiber *fiber, Address addrContext, SceSize sizeContext, SceUInt32 argOnRunTo, Ptr<SceUInt32> argOnRun) {
    TRACY_FUNC(_sceFiberAttachContextAndRun, fiber, addrContext, sizeContext, argOnRunTo, argOnRun);
    // Maybe Need more check on real hw
    STUBBED("Todo: not sure for now");
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    const auto thread = emuenv.kernel.get_thread(thread_id);
    assert(!get_thread_fiber(*state, thread->id));
    assert(!fiber->addrContext);
    if (LOG_FIBER) {
        log_fiber(*state, thread, fiber, "Attach context and run");
    }

    fiber->addrContext = addrContext;
    fiber->sizeContext = sizeContext;
    if (addrContext && sizeContext > 0) {
        fiber->cpu->set_sp(addrContext + sizeContext);
    }

    track_fiber_locked(emuenv, *state, fiber);
    setup_fiber_to_run(emuenv, thread, fiber, read_sp(*thread->cpu), argOnRunTo);
    set_thread_context(*state, thread->id, save_context(*thread->cpu));
    set_thread_fiber(*state, thread->id, fiber);

    load_context(*thread->cpu, *fiber->cpu);
    return fiber->cpu->cpu_registers[0];
}

EXPORT(int, _sceFiberAttachContextAndSwitch, SceFiber *fiber, Address addrContext, SceSize sizeContext, SceUInt32 argOnRunTo, Ptr<SceUInt32> argOnRun) {
    TRACY_FUNC(_sceFiberAttachContextAndSwitch, fiber, addrContext, sizeContext, argOnRunTo, argOnRun);
    // Maybe Need more check on real hw
    STUBBED("Todo: not sure for now");
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    const auto thread = emuenv.kernel.get_thread(thread_id);
    auto ctx = get_thread_context(*state, thread->id);
    SceFiber *thread_fiber = get_thread_fiber(*state, thread->id);
    if (LOG_FIBER) {
        log_fiber(*state, thread, fiber, "Attach context and switch");
    }

    assert(thread_fiber);
    assert(!fiber->addrContext);
    fiber->addrContext = addrContext;
    fiber->sizeContext = sizeContext;
    if (addrContext && sizeContext > 0) {
        fiber->cpu->set_sp(addrContext + sizeContext);
    }

    track_fiber_locked(emuenv, *state, thread_fiber);
    track_fiber_locked(emuenv, *state, fiber);
    *thread_fiber->cpu = save_context(*thread->cpu);
    setup_fiber_to_run(emuenv, thread, fiber, ctx.get_sp(), argOnRunTo);
    thread_fiber->status = FiberStatus::SUSPEND;
    thread_fiber->argOnRun = argOnRun;
    thread_fiber->cpu->cpu_registers[0] = SCE_FIBER_OK;
    set_thread_fiber(*state, thread->id, fiber);
    load_context(*thread->cpu, *fiber->cpu);

    return fiber->cpu->cpu_registers[0];
}

EXPORT(SceInt32, _sceFiberInitializeImpl, SceFiber *fiber, const char *name, Ptr<SceFiberEntry> entry, SceUInt32 argOnInitialize, Ptr<void> addrContext, SceSize sizeContext, SceFiberOptParam *params) {
    TRACY_FUNC(_sceFiberInitializeImpl, fiber, name, entry, argOnInitialize, addrContext, sizeContext, params);
    if (!fiber || !entry || !name) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }

    if ((sizeContext != 0) && (sizeContext < SCE_FIBER_CONTEXT_MINIMUM_SIZE)) {
        return RET_ERROR(SCE_FIBER_ERROR_RANGE);
    }

    if ((!addrContext && (sizeContext != 0)) || (addrContext && (sizeContext == 0)) || ((sizeContext & 7) != 0)) {
        return RET_ERROR(SCE_FIBER_ERROR_INVALID);
    }

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    }

    initialize_fiber(emuenv, thread, fiber, name, entry, argOnInitialize, addrContext, sizeContext, params);
    {
        const auto state = emuenv.kernel.obj_store.get<FiberState>();
        const std::lock_guard<std::mutex> lock(state->mutex);
        track_fiber_locked(emuenv, *state, fiber);
    }

    return SCE_FIBER_OK;
}

EXPORT(int, _sceFiberInitializeWithInternalOptionImpl, SceFiber *fiber, const char *name, Ptr<SceFiberEntry> entry, SceUInt32 argOnInitialize, Ptr<void> addrContext, SceSize sizeContext) {
    TRACY_FUNC(_sceFiberInitializeWithInternalOptionImpl, fiber, name, entry, argOnInitialize, addrContext, sizeContext);
    if (!fiber || !entry || !name) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }

    if ((sizeContext != 0) && (sizeContext < SCE_FIBER_CONTEXT_MINIMUM_SIZE)) {
        return RET_ERROR(SCE_FIBER_ERROR_RANGE);
    }

    if ((!addrContext && (sizeContext != 0)) || (addrContext && (sizeContext == 0)) || ((sizeContext & 7) != 0)) {
        return RET_ERROR(SCE_FIBER_ERROR_INVALID);
    }

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    }

    initialize_fiber(emuenv, thread, fiber, name, entry, argOnInitialize, addrContext, sizeContext, nullptr);
    {
        const auto state = emuenv.kernel.obj_store.get<FiberState>();
        const std::lock_guard<std::mutex> lock(state->mutex);
        track_fiber_locked(emuenv, *state, fiber);
    }

    return SCE_FIBER_OK;
}

EXPORT(SceInt32, sceFiberFinalize, SceFiber *fiber) {
    TRACY_FUNC(sceFiberFinalize, fiber);
    if (!fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }

    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    if (fiber->status == FiberStatus::RUN) {
        return RET_ERROR(SCE_FIBER_ERROR_STATE);
    }

    untrack_fiber_locked(emuenv, *state, fiber, true);
    return 0;
}

struct SceFiberInfo {
    Ptr<SceFiberEntry> entry;
    SceUInt32 argOnInitialize;
    Ptr<void> addrContext;
    SceUInt32 sizeContext;
    char name[32];
    SceUInt32 sizeContextMargin;
};

EXPORT(int, sceFiberGetInfo, SceFiber *fiber, SceFiberInfo *fiberInfo) {
    TRACY_FUNC(sceFiberGetInfo, fiber, fiberInfo);
    if (!fiber || !fiberInfo) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }
    fiberInfo->entry = fiber->entry;
    fiberInfo->argOnInitialize = fiber->argOnInitialize;
    fiberInfo->addrContext = fiber->addrContext;
    fiberInfo->sizeContext = fiber->sizeContext;
    memcpy(fiberInfo->name, fiber->name, sizeof(fiberInfo->name));
    STUBBED("sizeContextMargin is stubbed");
    fiberInfo->sizeContextMargin = -1;
    return 0;
}

EXPORT(SceUInt32, sceFiberGetSelf, Ptr<SceFiber> *fiber) {
    TRACY_FUNC(sceFiberGetSelf, fiber);
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    if (!fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }
    const std::lock_guard<std::mutex> lock(state->mutex);

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    SceFiber *thread_fiber = get_thread_fiber(*state, thread->id);
    if (thread_fiber)
        *fiber = Ptr<SceFiber>(thread_fiber, emuenv.mem);
    else
        *fiber = Ptr<SceFiber>(0);

    return SCE_FIBER_OK;
}

EXPORT(int, sceFiberOptParamInitialize) {
    TRACY_FUNC(sceFiberOptParamInitialize);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiberPopUserMarkerWithHud) {
    TRACY_FUNC(sceFiberPopUserMarkerWithHud);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiberPushUserMarkerWithHud) {
    TRACY_FUNC(sceFiberPushUserMarkerWithHud);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiberRenameSelf) {
    TRACY_FUNC(sceFiberRenameSelf);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceFiberReturnToThread, uint32_t argOnReturnTo, Ptr<uint32_t> argOnRun) {
    TRACY_FUNC(sceFiberReturnToThread, argOnReturnTo, argOnRun);
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    SceFiber *fiber = get_thread_fiber(*state, thread->id);
    if (!fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_PERMISSION);
    }

    CPUContext thread_context = get_thread_context(*state, thread->id);
    assert(fiber->status == FiberStatus::RUN);
    if (LOG_FIBER) {
        log_fiber(*state, thread, fiber, "Return to thread");
    }

    *fiber->cpu = save_context(*thread->cpu);
    fiber->cpu->cpu_registers[0] = SCE_FIBER_OK;
    fiber->status = FiberStatus::SUSPEND;
    fiber->argOnRun = argOnRun;
    set_thread_fiber(*state, thread->id, nullptr);

    load_context(*thread->cpu, thread_context);
    Address argOnReturn = thread_context.cpu_registers[2];
    if (argOnReturn) {
        *(Ptr<uint32_t>(argOnReturn).get(emuenv.mem)) = argOnReturnTo;
    }

    return SCE_FIBER_OK;
}

EXPORT(SceUInt32, sceFiberRun, SceFiber *fiber, SceUInt32 argOnRunTo, Ptr<SceUInt32> argOnReturn) {
    TRACY_FUNC(sceFiberRun, fiber, argOnRunTo, argOnReturn);
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }

    if (fiber->status == FiberStatus::RUN) {
        return RET_ERROR(SCE_FIBER_ERROR_STATE);
    }

    if (get_thread_fiber(*state, thread->id)) {
        return RET_ERROR(SCE_FIBER_ERROR_PERMISSION);
    }

    if (LOG_FIBER) {
        log_fiber(*state, thread, fiber, "Run");
    }

    setup_fiber_to_run(emuenv, thread, fiber, read_sp(*thread->cpu), argOnRunTo);
    set_thread_context(*state, thread->id, save_context(*thread->cpu));
    track_fiber_locked(emuenv, *state, fiber);
    set_thread_fiber(*state, thread->id, fiber);

    load_context(*thread->cpu, *fiber->cpu);
    return fiber->cpu->cpu_registers[0];
}

EXPORT(int, sceFiberStartContextSizeCheck) {
    TRACY_FUNC(sceFiberStartContextSizeCheck);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiberStopContextSizeCheck) {
    TRACY_FUNC(sceFiberStopContextSizeCheck);
    return UNIMPLEMENTED();
}

EXPORT(SceUInt32, sceFiberSwitch, SceFiber *fiber, SceUInt32 argOnRunTo, Ptr<SceUInt32> argOnRun) {
    TRACY_FUNC(sceFiberSwitch, fiber, argOnRunTo, argOnRun);
    const auto state = emuenv.kernel.obj_store.get<FiberState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    auto ctx = get_thread_context(*state, thread->id);
    if (!fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_NULL);
    }

    if (fiber->status == FiberStatus::RUN) {
        return RET_ERROR(SCE_FIBER_ERROR_STATE);
    }

    SceFiber *thread_fiber = get_thread_fiber(*state, thread->id);
    if (!thread_fiber) {
        return RET_ERROR(SCE_FIBER_ERROR_PERMISSION);
    }

    if (LOG_FIBER) {
        log_fiber(*state, thread, fiber, "Switch");
    }

    track_fiber_locked(emuenv, *state, thread_fiber);
    track_fiber_locked(emuenv, *state, fiber);
    *thread_fiber->cpu = save_context(*thread->cpu);
    thread_fiber->status = FiberStatus::SUSPEND;
    thread_fiber->argOnRun = argOnRun;
    thread_fiber->cpu->cpu_registers[0] = SCE_FIBER_OK;
    set_thread_fiber(*state, thread->id, fiber);
    setup_fiber_to_run(emuenv, thread, fiber, ctx.get_sp(), argOnRunTo);
    load_context(*thread->cpu, *fiber->cpu);

    return fiber->cpu->cpu_registers[0];
}

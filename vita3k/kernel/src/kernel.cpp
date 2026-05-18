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

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <cpu/common.h>
#include <kernel/state.h>
#include <mem/functions.h>

#include <kernel/thread/thread_state.h>

#include <cpu/functions.h>
#include <mem/ptr.h>
#include <util/lock_and_find.h>
#include <util/log.h>

#include <SDL3/SDL_mutex.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

int CorenumAllocator::new_corenum() {
    const std::lock_guard<std::mutex> guard(lock);

    uint32_t size = 1;
    return alloc.allocate_from(0, size);
}

void CorenumAllocator::free_corenum(const int num) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.free(num, 1);
}

void CorenumAllocator::set_max_core_count(const std::size_t max) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.set_maximum(max);
}

// TODO implement cross platform debug thread name setter and eliminate SDL thread
struct ThreadParams {
    KernelState *kernel = nullptr;
    SceUID thid = SCE_KERNEL_ERROR_ILLEGAL_THREAD_ID;
    SDL_Semaphore *host_may_destroy_params = nullptr;
};

static int SDLCALL thread_function(void *data) {
    assert(data != nullptr);
    const ThreadParams params = *static_cast<const ThreadParams *>(data);
    SDL_SignalSemaphore(params.host_may_destroy_params);
    const ThreadStatePtr thread = params.kernel->get_thread(params.thid);
#ifdef TRACY_ENABLE
    if (!thread->name.empty()) {
        tracy::SetThreadName(thread->name.c_str());
    } else {
        std::string th_name = "TID:" + std::to_string(thread->id);
        tracy::SetThreadName(th_name.c_str());
    }
#endif

    thread->run_loop();
    const uint32_t r0 = read_reg(*thread->cpu, 0);

    std::lock_guard<std::mutex> lock(params.kernel->mutex);
    params.kernel->threads.erase(thread->id);
    params.kernel->corenum_allocator.free_corenum(get_processor_id(*thread->cpu));

    return r0;
}

KernelState::KernelState()
    : debugger(*this) {
}

KernelState::~KernelState() {
    stop_deferred_wait_worker();
}

void KernelState::start_deferred_wait_worker() {
    std::lock_guard<std::mutex> lock(deferred_wait_mutex);
    if (deferred_wait_thread.joinable())
        return;

    deferred_wait_stop = false;
    deferred_wait_thread = std::thread([this]() {
        run_deferred_wait_worker();
    });
}

void KernelState::stop_deferred_wait_worker() {
    {
        std::lock_guard<std::mutex> lock(deferred_wait_mutex);
        deferred_wait_stop = true;
        deferred_wait_timeouts.clear();
    }
    deferred_wait_cond.notify_all();

    if (deferred_wait_thread.joinable() && deferred_wait_thread.get_id() != std::this_thread::get_id())
        deferred_wait_thread.join();
}

uint64_t KernelState::next_wait_generation() {
    return next_wait_generation_id.fetch_add(1, std::memory_order_relaxed);
}

void KernelState::schedule_deferred_wait_timeout(const uint64_t timeout_guest_us, std::function<void()> action) {
    if (!action)
        return;

    DeferredWaitTimeout timeout;
    timeout.due_guest_process_us = get_process_time() + timeout_guest_us;
    timeout.action = std::move(action);

    {
        std::lock_guard<std::mutex> lock(deferred_wait_mutex);
        if (deferred_wait_stop)
            return;

        timeout.sequence = deferred_wait_sequence++;
        deferred_wait_timeouts.push_back(std::move(timeout));
    }
    deferred_wait_cond.notify_all();
}

void KernelState::clear_deferred_wait_timeouts() {
    {
        std::lock_guard<std::mutex> lock(deferred_wait_mutex);
        deferred_wait_timeouts.clear();
    }
    deferred_wait_cond.notify_all();
}

void KernelState::run_deferred_wait_worker() {
    std::unique_lock<std::mutex> lock(deferred_wait_mutex);
    while (!deferred_wait_stop) {
        if (deferred_wait_timeouts.empty()) {
            deferred_wait_cond.wait(lock, [this]() { return deferred_wait_stop || !deferred_wait_timeouts.empty(); });
            continue;
        }

        const uint64_t now_guest_us = get_process_time();
        std::vector<std::function<void()>> due_actions;
        for (auto it = deferred_wait_timeouts.begin(); it != deferred_wait_timeouts.end();) {
            if (it->due_guest_process_us <= now_guest_us) {
                due_actions.push_back(std::move(it->action));
                it = deferred_wait_timeouts.erase(it);
            } else {
                ++it;
            }
        }

        if (!due_actions.empty()) {
            lock.unlock();
            for (auto &action : due_actions) {
                if (action)
                    action();
            }
            lock.lock();
            continue;
        }

        const auto next_timeout = std::min_element(
            deferred_wait_timeouts.begin(), deferred_wait_timeouts.end(),
            [](const DeferredWaitTimeout &lhs, const DeferredWaitTimeout &rhs) {
                if (lhs.due_guest_process_us != rhs.due_guest_process_us)
                    return lhs.due_guest_process_us < rhs.due_guest_process_us;
                return lhs.sequence < rhs.sequence;
            });
        if (next_timeout == deferred_wait_timeouts.end())
            continue;

        const uint64_t guest_delta_us = next_timeout->due_guest_process_us > now_guest_us ? next_timeout->due_guest_process_us - now_guest_us : 1;
        const uint64_t speed = std::max<uint32_t>(speed_percent.load(), 1);
        const uint64_t host_wait_us = std::clamp<uint64_t>((guest_delta_us * 100) / speed, 1, 1000000);
        deferred_wait_cond.wait_for(lock, std::chrono::microseconds(host_wait_us));
    }

    deferred_wait_timeouts.clear();
}

static uint64_t speeded_process_time_locked(const KernelState &kernel, const uint64_t host_process_time) {
    const uint64_t speed_percent = std::max<uint32_t>(kernel.speed_percent.load(), 1);
    const uint64_t host_delta = host_process_time - kernel.speed_anchor_host_process_us;
    return kernel.speed_anchor_guest_process_us + ((host_delta * speed_percent) / 100);
}

bool KernelState::init(MemState &mem, const CallImportFunc &call_import, bool cpu_opt) {
    corenum_allocator.set_max_core_count(MAX_CORE_COUNT);
    base_tick = { rtc_base_ticks() };
    start_tick = rtc_get_ticks(base_tick.tick);
    speed_anchor_host_process_us = 0;
    speed_anchor_guest_process_us = 0;
    speed_percent.store(100);
    start_deferred_wait_worker();
    this->call_import = call_import;
    this->cpu_opt = cpu_opt;

    // Generate halt instruction (NOP + WFI)
    halt_instruction = alloc_block(mem, 4, "halt_instruction");
    const auto halt_ptr = halt_instruction.get_ptr<uint16_t>().get(mem);
    halt_ptr[0] = 0xBF00; // NOP
    halt_ptr[1] = 0xBF30; // WFI
    halt_instruction_pc = halt_instruction.get() | 1; // thumb mode pc

    return true;
}

uint64_t KernelState::get_process_time() const {
    const std::lock_guard<std::mutex> guard(speed_mutex);
    const uint64_t host_process_time = rtc_get_ticks(base_tick.tick) - start_tick;
    return speeded_process_time_locked(*this, host_process_time);
}

uint64_t KernelState::get_guest_tick() const {
    return start_tick + get_process_time();
}

void KernelState::set_speed_percent(const uint32_t new_speed_percent) {
    const std::lock_guard<std::mutex> guard(speed_mutex);
    const uint64_t host_process_time = rtc_get_ticks(base_tick.tick) - start_tick;
    speed_anchor_guest_process_us = speeded_process_time_locked(*this, host_process_time);
    speed_anchor_host_process_us = host_process_time;
    speed_percent.store(std::max<uint32_t>(new_speed_percent, 1));
    deferred_wait_cond.notify_all();
}

void KernelState::load_process_param(MemState &mem, Ptr<uint32_t> ptr) {
    const SceProcessParam *param = ptr.cast<SceProcessParam>().get(mem);
    if (param->version == 0) {
        // Homebrews built with old vitasdk
        process_param = nullptr;
        return;
    }
    process_param = ptr.cast<SceProcessParam>();
    // VAR_NID(__sce_libcparam, 0xDF084DFA)
    // no memory leak because we don't allocate memory for this variable intially
    export_nids[0xDF084DFA] = process_param.get(mem)->sce_libc_param.address();
}

void KernelState::set_memory_watch(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &thread : threads) {
        auto &cpu = *thread.second->cpu;
        if (enabled != get_log_mem(cpu)) {
            if (enabled)
                set_log_mem(cpu, true);
            else
                set_log_mem(cpu, false);
        }
    }
}

void KernelState::invalidate_jit_cache(Address start, size_t length) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &[_, thread] : threads) {
        ::invalidate_jit_cache(*thread->cpu, start, length);
    }
}

ThreadStatePtr KernelState::get_thread(SceUID thread_id) {
    return lock_and_find(thread_id, threads, mutex);
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point) {
    return create_thread(mem, name, entry_point, SCE_KERNEL_DEFAULT_PRIORITY, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, SCE_KERNEL_STACK_SIZE_USER_MAIN, nullptr);
}

static ThreadStatePtr create_thread_with_uid(KernelState &kernel, MemState &mem, const SceUID uid, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option) {
    ThreadStatePtr thread = std::make_shared<ThreadState>(uid, kernel, mem);
    if (thread->init(name, entry_point, init_priority, affinity_mask, stack_size, option) < 0)
        return nullptr;
    {
        const auto lock = std::lock_guard(kernel.mutex);
        if (kernel.threads.contains(thread->id))
            return nullptr;
        kernel.threads.emplace(thread->id, thread);
    }

    ThreadParams params;
    params.kernel = &kernel;
    params.thid = thread->id;

    params.host_may_destroy_params = SDL_CreateSemaphore(0);
    SDL_DetachThread(SDL_CreateThread(&thread_function, thread->name.c_str(), &params));
    SDL_WaitSemaphore(params.host_may_destroy_params);
    SDL_DestroySemaphore(params.host_may_destroy_params);
    return thread;
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option) {
    return create_thread_with_uid(*this, mem, get_next_uid(), name, entry_point, init_priority, affinity_mask, stack_size, option);
}

ThreadStatePtr KernelState::create_thread_for_restore(MemState &mem, const SceUID uid, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size) {
    reserve_uid_for_restore(uid);
    return create_thread_with_uid(*this, mem, uid, name, entry_point, init_priority, affinity_mask, stack_size, nullptr);
}

void KernelState::reserve_uid_for_restore(const SceUID uid) {
    SceUID next = next_uid.load();
    while (next <= uid && !next_uid.compare_exchange_weak(next, uid + 1)) {
    }
}

Ptr<Ptr<void>> KernelState::get_thread_tls_addr(MemState &mem, SceUID thread_id, int key) {
    Ptr<Ptr<void>> address(0);
    // magic numbers taken from decompiled source. There is 0x400 unused bytes of unknown usage
    if (key <= 0x100 && key >= 0) {
        const ThreadStatePtr thread = get_thread(thread_id);
        address = thread->tls.get_ptr<Ptr<void>>() + key;
    } else {
        LOG_ERROR("Wrong tls slot index. TID:{} index:{}", thread_id, key);
    }
    return address;
}

ThreadStatus KernelState::snapshot_thread_status_unlocked(const SceUID thread_id, const ThreadStatus current_status) const {
    const auto paused_status = paused_threads_status.find(thread_id);
    return paused_status == paused_threads_status.end() ? current_status : paused_status->second;
}

void KernelState::set_paused_thread_status_for_restore(const SceUID thread_id, const ThreadStatus status) {
    const std::lock_guard<std::mutex> lock(mutex);
    if (threads_pause_active.load())
        paused_threads_status[thread_id] = status;
}

void KernelState::exit_delete_all_threads() {
    clear_deferred_wait_timeouts();
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads)
        // Skip end callbacks; running guest code can access torn-down state
        thread->exit_delete(false);
}

void KernelState::pause_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    threads_pause_active.store(true);
    for (auto &[_, thread] : threads) {
        if (!paused_threads_status.contains(thread->id))
            paused_threads_status[thread->id] = thread->status;
        if (thread->status == ThreadStatus::run)
            thread->suspend();
    }
}

void KernelState::resume_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    threads_pause_active.store(false);
    for (auto &[_, thread] : threads) {
        const auto paused_status = paused_threads_status.find(thread->id);
        thread->resume_after_pause_if_needed(paused_status != paused_threads_status.end() && paused_status->second == ThreadStatus::run);
    }
    paused_threads_status.clear();
}

SceKernelModuleInfo *KernelState::find_module_by_addr(Address address) {
    const auto lock = std::lock_guard(mutex);
    for (auto &[_, mod] : loaded_modules) {
        for (auto &seg : mod->info.segments) {
            if (!seg.size)
                continue;
            if (seg.vaddr.address() <= address && address <= seg.vaddr.address() + seg.memsz) {
                return &mod->info;
            }
        }
    }
    return nullptr;
}

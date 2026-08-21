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

#include <cpu/state.h>
#include <kernel/callback.h>
#include <kernel/types.h>
#include <mem/block.h>
#include <mem/ptr.h>

#include <condition_variable>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

struct CPUContext;

struct ThreadState;
struct ThreadParams;
struct KernelState;

typedef std::unique_ptr<CPUState, std::function<void(CPUState *)>> CPUStatePtr;
typedef std::function<void(CPUState &, uint32_t, SceUID)> CallImport;
typedef std::function<std::string(Address)> ResolveNIDName;

enum class ThreadStatus {
    run, // Running
    dormant, // Waiting for a job
    suspend, // Suspended by debugger
    wait, // Waiting to be awaken by sync object or operation
};

struct ThreadSignal {
    ThreadSignal() = default;
    ~ThreadSignal() = default;

    void wait();
    bool send();

private:
    std::mutex mutex;
    std::condition_variable recv_cond;
    bool signaled = false;
};

struct ThreadState {
    std::mutex mutex;
    std::string name;
    SceUID id;
    Address entry_point;

    Block stack;
    int stack_size;
    Block tls;

    int priority;
    SceInt32 affinity_mask;
    uint64_t start_tick;
    uint64_t last_vblank_waited;
    // set to true if thread is processing kernel callbacks
    bool is_processing_callbacks = false;

    CPUStatePtr cpu;
    ThreadStatus status = ThreadStatus::dormant;

    ThreadSignal signal;
    std::vector<CallbackPtr> callbacks;
    std::condition_variable status_cond;
    std::vector<std::shared_ptr<ThreadState>> waiting_threads;
    uint32_t returned_value = 0;

    ThreadState() = delete;
    explicit ThreadState(SceUID id, KernelState &kernel, MemState &mem);

    int init(const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option);
    int start(SceSize arglen, const Ptr<void> argp, bool run_entry_callback = false);
    void exit(SceInt32 status);
    void exit_delete(bool exit = true);
    bool try_exit_delete_for_quick_state(std::chrono::milliseconds timeout);

    void update_status(ThreadStatus status, std::optional<ThreadStatus> expected = std::nullopt);
    Address stack_top() const;

    void run_loop();
    void raise_waiting_threads();

    // this function must be called from the thread itself (inside a svc call)
    uint32_t run_callback(Address callback_address, const std::vector<uint32_t> &args);

    // this function is called from another thread when this one is dormant
    // it is only used for module loading and gxm display queue right now
    // args and argp are passed to thread->start as is
    uint32_t run_guest_function(Address callback_address, SceSize args = 0, const Ptr<void> argp = Ptr<void>{});

    void suspend();
    void resume(bool step = false);
    bool is_quick_state_pause_quiesced() const;
    bool needs_quick_state_stop_pulse() const;
    bool begin_deferred_import_wait();
    bool restore_deferred_import_wait();
    void clear_deferred_import_wait_for_restore();
    bool complete_deferred_import_wait(uint32_t return_value);
    bool consume_deferred_import_return();
    void resume_after_pause_if_needed(bool saved_running_before_pause);
    bool has_deferred_import_wait();
    void set_active_import_detail(uint32_t detail);
    void release_memory_blocks_for_quick_state();
    void release_memory_blocks_for_removed_quick_state_thread();
    void restore_memory_blocks_for_quick_state(Address stack_address, int stack_size, Address tls_address);
    std::string quick_state_debug_summary() const;
    std::string log_stack_traceback() const;

private:
    void push_arguments(const std::vector<uint32_t> &args);
    void dispatch_abort(CPUState &cpu);
    void exit_delete_locked(bool exit);

    KernelState &kernel;

    CPUContext init_cpu_ctx;
    // sceKernelExitThread (or top-level guest function return): park at dormant, thread reusable via start() / run_guest_function().
    bool exit_requested = false;
    // sceKernelExitDeleteThread (or external kill): will return from top-level run_loop(), then host thread joins.
    bool delete_requested = false;
    // Set by suspend(), consumed in run_loop() to transition to ThreadStatus::suspend.
    bool suspend_requested = false;
    // Single stepping mode.
    bool single_stepping = false;

    // Number of active run_loop frames. The top-level host thread keeps one
    // frame alive (run_loop()) while parked dormant; callbacks add nested frames.
    int call_level = 0;

    // when calling sceKernelStartThread
    bool run_start_callback = false;
    // when calling sceKernelExitThread or sceKernelExitDeleteThread
    bool run_end_callback = false;
    bool deferred_import_wait = false;
    bool deferred_import_return = false;
    bool deferred_resume_after_pause = false;
    std::atomic<uint32_t> active_import_nid{ 0 };
    std::atomic<Address> active_import_pc{ 0 };
    std::atomic<uint32_t> active_import_detail{ 0 };

    MemState &mem;
};

typedef std::shared_ptr<ThreadState> ThreadStatePtr;

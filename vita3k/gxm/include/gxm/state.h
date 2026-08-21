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

#include <display/state.h>
#include <gxm/types.h>
#include <mem/ptr.h>
#include <threads/queue.h>

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct EmuEnvState;
struct SceGxmContext;
struct SceGxmRenderTarget;

struct SceGxmInitializeParams {
    uint32_t flags = 0;
    uint32_t displayQueueMaxPendingCount = 0;
    Ptr<void> displayQueueCallback;
    uint32_t displayQueueCallbackDataSize = 0;
    uint32_t parameterBufferSize = 0;
};

struct DisplayCallback {
    Address data;
    Ptr<SceGxmSyncObject> old_sync;
    Ptr<SceGxmSyncObject> new_sync;
    uint32_t old_sync_timestamp;
    uint32_t new_sync_timestamp;
    bool frame_predicted;
};

struct PendingDisplayCallback {
    SceUID thread_id = 0;
    DisplayCallback callback;
    DisplayFrameInfo *frame = nullptr;
    std::optional<DisplayFrameInfo> owned_frame;
    bool wait_until_empty_after_push = false;

    PendingDisplayCallback() = default;

    PendingDisplayCallback(const PendingDisplayCallback &other)
        : thread_id(other.thread_id)
        , callback(other.callback)
        , frame(other.frame)
        , owned_frame(other.owned_frame)
        , wait_until_empty_after_push(other.wait_until_empty_after_push) {
        if (owned_frame)
            frame = &*owned_frame;
    }

    PendingDisplayCallback &operator=(const PendingDisplayCallback &other) {
        if (this == &other)
            return *this;

        thread_id = other.thread_id;
        callback = other.callback;
        frame = other.frame;
        owned_frame = other.owned_frame;
        wait_until_empty_after_push = other.wait_until_empty_after_push;
        if (owned_frame)
            frame = &*owned_frame;
        return *this;
    }

    PendingDisplayCallback(PendingDisplayCallback &&other) noexcept
        : thread_id(other.thread_id)
        , callback(other.callback)
        , frame(other.frame)
        , owned_frame(std::move(other.owned_frame))
        , wait_until_empty_after_push(other.wait_until_empty_after_push) {
        if (owned_frame)
            frame = &*owned_frame;
    }

    PendingDisplayCallback &operator=(PendingDisplayCallback &&other) noexcept {
        if (this == &other)
            return *this;

        thread_id = other.thread_id;
        callback = other.callback;
        frame = other.frame;
        owned_frame = std::move(other.owned_frame);
        wait_until_empty_after_push = other.wait_until_empty_after_push;
        if (owned_frame)
            frame = &*owned_frame;
        return *this;
    }
};

struct GxmNotificationWait {
    SceUID thread_id = 0;
    Address address = 0;
    uint32_t target_value = 0;
};

struct MemoryMapInfo {
    Address offset;
    std::uint32_t size;
    std::uint32_t perm;
};

struct GxmState {
    SceGxmInitializeParams params;

    Queue<DisplayCallback> display_queue;
    SceUID display_queue_thread;
    std::atomic<uint64_t> display_queue_restore_generation{ 1 };
    std::mutex display_queue_waiters_mutex;
    std::vector<SceUID> display_queue_waiters;
    std::deque<PendingDisplayCallback> pending_display_callbacks;
    std::atomic<uint64_t> notification_wait_restore_generation{ 1 };
    std::mutex notification_waits_mutex;
    std::vector<GxmNotificationWait> notification_waits;

    // global timestamp used by sync objects
    std::atomic<uint32_t> global_timestamp{ 1 };
    // last display operation, as given by the global timestamp
    uint32_t last_display_global = 0;

    Ptr<uint32_t> notification_region;

    std::mutex sync_objects_mutex;
    std::unordered_set<SceGxmSyncObject *> sync_objects;

    std::map<Address, MemoryMapInfo> memory_mapped_regions;
    std::mutex callback_lock;
    Address immediate_context = 0;
    std::unordered_map<SceGxmContext *, Address> deferred_contexts;
    std::unordered_map<SceGxmRenderTarget *, Address> render_targets;

    void deinit() {
        if (display_host_thread.joinable())
            display_host_thread.join();

        {
            const std::lock_guard<std::mutex> lock(sync_objects_mutex);
            sync_objects.clear();
        }

        memory_mapped_regions.clear();
        display_queue.reset();
        params = {};
        display_queue_thread = 0;
        global_timestamp = 1;
        last_display_global = 0;
        notification_region = Ptr<uint32_t>(0);
        immediate_context = 0;
        deferred_contexts.clear();
        render_targets.clear();
    }
};

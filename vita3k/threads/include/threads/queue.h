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

#ifndef queue_h
#define queue_h

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

template <typename T>
class Queue {
public:
    // default value: unlimited
    unsigned int maxPendingCount_ = -1;

    std::unique_ptr<T> top(const int ms = 0) {
        T item{ T() };
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            if (ms == 0) {
                while (!aborted && queue_.empty()) {
                    condempty_.wait(mlock);
                }
            } else {
                if (queue_.empty()) {
                    condempty_.wait_for(mlock, std::chrono::microseconds(ms));
                }
            }
            if (aborted || queue_.empty()) {
                return {};
            }

            item = queue_.front();
        }
        return std::make_unique<T>(item);
    }

    std::unique_ptr<T> pop(const int ms = 0) {
        T item{ T() };
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            if (ms == 0) {
                while (!aborted && queue_.empty()) {
                    condempty_.wait(mlock);
                }
            } else {
                if (queue_.empty()) {
                    condempty_.wait_for(mlock, std::chrono::microseconds(ms));
                }
            }
            if (aborted || queue_.empty()) {
                return {};
            }

            item = queue_.front();
            queue_.pop();
        }
        cond_.notify_all();
        return std::make_unique<T>(item);
    }

    void push(const T &item) {
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            while (!aborted && queue_.size() == maxPendingCount_) {
                cond_.wait(mlock);
            }
            if (aborted) {
                return;
            }
            queue_.push(item);
        }
        condempty_.notify_one();
    }

    bool try_push(const T &item) {
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            if (aborted || queue_.size() == maxPendingCount_) {
                return false;
            }
            queue_.push(item);
        }
        condempty_.notify_one();
        return true;
    }

    size_t size() {
        std::unique_lock<std::mutex> mlock(mutex_);
        return queue_.size();
    }

    bool empty() {
        std::unique_lock<std::mutex> mlock(mutex_);
        return queue_.empty();
    }

    void abort() {
        aborted = true;
        condempty_.notify_all();
        cond_.notify_all();
    }

    void reset() {
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            std::queue<T> empty;
            std::swap(queue_, empty);
            aborted = false;
        }
        condempty_.notify_all();
        cond_.notify_all();
    }

    std::vector<T> snapshot() {
        std::unique_lock<std::mutex> mlock(mutex_);
        std::vector<T> items;
        items.reserve(queue_.size());
        std::queue<T> copy = queue_;
        while (!copy.empty()) {
            items.push_back(copy.front());
            copy.pop();
        }
        return items;
    }

    void replace(const std::vector<T> &items) {
        {
            std::unique_lock<std::mutex> mlock(mutex_);
            std::queue<T> empty;
            std::swap(queue_, empty);
            for (const T &item : items)
                queue_.push(item);
            aborted = false;
        }
        condempty_.notify_all();
        cond_.notify_all();
    }

    void wait_empty() {
        std::unique_lock<std::mutex> mlock(mutex_);
        cond_.wait(mlock, [&]() { return aborted || queue_.empty(); });
    }

    Queue() = default;
    Queue(const Queue &) = delete; // disable copying
    Queue &operator=(const Queue &) = delete; // disable assignment

    std::mutex &get_mutex() {
        return mutex_;
    }

private:
    std::condition_variable cond_;
    std::condition_variable condempty_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::atomic<bool> aborted{ false };
};

#endif /* queue_h */

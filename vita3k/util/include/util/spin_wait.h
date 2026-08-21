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

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(__aarch64__) || defined(_M_ARM64)
#define VITA3K_SPIN_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define VITA3K_SPIN_X86 1
#endif

#if defined(VITA3K_SPIN_X86)
#include <emmintrin.h>
#endif

#if defined(VITA3K_SPIN_ARM64) && defined(_MSC_VER)
#include <intrin.h>
#endif

namespace spin {

/**
 * @brief One backoff step inside a busy-wait loop.
 *
 * On AArch64 the `YIELD` hint retires as a NOP on every core we ship against,
 * so it is not an x86 `pause` equivalent - `ISB` is. It restarts instruction
 * fetch, which is a real stall for the sibling SMT-less core and measured as a
 * net power win in RPCS3's spin loops (PR 18151). See
 * docs/research/20260820-rpcs3-arm64-optimizations-for-vita3k.md.
 */
inline void cpu_relax() {
#if defined(VITA3K_SPIN_ARM64)
#if defined(_MSC_VER)
    __isb(_ARM64_BARRIER_SY);
#else
    __asm__ __volatile__("isb" ::: "memory");
#endif
#elif defined(VITA3K_SPIN_X86)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

/**
 * @brief Frequency of the host's constant-rate counter, in Hz.
 *
 * On AArch64 this is `CNTFRQ_EL0` (19.2 MHz on the Qualcomm parts we target).
 * Spin budgets derived from it are wall-clock stable across cores and SoCs,
 * unlike the x86-tuned iteration constants they replace. Returns 0 when no
 * such counter is available, in which case callers fall back to iteration
 * counts.
 */
inline uint64_t counter_frequency() {
#if defined(VITA3K_SPIN_ARM64)
#if defined(_MSC_VER)
    return _ReadStatusReg(ARM64_CNTFRQ_EL0);
#else
    uint64_t freq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#endif
#else
    return 0;
#endif
}

/**
 * @brief Reads the host's constant-rate counter.
 *
 * Only meaningful when counter_frequency() is non-zero.
 */
inline uint64_t counter_now() {
#if defined(VITA3K_SPIN_ARM64)
#if defined(_MSC_VER)
    return _ReadStatusReg(ARM64_CNTVCT_EL0);
#else
    uint64_t ticks = 0;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
#endif
#else
    return 0;
#endif
}

/**
 * @brief Escalating backoff for a wait whose expected duration is unknown.
 *
 * Spins with cpu_relax() for a fixed wall-clock budget, then hands the core to
 * the scheduler, then sleeps. The spin budget is derived from CNTFRQ_EL0 where
 * available rather than from an iteration count tuned on x86.
 *
 * Usage:
 *   spin::Backoff backoff;
 *   while (!condition())
 *       backoff();
 */
class Backoff {
public:
    /// @param spin_budget_us how long to stay in the ISB/pause spin before yielding.
    explicit Backoff(uint32_t spin_budget_us = 20)
        : spin_budget_us(spin_budget_us) {
        const uint64_t freq = counter_frequency();
        if (freq != 0) {
            deadline = counter_now() + (freq * spin_budget_us) / 1000000;
            use_counter = true;
        }
    }

    void operator()() {
        if (spinning()) {
            cpu_relax();
            ++iterations;
            return;
        }

        // Past the spin budget: the wait is long enough that burning a core is
        // no longer the cheaper option.
        if (yields < max_yields) {
            ++yields;
            std::this_thread::yield();
            return;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        if (sleep_us < max_sleep_us)
            sleep_us *= 2;
    }

    /// Resets the budget, e.g. after the awaited state visibly advanced.
    void reset() {
        iterations = 0;
        yields = 0;
        sleep_us = 50;
        const uint64_t freq = counter_frequency();
        if (freq != 0) {
            deadline = counter_now() + (freq * spin_budget_us) / 1000000;
            use_counter = true;
        }
    }

private:
    bool spinning() const {
        if (use_counter)
            return counter_now() < deadline;
        // No constant-rate counter (x86, or a core that does not expose one):
        // fall back to a plain iteration cap.
        return iterations < fallback_spins;
    }

    static constexpr uint32_t fallback_spins = 1024;
    static constexpr uint32_t max_yields = 32;
    static constexpr uint32_t max_sleep_us = 2000;

    uint32_t spin_budget_us;
    uint64_t deadline = 0;
    bool use_counter = false;
    uint32_t iterations = 0;
    uint32_t yields = 0;
    uint32_t sleep_us = 50;
};

/**
 * @brief Waits for a predicate with the escalating backoff above.
 *
 * Fast-paths the already-satisfied case so an uncontended call costs one
 * predicate evaluation.
 */
template <typename Pred>
inline void wait_until(Pred &&pred, uint32_t spin_budget_us = 20) {
    if (pred())
        return;

    Backoff backoff(spin_budget_us);
    while (!pred())
        backoff();
}

} // namespace spin

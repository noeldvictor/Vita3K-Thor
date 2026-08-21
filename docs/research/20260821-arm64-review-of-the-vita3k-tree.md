# ARM64 review of the Vita3K tree, on AYN Thor hardware

Date: 2026-08-21. Target: AYN Thor, Snapdragon 8 Gen 2 / QCS8550
(1x Cortex-X3 + 2x A715 + 2x A710 + 3x A510, Adreno 740).

This is a review of *our* tree for AArch64-specific waste, done by
disassembling the shipped `libVita3K.so` rather than by reading source and
guessing. Companion to `20260820-rpcs3-arm64-optimizations-for-vita3k.md`,
which ported ideas in from RPCS3; this one looks inward.

Method:

```
llvm-objdump -d --no-show-raw-insn \
  android/app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/libVita3K.so
```

7.68M lines of disassembly, plus the emitted flags from
`android/app/.cxx/RelWithDebInfo/*/arm64-v8a/compile_commands.json`.

## Finding 1 - every atomic in the binary goes through an outline helper

**Fixed.** This is the big one, and it is entirely a build-flag problem.

The NDK's `arm64-v8a` ABI is Armv8.0-A, which predates LSE atomics. Clang's
default response is `-moutline-atomics`: instead of emitting an atomic
instruction, it emits a call to a stub that decides at runtime which
implementation to use.

```
; call site - one atomic decrement of a refcount
  add  x1, x22, #0x8
  mov  x0, #-1
  bl   __aarch64_ldadd8_acq_rel

; the stub
__aarch64_ldadd8_acq_rel:
  bti     c
  adrp    x16, ...                 ; address of the feature flag
  ldrb    w16, [x16, #0x7f8]       ; load it
  cbz     w16, .Lllsc              ; branch on it
  ldaddal x0, x0, [x1]             ;   <- the entire point
  ret
.Lllsc:
  mov     x16, x0
  ldaxr   x0, [x1]
  add     x17, x0, x16
  stlxr   w15, x17, [x1]
  cbnz    w15, .Lllsc
  ret
```

Counted call sites in the shipped library:

| helper | sites |
|---|---|
| `__aarch64_swp1_acq_rel` | 12,074 |
| `__aarch64_ldadd8_acq_rel` | 10,518 |
| `__aarch64_ldadd8_relax` | 1,880 |
| everything else | ~800 |
| **total** | **25,276** |

So a call, a BTI landing pad, an ADRP, a *dependent load*, and a conditional
branch, on a core with three load pipes, to reach one instruction the CPU has
had since Armv8.1. The flag can never change during the process.

Naming a baseline that includes LSE makes clang inline the atomic instead. The
fix is in `CMakeLists.txt`, arm64-v8a only:

```cmake
set(VITA3K_ARM64_BASELINE "armv8.2-a+lse+fp16+dotprod" CACHE STRING ...)
```

Armv8.2-A is deliberately conservative - it holds for every arm64 Android device
from 2017 onward, and the Thor's cores are Armv9. Set the cache variable to
`armv8-a` to get the stock NDK behaviour back.

Measured on the rebuilt library:

| | before | after |
|---|---|---|
| outline-atomic call sites | 25,276 | **682** |
| inline LSE instructions | 30 | **24,552** |
| ll/sc exclusive loads | 30 | 15 |

The 682 that remain are all inside prebuilt vcpkg static libraries - libc++
locale construction, `boost::filesystem` directory iteration, OpenSSL - which
vcpkg builds under its own `arm64-android` triplet and so never see our
`-march`. None are on the emulation hot path. Reaching them would need a custom
vcpkg triplet, which is a bigger change than it is worth right now.

Not chosen: `-mcpu=cortex-x3`. That would also pick an X3-specific *scheduling*
model, and the same code runs on A510 little cores. Baseline features are the
part that is unambiguously right for this device.

## Finding 2 - the once-only log guards are an atomic RMW on the hot path

**Fixed.** This is what those 12,074 `swp1_acq_rel` sites are.

```cpp
#define LOG_ONCE(log_function, ...)         \
    do {                                    \
        static std::atomic_flag has_logged; \
        if (!has_logged.test_and_set())     \
            log_function(__VA_ARGS__);      \
    } while (0)
```

`test_and_set()` with the default ordering is a full sequentially-consistent
read-modify-write, executed *every* time control reaches the site, forever -
not just the first time. And these are not rare: `RET_ERROR` expands to one, so
every HLE stub returning an error pays it, and `TextureCache::upload_texture`
alone contains six.

Once the flag is set the exchange can never succeed again, so a relaxed load
answers the question:

```cpp
if (!has_logged.test(std::memory_order_relaxed)
    && !has_logged.test_and_set())
```

Steady state becomes `LDRB` + `TBNZ`. The RMW still runs on the first pass, so
the "log exactly once" guarantee is unchanged.

## Finding 3 - LTO is configured but never actually on

**Reported, not changed.** `USE_LTO` defaults to `RELEASE_ONLY`, which sets
`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`. That applies to the `Release`
configuration only - and neither build we ship is `Release`:

* the APK's `reldebug` variant passes `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
* the Windows build is `--config RelWithDebInfo`

So in practice the default means "never". Confirmed against the emitted flags:
all 973 translation units compile with `-O2 -DNDEBUG` and no `-flto`.

Turning on ThinLTO for RelWithDebInfo is a real opportunity - Vita3K is
header-heavy and cross-TU inlining across `mem/ptr.h`, `renderer/`, and the HLE
export layer should pay. It is not applied here because it changes link time
and binary layout enough to want its own before/after, not a drive-by.

## Finding 4 - dynarmic leaves exclusive accesses on the slow path

**Reported, needs A/B.** `vita3k/cpu/src/dynarmic_cpu.cpp` sets fastmem but
leaves `config.fastmem_exclusive_access` at its default `false`, so guest
`LDREX`/`STREX` - which the Vita kernel uses for every lock - fall out of the
fastmem arena into `MemoryWriteExclusive*` callbacks. Enabling it (with
`recompile_on_exclusive_fastmem_failure`, already `true`) keeps them inline.

Correctness risk is real (it interacts with the shared `ExclusiveMonitor`), so
this wants a measured A/B on a game that leans on kernel sync, not a blind flip.

Related, also unset: `unsafe_optimizations`. Dynarmic exposes
`Unsafe_UnfuseFMA`, `Unsafe_ReducedErrorFP`, `Unsafe_InaccurateNaN`,
`Unsafe_IgnoreStandardFPCRValue` and `Unsafe_IgnoreGlobalMonitor`. These are the
same class of knob RPCS3 exposes to users. Worth surfacing as a per-game config
toggle rather than enabling globally.

## Finding 5 - nothing sets thread affinity

**Reported.** There is no `sched_setaffinity` / `pthread_setaffinity_np` /
`setpriority` anywhere in `vita3k/`. On a 1+2+2+3 big.LITTLE part that means the
guest CPU threads and the render thread can be scheduled onto an A510, whose
per-clock throughput is a fraction of the X3's, at the scheduler's discretion.

This is not a one-line fix and it can easily make things worse - pinning fights
the kernel's own EAS placement, and getting it wrong costs more than it saves.
See `docs/reference/snapdragon/qualcomm-linux-kernel-guide.pdf` for what the
governor is actually doing before touching it.

## What was checked and found already fine

* **XXH3** (`renderer/src/texture/cache.cpp`, `SceGxm.cpp`) compiles its NEON
  path on aarch64 by default. Texture hashing is not leaving vector width on
  the table.
* **Spin loops.** There is no ad-hoc busy-waiting left in `kernel/` or
  `renderer/`; the one spin site (`sync_primitives.cpp`, msgpipe teardown) uses
  `spin::Backoff` from `util/spin_wait.h`, and the render loop is a proper
  blocking wait rather than a spin.
* **Fastmem vs page tables.** `use_page_table` defaults to false, so the guest
  address space is the fastmem arena and not a per-access table lookup.

## Still unmeasured

Findings 1 and 2 are both "strictly less work for identical behaviour", which is
why they were applied without a benchmark. Neither has been measured on device,
and **no speedup is claimed for either** - same as the spin-backoff work from
20260820, which also remains unmeasured. An on-device A/B across all three is
the obvious next step.

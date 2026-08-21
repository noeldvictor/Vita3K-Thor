# RPCS3's ARM64 work — what transfers to Vita3K-Thor (2026-08-20)

Ported from `xenia-thor/docs/research/20260805-rpcs3-arm64-optimizations-applicable.md`
and re-derived against Vita3K's actual shape, plus a web pass for anything after
that date. **Every item below was re-checked against this tree; several of the
xenia conclusions do not survive the port, and one Vita3K-specific finding is
larger than anything in the original list.**

## Why the mapping is not one-to-one

xenia and RPCS3 both recompile **PowerPC with AltiVec/VMX** through an **LLVM or
hand-written a64 backend**. Vita3K does neither:

| | RPCS3 / xenia-thor | Vita3K-Thor |
|---|---|---|
| guest ISA | PowerPC + VMX/AltiVec | **ARMv7-A + NEON/VFPv4** (Cortex-A9) |
| recompiler | LLVM / hand-written a64 emitter | **dynarmic** (A32 frontend → A64 backend) |
| shaders | host-side codegen | SPIR-V translation, no LLVM |

The guest and the host are the **same ISA family**. That kills several RPCS3
items outright and promotes one that neither xenia nor RPCS3 has.

Device features unchanged from the xenia probe — the Thor is the same QCS8550
(1× Cortex-X3, 2× A715, 2× A710, 3× A510): `asimddp i8mm bf16 fphp asimdhp
atomics lrcpc ilrcpc sha3`, **no SVE/SVE2**. RPCS3's SVE2 PRs still do not apply.

---

## 1. The guest's own backoff hints were being thrown away ⭐ THE ITEM

This is the Vita3K form of RPCS3's `yield`-is-a-NOP finding, and it is worse
here than it was there.

`vita3k/cpu/src/dynarmic_cpu.cpp` sets `config.hook_hint_instructions = true`,
so dynarmic raises an exception for every guest hint. Thor's handler then did:

```cpp
case Dynarmic::A32::Exception::WaitForEvent:
    break;
case Dynarmic::A32::Exception::Yield:
    break;
```

So when a Vita game executed `YIELD` or `WFE` — the standard bodies of
`SceKernel` spinlocks and of every hand-rolled guest spin-wait — Thor paid the
JIT-exit and callback cost and then **returned straight into the loop at full
speed**. A guest spinlock became a flat-out busy loop pinning a host core.

RPCS3's problem was that the *host* `yield` instruction is a NOP. Ours was that
the *guest's* explicit backoff request was discarded entirely. Same waste,
larger surface: it fires on every guest spin, in every title, on both Windows
and Thor.

**Done.** Both hints now call `spin::cpu_relax()`. `SendEvent` /
`SendEventLocal` deliberately still fall through to `break` — those are wake
signals, not waits, and stalling the signaller is exactly wrong.

WFE is allowed to wake spuriously by the ARM ARM (see `docs/reference/arm/`),
so relaxing instead of blocking is semantics-preserving. No guest-visible
behaviour changes.

## 2. `ISB`, not `yield`, is the AArch64 backoff ⭐ HIGH — done

On every core we target, AArch64 `YIELD` retires as a NOP. The effective
x86-`pause` equivalent is **`ISB`**: it restarts instruction fetch, which is a
real front-end stall, and RPCS3 measured it as a net *power* win as well as a
throughput win (PR 18151).

`vita3k/util/include/util/spin_wait.h` is new: `spin::cpu_relax()` emits `ISB`
on AArch64, `_mm_pause()` on x86, and falls back to `std::this_thread::yield()`
elsewhere. Verified with the NDK 27 clang for `aarch64-linux-android28` — `isb`
is emitted, and the x86_64 path builds clean.

### 2a. Spin budgets from `CNTFRQ_EL0`, not x86-derived constants

RPCS3 PR 18055 rescaled spin counts by the ARM timer frequency instead of a
constant tuned on x86. `spin::Backoff` does the same: it reads `CNTFRQ_EL0`
(19.2 MHz on Qualcomm) and `CNTVCT_EL0` to hold a **wall-clock** spin budget
(default 20 µs), then escalates to `std::this_thread::yield()`, then to an
exponentially-growing sleep. On hosts with no such counter it falls back to a
plain iteration cap. Confirmed `mrs CNTFRQ_EL0` / `mrs CNTVCT_EL0` are emitted.

### 2b. All 12 host busy-loops converted

`std::this_thread::yield()` in a `while` loop is a `sched_yield` syscall per
iteration — different from RPCS3's NOP problem, but still the wrong tool: under
load it returns immediately and spins, and it cannot express "this wait may be
milliseconds long".

| site | what it waits for | why it mattered |
|---|---|---|
| `SceGxm.cpp` ×6 | `compile_threads_on` drain | a **shader compile**, i.e. milliseconds — spinning through it wasted a whole core |
| `renderer/src/vulkan/pipeline_cache.cpp:548` | `shader_module == shader_compiling` | same, on the pipeline path |
| `kernel/src/sync_primitives.cpp:2489` | msgpipe `remainingThreads` | carries a `// FIXME busy loop bad` from upstream |

All now use `spin::wait_until` / `spin::Backoff`. There are **no raw
`std::this_thread::yield()` spin loops left** in `vita3k/`.

### 2c. WFE / WFET — RPCS3 tried and rejected them

New since the xenia doc, from Whatcookie's "what didn't make the cut" post:
`WFE` waits on a cache-line write but in practice relies on the periodic event
stream, which **each OS configures differently** (Linux/Android ~100 µs, Apple
~1 µs). That makes wake timing unpredictable and causes a *stampede* — every
core waking at once amplifies contention and loses in the real world despite
good microbenchmarks. `WFET` was unavailable on their device and would still
need the ISB fallback. **Verdict: do not pursue; the ISB ladder above is the
endpoint, not a stepping stone.**

## 3. `fmax`/`fmin` NaN semantics — **does not apply here**

The single largest item in the xenia doc, and it evaporates on the port. It
exists because PowerPC `vmaxfp` NaN behaviour has to be *reconstructed* on a
non-PowerPC host. Vita3K's guest is ARMv7 NEON on an AArch64 host: `VMAX.F32`
maps to `FMAX` with **identical** architected NaN behaviour, and dynarmic
already emits it directly. There is no reconstruction to delete because there
was never any to add.

Do not port the xenia investigation. It is a real open question *there*.

## 4. Host feature detection → LLVM target attributes — **does not apply**

RPCS3 PR 18133 fixed FMA being gated on the CPU *name* containing "cortex",
which silently excluded every Qualcomm core. Vita3K has no LLVM JIT: dynarmic's
A64 backend detects host features itself at runtime. There is no name-based
gate in this tree to fix.

The adjacent knob that *is* real: `dynarmic_cpu.cpp:350` sets
`config.optimizations = cpu_opt ? Dynarmic::all_safe_optimizations :
Dynarmic::no_optimizations`. dynarmic also exposes `Unsafe_UnfuseFMA`,
`Unsafe_ReducedErrorFP`, `Unsafe_InaccurateNaN`, `Unsafe_IgnoreStandardFPCRValue`
and `Unsafe_IgnoreGlobalMonitor`. Those are the "unsafe CPU" options other
dynarmic-based emulators expose, and they are worth real money on ARM hosts —
but each trades guest-visible FP accuracy, so they need a config toggle, a
per-title compatibility note, and A/B evidence. **Deliberately not touched
here**; that is the risky tier.

## 5. Mid-core asymmetry: 3 load ports, 2 vector-arithmetic ports ⭐ still novel

The A715/A710 have three 128-bit load pipes but only two 128-bit FP/ASIMD pipes,
so on the mid cores a materialized constant load can beat computing the value.
Whatcookie's search for a three-input op landed on the
**absolute-difference-and-accumulate** family (two sources accumulating into the
destination), which feeds all three load ports; the mid-cores gained and even
the A510s came out ~16% faster.

**Where this lands in Vita3K** — not the CPU JIT (dynarmic owns that codegen),
but our own hot NEON:
- texture decode / detile / swizzle in `renderer/src/texture/`
- surface format conversion
- the texture-hash path (xxHash over texture bytes)

These are fixed-length buffer walks, which is exactly the shape RPCS3 wired it
into. Integer-only, so it stays inside the standing "dot-product units are
heuristics, never guest FP32 geometry" rule.

## 6. `sha3` three-input bitwise: `EOR3`, `BCAX`, `RAX1`, `XAR`

Present on this device. They fuse three-input bitwise ops that otherwise cost
two instructions, and are useful well outside crypto. In Vita3K the candidates
are the same texture swizzle/detile bit math as #5 — Vita texture tiling is
pure bit interleave. Cheap to try, gated per-path.

## 7. `UDOT`/`SDOT` byte reductions

`asimddp` is present. RPCS3 used `UDOT` with a multiplicand of 1 as a
sum-of-bytes. Vita3K's analogue is texture/vertex-buffer hashing on the cache
path — a genuinely hot loop. Exact integer reduction, so correctness-neutral.

## 8. Inline the guest timer read

RPCS3 inlined guest timer reads into recompiled code rather than calling out per
read. Vita3K's guest polls `sceKernelGetSystemTimeWide` and friends through the
HLE module boundary, which is far more expensive than a `CNTVCT_EL0` read. Worth
measuring how hard real titles poll it before doing anything.

## 9. A510 shared vector unit ⚠️ affects thread placement

Two of the three A510s **share a single 128-bit vector unit**; the third has its
own. NEON-heavy guest threads landing on the shared pair do much worse than the
core count suggests. Vita3K runs up to 4 guest threads plus render, shader-compile
and audio threads, so placement matters.

**Which of cpu0-2 is the exclusive one is still unknown** — the kernel exposes no
cluster grouping and reports all three identically. Determining it needs a
per-core NEON microbenchmark (pin a loop to each of cpu0/1/2, then run pairs; the
sharing pair shows ~half throughput). Do not guess an index.

## 10. Newer than the xenia doc (web pass, 2026-08-20)

- **March 2026**: RPCS3 used `SDOT`/`UDOT` to optimize the SPU `SUMB` instruction,
  and `GB`/`GBH`/`GBB` with `UDOT` — the concrete form of #7.
- **v0.0.42 alpha, 2026-07-31**: ARM64 correctness fixes for PS1 Classics and
  RawSPU; an ARM64 change fixed several hundred titles. Correctness, not speed,
  and SPU-specific — nothing to port.
- Nothing merged between 2026-08-05 and today changes the ordering below.

---

## What was implemented in this pass

| item | status |
|---|---|
| #1 guest `YIELD`/`WFE` hints honoured | **done** |
| #2 `ISB` backoff + `CNTFRQ_EL0` budgets (`util/spin_wait.h`) | **done** |
| #2b all 12 host spin loops converted | **done** |
| #3, #4 | **ruled out** — do not port |
| #5, #6, #7, #8, #9 | open, ordered below |

## Suggested order for the rest

1. **#5 mid-core load-vs-arithmetic** in texture decode/swizzle — the
   differentiator, and nobody else has it.
2. **#7 `UDOT`** in the texture-hash path — free correctness-wise.
3. **#6 `EOR3`/`BCAX`** in tiling bit math — cheap, gated.
4. **#9 A510 vector-unit probe**, then thread affinity.
5. **#8 inline timer read** — measure guest poll rate first.
6. **#4 dynarmic unsafe FP flags** — only behind a config toggle with A/B data.

**Nothing above is a measured Vita3K number.** RPCS3's "~60% faster at ~75% the
power" is *their* claim on *their* workload; it is not ours and must not be
restated as ours. Every item needs a Thor A/B before any number is claimed here.

## Sources

- [RPCS3 optimizations on ARM64: What Didn't Make the Cut — Whatcookie](https://whatcookie.github.io/posts/rpcs3-on-arm64-what-didnt-make-the-cut/)
- [Introducing RPCS3 for arm64 — RPCS3 blog](https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/)
- [RPCS3 boosts PlayStation 3 emulation speed and compatibility on ARM64 — VideoCardz](https://videocardz.com/newz/rpcs3-boosts-playstation-3-emulation-speed-and-compatibility-on-arm64)
- [RPCS3 v0.0.42 ARM64 update — XenoSpectrum](https://xenospectrum.com/en/rpcs3-v0042-arm64-integrity-update/)
- `docs/reference/arm/` — Arm ARM DDI 0487H.a and the Cortex-X3/A715/A710/A510 SWOGs

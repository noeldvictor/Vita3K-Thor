---
name: vita3k-perf-profiler
description: Use for Vita3K Thor performance work, frame pacing, fast-forward speed, audio timing, Android thermal/GPU/CPU profiling, before-after benchmark captures, and quality/performance tradeoff proof.
metadata:
  short-description: Measure Vita3K performance
---

# Vita3K Perf Profiler

Use this skill for speed, frame pacing, fast-forward, audio timing, thermals, or "is this faster/smoother" questions.

## Baseline

Define the metric before changing code:

- Scene/title ID and exact camera/menu state.
- Platform: Windows or AYN Thor.
- Build commit/APK, renderer, resolution scale, custom driver, and debug props.
- Metric: FPS, frame time, stutter count, audio underruns, fast-forward multiplier, CPU/GPU load, thermal throttling, or battery/power proxy.

Record the baseline in SQLite before the optimization attempt.

## Windows

Use Windows first for emulator-core CPU/GPU hot paths when the issue reproduces locally. Capture logs, renderer timing counters, and a burst/video when visual pacing matters. For C++ work, prefer targeted incremental builds:

```powershell
cmake --build build/windows-vs2022 --config RelWithDebInfo --target vita3k --parallel
```

## Android / Thor

Use non-invasive device captures:

```powershell
.\tools\thor_profile_dump.ps1 -Topic <semantic-topic> -TitleId <TITLEID> -RenderTrace -Adb $adb
.\tools\thor_live_debug_stream.ps1 -Topic <semantic-topic> -RenderTrace -Adb $adb
```

When deeper profiling is needed, use available Android tools such as `dumpsys gfxinfo`, `dumpsys SurfaceFlinger`, Perfetto, simpleperf, or GPU vendor profilers. Record the exact command and trace path.

## Rules

- No subjective performance claims without before/after evidence.
- Do not compare runs with different scenes, drivers, render scales, debug props, or thermal states unless the difference is the experiment.
- Fast-forward fixes must measure both emulation speed and audio behavior.
- If a performance change risks quality or compatibility, pair this skill with `vita3k-regression-ledger`.

# Windows Primary Debugger, Android Static Plan

Date: 2026-05-12 18:16:56 America/New_York

## Question

Can Vita3K Thor use Windows as the primary debugger for major emulator issues like the UPPERS renderer glitch, while still compiling the final Android build as a static/packaged app for AYN Thor?

Yes. The clean architecture is **one renderer implementation, multiple debug/package front doors**.

## Current Repo Reality

- `vita3k/renderer/CMakeLists.txt` currently builds `renderer` as a static library.
- `vita3k/CMakeLists.txt` links that same `renderer` target into:
  - Windows/macOS/Linux `Vita3K.exe`
  - Android `libvita3k.so`
- That means the source path is already shared, but the debug loop is expensive because renderer edits require relinking the app.

This is good for correctness, but bad for iteration.

## Research Notes

- CMake supports normal `STATIC`, `SHARED`, and `MODULE` libraries. `MODULE` is specifically for plugin-like libraries loaded at runtime with `dlopen`-style behavior. Source: <https://cmake.org/cmake/help/latest/command/add_library.html>
- CMake object libraries compile source files without making a final archive; other libraries/executables can consume the same compiled objects through `$<TARGET_OBJECTS:...>`. This is useful for building several debug/production packages from one source list. Source: <https://cmake.org/cmake/help/latest/command/add_library.html#object-libraries>
- Android NDK supports CMake through the Gradle `externalNativeBuild` path or direct toolchain-file invocation. Source: <https://developer.android.com/ndk/guides/cmake>
- Android NDK docs recommend the static C++ runtime when all native code is contained in a single shared library, and warn that multiple shared libraries with static runtime can break C++ object/function identity. This supports keeping Android as one packaged `libvita3k.so` unless we intentionally switch the runtime/linking model. Source: <https://developer.android.com/ndk/guides/cpp-support>
- RenderDoc exposes an in-application API for custom capture triggering. It can be detected dynamically and safely ignored when absent, which fits a debug-only Windows/Android capture path. Source: <https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/docs/in_application_api.rst>

## Recommended Architecture

### Layer 1: Shared Renderer Core

Keep renderer code as one source tree and one behavior path:

```text
vita3k/renderer/src/*
vita3k/shader/src/*
vita3k/gxm/*
```

Do not create a Windows renderer that behaves differently from Android. The entire point is to fix Vita/GXM emulation once and get the same fix on Thor.

### Layer 2: Packaging Targets

Use build-target structure, not source forks:

```text
renderer_core OBJECT or source-list target
  -> renderer STATIC for normal Vita3K linking
  -> Vita3K.exe on Windows
  -> libvita3k.so on Android
  -> vita3k-render-replay.exe for desktop capture/replay
  -> optional vita3k-renderdev.dll/MODULE later
```

Android can keep using the static/packaged path:

```text
renderer sources -> renderer static/object -> libvita3k.so -> APK
```

Windows can add faster diagnostic paths:

```text
renderer sources -> render replay harness
renderer sources -> full Vita3K.exe debug build
renderer sources -> optional dev module
```

### Layer 3: Debug Inputs

Windows should not rely only on replaying the full game. It should accept captured renderer inputs:

- quickstate to jump to the game scene
- RenderDoc capture of the bad frame
- Vita3K GXM scene/frame bundle saved under ignored `tmp/`
- shader override by hash
- draw/state override control file

The key idea is that Windows becomes the "wind tunnel." Android remains the road test.

## What This Looks Like In Practice

### Day-To-Day Loop For UPPERS

1. Use Windows Vita3K Vulkan to reach the bad UPPERS scene.
2. Save slot 0 or capture a GXM frame bundle.
3. Iterate renderer fixes on Windows:
   - edit renderer/shader/GXM code
   - rebuild only the affected Windows target
   - relaunch or replay directly
4. Once Windows output is correct, build Android APK.
5. Install on Thor and verify on Adreno/Turnip.

### If The Bug Reproduces On Windows

Use Windows as primary. This applies to UPPERS because the artifact reproduces in Windows Vulkan too.

### If The Bug Is Android/Adreno Only

Use Android as primary, but still keep Windows tools useful for:

- shader translation sanity checks
- GXM command dumps
- comparing render-state interpretation
- source-level debugging of shared code

Then validate with:

- ADB screenshots/logcat
- Turnip/custom driver selection
- Android Vulkan/RenderDoc capture if needed
- Thor profile dump

## Why Not Just Make Renderer A DLL Right Now?

A Windows-only renderer DLL sounds attractive because we could replace `vita3k_renderer.dll` and restart quickly. The problem is that Vita3K's renderer is not isolated behind a small ABI today. It touches:

- `MemState`
- `GxmRecordState`
- shader cache
- texture/surface caches
- config
- display state
- Vulkan state/lifetimes
- filesystem/cache paths

A C++ DLL boundary across all of that is fragile. It also does not map cleanly to Android if Android continues as one static/runtime-safe library.

So the better order is:

1. Capture/replay harness first.
2. Refactor small pure translation pieces into testable units.
3. Only then consider optional Windows dev module loading.

## Fastest Useful Pieces To Build

### 1. Control-File Runtime Commands

Extend the existing render-control file:

```text
action=save_state
action=load_state
action=pause
action=resume
action=renderdoc_capture
```

This lets Codex save the exact UPPERS scene while the user plays. On Android, the same idea can be mapped to app-private files or ADB properties.

### 2. Shader Override Folder

Add a debug-only shader override lookup:

```text
shader_overrides/<TITLEID>/<shader_hash>.spv
shader_overrides/<TITLEID>/<shader_hash>.glsl
```

This helps when the suspected bug is shader translation. We can test a shader hypothesis without recompiling the whole emulator. Final fixes still go into the shader recompiler.

### 3. GXM Frame Bundle

Add capture of one scene/frame:

```text
tmp/render-captures/PCSG00633/uppers-glitch-*.thorframe
```

Bundle should include:

- render target state
- draw list
- vertex/index slices
- uniform buffers
- texture descriptors and texture bytes
- shader hashes/GXP/SPIR-V paths
- surface metadata

Do not commit these captures because they may contain commercial game data.

### 4. Replay Harness

Add a desktop-only target:

```text
vita3k-render-replay.exe --capture tmp/render-captures/...thorframe
```

It replays the frame directly through the same Vulkan renderer code. That avoids game boot, intro movies, saves, controller input, and Android install time.

### 5. Separate Windows Ninja Build

Use an ignored `build/windows-ninja` tree for faster command-line iteration. Keep Android using Gradle/NDK exactly as production expects.

## Android Compatibility Rules

- Keep Android production as a single packaged native library unless we deliberately change runtime strategy.
- Do not require runtime-loaded renderer plugins on Android.
- Any debug controls added for Windows should be guarded and portable:
  - Windows: control file
  - Android: app-private file, ADB property, or logcat-triggered debug tool
- Renderer fixes must remain in shared renderer/GXM/shader code, not Windows-only code.
- After a Windows fix, always run the Android APK on Thor/Turnip before calling it done.

## Proposed Next Decision

The next practical step should be:

1. Add control-file runtime actions for save/load/pause/resume.
2. Use that to save UPPERS at the glitch scene after the movie.
3. Add RenderDoc capture trigger support behind the same control file.
4. Start designing the `.thorframe` GXM scene capture format.

This gives us speed immediately and creates the path to the real prize: renderer replay without playing the game every time.

# Renderer Debug Loop Options

Date: 2026-05-12 18:09:02 America/New_York

## Goal

Speed up UPPERS renderer debugging without breaking Android/AYN Thor compatibility. The current bottleneck is not just build time; it is also manual repro time: relaunching the game, sitting through the intro/movie path, and navigating back to the scene.

## Current Architecture

- `vita3k/renderer/CMakeLists.txt` builds `renderer` as a static library.
- `vita3k/CMakeLists.txt` links `renderer` into `Vita3K.exe` on Windows and into Android's `libvita3k.so`.
- Because renderer code is statically linked, changing `scene.cpp`, `texture.cpp`, `pipeline_cache.cpp`, etc. requires rebuilding `renderer.lib` and relinking the final Vita3K binary.
- Measured local Windows timings on this repo:
  - no-op full `vita3k` target through Visual Studio generator: about `10.6s`
  - no-op `renderer` target: about `12.5s`
  - direct `vita3k.vcxproj` relink with `BuildProjectReferences=false`: about `7.7s`

## External Notes

- CMake already supports the Ninja Multi-Config generator, which can build named configs from one build tree and usually has less project overhead than Visual Studio/MSBuild for command-line iteration: <https://cmake.org/cmake/help/latest/generator/Ninja%20Multi-Config.html>
- CMake object libraries can compile sources to object files for reuse by other targets without producing a normal archive first. This is useful if we split renderer core objects from platform packaging later: <https://cmake.org/cmake/help/latest/command/add_library.html#object-libraries>
- CMake has `target_precompile_headers`, useful if repeated renderer compiles are dominated by heavy includes: <https://cmake.org/cmake/help/latest/command/target_precompile_headers.html>
- MSVC supports incremental linking with `/INCREMENTAL`; this helps relink speed when the binary shape allows it: <https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally>
- MSVC supports `/MP` for parallel compilation; this repo already enables `/MP` for the Visual Studio generator: <https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes>
- `sccache` supports MSVC-style compiler caching and can help when switching branches or rebuilding repeated compile units: <https://github.com/mozilla/sccache>
- RenderDoc can capture and inspect Vulkan frames and has an in-application capture API. It is most useful for pipeline/shader/state inspection and shader experiments, not for C++ renderer source hot-reload: <https://renderdoc.org/docs/in_application_api.html>

## Option 1: Fastest Today, No Architecture Change

Use the Windows build as the first truth source for renderer bugs that reproduce outside Android.

Loop:

1. Save/load directly to the UPPERS glitch scene.
2. Edit renderer code.
3. Build only `renderer`.
4. Relink only `Vita3K.exe` with project references disabled.
5. Restart Windows Vita3K and load the quickstate.
6. Only when the fix looks right on Windows, build/install Android.

Commands:

```powershell
cmake --build build\windows-vs2022 --config RelWithDebInfo --target renderer -- /m /nr:false
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' build\windows-vs2022\vita3k\vita3k.vcxproj /p:Configuration=RelWithDebInfo /p:Platform=x64 /p:BuildProjectReferences=false /m /nr:false /v:minimal
```

Pros:

- Keeps Android untouched.
- Uses the existing binary layout.
- Can be used immediately.

Cons:

- Still requires relaunching Vita3K because renderer code is compiled into the EXE.
- Still too slow if we must manually replay minutes of intro content.

## Option 2: Add Runtime Control-File Commands

Extend the existing Windows renderer control file so Codex can trigger quick actions while the user is in-game:

```text
action=save_state
action=load_state
action=pause
action=resume
```

This should share logic with the OSD `Save State 0` and `Load State 0` buttons. It should work on Windows first and later map cleanly to Android via app-private file or `setprop`/ADB trigger.

Pros:

- Eliminates manual OSD navigation for saving the exact repro point.
- Lets Codex save the state as soon as the user says "ready".
- Android-compatible as a concept because it is just a runtime command input, not a platform-specific renderer change.

Cons:

- Current quickstate is still experimental. Same-session load is more reliable than full app-restart load, especially if AVPlayer/movie threads are present.
- We should save after the intro/movie and once the game is in regular 3D gameplay.

## Option 3: Build A Faster Windows Dev Tree

Create a separate ignored `build/windows-ninja` or `build/windows-ninja-clang` tree and compare:

```powershell
cmake --preset windows-ninja
cmake --build --preset windows-ninja-relwithdebinfo --target vita3k
```

If ClangCL and `lld-link` are available, also test `windows-ninja-clang`.

Pros:

- Does not affect Android.
- Cleaner command-line incremental builds than Visual Studio generator.
- Can coexist with current `build/windows-vs2022`.

Cons:

- First configure/build costs time and disk.
- Need to verify Boost/external dependencies and debug behavior match the current VS build.

## Option 4: Compile-Time Dev Flags, Runtime Switches

For renderer hypotheses, compile several experimental branches once and toggle them live:

- force/disable color blend for matching draw/hash
- alpha visualize or alpha threshold mode
- texture swizzle override for a specific texture hash/address
- depth/cull/write-mask override
- surface clear/load/store behavior override
- shader hash replacement or shader debug dump

Pros:

- One compile can test multiple theories live.
- Works on both Windows and Android if controls read from file/properties.
- Very useful for UPPERS because draws `76-77` are already isolated.

Cons:

- Diagnostic only. Final fixes still need correct emulator behavior, not per-game hacks.
- Too many toggles can become confusing unless reports record exactly what was tested.

## Option 5: RenderDoc / Graphics Capture Path

Use RenderDoc on Windows first for the UPPERS frame:

- capture the bad frame
- inspect pipeline state for draws `76-77`
- inspect textures, alpha, render target, blend state, depth/stencil state
- compare shader output and resource bindings
- try shader-only experiments where possible

Pros:

- Faster than guessing from logs.
- Great for shader/blend/texture questions.
- Can also support Android captures later, but Windows is easier for first-pass diagnosis.

Cons:

- RenderDoc cannot hot-reload arbitrary C++ renderer code.
- If the bug is in GXM command interpretation or memory lifetime, RenderDoc shows symptoms but not the whole cause.

## Option 6: Optional Windows Renderer DLL, Keep Android Static

Split renderer packaging into:

- shared renderer core source used by all platforms
- normal static link for Android production
- optional Windows dev-only `vita3k_renderer_vulkan.dll` loaded by `Vita3K.exe`

Pros:

- Could reduce Windows iteration to rebuild/copy one DLL plus restart.
- Keeps Android production path stable.

Cons:

- Big refactor. Current renderer API is not a small plugin boundary; it crosses `MemState`, `GxmRecordState`, shader cache, config, texture cache, surface cache, and app lifetime.
- ABI boundaries in C++ are fragile unless we design a C API or stable vtable.
- Android can load `.so` libraries, but hot-swapping native code on-device is not the same as replacing a Windows DLL. Production Android should probably stay statically/packaged-linked until the boundary is mature.

Recommendation: do not start here. It is useful after we know the renderer subsystem boundaries better.

## Option 7: GXM Scene/Frame Replay Harness

This is the most valuable long-term loop.

Add a debug capture mode that records enough Vita-side renderer input for one bad scene/frame:

- render target metadata
- GXM draw records
- shader hashes and GXP/SPIR-V linkage
- referenced vertex/index/uniform memory slices
- referenced texture descriptors and texture memory slices
- surface/cache metadata needed by the Vulkan renderer

Then build a tiny desktop test runner, for example `vita3k-render-replay.exe`, that replays that scene directly into the renderer without booting UPPERS.

Pros:

- Solves the manual repro problem at the root.
- Lets us iterate renderer fixes without game boot, movie playback, controller input, or Android install.
- Android-compatible because it preserves the same renderer core and same captured GXM data. The replay harness can be desktop-only while the fix remains shared.
- Creates durable bug artifacts for future regressions.

Cons:

- Medium-to-large engineering task.
- Need careful memory/surface capture so the replay is faithful enough to reproduce the bug.
- Must not commit commercial game data captures; keep captures ignored under `tmp/`.

Recommendation: this should become the main serious renderer-debug investment after the immediate quickstate/control-file work.

## Recommended Order

1. Add control-file save/load/pause/resume commands so Codex can save the exact UPPERS repro point.
2. Save a quickstate after the movie, at the glitch area, while gameplay is active.
3. Use targeted Windows build/relink commands and skip Android until Windows looks fixed.
4. Try Ninja Multi-Config or ClangCL/lld in a separate build tree and compare timings.
5. Add more runtime renderer toggles for isolated draws `76-77`.
6. Capture the UPPERS bad frame in RenderDoc.
7. Build the GXM scene/frame replay harness.
8. Only after a Windows-renderer fix is credible, build/install Android and verify on Thor/Turnip.

## Questions For The User

1. Are you okay with a separate ignored `build/windows-ninja` or `build/windows-ninja-clang` tree for faster local iteration?
2. Should the next code change be the control-file quickstate commands so Codex can save/load the exact scene without you opening the OSD?
3. Are you willing to use RenderDoc on Windows for this bug, or should we keep the loop entirely inside Vita3K logs/screenshots for now?

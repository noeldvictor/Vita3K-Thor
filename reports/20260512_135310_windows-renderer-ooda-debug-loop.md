# Windows Renderer OODA Debug Loop - 2026-05-12 13:53:10

## Goal

We need a faster renderer-debug loop for Windows so a visible bug like UPPERS' blue geometry can go from "I see it in-game" to "we know the exact draw/state/shader that caused it" without rebuilding, reinstalling, or replaying long intros over and over.

The winning pattern from other mature emulators is simple: capture the graphics workload, replay it locally, bisect the render commands, and label every object/draw so external GPU tools are useful instead of noisy.

## Research Signals

- RenderDoc has an in-application API that can be loaded opportunistically at runtime and used to start/end captures at precise points. It can also set capture file paths, titles, comments, and launch/show the replay UI when connected.
- GFXReconstruct captures Vulkan API calls through a layer and replays them later with `gfxrecon-replay`. Replay can generate screenshots, replace shaders, validate, extract shaders, convert captures to JSON lines, compress captures, optimize trimmed captures, and dump resources for selected draw/dispatch command indices.
- `VK_EXT_debug_utils` lets us give Vulkan buffers/images/pipelines human names and insert command-buffer labels. Those labels are visible in external tools, and GFXReconstruct captures object names/debug markers.
- Khronos validation should be run as targeted passes, not all at once: Core, GPU-Assisted, Synchronization, Best Practices, and Debug Printf each answer different questions and heavy combinations can slow or distort the repro.
- Dolphin's FIFO Player is the closest emulator precedent: users can record a graphics log, developers can replay it without the game ISO, inspect individual GPU commands, and restrict output to selected objects.
- PCSX2 asks users to provide a GS dump for graphical glitches. The important lesson: a graphics bug report should include the minimum GPU state needed for developers to reproduce the bug, plus logs.
- PPSSPP exposes developer tools, debug overlays, framedump tests, downloadable frame dumps, remote debugging, logging channels, and graphics-driver tests. That supports the same direction: build debug affordances into the emulator, not just around it.
- Nsight Graphics and Radeon GPU Profiler are useful second-line tools, especially when vendor-specific GPU timing, shader debugging, or marker-based workload analysis matters. They should consume our labels rather than be our first and only debugging path.

## Proposed OODA Loop

### Observe

Add a one-button "Capture Renderer Bug" action on Windows, later mirrored on Thor:

- screenshot of current frame
- `config.yml` copy and active renderer settings
- title ID, title name, frame counter, scene counter
- GPU name, driver version, Vulkan version, enabled extensions
- last 5-10 seconds of renderer/log output
- optional RenderDoc `.rdc` if launched under RenderDoc
- optional GFXReconstruct `.gfxr` if layer is enabled
- Vita3K-native GXM scene trace once implemented

This turns a live play session into a debug bundle immediately. The user should not need to explain "blue slab in alley"; the bundle should carry the frame and state.

### Orient

Automate first-pass classification:

- compare captured screenshot against known-good/known-bad images with SSIM or perceptual hash
- parse the trace for scene count, draw count, render target formats, shader IDs, texture IDs, index ranges, and unusual viewport/scissor values
- summarize "which settings changed the artifact" in a table
- attach the relevant screenshots and logs to the report

For UPPERS we already know:

- bug reproduces on Windows Vulkan, so it is not Android-only
- disabling double-buffer memory mapping did not fix it
- a quick max-index patch did not fix it
- high-accuracy mode still needs a clean check at the bad scene

### Decide

Use a scripted matrix instead of hand-testing one knob at a time:

| Axis | Values | Why |
| --- | --- | --- |
| Accuracy | normal, high accuracy | Is this a known approximate-rendering failure? |
| Memory mapping | double-buffer, disabled | Already likely ruled out, but keep it in regression matrix. |
| Backend | Vulkan, OpenGL if usable | Separates Vulkan backend bug from shared GXM translation bug. |
| Texture viewport / direct frag color | on/off where available | Likely suspects for weird full-screen/large geometry artifacts. |
| Validation | core, sync, GPU-assisted, best-practices, debug-printf | Catch API misuse without drowning in overhead. |
| Driver | Windows PC GPU, Thor Turnip | Separate emulator bug from driver bug. |

The matrix runner should produce a single markdown table and screenshot grid so the next action is obvious.

### Act

Build a draw-bisect harness:

- run a captured frame
- skip draw ranges by scene/draw index/hash
- save screenshot after each replay
- binary-search the first draw/object that introduces the blue geometry
- dump the inputs for that draw: vertex/index buffers, shader IDs, constants/uniforms, textures, render target state, viewport/scissor, primitive type

Once we know the exact draw, we can inspect it in RenderDoc/GFXReconstruct and patch the renderer with far less guesswork.

## Two-Layer Capture Strategy

### Layer 1: Vulkan Capture Now

This is the immediate loop because it uses existing tools:

1. Launch Vita3K-Thor from RenderDoc or with GFXReconstruct enabled.
2. Hit the bad UPPERS scene.
3. Press "Capture Renderer Bug."
4. Reopen `.rdc` in RenderDoc for draw/pipeline/resource inspection.
5. Replay `.gfxr` with screenshots to validate changes without the game running.
6. Use `gfxrecon-extract`, `gfxrecon-convert --format jsonl`, `gfxrecon-replay --replace-shaders`, and resource dumps for selected command indices.

This is good for "what Vulkan work did Vita3K submit?"

### Layer 2: Vita GXM Scene Dump/Replay

This is the real speed breakthrough. Vulkan captures are useful but they are after Vita3K has already translated the Vita GPU state. We also need a native emulator-level dump, closer to Dolphin FIFO logs or PCSX2 GS dumps.

Add a `vita3k-gxmreplay` style tool or CLI mode that can replay a single captured Vita GXM scene/frame without booting the game:

- raw GXM command/scene boundaries
- render target state
- shader program IDs plus GXP/shader metadata
- vertex/index buffer snapshots
- texture/surface snapshots
- uniform/constant buffers
- scissor, viewport, depth/stencil/blend state
- memory ranges referenced by the scene
- title ID, module, frame/scene/draw counters

Target loop: edit renderer code, build replay tool, run one UPPERS frame, screenshot, compare. That should be seconds, not minutes.

## Implementation Plan

### Phase 0: Today-Scale

- Add `--renderer-debug-dir <path>` and `VITA3K_RENDER_CAPTURE_DIR`.
- Add a Windows OSD/dev menu action: `Capture Renderer Bug`.
- Add `VK_EXT_debug_utils` labels around scenes, passes, render targets, and draws.
- Add draw labels like `PCSG00633 frame=123 scene=5 draw=186 prim=triangles shader=abcd rt=main`.
- Add a draw-skip filter:
  - `VITA3K_RENDER_SKIP=PCSG00633:scene=5:draw=100-200`
  - `VITA3K_RENDER_STOP_AFTER=PCSG00633:scene=5:draw=186`
- Add scripts:
  - `tools/windows/capture-renderdoc.ps1`
  - `tools/windows/capture-gfxrecon.ps1`
  - `tools/windows/replay-gfxrecon-screenshots.ps1`
  - `tools/windows/render-matrix.ps1`

### Phase 1: Fast External Replay

- Save debug bundles in `reports/captures/<title-id>_<semantic-name>_<datetime>/`.
- GFXReconstruct replay should emit screenshot frames into that bundle.
- Use `gfxrecon-info` and `gfxrecon-convert --format jsonl` to find draw command indices.
- Use GFXReconstruct resource dumping for the suspicious command indices.
- Use shader replacement during replay for shader-only experiments.
- Add a simple screenshot diff script to classify pass/fail automatically.

### Phase 2: Emulator-Native GXM Replay

- Implement a compact GXM frame dump format.
- Add a standalone replay path that initializes renderer only, loads the dump, renders one frame, writes PNG.
- Support draw/object range filtering in the replay tool.
- Make the dump safe to share for debugging by keeping it focused on the minimum frame resources, similar in spirit to FIFO/GS dumps.
- Add CI-style regression snapshots for known renderer bugs once fixed.

## Windows Live Debug Workflow

Fast human loop:

1. User plays on Windows.
2. At the glitch, press OSD capture.
3. Codex inspects the bundle and capture.
4. Codex patches renderer code.
5. Rebuild Windows only.
6. Re-run the captured replay or launch directly into the saved scene.
7. If fixed on replay, verify live game.
8. If fixed on Windows, build/install Thor and test on device.

Best-case inner loop after GXM replay exists:

1. edit renderer code
2. build replay target
3. replay UPPERS dump
4. screenshot diff
5. repeat

That should feel like unit testing the renderer instead of replaying a game.

## UPPERS Next Move

The next practical fix path:

1. Finish the high-accuracy check at the same alley scene.
2. Add debug labels and draw skip/stop filters.
3. Capture the bad frame with RenderDoc or GFXReconstruct.
4. Bisect by draw range until the first bad draw is known.
5. Dump the draw's vertex/index buffers, shader constants, textures, and render target state.
6. Inspect whether the geometry is:
   - bad vertex decode
   - bad index bounds
   - bad transform/viewport
   - bad render target aliasing
   - bad shader constant upload
   - bad depth/stencil/blend state
   - a missing Vita hardware quirk

Ghidra is still useful if the bad draw depends on game-side code behavior, but it should not be the first move. First we need the exact bad graphics command and state. Reverse engineering the game before isolating the draw is slower than necessary.

## What This Changes

Current loop:

- boot game
- wait through scene
- see bug
- guess renderer area
- patch
- rebuild
- repeat

Target loop:

- capture once
- replay exact frame
- bisect exact draw
- inspect exact resources
- patch with evidence
- regression-test the frame forever

That is the difference between "try stuff" and an actual renderer lab.

## Source Notes

- RenderDoc in-application API: https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/docs/in_application_api.rst
- RenderDoc Python API overview: https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/docs/python_api/index.rst
- GFXReconstruct desktop Vulkan capture/replay: https://vulkan.lunarg.com/doc/view/latest/windows/capture_tools.html
- GFXReconstruct resource dumping: https://vulkan.lunarg.com/doc/view/latest/windows/vulkan_dump_resources.html
- GFXReconstruct buffer-device-address replay portability: https://www.lunarg.com/improving-replay-portability-initial-support-for-buffer-device-address-rebinding-in-gfxreconstruct/
- GFXReconstruct capture-file contents: https://www.lunarg.com/mastering-gfxreconstruct-part-4/
- Khronos validation layer settings: https://vulkan.lunarg.com/doc/view/1.4.304.0/linux/khronos_validation_layer.html
- Vulkan synchronization guide: https://docs.vulkan.org/guide/latest/synchronization.html
- `VK_EXT_debug_utils`: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_debug_utils.html
- Dolphin FIFO Player overview: https://github.com/dolphin-emu/dolphin/wiki/FIFO-Player-Overview
- PCSX2 GS dump reporting guide: https://pcsx2.net/docs/troubleshooting/identify/
- PCSX2 debugger docs: https://pcsx2.net/docs/advanced/debugger/
- PPSSPP developer tools: https://www.ppsspp.org/docs/development/developer-tools/
- PPSSPP GE overview: https://www.ppsspp.org/docs/psp-hardware/gpu/ge-overview/
- NVIDIA Nsight Graphics overview: https://docs.nvidia.com/nsight-graphics/index.html
- AMD RGP user debug markers: https://gpuopen.com/manuals/rgp_manual/user_debug_markers/

# Vita GPU CPU Ghidra Debug Workflow - 2026-05-12 17:14:12

## Why This Matters

Some Vita3K Thor glitches will not be fixed by toggling renderer settings. A bad frame can come from Vulkan translation, GXM state interpretation, CPU/GPU synchronization, memory mapping, shader patcher behavior, texture upload rules, or a game-specific draw setup that exposes a missing emulator edge case.

The UPPERS blue-geometry issue is a good example: draw skipping isolated the visible glitch to two draws, but that only proves where the artifact appears. The actual fix still needs evidence about vertex streams, index bounds, attribute formats, texture bindings, and surface state.

## Public Research Anchors

- PSDevWiki Vita graphics overview: https://www.psdevwiki.com/vita/Graphics
- VitaSDK `SceGxm` reference entry point: https://docs.vitasdk.org/group__SceGxmUser.html
- Vita3K source and renderer implementation: https://github.com/Vita3K/Vita3K
- Ghidra Vita loader option for legally owned dumps: https://github.com/CreepNT/VitaLoaderRedux

## Practical Debug Ladder

1. Reproduce in Windows first when possible.
   - Use the local Vulkan desktop loop because build/restart is much faster than Android APK rebuild/install.
   - Still verify final changes on AYN Thor because NVIDIA/desktop Vulkan and Adreno/Turnip can diverge.

2. Capture before changing renderer logic.
   - Screenshot of the bad scene.
   - Log title ID, renderer backend, resolution multiplier, mapping mode, custom driver, and whether the issue is Windows-only or Android-visible.
   - Capture `ThorRenderTrace` and, for isolated draws, `ThorRenderDump`.

3. Isolate draw scope.
   - Use `skip=rt=<width>x<height>:draw=<range>` to find the smallest bad draw range.
   - Use shader hashes and render target dimensions to keep the filter stable across nearby frames.

4. Dump state instead of guessing.
   - Vertex/index stream addresses, strides, required sizes, index max.
   - Attribute register, shader location, GXM format, component count, computed Vulkan byte size, and regformat handling.
   - Texture slot addresses, formats, dimensions, types, stride, mip count, palette, filters, and address modes.
   - Surface color/depth/stencil addresses and draw-state flags.

5. Only then choose a fix area.
   - Vertex stream lifetime or trap sizing.
   - Attribute format/regformat conversion.
   - Index bounds or draw primitive interpretation.
   - Texture/surface cache aliasing.
   - CPU/GPU sync, mid-scene flush, or memory mapping.
   - Shader translation or GXP analysis.

## When To Use Ghidra

Use Ghidra when emulator traces show that the game is doing something unusual and the missing behavior is not obvious from Vita3K state alone.

Good uses:

- Identify which imported `sceGxm*` APIs and shader-patcher paths the game calls around the bad scene.
- Find whether a specific scene uses precomputed draw/state objects, unusual vertex streams, dynamic shader patching, or texture updates immediately before draw.
- Map a logged draw pattern back to game-side setup logic when repeated traces still do not explain the bad state.

Bad uses:

- Do not patch or redistribute commercial game binaries.
- Do not commit decrypted modules, game data, or proprietary assets.
- Do not treat game-specific reverse engineering as a substitute for emulator correctness unless we are only documenting a temporary diagnostic.

## Current UPPERS Action

The next patch adds a live `dump=` renderer filter. For the known UPPERS glitch, run:

```powershell
.\tools\windows\set-render-debug-control.ps1 -Dump "rt=704x396:draw=76-77" -TraceLimit 512
```

This keeps the bad draws rendering and emits `ThorRenderDump` lines for their state. The goal is to find the renderer subsystem to fix, not to hide the artifact with a skip rule.

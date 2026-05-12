# UPPERS Depth Clamp Renderer Fix

Date: 2026-05-12 18:36-18:37 local

## Change

- Forced Vulkan pipeline `depthClampEnable` to `VK_FALSE` in `vita3k/renderer/src/vulkan/pipeline_cache.cpp`.
- Moved Windows/runtime control-file polling ahead of the paused-frame wait in the game loop so external debug actions keep working after an external pause.
- Documented both rules in `AGENTS.md`.

## Why

UPPERS render trace isolated the visible foreground slab/glitch to render target `704x396`, draws `76-77`. Those draws had sane vertex data, sane uniforms, no blend/discard/depth-replace flags, and a normal `SCE_GXM_DEPTH_FUNC_LESS` depth test. Vulkan was enabling depth clamp globally whenever the physical device supported it, which can turn geometry that Vita/GXM should clip outside the depth range into large clamped foreground surfaces.

## Verification

- Rebuilt Windows renderer and relinked `Vita3K.exe` successfully:
  - `cmake --build build/windows-vs2022 --config RelWithDebInfo --target renderer -- /m /nr:false`
  - `MSBuild ... vita3k.vcxproj /p:Configuration=RelWithDebInfo /p:Platform=x64 /p:BuildProjectReferences=false`
- Launched UPPERS through `tools/windows/start-uppers-render-debug.ps1` with `--thor-render-trace`.
- Captured a post-change window screenshot at `tmp/vita3k-win-debug/uppers-depthclamp-test.png`; the captured frame did not show the large foreground slab.
- Confirmed the runtime-control pause action still works after the loop change.
- Confirmed a follow-up `load_state` action now fires after external pause.

## Remaining Issue

Durable UPPERS quickstate restore is still unsafe after app restart. The state loads from disk, then crashes during restore:

```text
Runtime control action: load_state
Loaded durable quickstate slot 0 for PCSG00633 ...
Exception EXCEPTION_ACCESS_VIOLATION (0xC0000005)
Read violation at address 0x29127FCD940
```

That means the depth-clamp renderer fix is buildable and visually promising, but the saved UPPERS repro state is not yet a reliable app-restart test harness. Next save-state work should focus on restoring host-side/kernel/GPU object references safely or refusing unsafe durable states before memory restore.

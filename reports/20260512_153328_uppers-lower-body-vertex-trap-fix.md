# UPPERS Lower Body Vertex Trap Fix - 2026-05-12 15:33:28

## Context

While debugging UPPERS (PCSG00633) on the Windows Vulkan build, the alley camera-training scene showed a lower-body render issue: torso, arms, and head rendered, but the lower body collapsed into a narrow black/deformed cluster.

## Live Isolation

Renderer live-control was reset and tested with draw skipping:

- Clean baseline: `reports/windows-uppers-baseline-clean_20260512_152145.png`
- `skip=draw=70-91`: removed lower-body geometry and the black junk
- `skip=draw=81-91`: removed lower body outright
- `skip=draw=81-86`: removed most lower-body geometry
- `skip=draw=82-83`: removed the main bad lower-body mass

Trace rows for the affected current scene repeatedly showed:

- Draw `81`: `vhash=659a464ccd9e...`, `fhash=4f72fd7fb72e...`, small textured draw
- Draws `82-86`: `vhash=3dd0dc70c764...`, `fhash=e404b437caab...`, no fragment textures, three vertex buffers, one fragment buffer

That points away from a texture upload failure and toward vertex stream data/state.

## Candidate Fix

Windows was running Vulkan with `memory-mapping: double-buffer`.

In Vulkan double-buffer mapping, `bind_vertex_streams()` recalculated the vertex stream byte range for buffer trapping using the raw GXM attribute declaration:

```cpp
gxm::attribute_format_size(attribute.format) * attribute.componentCount
```

For regformat shaders, the Vulkan pipeline derives the actual input component count/type from `shader::usse::AttributeInformation`. If the trap range is smaller than what Vulkan actually reads, the double-buffer path can preserve stale vertex data at the tail of a stream.

Added `get_vulkan_attribute_byte_size()` in `vita3k/renderer/src/vulkan/scene.cpp` so the double-buffer trap range mirrors Vulkan's regformat attribute sizing, including C10 and matrix-style inputs.

## Build

Windows `RelWithDebInfo` build succeeded after stopping the running emulator that locked `Vita3K.exe`.

Command:

```powershell
cmake --build build\windows-vs2022 --config RelWithDebInfo --target vita3k -- /m
```

## Current Test State

The patched Windows build was restarted with:

```powershell
.\tools\windows\start-uppers-render-debug.ps1 -TraceLimit 512 -LogLevel 0
```

Renderer control is reset to no skip:

```text
trace=1
trace_limit=512
labels=1
skip=
stop_after=
```

Next step: return to the same UPPERS glitch scene and capture a new baseline screenshot/trace to confirm whether the lower-body corruption is fixed.

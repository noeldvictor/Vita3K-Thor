# UPPERS Targeted Render Filter Debug - 2026-05-12 16:22:10

## Context

After the vertex trap sizing fix, UPPERS still shows a large blue geometry/texture slab in the alley camera tutorial scene. A fresh screenshot captured the issue:

- `reports/windows-uppers-post-vertex-trap-fix-visible_20260512_161157.png`

## Isolation Findings

The active visible scene was:

```text
rt=704x396
color_addr=0x624F3000
draws=0-45
```

The large slab survived broad skips of later draw ranges:

- `skip=draw=35-45`
- `skip=draw=20-34`
- `skip=draw=10-19`

Skipping early draw bands made the scene go black:

- `skip=draw=1-9`
- `skip=draw=1-4`
- `skip=draw=1-2`
- `skip=draw=1`

This showed that the existing draw-only filter is too blunt: draw numbers repeat in every render scene, so `skip=draw=1` disables draw 1 in all offscreen/main passes, not only the visible scene.

## Tooling Added

Renderer live-control filters now support:

```text
rt=WIDTHxHEIGHT
vhash=HASH_PREFIX
fhash=HASH_PREFIX
```

Examples:

```text
skip=rt=704x396:draw=1
skip=rt=704x396:vhash=d9a93647:fhash=453c0f42:draw=1
stop_after=rt=704x396:draw=12
```

This should let us isolate the bad draw in the visible render target without also skipping matching draw indices in shadow/offscreen/composite scenes.

## Build

Windows `RelWithDebInfo` build passed after restarting Vita3K:

```powershell
cmake --build build\windows-vs2022 --config RelWithDebInfo --target vita3k -- /m
```

UPPERS has been restarted with trace/labels and no skip filter. Next step: return to the same glitch scene and use targeted filters.

# Windows Renderer Debug Hooks - 2026-05-12 14:26:49

## What Changed

- Added Vulkan debug-label support for renderer captures when `VITA3K_RENDER_LABELS=1` or `VITA3K_RENDER_DEBUG=1` is set.
- Added env-driven draw tracing without needing `--thor-render-trace`:
  - `VITA3K_RENDER_TRACE=1`
  - `VITA3K_RENDER_TRACE_LIMIT=256`
- Added draw-range skipping:
  - `VITA3K_RENDER_SKIP=scene=5:draw=100-200`
- Added stop-after filtering:
  - `VITA3K_RENDER_STOP_AFTER=scene=5:draw=186`
- Added a Windows launcher:
  - `tools/windows/start-uppers-render-debug.ps1`

## Why It Helps

The renderer can now be restarted with labels and trace logging, then we can bisect a bad frame by skipping draw ranges or stopping after a draw. That gives us a faster path from "UPPERS has the big blue geometry" to "draw N in scene M is the first bad command."

## Current Windows Launch

Vita3K was rebuilt as `RelWithDebInfo` and restarted with:

```powershell
.\tools\windows\start-uppers-render-debug.ps1 -TraceLimit 512
```

That uses the high-accuracy config and the local UPPERS zip:

- config: `tmp/vita3k-win-debug/config_highacc.yml`
- game: `tmp/local-games/Uppers (English v0.97)[vita3k].zip`
- trace limit: `512`
- log level: `2` / INFO
- labels: enabled

## Live Check

- Confirmed the UPPERS alley artifact on Windows after the first debug build.
- Screenshot: `reports/windows-uppers-live-debug-hooks_20260512_143406.png`
- Visible failure: large blue geometry slab covers the alley and foreground while the player model, UI prompt, and background still render.
- Fixed a launcher quoting bug that split the UPPERS zip path at the first space.
- Fixed the launcher default log level from WARN to INFO so `ThorRenderTrace` lines are visible during the next check.
- Fixed the custom-config override by generating `tmp/vita3k-win-debug/config_render_debug.yml` with the requested log level before launch.
- Relaunched with `--thor-render-trace` and confirmed `ThorRenderTrace scene` / `ThorRenderTrace draw` lines are now present.

## Next Debug Loop

1. Drive to the glitchy scene.
2. Grab the current scene/draw counts from the `ThorRenderTrace` log.
3. Relaunch with `-StopAfter "scene=N:draw=D"` or `-Skip "scene=N:draw=A-B"`.
4. Repeat with narrowed ranges until the first bad draw is isolated.
5. Inspect that draw in RenderDoc/GFXReconstruct or dump its state from Vita3K.

## Notes

- The first build failed only because the old `Vita3K.exe` process was still running and Windows blocked the linker from overwriting it.
- After closing the old process, the Windows build completed successfully.

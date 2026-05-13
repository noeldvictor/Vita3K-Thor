# Runtime Control Quickstate Actions

Date: 2026-05-12 18:23:37 America/New_York

## Why

Renderer debugging UPPERS is too slow if the user has to manually open the OSD, save/load state, or replay intro/movie content before every test. The Windows renderer debug loop needs external runtime actions that Codex can trigger while the game is running.

## What Changed

- Added `runtime_poll_control_file(emuenv)` in the main game loading/rendering loops.
- The runtime control file path is read from `VITA3K_RUNTIME_CONTROL_FILE`, falling back to the existing `VITA3K_RENDER_CONTROL_FILE`.
- Added `action=` support:
  - `save_state`
  - `load_state`
  - `pause`
  - `resume`
  - `toggle_pause`
  - `open_osd`
  - `close_osd`
- Added `action_id=` support so repeated actions can be distinguished when writing the same command again.
- Updated `tools/windows/set-render-debug-control.ps1` with `-Action` and `-ActionId`.
- Updated `tools/windows/start-uppers-render-debug.ps1` so newly-created control files include blank action fields.
- Updated `AGENTS.md` with the workflow.

## Example

```powershell
.\tools\windows\set-render-debug-control.ps1 -Dump "rt=704x396:draw=76-77" -Action save_state
.\tools\windows\set-render-debug-control.ps1 -Dump "rt=704x396:draw=76-77" -Action load_state
.\tools\windows\set-render-debug-control.ps1 -Action pause
.\tools\windows\set-render-debug-control.ps1 -Action resume
```

## Android Compatibility

The runtime action implementation is platform-neutral. Windows uses the existing control-file environment variable today. Android can later point `VITA3K_RUNTIME_CONTROL_FILE` at an app-private file or route ADB/property-triggered commands into the same action parser without changing quickstate behavior.

## Next

- Windows build passed after closing the previously running `Vita3K.exe`.
- Relaunched UPPERS with the render debug control file active.
- Smoke-tested external pause/resume:
  - `Runtime control paused emulation`
  - `Runtime control resumed emulation`
- Once the game is at the UPPERS glitch scene, trigger `save_state` externally so future renderer tests can start from the repro point.

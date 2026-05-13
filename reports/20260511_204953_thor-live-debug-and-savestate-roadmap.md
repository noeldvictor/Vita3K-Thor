# Thor Live Debug And Save-State Roadmap - 2026-05-11 20:49:53

## Summary

Added a repeatable live ADB debug workflow for AYN Thor testing and clarified the current save-state durability target.

## What Changed

- Added `tools/thor_live_debug_stream.ps1` for active play sessions.
- Updated `tools/thor_adb_debug_capture.ps1` to use device-side `screencap` plus `adb pull`, avoiding unreliable PowerShell binary redirection.
- Expanded `ThorRenderTrace` texture diagnostics in Vulkan texture configure/upload paths.
- Cleaned the README feature-difference section into user-facing features and technical/development goals.
- Updated `AGENTS.md` with the PPSSPP-level save-state target and the live debug process.

## Save-State Reality

Same-session quickstates work for the tested path, but PPSSPP-style durable save/load across app restarts needs more serialized state:

- kernel objects, waits, and thread runtime state
- GPU/display state and renderer caches
- texture/surface cache state
- audio and AVPlayer/movie state
- IO/VFS handles and mounted cartridge state
- timing state and per-game metadata

Until those are serialized, app-restart state loading must fail closed for unsafe cases instead of crashing.

## Renderer Debugging

The next UPPERS graphics pass should run with `--thor-render-trace` or the live debug script's `-RenderTrace` option. The trace now includes texture configure/upload lines with texture address, format, type, stride, upload byte count, hash, staging use, and cache index.

## Validation

- `git diff --check` passed with CRLF warnings only.
- PowerShell syntax parsing passed for `tools/thor_live_debug_stream.ps1` and `tools/thor_adb_debug_capture.ps1`.
- Android `:android:assembleReldebug` passed.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to the connected AYN Thor with `adb install -r`.

## Upstream

Fetched `upstream/master`; no new upstream commits were available beyond the already merged state.

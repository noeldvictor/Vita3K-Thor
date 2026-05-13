# 2026-05-10 16:06:13

## Changes

- Verified the connected AYN Thor is loading the selected Turnip custom driver:
  - `turnip_a8xx` injected successfully.
  - Vulkan device reported as `Turnip Adreno (TM) 740`.
  - Driver version reported as `25.99.99`.
- Expanded VitaCheat runtime support:
  - Static writes: `$0000`, `$0100`, `$0200`.
  - ARM/code writes: `$A000`, `$A100`, `$A200`.
  - Simple level-1 pointer writes ending in `$3300`.
  - `$B200` segment-relative base selection.
- Added JIT cache invalidation when an ARM/code write actually changes bytes.
- Added an in-game runtime status overlay for fast-forward speed and active cheat write count.
- Added `fast-forward-speed-percent`, defaulting to `200`, clamped from `101` to `1000`.
- Replaced the save/load hotkey placeholder with an experimental same-session slot 0 quickstate that snapshots CPU contexts and allocated guest memory pages for the running title.
- Updated `tools/convert_vitacheat.py` and cheat documentation to match the expanded runtime support.

## Save-State Deep Notes

Full save states are not just a memory dump in Vita3K. A reliable per-game slot needs at least:

- Guest RAM and mapped memory contents.
- CPU contexts for every guest thread.
- Kernel thread status, wait objects, callbacks, semaphores, mutexes, event flags, timers, TLS, and loaded module state.
- Renderer/GXM state, queued command buffers, shader/cache state, textures, and display timing.
- Audio queues, IO state, open handles, dialogs, and common-dialog state.

The codebase already has useful pieces: `KernelState::pause_threads()`, `resume_threads()`, CPU `save_context()` / `load_context()`, and allocated memory range validation. This pass implements the first same-session experimental quickstate step. It is not durable across app restarts and does not yet capture GPU/display/audio/IO state, so it needs real-game testing before treating it as reliable.

Suggested save-state sequence:

1. Validate same-session slot 0 on simple 2D games and one heavier 3D game.
2. Add validation: title ID, app version, module layout, memory map, active renderer backend, and driver name must match before load.
3. Add GPU/display/audio snapshots or explicit invalidation/rebuild paths.
4. Only then write durable `.thorstate` files to `states/<TITLEID>/slot0.thorstate`.

## Verification

- `git diff --check` passed.
- `python tools\convert_vitacheat.py tmp\sample_cheats.psv -o tmp\converted_cheats` converted static, ARM, and level-1 pointer examples and preserved unsupported lines.
- Android reldebug APK built successfully with `:android:assembleReldebug` before and after the same-session quickstate patch.

## Remaining Blockers

- No real Vita `.psv` cheat database was available in the repo or on `/sdcard` during this pass, so runtime cheat application still needs a real per-game smoke test.
- Multi-level pointer, condition, button, block, copy/fill, and modifier code types are still intentionally skipped.
- Durable full save/load state is still not implemented; current hotkeys are same-session only.

# OSD Pauses Emulation - 2026-05-11 23:12:14

## Change

- Runtime OSD now pauses through `app::switch_state(emuenv, true)` when it opens.
- Closing/resuming the OSD restores through `app::switch_state(emuenv, false)` only when the OSD performed the pause.
- The OSD no longer exposes a `Resume Threads` button while the menu is open; the middle action now shows a disabled `Paused` state.

## Why

- The previous OSD path paused kernel threads directly, which skipped the normal app pause behavior for audio and Android overlay state.
- A visible `Resume Threads` action also allowed the game to run behind the OSD, which made the menu feel like an overlay instead of a proper pause menu.

## Test Plan

- Build Android `reldebug`.
- Install to AYN Thor.
- Open a running game, tap Back to open the OSD, confirm gameplay/audio pause.
- Press `Resume`/Back/Circle to close the OSD and confirm gameplay resumes.

# 2026-05-10 16:20:37

## Change

- Removed Android toast popups from fast-forward, same-session save state, and same-session load state hotkey actions.
- Kept log messages for debugging.
- Updated `AGENTS.md` to make OSD/overlay feedback the expected path for these runtime actions.

## Rationale

Hotkey feedback should not interrupt gameplay. Fast-forward already has a runtime overlay, and save/load feedback should move into the planned Back/Select OSD.

## Verification

- `git diff --check` passed.
- Confirmed the hotkey toast helper and save/load/fast-forward toast strings were removed from `vita3k/interface.cpp`.
- Android reldebug incremental build passed with `:android:assembleReldebug`.

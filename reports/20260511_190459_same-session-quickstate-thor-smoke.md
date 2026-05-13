# Same-Session Quickstate Thor Smoke - 2026-05-11 19:04:59

## Result

Same-session save/load state works in the current Android reldebug build for a basic live smoke test, but it is not a durable full emulator save-state format yet.

## Thor Test

- Device: AYN Thor
- Package: `org.vita3k.emulator.debug`
- Running title: `PCSG00633`
- Entry point: Runtime OSD
- Actions:
  - Opened OSD with short Android Back.
  - Pressed `Save State 0`.
  - Pressed `Load State 0`.
  - Resumed gameplay.

## Log Evidence

- Save captured: `Captured same-session quickstate slot 0 for PCSG00633`
- Captured guest memory: `456531968` bytes
- Captured thread contexts: `30`
- Load restored: `Restored same-session quickstate slot 0 for PCSG00633`
- App process stayed alive after resume.

## Limitations

- The current implementation stores the slot in emulator process memory only.
- The marker file at `states/<TITLEID>/slot0.same-session.txt` is informational, not a restorable disk state.
- It does not yet serialize kernel objects, GPU/display state, audio, IO handles, or other full emulator state needed for reliable cross-session `.thorstate` saves.

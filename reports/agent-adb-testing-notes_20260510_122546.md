# 2026-05-10 12:25:46 - Agent ADB Testing Notes

## Summary

Updated repository agent instructions to make AYN Thor ADB testing and hardware targeting expectations explicit.

## Changes

- Added an `ADB Thor Testing` section to `AGENTS.md`.
- Documented that built APKs should be installed to the connected AYN Thor with ADB when available.
- Added safeguards for ADB installs: verify device, prefer `adb install -r`, and avoid uninstalling or clearing app data without explicit approval.
- Clarified that Thor Base/Pro/Max specs drive defaults and Thor Lite should be ignored unless specifically requested.

## Verification

- Documentation-only change.

## Remaining Blockers

- No APK was built or pushed in this checkpoint.
- Real-device ADB testing still depends on a connected AYN Thor and a successfully built APK.

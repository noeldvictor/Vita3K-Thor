# 2026-05-10 16:23:01

## Change

- Added a standing `AGENTS.md` rule: after Android-affecting commits with a successful APK build, install the latest APK to the connected Android/AYN Thor with `adb install -r` and record the result.

## Android Push

- Verified connected device: `AYN_Thor`.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` with `adb install -r`.
- Install result: `Success`.

## Notes

- This installed the current APK that includes commit `c082a011` removing fast-forward/save/load Android toasts.

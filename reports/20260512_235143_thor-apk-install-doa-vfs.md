# Thor APK install DOA VFS pass

## Summary

Installed the Android reldebug APK containing commit `47fc72b1` on the connected AYN Thor after the Windows-first DOA Venus cartridge-ZIP VFS fix.

## Build

- Command: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Result: success.
- APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`
- APK size: 52,120,967 bytes.
- Build timestamp: 2026-05-12 23:51:00.
- Note: this APK was built from the current worktree, which still includes existing uncommitted renderer/shader debug edits outside the focused VFS commit.

## Device Install

- Device: AYN Thor (`kalama`)
- Android: 13
- ADB serial: `c3ca0370`
- Install command: `adb install -r android/build/outputs/apk/reldebug/android-reldebug.apk`
- Install result: `Success`
- Package: `org.vita3k.emulator.debug`
- Version: `0.2.1` / versionCode `21`
- Last update time on device: 2026-05-12 23:51:17.

## Launch Proof

- Launch command: `adb shell monkey -p org.vita3k.emulator.debug 1`
- Running PID after launch: `8624`
- Foreground focus on display 0: `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`
- Screenshot proof saved locally only: `tmp/thor-install-proof-20260512_235134.png`

## Next

Open DOA Venus on Thor and capture a fresh renderer-trace profile. If the black screen remains after the `app0:.` fix, classify it as Android/Adreno/presentation or a later renderer-content issue rather than the earlier archive-root crash.

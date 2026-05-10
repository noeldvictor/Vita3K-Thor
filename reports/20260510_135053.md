# 2026-05-10 13:50:53

## Summary

- Added an experimental virtual cartridge launch mode for `.zip` / `.vpk` game archives.
- New CLI flag: `--cartridge`. When used with `content-path`, Vita3K mounts the archive as a read-only virtual game card path instead of installing it into the normal app library.
- Cartridge content is extracted into app-local `ux0/cart/<TITLEID>` and `app0:` file access is redirected there through `IOState::app0_host_path`.
- Runtime app reads, module loads, `sceAppMgrLoadExec`, icon loading, background loading, and current-app `param.sfo` loading now account for the virtual cartridge app0 path.
- Cartridge app0 writes/removes/renames/directory creation are blocked as read-only operations.

## Verification

- Ran `git diff --check`: passed.
- Ran Android build: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`: passed.
- Built APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`.
- Verified ADB target `c3ca0370` as `AYN Thor`.
- Installed APK with `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`: success.
- Launched `org.vita3k.emulator.debug` with `monkey`; `pidof` returned `2361`.

## Notes

- This is the first cartridge-code checkpoint. It gives Vita3K a non-library game-card path, but it still extracts archive files into a virtual cartridge cache for runtime filesystem compatibility.
- A later UI pass should add an Android-facing "Play ZIP as Cartridge" picker so users do not need to pass `--cartridge` manually.
- True compressed ZIP random-access mounting would need a larger archive-backed file handle and directory backend in the IO layer.

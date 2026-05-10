# 2026-05-10 13:27:45

## Summary

- Expanded the Android Turnip driver picker so GitHub release ZIPs from K11MCH1/AdrenoToolsDrivers can be refreshed, labeled with an AYN Thor / Adreno 740 recommendation, downloaded, installed, selected, and cleaned up from the same popup.
- Added cached ZIP handling under app-local `driver_downloads/`, including a "Download ZIP only" path, "Install cached ZIP and select", "Select installed driver", "Delete downloaded ZIP", and optional "Delete ZIP after install".
- Kept driver installs on the existing safe custom-driver extraction path, including relative-only ZIP entry extraction.
- Fixed Windows Android CMake configuration by normalizing `ANDROID_NDK_HOME` and `VCPKG_ROOT` with `file(TO_CMAKE_PATH ...)`.
- Updated `AGENTS.md` with the local APK build recipe, Turnip workflow expectations, ADB Thor testing notes, and the current "play without install" finding.

## Build And Device Verification

- Ran `git diff --check`: passed.
- Bootstrapped vcpkg and installed Android arm64 dependencies: `boost-system`, `boost-filesystem`, `boost-program-options`, `boost-icl`, `boost-variant`, `openssl`, and `zlib`.
- Ran `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`: passed.
- APK built at `android/build/outputs/apk/reldebug/android-reldebug.apk`.
- Verified ADB device `c3ca0370` is `AYN Thor`, device `kalama`, Android `13`.
- Installed APK with `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`: success.
- Package installed as `org.vita3k.emulator.debug`.
- Launched with `adb -s c3ca0370 shell monkey -p org.vita3k.emulator.debug 1`; `pidof org.vita3k.emulator.debug` returned `25736`.

## Play Without Install Finding

- Current Vita3K does not directly run external game archives/folders without first installing or staging them into Vita3K storage.
- `content-path` is documented in code as a `.vpk`/`.zip`/folder path to install and run.
- `--installed-path` / `-r` is for an already-installed app path. A true no-install mode would need separate design work around staging/cache/mount behavior.

## Remaining Notes

- The Thor recommendation is a heuristic based on release/asset names, with preference for recent Turnip Gmem/a7xx-friendly ZIPs and lower priority for debug/beta/a8xx-specific assets.
- No game compatibility claim was made in this pass; only app build, install, and launch were verified.

# 2026-05-10 12:24:21 - Turnip Driver Picker Checkpoint

## Summary

Added a root `AGENTS.md` for Vita3K Thor work rules and implemented an Android GPU settings flow for downloading, installing, and selecting custom Turnip drivers.

## Changes

- Added `AGENTS.md` with source-control, safety, Android/Thor, custom-driver, build, and reporting expectations.
- Reworked Android custom driver install helpers to return the installed driver name.
- Added safe ZIP-entry checks for custom driver extraction to reject absolute paths and `..` traversal.
- Added an in-app `Download Turnip driver` modal in GPU settings.
- The modal fetches K11MCH1/AdrenoToolsDrivers GitHub releases, filters Turnip/Mesa ZIP assets, downloads a selected ZIP, installs it through Vita3K's existing custom driver folder, and selects it for Vulkan.
- Updated README divergence notes to mention the in-app Turnip picker.

## Verification

- Ran `git diff --check`: passed.
- Initialized submodules with `git submodule update --init --recursive`.
- Ran `.\gradlew.bat :android:compileReldebugJavaWithJavac` with bundled JDK and Android SDK: passed.
- Ran `.\gradlew.bat :android:externalNativeBuildReldebug`: blocked before C++ compilation because `VCPKG_ROOT` is not set.

## Remaining Blockers

- Full Android native/APK verification still needs a valid vcpkg checkout and `VCPKG_ROOT`.
- The Turnip picker still needs live-device testing on AYN Thor to prove download, extraction, selection, and reboot/apply behavior end to end.

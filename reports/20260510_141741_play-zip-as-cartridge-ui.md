# Play ZIP as Cartridge UI

## What Changed

- Added a visible device UI path: `File` -> `Play ZIP as Cartridge`.
- The new dialog lets the user pick a `.zip` or `.vpk`, mounts it through the virtual cartridge backend, and offers `Start Cartridge`.
- Launching from this path creates a transient in-memory app entry and does not save the cartridge title into the installed app cache.
- Updated `AGENTS.md` with the on-device menu location and transient-cache expectations.

## Verification

- Built Android reldebug APK successfully:
  - `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- APK produced at:
  - `android/build/outputs/apk/reldebug/android-reldebug.apk`
- ADB device check:
  - Serial: `c3ca0370`
  - Model: `AYN Thor`
  - Android: `13`
- Installed with:
  - `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`
- Launched with:
  - `adb -s c3ca0370 shell monkey -p org.vita3k.emulator.debug 1`
- Runtime proof:
  - `pidof org.vita3k.emulator.debug` returned `11109`.

## Notes

- This is still a virtual cartridge cache: archives are extracted into app-local `ux0/cart/<TITLEID>` and `app0:` is redirected there.
- This is not direct compressed ZIP random-access yet.
- The option is visible only from the app selector state, before a Vita title is already running.

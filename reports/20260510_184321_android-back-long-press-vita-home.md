# Android Back Long Press Vita Home - 2026-05-10 18:43:21

## Goal

Make AYN Thor physical Back behavior less confusing while a Vita title is running:

- Short Back press: toggle the Vita3K runtime OSD.
- Long Back press: route to Vita PS/Home behavior so the running app returns to Vita LiveArea/home flow.

## Change

- Added Android Back hold tracking in `vita3k/interface.cpp`.
- Kept the existing Java `KEYCODE_BACK` to SDL native key forwarding path.
- Added a 400 ms hold threshold. ADB's `input keyevent --longpress KEYCODE_BACK` emits its repeat/down near this boundary, and physical Back should feel responsive without colliding with normal taps.
- On long Back, close the runtime OSD if it is open, then call the same `SCE_CTRL_PSBUTTON` UI navigation path used by Vita PS button handling.
- On short Back release, keep toggling the runtime OSD.
- Updated `AGENTS.md` to document the split.

## Verification

- `:android:assembleReldebug` passed after the input change.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to connected `AYN_Thor` with `adb install -r`.
- Launched Catherine direct ZIP cartridge from `/storage/2664-21DE/Roms/psvita`.
- Short `KEYCODE_BACK` logged `Android Back short press: toggling runtime OSD`.
- Long `KEYCODE_BACK` logged `Android Back long press: routing to Vita PS/Home`.

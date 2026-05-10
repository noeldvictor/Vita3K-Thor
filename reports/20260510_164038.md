# 2026-05-10 16:40:38 - Runtime OSD controller navigation fix

## Changes

- Enabled Dear ImGui keyboard/gamepad navigation during GUI initialization.
- Fixed the SDL3 ImGui backend to resolve a real connected gamepad through player index or SDL gamepad instance IDs instead of opening gamepad `0`.
- Added a small owned-gamepad fallback for ImGui navigation if the controller layer has not already opened the device.
- Focuses the OSD window and defaults focus to the Resume button when the OSD appears.
- Added Circle/B or Escape as an explicit OSD cancel path.
- Updated `AGENTS.md` with the SDL3 gamepad instance-ID requirement.

## Verification

- `:android:assembleReldebug` completed successfully after moving SDL-specific declarations out of the shared ImGui state header.
- Reinstalled `android/build/outputs/apk/reldebug/android-reldebug.apk` to the connected AYN Thor (`c3ca0370`, model `AYN_Thor`) with `adb install -r`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator` with ADB.
- Logcat smoke sample showed Vita3K startup and Turnip driver injection (`Turnip Adreno (TM) 740`) with no `AndroidRuntime` or `FATAL` entries in the sampled lines.

## Remaining test gap

- A real title still needs to be opened on-device to verify D-pad/left-stick focus movement, Cross/A activation, Circle/B cancel, and Back/Select close inside the runtime OSD.

# 2026-05-10 15:49:01

## Changes

- Added first-pass VitaCheat runtime support for enabled `_V1` static write codes: `$0000`, `$0100`, `$0200`, and simple `$B200` main-module segment-relative bases.
- Added `tools/convert_vitacheat.py` to convert `.psv` files into reviewable JSON metadata while preserving unsupported lines for auditing.
- Added `cheats-enabled` config flag and updated cheat documentation with supported formats and safety scope.
- Changed Android virtual cartridge scanning to shallow scan `/sdcard/roms/psvita` and `/storage/emulated/0/roms/psvita`: root archives, one direct child level of archives, and direct extracted cartridge folders.
- Made save/load hotkeys reserve per-game slot 0 under shared `states/<TITLEID>/` while the real serialization backend remains unimplemented.
- Updated `AGENTS.md` with the current cheat/runtime and shallow cartridge scan behavior.

## Verification

- `git diff --check` passed.
- `python tools\convert_vitacheat.py --help` passed.
- Built Android reldebug APK with Gradle: `:android:assembleReldebug` passed.
- Installed APK on connected AYN Thor with `adb install -r android\build\outputs\apk\reldebug\android-reldebug.apk`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator` through ADB.
- ADB confirmed device model `AYN Thor`, Android `13`, hardware `qcom`.
- Runtime smoke log showed Vita3K startup and base/pref paths; no `AndroidRuntime` fatal exception appeared in the sampled log.

## Notes

- Existing device config selected `turnip_a8xx`; that is a user/device setting and not changed by this work.
- The cheat runtime skips unsupported pointer, condition, block, and button-code formats. Those need per-game validation before enabling.
- No third-party cheat database was committed.

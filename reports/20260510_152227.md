# 2026-05-10 15:22:27 - Virtual Cartridges, Cheat Badges, Runtime Hotkeys

## Summary

- Added Android virtual cartridge library scanning.
- Default scan roots:
  - `/sdcard/roms/psvita`
  - `/storage/emulated/0/roms/psvita`
- Compatible `.zip`/`.vpk` archives and extracted folders containing `sce_sys/param.sfo` are listed as virtual cartridges.
- Virtual cartridge entries launch directly as read-only cartridge mounts instead of using Live Area or installing to `ux0/app`.
- Added directory cartridge mounting for extracted folder layouts.
- Added cheat-file detection by title ID and a `C` badge on games with matching `.psv` files.
- Added repo/user cheat roots documentation in `cheats/README.md`.
- Added 200% fast-forward timing:
  - `Select + R1` toggles 100%/200%.
  - The main frame limiter and vblank thread both honor the speed percent.
- Reserved save/load-state shortcuts:
  - `Select + right-stick down` requests save state.
  - `Select + right-stick up` requests load state.
  - These currently toast/log that the save-state backend is not implemented yet.

## Cheat Notes

- VitaCheat databases exist, especially `r0ah/vitacheat`, but I did not vendor public cheat files because redistribution/license status must be confirmed first.
- The badge detects user/repo `.psv` files named by title ID under `cheats/`, `cheats/db/`, shared `cheats/`, and `ux0/vitacheat/db/`.
- Runtime cheat application still needs a parser/interpreter and emulator memory-write integration.

## Build

- Command: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Result: success
- APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`

## Thor Test

- Device: AYN Thor
- Serial: `c3ca0370`
- Install command: `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`
- Install result: success
- Launch command: `adb -s c3ca0370 shell monkey -p org.vita3k.emulator.debug 1`
- Runtime check: `pidof org.vita3k.emulator.debug` returned `6543`

## Remaining Questions

- Which cheat database/source should be imported, and do we have permission to redistribute it in this repo?
- Should translated game variants with the same title ID appear as separate virtual cartridges by filename, or should title ID stay the unique app identity?
- Should virtual cartridge scanning recurse through every subfolder under `/sdcard/roms/psvita`, or should it stay shallower for faster startup?
- Save states need a dedicated serializer plan before claiming support.

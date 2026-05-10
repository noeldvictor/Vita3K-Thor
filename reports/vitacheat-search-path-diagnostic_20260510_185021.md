# VitaCheat Search Path Diagnostic - 2026-05-10 18:50:21

## Finding

Games report no Vita cheats because Vita3K Thor only loads VitaCheat `.psv` files named by title ID, and no matching `.psv` files were found on the connected Thor for the tested titles.

Examples:

- Catherine title ID: `PCSG01179`, expected file: `PCSG01179.psv`.
- Current app data had no `.psv` files under Vita3K Thor storage.
- `/storage/2664-21DE` had emulator cheat folders for other systems, but no VitaCheat `.psv` files in the scanned depth.

## Change

- Moved VitaCheat root discovery into shared `util/cheat_paths`.
- App-grid cheat badges and runtime cheat loading now use the same search list.
- Added Android shared-storage and removable SD-card roots:
  - `/sdcard/cheats`, `/sdcard/cheats/psvita`, `/sdcard/VitaCheat/db`
  - `/storage/<card>/cheats`, `/storage/<card>/cheats/psvita`, `/storage/<card>/VitaCheat/db`
  - `/storage/<card>/Roms/psvita/cheats` and matching lowercase `roms` variants
- Added an OSD "Searched paths" section when no matching VitaCheat file is loaded.
- Updated cheat docs and agent notes.

## Remaining Need

The emulator still needs real, legally usable `.psv` cheat files for the user's title IDs. Public VitaCheat databases should not be committed unless their license/source permits redistribution.

## Verification

- `:android:assembleReldebug` passed.
- Installed the APK to connected `AYN_Thor` with `adb install -r`.
- Created a temporary harmless detection-only file at `/storage/2664-21DE/cheats/psvita/PCSG01179.psv`.
- Launched Catherine direct ZIP cartridge from `/storage/2664-21DE/Roms/psvita`.
- Runtime log confirmed: `Loaded VitaCheat file "/storage/2664-21DE/cheats/psvita/PCSG01179.psv" for PCSG01179`.
- Removed the temporary detection file after the smoke test.

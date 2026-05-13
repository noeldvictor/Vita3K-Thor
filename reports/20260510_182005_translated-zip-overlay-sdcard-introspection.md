# Translated ZIP Overlay SD Card Introspection - 2026-05-10 18:20:05

## Context

- Device checked with ADB: `AYN_Thor` at serial `c3ca0370`.
- `/sdcard/roms/psvita` was not the active library path on this device.
- Actual Vita ZIP folder found on removable storage: `/storage/2664-21DE/Roms/psvita`.

## ZIP Shapes Observed

- `Catherine - Full Body (English v1.0)[JP DUB][vita3k].zip`
  - Base game root: `app/PCSG01179/`
  - Translation/update root: `patch/PCSG01179/`
  - Direct app-root mounting alone would miss files such as patched movies, `Puzzle.cpk`, `Model.cpk`, manuals, and patch `param.sfo`.
- `Amanane[eng].zip`
  - Base game root: `app/PCSG01293/`
  - Translation root: `rePatch/PCSG01293/`
  - Direct app-root mounting alone would miss translated scenario Lua files, UI images, names, and other rePatch assets.
- `Chaos Rings III (English Patched)[vita3k].zip`
  - Patched content appears already folded into `app/PCSG00500/`.
- `Tales of Innocence R (English jun.23.2024)[vita3k].zip`
  - Patched content appears already folded into `app/PCSG00009/`.

## Changes Made

- Direct ZIP cartridge mounting now accepts the title ID and discovers same-archive `patch/<TITLEID>/` and `rePatch/<TITLEID>/` roots.
- The archive VFS mounts the base app root first, then overlays `patch`, then overlays `rePatch` so translated/modded files can override base and update files at read time.
- Virtual cartridge archive scanning now scores content roots similarly to launch introspection, so patch/rePatch `param.sfo` entries do not hide the real base app root.
- Android virtual cartridge scanning now includes capitalized `Roms/psvita` defaults and dynamic removable SD card discovery under `/storage/<card>/...`.
- Android CLI parsing now enables Windows-style slash options only on Windows. This lets Android absolute content paths such as `/storage/2664-21DE/Roms/psvita/...zip` parse as positional `content-path` values instead of being misread as options.
- Android now logs native startup arguments with the `Vita3KThor` tag, which makes Cocoon/open-with and ADB cartridge launches easier to diagnose.

## Verification

- Built `:android:assembleReldebug` successfully on Windows with Java 21 / Android NDK 27.3.13750724.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to AYN Thor with `adb install -r`; install returned `Success`.
- Launched the app on Thor and verified the virtual cartridge scanner found `/storage/2664-21DE/Roms/psvita` directly, including Catherine and Amanane, without the earlier duplicate lowercase-root scan entries.
- ADB cartridge launch for `Amanane[eng].zip` logged:
  - `Native arguments: [--cartridge, /storage/2664-21DE/Roms/psvita/Amanane[eng].zip]`
  - `input-content-path: /storage/2664-21DE/Roms/psvita/Amanane[eng].zip`
  - `Applied rePatch archive overlay root rePatch/PCSG01293 for PCSG01293 with 400 entries`
  - `Mounted app0 directly from archive ... base_entries=54 overlay_roots=1 overlay_entries=400`
- ADB cartridge launch for `Catherine - Full Body (English v1.0)[JP DUB][vita3k].zip` logged:
  - `Native arguments: [--cartridge, /storage/2664-21DE/Roms/psvita/Catherine - Full Body (English v1.0)[JP DUB][vita3k].zip]`
  - `input-content-path: /storage/2664-21DE/Roms/psvita/Catherine - Full Body (English v1.0)[JP DUB][vita3k].zip`
  - Base app root selected ahead of patch root: `app/PCSG01179/` scored `265`, `patch/PCSG01179/` scored `210`
  - `Applied patch archive overlay root patch/PCSG01179 for PCSG01179 with 140 entries`
  - `Mounted app0 directly from archive ... base_entries=474 overlay_roots=1 overlay_entries=140`
  - The game then opened patched files through `app0:`, including `app0:/data/Model.cpk` and `app0:/data/Puzzle.cpk`, proving the overlay entries are visible through the normal Vita path.

## Remaining Notes

- Amanane reaches module load but fails to decrypt `app0:eboot.bin` in direct cartridge mode. The overlay itself is applied; the remaining issue is NoNpDrm/decryption handling for that title shape.
- Catherine direct ZIP launch mounts and starts reading patched assets from the same archive. Further renderer/gameplay validation is separate from the ZIP overlay fix.

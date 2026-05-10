# Smarter Zip Cartridge Introspection

## Why

- Some Vita zip/vpk files are not laid out as a clean root-level `sce_sys/param.sfo` plus `eboot.bin`.
- Translated or repacked archives may add wrapper folders, uppercase paths, backslash separators, `ux0/app/TITLEID/` paths, or patch/rePatch folders alongside base game data.
- Direct cartridge mounting should pick the most likely game root instead of relying on one exact, case-sensitive path.

## Change

- Archive scanning now normalizes member paths for discovery:
  - converts backslashes to slashes
  - ignores leading `/` and `./`
  - matches `sce_sys/param.sfo`, `theme.xml`, and `eboot.bin` case-insensitively
- Candidate roots are scored before mount/install:
  - root with `param.sfo` is primary
  - root with `eboot.bin` is boosted
  - title-id-looking roots such as `PCSG00633/` are boosted
  - `app/` / `ux0/app/` roots are boosted
  - `patch/` and `rePatch/` roots are deprioritized for cartridge launch
- Mounting an archive root is now case-insensitive and separator-normalized while preserving the real zip member name for extraction.
- Param/theme extraction for install and cartridge mode now uses case-insensitive lookup.

## What This Should Fix

- Archives with `SCE_SYS/PARAM.SFO`.
- Archives wrapped like `Some Translation Folder/PCSG00633/sce_sys/param.sfo`.
- Archives laid out like `ux0/app/PCSG00633/sce_sys/param.sfo`.
- Archives containing both game and patch/rePatch data where the base game root should be tried first.

## Still To Verify

- A real translated zip from `/sdcard/roms/psvita`.
- A zip that only contains translation patch/rePatch files still cannot be launched alone as a cartridge; it needs a base game root or a future overlay mount.

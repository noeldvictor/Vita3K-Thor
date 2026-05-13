# Third Evolution Large Archive Cache - 2026-05-10 19:12:14

## Problem

`PCSG00490` / Sora no Kiseki the 3rd Evolution opened from ZIP cartridge mode was hitting Android memory pressure while opening huge PSARC files:

- `app/PCSG00490/gamedata/data.psarc` is about 1.3 GiB uncompressed.
- `app/PCSG00490/gamedata/data0.psarc` is about 1.6 GiB uncompressed.
- Direct ZIP cartridge reads were inflating archive members into memory-backed `FileStats` buffers on every `sceIoOpen`.

## Fix

- Added `--thor-render-trace` so ADB launches can enable the existing Thor renderer trace at startup.
- Added `tools/thor_adb_debug_capture.ps1` for repeatable ADB capture bundles.
- Added lazy app-local cache extraction for archive members larger than 64 MiB when opening from a virtual cartridge ZIP.
- Small archive files still use the existing in-memory direct ZIP path.
- Large deflated files are extracted once to `pref_path/cache/cartridge_archive/<TITLEID>/<archive-key>/...` and then opened as normal host files.

## Result

The user confirmed the 3rd Evolution game now works on the Thor after the large-member cache change.

## Notes

This keeps the library and launch model "ZIP as cartridge" from the user's perspective, but avoids treating multi-gigabyte compressed assets as RAM-resident files.

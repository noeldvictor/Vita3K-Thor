# Vita3K Thor Cheats

This folder is for emulator-side cheat metadata and legally redistributable offline single-player cheat files.

## Supported Detection

Vita3K Thor detects VitaCheat-style `.psv` files named by title ID:

- `cheats/PCSE00000.psv`
- `cheats/db/PCSE00000.psv`
- app shared storage `cheats/PCSE00000.psv`
- app shared storage `cheats/db/PCSE00000.psv`
- `ux0/vitacheat/db/PCSE00000.psv`

When a matching file exists, the game shows a `C` cheat badge in the app list.

## Runtime Support

Runtime cheat support is intentionally fail-closed. Vita3K Thor currently applies only enabled `_V1` VitaCheat sections with these static write formats:

- `$0000 address value` for 8-bit writes
- `$0100 address value` for 16-bit writes
- `$0200 address value` for 32-bit writes
- `$A000 address value`, `$A100 address value`, and `$A200 address value` for ARM/code writes with JIT cache invalidation when bytes change
- Level-1 pointer writes such as `$3201 pointer offset` followed by `$3300 final_offset value`
- `$B200 00000000 00000000` through `$B200 00000003 00000000` to make later writes relative to a main-module segment

Unsupported VitaCheat code types, malformed lines, multi-level pointer chains, conditions, block writes, and database-specific extensions are skipped and logged. Disabled `_V0` sections are detected but not applied.

Cheat application is controlled by `cheats-enabled` in the emulator config.

## Conversion Tool

Use `tools/convert_vitacheat.py` to convert `.psv` files into reviewable JSON metadata:

```powershell
python tools/convert_vitacheat.py C:\path\to\PCSE00000.psv -o cheats\converted
```

The emulator runtime still reads `.psv` files directly for now; the JSON output is for review, auditing unsupported codes, and future UI work.

## Import Rules

- Only add cheats for legally owned offline single-player games.
- Do not add online cheating, anti-cheat bypasses, DRM bypasses, license bypasses, or trainer redistributions.
- Do not vendor public cheat databases unless their license/source clearly allows redistribution in this repository.
- Prefer conversion tooling and source attribution notes over copying unlicensed `.psv` collections.

## Current Gaps

- Cheat toggles are not exposed in the in-game UI yet; `_V1` means enabled and `_V0` means off.
- Multi-level pointer, condition, increment/decrement, copy/fill, and button-conditional code types need per-game validation before enabling.
- Do not commit public cheat databases unless their license/source clearly allows redistribution here.

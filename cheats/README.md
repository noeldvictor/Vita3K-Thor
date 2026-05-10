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

## Import Rules

- Only add cheats for legally owned offline single-player games.
- Do not add online cheating, anti-cheat bypasses, DRM bypasses, license bypasses, or trainer redistributions.
- Do not vendor public cheat databases unless their license/source clearly allows redistribution in this repository.
- Prefer conversion tooling and source attribution notes over copying unlicensed `.psv` collections.

## Current State

Detection and badges are implemented. Runtime application of VitaCheat code types still needs a fail-closed parser, address validation, and emulator memory-write integration.

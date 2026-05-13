# Cheat Coverage Inventory - 2026-05-10 19:28:47

## Device Inventory

- Device: AYN Thor `c3ca0370`.
- Game root checked: `/storage/2664-21DE/Roms/psvita`.
- Cheat DB checked: `tmp/vitacheat-db/db` and `/storage/2664-21DE/cheats/psvita/db`.
- Current DB source: `https://github.com/r0ah/vitacheat.git`, local clone commit `bb8158a`.
- DB size: 677 `.psv` files in the local clone.

## Exact Cheat Matches

These title IDs have exact `.psv` files in the current VitaCheat DB and should show cheat badges/loadable cheats:

- `PCSE00905` - Adventures of Mana
- `PCSG01179` - Catherine: Full Body
- `PCSG00500` - Chaos Rings III
- `PCSG00008` - Lord of Apocalypse
- `PCSG00972` - Metal Max Xeno
- `PCSE00429` - Tales of Hearts R
- `PCSG00009` - Tales of Innocence R
- `PCSA00029` - Uncharted: Golden Abyss
- `PCSG00633` - Uppers

## Missing Exact Matches

These games do not have exact title-ID `.psv` files in the current synced DB:

- `PCSE00871` - A.W.: Phoenix Festa
- `PCSG01293` - Amanane
- `PCSG00599` - Date-A-Live Twin Edition: Rio Reincarnation
- `PCSH00250` - Dead or Alive Xtreme 3 Venus
- `PCSG00488` - Trails in the Sky FC Evolution
- `PCSG00489` - Trails in the Sky SC Evolution
- `PCSG00490` - Trails in the Sky the 3rd Evolution
- `PCSA00011` - Gravity Rush
- `PCSG00421` - Grisaia no Meikyuu
- `PCSA00155` - Oreshika: Tainted Bloodlines
- `PCSE00644` - Steins;Gate
- `PCSG00146` - Steins;Gate: My Darling's Embrace

## Nearby Sources Found

- `PCSG00490` has public Japanese VitaCheat listings on PSVita cheat summary sites, but the sample codes use pointer/block formats such as `$7202`, `$7702`, and repeat/count code types that Vita3K Thor does not fully execute yet.
- `PCSG00488` appears in Japanese VitaCheat analysis threads with at least speed-up style codes.
- `PCSG00489` appears to have community-created VitaCheat material, but not in the current r0ah DB clone.
- Gravity Rush has nearby DB entries for other regions/editions: `PCSD00003`, `PCSD00035`, and `PCSF00024`.
- Oreshika has nearby DB entries for other regions/editions: `PCSD00084` and `PCSF00536`.
- Trails SC/3rd have nearby Asia/HK DB entries: `PCSH10060` and `PCSH10082`.

## Next Work

- Add broader VitaCheat opcode support before importing the more complex Trails/Oreshika/Gravity-style codes. In particular, implement and test common `$7xxx` pointer reads, `$77xx` pointer writes, and repeat/block code formats in a fail-closed way.
- Add a local cheat-source import workflow that stores provenance comments, source URL, title ID, game version, and supported/unsupported code counts per imported file.
- Treat direct region-title renames as experimental only. Prefer porting by matching game version, module segment, expected bytes, or validated pointer base instead of blindly copying another region's `.psv`.
- Keep third-party cheat DBs out of git unless their license/source clearly permits redistribution. Commit importer/conversion tooling and provenance notes instead.

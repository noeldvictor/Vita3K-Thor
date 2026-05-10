# VitaCheat DB Sync To Thor - 2026-05-10 18:56:59

## What Happened

The emulator was working as coded, but the Thor SD card had no VitaCheat `.psv` files. I pulled the public `r0ah/vitacheat` database into ignored local scratch storage and pushed its `db` folder to the Thor SD card.

Source checked:

- `https://github.com/r0ah/vitacheat`
- The repo has no `LICENSE` file, so the cheat database files are not committed here.

Thor install path:

- `/storage/2664-21DE/cheats/psvita/db`

## Exact Matches For Current SD Card Games

These current `/storage/2664-21DE/Roms/psvita` title IDs now have matching `.psv` files on the Thor:

- `PCSG01179` - Catherine: Full Body
- `PCSG00500` - Chaos Rings III
- `PCSG00008` - Lord of Apocalypse
- `PCSG00972` - Metal Max Xeno
- `PCSE00429` - Tales of Hearts R
- `PCSG00009` - Tales of Innocence R
- `PCSA00029` - Uncharted: Golden Abyss
- `PCSG00633` - Uppers
- `PCSE00905` - Adventures of Mana

## No Exact Match Yet

These current SD card title IDs did not have exact same-ID files in the pulled DB:

- `PCSE00871` - A.W. Phoenix Festa
- `PCSH00250` - Dead or Alive Xtreme 3 Venus
- `PCSA00011` - Gravity Rush
- `PCSG00421` - Grisaia no Meikyuu
- `PCSA00155` - Oreshika Tainted Bloodlines
- `PCSG00146` - Steins Gate: My Darling's Embrace
- `PCSG00490` - Sora no Kiseki the 3rd Evolution
- `PCSG01293` - Amanane
- `PCSG00599` - Date-A-Live Twin Edition
- `PCSG00488` - Sora no Kiseki FC Evolution
- `PCSG00489` - Sora no Kiseki SC Evolution
- `PCSE00644` - Steins;Gate

Some have same-game or related region entries in the DB, but VitaCheat codes are title/version/region sensitive, so they should be ported and validated per game before automatic use.

## Verification

- Pushed 677 files to `/storage/2664-21DE/cheats/psvita/db`.
- Launched Catherine direct ZIP cartridge.
- Runtime log confirmed: `Loaded VitaCheat file "/storage/2664-21DE/cheats/psvita/db/PCSG01179.psv" for PCSG01179`.

## Repo Change

- Added `tools/sync_vitacheat_db.ps1` so the DB sync can be repeated without committing third-party cheat files.
- Added `tmp/` to `.gitignore` so cloned/imported cheat DBs stay out of git.

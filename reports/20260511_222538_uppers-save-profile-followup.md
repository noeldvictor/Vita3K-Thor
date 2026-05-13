# uppers-save-profile-followup - 2026-05-11 22:25:38

## Public Save Search

- Checked the Apollo PSV save list for `UPPERS` / `PCSG00633`; no matching public save was present.
- No clean public UPPERS save source was found during this pass, so no external save was installed.
- Avoid installing random ad-heavy save uploads into the Thor profile unless the source is trustworthy and the archive contents are inspected first.

## Existing Thor Save

- Existing UPPERS savedata was found on the Thor at `/sdcard/Android/data/org.vita3k.emulator.debug/files/vita/ux0/user/00/savedata/PCSG00633`.
- Files present: `SAVEDATA0000.bin`, `SAVEDATA0010.bin`, `SlotParam_0.bin`, and `SlotParam_10.bin`.
- Backed up with `tools/thor_save_sync.ps1 -TitleId PCSG00633 -Backup`.
- Backup artifacts are under `tmp/thor-saves/save-pcsg00633_20260511_222300/PCSG00633` and should not be committed.

## Profile Dump

- Added `tools/thor_profile_dump.ps1` for one-shot Thor profile bundles.
- Captured `uppers-profile-launch_20260511_222430` by launching `/storage/2664-21DE/Roms/psvita/Uppers (English v0.97)[vita3k].zip` with renderer trace enabled.
- The Thor was using multiple displays; Android window focus included launcher lines from another display, but the screenshot captured Vita3K/UPPERS correctly.
- The launched repro reached the UPPERS auto-save notice, not the later bad-render scene.
- Profile summary from that launch: 2514 render trace lines, 523 scene lines, 86 texture lines, and no suspicious macroblock lines in the early auto-save screen.

## Save-State Impact

- Durable per-game save/load states would make late-scene renderer debugging much easier because Codex could repeatedly launch directly into the exact bad frame area.
- Current quickstate support is still not PPSSPP-level durable across app restarts, so for now the most reliable path is an in-game savedata anchor plus live/profile dumps.
- If the user exports a decrypted UPPERS save from their Vita, install it with `tools/thor_save_sync.ps1 -TitleId PCSG00633 -InstallPath <folder-or-zip>`.

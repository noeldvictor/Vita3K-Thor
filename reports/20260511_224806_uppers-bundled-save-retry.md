# uppers-bundled-save-retry - 2026-05-11 22:48:06

## Result

- Retried installing the bundled UPPERS savedata extracted from `/storage/2664-21DE/Roms/psvita/Uppers (English v0.97)[vita3k].zip`.
- Pre-created the remote `sce_sys` directory so Android `adb push` could install nested savedata files.
- Pushed `SAVEDATA0000.bin`, `SAVEDATA0009.bin`, `SAVEDATA0010.bin`, `sce_sys/param.sfo`, `sce_sys/sdslot.dat`, and `sce_sys/_safemem.dat`.
- Launched UPPERS after the push and captured `tmp/uppers-bundled-save-retry.png`.
- The game still reached the auto-save notice, so this bundled savedata is not a post-intro skip save by itself.

## Current Device State

- The bundled savedata retry is currently installed on the Thor for `PCSG00633`.
- A backup of the pre-retry save exists under `tmp/thor-saves/save-pcsg00633_20260511_224657/PCSG00633`.
- The earlier known backup from before the bundled-save experiments is under `tmp/thor-saves/save-pcsg00633_20260511_222300/PCSG00633`.

## Tooling Follow-Up

- Updated `tools/thor_save_sync.ps1` to pre-create subdirectories before pushing a savedata tree.
- This avoids the scoped-storage failure previously seen when pushing `sce_sys/param.sfo`.

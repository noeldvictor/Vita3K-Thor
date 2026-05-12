# uppers-save-error-cleanup - 2026-05-11 22:51:24

## What Happened

- After the bundled-save retry, the device returned to the launcher.
- Captured evidence under `tmp/thor-error-20260511_224854`.
- Logcat showed repeated `0x80010002` file-not-found results while UPPERS probed empty save slots such as `SlotParam_1.bin` through `SlotParam_9.bin`.
- The fatal app crash was `vk::Device::createSwapchainKHR: ErrorSurfaceLostKHR`, followed by `SIGABRT`, after the Android surface was lost/recreated.

## Cleanup

- Restored the earlier known UPPERS save snapshot from `tmp/thor-saves/save-pcsg00633_20260511_222300/PCSG00633`.
- Removed stale bundled-save extras (`SAVEDATA0009.bin` and `sce_sys/`) from the remote save directory.
- Re-ran restore with `tools/thor_save_sync.ps1 -Replace`, leaving exactly the four original files: `SAVEDATA0000.bin`, `SAVEDATA0010.bin`, `SlotParam_0.bin`, and `SlotParam_10.bin`.
- Applied group-write permissions after push so Vita3K can update the files through Android external-data storage.

## Follow-Up

- Updated `tools/thor_save_sync.ps1` with `-Replace` for exact snapshot restore.
- The bundled save is not useful as a post-intro skip save; keep searching for a real decrypted UPPERS save or build a better skip/save-state path.

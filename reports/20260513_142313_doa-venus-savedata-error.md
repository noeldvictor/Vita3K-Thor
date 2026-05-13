# DOA Venus Savedata Error - 2026-05-13 14:23:13

## Trigger

- Device: AYN Thor
- Package: `org.vita3k.emulator.debug`
- Title ID: `PCSH00250`
- Symptom on screen: Vita savedata dialog displayed `An error occurred. Error code: 0x80100C06`.
- Screenshot artifact: `tmp/thor-live-error-20260513_141440/screen-pulled.png`

## Evidence

Logcat around the failure showed Vita3K returning missing slot metadata from AppUtil:

```text
open_file: Missing file at ".../ux0/user/00/savedata/PCSH00250/SlotParam_0.bin" (target path: savedata0:/SlotParam_0.bin)
open_file (sceAppUtilSaveDataSlotGetParam) returned 0x80010002
sceAppUtilSaveDataSlotGetParam returned SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND (0x80100641)
```

The save directory existed on-device but was empty:

```text
/sdcard/Android/data/org.vita3k.emulator.debug/files/vita/ux0/user/00/savedata/PCSH00250
```

## Fix

- `sceAppUtilSaveDataDataSave` now uses checked helper writes instead of blindly seeking/writing/closing failed descriptors.
- Save-data directory creation is now recursive so nested save paths can be created from Android without depending on earlier calls.
- Slot metadata writes now use create + truncate, preventing stale or partial `SlotParam_*.bin` data.
- `sceAppUtilSaveDataSlotCreate` now shares the same slot metadata write path.
- `sceAppUtilSaveDataSlotSearch` no longer writes result entries by absolute slot ID, and now treats file descriptor `0` as valid.
- `sceAppUtilSaveDataDataRemove` no longer reads `files[0]` when `fileNum == 0`.

## Verification

- Windows `RelWithDebInfo` build of `vita3k` completed successfully.
- Android `:android:assembleReldebug` completed successfully.
- APK installed to AYN Thor with `adb install -r`.
- After relaunch, Thor was on the Android lock screen, so the game-side retry still needs manual unlock and re-entry to the save prompt.

## Notes

- DOA Venus rendering itself was already confirmed on Thor after the `app0:.` archive path fix.
- This pass targets the follow-up savedata dialog error, not the previous black-screen renderer issue.

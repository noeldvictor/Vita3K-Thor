# PSN NetCheck Offline Signed-In - 2026-05-13 14:55:53

## Trigger

- Device: AYN Thor
- Title ID under test: `PCSH00250`
- On-screen error: `0x80100C06`
- Earlier hypothesis: savedata slot metadata.
- Better mapping: `0x80100C06` is `SCE_NETCHECK_DIALOG_ERROR_SIGN_OUT`.

## Evidence

- VitaSDK documents `0x80100C06` as NetCheck sign-out, not an AppUtil savedata code.
- Thor config had `psn-signed-in: 0` when the error was first captured.
- Logcat showed `sceNetCheckDialogInit`, `sceNetCheckDialogGetStatus`, `sceNetCheckDialogGetResult`, and `sceNetCheckDialogTerm` right before the title displayed the error.
- The old `sceNetCheckDialogGetResult` path only filled `result->result`; it left `psnModeSucceeded` untouched. A title can interpret that as PSN-mode failure even when the dialog result is OK.

## Fix

- Added `SCE_NETCHECK_DIALOG_ERROR_SIGN_OUT` to dialog types for the documented NetCheck sign-out code.
- `sceNetCheckDialogGetResult` now clears the full result struct, fills `result`, and fills `psnModeSucceeded` for PSN/PSN online modes.
- `sceNetCheckDialogInit` keeps common dialog state as OK/finished while `GetResult` reports sign-out only if the user explicitly disables local PSN signed-in mode.
- New configs default `psn-signed-in` to `1`, matching this fork's offline handheld compatibility goal.
- Thor test config was also set to `psn-signed-in: 1` over ADB.

## Verification

- Windows `RelWithDebInfo` build of `vita3k` passed.
- Android `:android:assembleReldebug` passed.
- APK installed to AYN Thor with `adb install -r`.
- Thor config confirmed:

```text
psn-signed-in: 1
```

## Notes

- The savedata slot probe for `SlotParam_0.bin` remains normal documented behavior and should not be faked globally.
- This is an offline compatibility state, not a real PSN login and not network-service emulation.
- Next step is to relaunch DOA Venus and confirm the `0x80100C06` dialog no longer appears.

# 2026-05-10 16:55:01 - Fast-forward timing fix

## Changes

- Confirmed through Thor logcat that the fast-forward command was reaching Vita3K, but runtime pacing still depended on real-time kernel waits.
- Added a shared kernel speed percentage and keep it in sync with the existing display speed percentage.
- Scaled `sceKernelDelayThread`, `sceKernelDelayThread200`, and callback delay variants by the active fast-forward percentage.
- Scaled kernel wait timeouts and kernel timer event scheduling by the active fast-forward percentage.
- Notified active kernel timers when fast-forward toggles so sleeping timer waits get a chance to re-check.
- Made the `Select/Back + R1` shortcut use configured controller bindings and work in either press order.
- Added Android Back + held R1 handling for AYN Thor physical Back-key chords.

## Verification

- `git diff --check` passed.
- `:android:assembleReldebug` completed successfully.
- Reinstalled `android/build/outputs/apk/reldebug/android-reldebug.apk` to the connected AYN Thor (`c3ca0370`, model `AYN_Thor`) with `adb install -r`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator` with ADB.
- Logcat smoke sample showed Vita3K startup and Turnip driver injection (`Turnip Adreno (TM) 740`) with no `AndroidRuntime` or `FATAL` entries in the sampled lines.

## Remaining test gap

- Needs an on-device title test to confirm gameplay visibly speeds up at 200%.

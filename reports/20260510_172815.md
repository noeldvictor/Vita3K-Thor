# Fast Forward Guest Clock Follow-Up

## Context

- User reported fast-forward still did not appear to affect gameplay on AYN Thor.
- Previous on-device logcat confirmed the shortcut path fired: `Fast forward 200%`.
- That meant input/toggle was working, but guest-facing timing could still be advancing at real time.

## Changes

- Added an anchored speeded kernel process clock in `KernelState`.
- Switched the fast-forward toggle to `KernelState::set_speed_percent()` so clock continuity is preserved when speed changes.
- Routed process/system timing through the speeded clock:
  - `sceKernelGetProcessTime*`
  - `sceKernelGetSystemTimeWide`
  - timer elapsed-time reads
  - `sceKernelLibcClock`
  - `sceKernelLibcTime`
  - `sceKernelLibcGettimeofday`
  - current RTC tick/clock APIs
  - thread start ticks
  - NetCtl adhoc peer timing
- Kept vblank pacing, delay-thread pacing, kernel timer event waits, and sync wait timeouts tied to the same speed percentage.

## Verification

- `git diff --check` passed.
- Android `:android:assembleReldebug` completed successfully before this report.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to connected AYN Thor:
  - Device: `c3ca0370`
  - Model: `AYN_Thor`
  - Install result: `Success`
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator` with ADB after install.
- Launch smoke log did not show `AndroidRuntime`/`FATAL` crash output.

## Remaining Risk

- Needs in-game on-device confirmation with a title that has obvious clock movement.
- If gameplay still appears real-time, next debugging step should add a small runtime diagnostic showing vblank rate, process-time rate, display speed, and kernel speed while fast-forward is active.

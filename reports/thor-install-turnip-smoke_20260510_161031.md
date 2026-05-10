# 2026-05-10 16:10:31

## Thor Install Smoke Test

- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` on connected AYN Thor with `adb install -r`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`.
- Startup completed without an `AndroidRuntime` fatal exception in the sampled log.
- Confirmed selected custom driver:
  - `Custom Adreno driver turnip_a8xx injected successfully`
  - `Vulkan device: Turnip Adreno (TM) 740`
  - `Driver version: 25.99.99`

## Notes

- This smoke test verifies app startup and Turnip injection after commit `a55bf75c`.
- No Vita title was available on `/sdcard/roms/psvita`, so cheat application and same-session quickstate need a real game test next.

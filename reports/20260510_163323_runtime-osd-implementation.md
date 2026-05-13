# 2026-05-10 16:33:23

## Change

- Implemented the first runtime OSD in the game rendering path.
- Short Back/Select press now opens/closes the OSD during a running title.
- Android `AC_BACK` key events are handled as OSD/back modifier input on AYN Thor instead of going straight to Vita PS button behavior while a title is running.
- `Select/Back + R1` now toggles fast-forward without opening the OSD.
- `Select/Back + right-stick down/up` still requests same-session save/load without opening the OSD.
- OSD includes Resume, Pause/Resume, Save State 0, Load State 0, Fast Forward, Screenshot, Settings, cheat reload, and per-cheat enable toggles.
- Reset Game and Close Game are shown as disabled placeholders until safe behavior is implemented.

## Verification

- `git diff --check` passed.
- Android reldebug build passed with `:android:assembleReldebug`.
- Installed the APK to connected AYN Thor with `adb install -r`; result was `Success`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`; startup log showed Vita3K booting and Turnip custom driver injection:
  - `Custom Adreno driver turnip_a8xx injected successfully`
  - `Vulkan device: Turnip Adreno (TM) 740`

## Remaining Test Gap

- A real running Vita title is still needed on the Thor to verify Back/Select opens the OSD in-game and `Back/Select + R1` toggles fast-forward against gameplay.

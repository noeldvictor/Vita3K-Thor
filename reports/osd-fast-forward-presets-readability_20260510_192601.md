# OSD Fast-Forward Presets And Readability - 2026-05-10 19:26:01

## Change

- Added runtime OSD fast-forward preset buttons for Off, 2x, 3x, and 4x.
- Choosing 2x/3x/4x updates `fast-forward-speed-percent`, so the Thor `Select + R1` shortcut follows the last OSD-selected preset.
- Moved runtime speed changes through shared `runtime_set_speed_percent`, keeping display speed, kernel speed, and kernel timers synchronized.
- Reworked the game-running OSD into a larger high-contrast panel with bigger buttons and an opaque background.
- Fixed the readability regression found during device testing: the game dimming layer now draws behind the OSD instead of over it.

## Thor Verification

- Device: `AYN_Thor` (`c3ca0370`, product `kalama`).
- Build: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`.
- Build result: success.
- APK installed with `adb install -r android/build/outputs/apk/reldebug/android-reldebug.apk`.
- Install result: success.
- Launched `/storage/2664-21DE/Roms/psvita/Eiyuu Densetsu - Sora no Kiseki the 3rd Evolution (English v1.0)(PCSG00490)[vita3k].zip` through Android `ACTION_VIEW`.
- Opened the runtime OSD with short Back on Thor and captured a screenshot to temp storage. The OSD text/buttons were readable after moving the dim layer behind the menu.
- Verified OSD preset taps through logcat:
  - `Runtime speed set to 200%`
  - `Runtime speed set to 300%`
  - `Runtime speed set to 400%`

## Notes

- Raw screenshots were kept out of git.
- The game title still renders Japanese glyphs as `????` where the current font lacks glyph coverage; that is separate from the OSD brightness/contrast issue.

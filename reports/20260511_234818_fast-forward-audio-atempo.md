# Fast-Forward Audio Atempo - 20260511_234818

## What Changed

- Replaced the SDL fast-forward pitch correction grain skipper with an FFmpeg `atempo` filter graph per SDL audio port.
- Kept SDL stream frequency ratio at `1.0x` when `atempo` is active so fast-forward can preserve pitch instead of raising it.
- Added a fallback back to SDL frequency-ratio fast-forward if `abuffer`, `abuffersink`, or `atempo` is unavailable in a build.
- Linked the `audio` target against the existing vendored FFmpeg interface target.

## Why

The prior pitch-correction path rebuilt a tiny grain blend for every emulator audio callback. That made fast-forward audio sound choppy because each callback was processed independently. FFmpeg `atempo` keeps filter state across callbacks and is designed for tempo changes while preserving pitch.

## Verification

- `git diff --check` passed, with only the existing line-ending warning for `vita3k/audio/CMakeLists.txt`.
- Android `:android:assembleReldebug` succeeded.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to connected Thor device `c3ca0370` with `adb install -r`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`; Android reports the Vita3K activity resumed on the main display.

## Notes

- Runtime `atempo` logs only appear after a game opens an SDL audio output port and fast-forward is set above `100%`.
- If the FFmpeg filter cannot initialize, the fallback is smooth SDL speed-up audio rather than the choppy homemade pitch-correction path.

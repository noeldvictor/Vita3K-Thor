# Fast-Forward Audio No-Chipmunk Fallback - 20260511_235548

## What Changed

- Locked the SDL audio stream frequency ratio to `1.0x` during fast-forward.
- Added a normal-pitch fallback for Android builds where FFmpeg `atempo` is unavailable.
- The fallback submits the right fraction of full-speed audio buffers for 2x/3x/4x and lightly crossfades buffer boundaries to reduce clicks.
- Updated `AGENTS.md` so future work does not reintroduce SDL frequency-ratio chipmunk audio.

## Why

The Android FFmpeg prebuilt linked by this tree does not expose the `atempo` filter, so the previous patch fell back to SDL frequency-ratio speed-up. That made fast-forward audio sound high-pitched.

## Verification

- `git diff --check` passed.
- Android `:android:assembleReldebug` succeeded.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to connected Thor device `c3ca0370` with `adb install -r`.

## Remaining Work

- For smoother fast-forward audio than buffer skipping, bundle or implement a real time-stretch library for Android instead of relying on the current FFmpeg prebuilt.

# Fast Forward Audio Pacing - 2026-05-11 23:20:46

## Problem

- Runtime fast-forward updated display pacing, kernel time, timers, and AVPlayer video frame timing.
- Audio output still paced at normal host time, so games and AVPlayer movies could block in `sceAudioOutOutput` and stay near 1x.

## Change

- Added runtime speed tracking to `AudioState`.
- Scaled the shared audio output wait by the current fast-forward speed.
- SDL audio streams now update `SDL_SetAudioStreamFrequencyRatio` to match fast-forward speed.
- SDL stream backpressure waits are scaled by fast-forward speed.
- Cubeb no longer blocks the emulation thread on a full audio queue while fast-forward is active; it drops the oldest queued buffer instead.

## Notes

- This fixes fast-forward pacing first.
- Audio pitch correction is still a separate follow-up; the SDL ratio path speeds audio up and raises pitch. Proper pitch correction needs a time-stretch path instead of simple resampling.

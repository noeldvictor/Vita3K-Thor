# AVPlayer Fast Forward Movies - 2026-05-11 23:15:49

## Problem

- Runtime fast-forward updated kernel/display pacing, but `sceAvPlayerGetVideoData` still throttled video frames using normal host wall-clock movie FPS.
- Result: intro movies and AVPlayer cutscenes could ignore 2x/3x/4x fast-forward even while the rest of emulation sped up.

## Change

- Scaled AVPlayer video frame interval by `emuenv.kernel.speed_percent`.
- Example: a 33.3 ms movie frame interval becomes about 16.7 ms at 2x, 11.1 ms at 3x, and 8.3 ms at 4x.

## Test Plan

- Build Android `reldebug`.
- Install to AYN Thor.
- Launch a game with an AVPlayer intro movie.
- Toggle fast-forward and confirm movie video advances faster at 2x/3x/4x.

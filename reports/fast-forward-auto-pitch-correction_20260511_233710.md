# Fast Forward Auto Pitch Correction - 2026-05-11 23:37:10

## Change

- Added automatic pitch correction for the SDL audio backend used on Thor.
- Fast-forward no longer raises SDL's stream frequency ratio; the stream stays at normal pitch.
- Fast-forward audio chunks are time-compressed before queueing using short PCM grains with overlap/crossfade.

## Result

- 2x/3x/4x fast-forward should remain fast without the obvious chipmunk pitch shift.
- This is a lightweight emulator-side time-compression pass, not a full high-quality SoundTouch/Rubber Band style stretcher yet.

## Follow-Up

- If the audio sounds choppy, tune grain/overlap sizes or replace the lightweight stretcher with a proper WSOLA/SoundTouch-style backend.

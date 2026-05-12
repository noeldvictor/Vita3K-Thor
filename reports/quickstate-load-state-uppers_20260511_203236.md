# Quickstate Load State UPPERS - 2026-05-11 20:32:36

## Summary

Load state now works for same-session UPPERS quickstates on AYN Thor. The crash was caused by save/load treating "pause requested" as "all guest threads are stopped"; OSD pause could still leave busy AVPlayer threads running while quickstate memory was copied.

## Changes Verified

- Added a real guest-memory commit helper for allocation-map restore so saved guest ranges can be made writable before replacing the saved allocator bitmap/table.
- Save/load now waits for guest threads to fully leave `ThreadStatus::run` before copying quickstate memory, even when the OSD had already requested pause.
- Renderer runtime state is reset after restore to avoid stale texture/display queues.
- Same-session UPPERS state saved successfully:
  - Title ID: `PCSG00633`
  - Raw guest memory: `454434816` bytes
  - Compressed state file: `33781151` bytes
  - Thread contexts: `30`
- Same-session UPPERS load restored and resumed without crash.

## App-Restart Result

App-restart durable restore still cannot safely restore UPPERS states taken during AVPlayer/movie scenes. After restart, the saved CPU/RAM state references AVPlayer host-side decoder/audio/mspace objects that are not serialized yet. The previous behavior crashed in `avPlayer AudioD` / `mspace_free`.

The build now refuses disk-loaded AVPlayer quickstates with a clear log instead of crashing:

```text
Refused durable quickstate restore for PCSG00633 because this state contains AVPlayer movie/audio threads.
Same-session AVPlayer states can load, but app-restart restores need AVPlayer host object serialization before they are safe.
```

## Verification

- `git diff --check` passed, with only existing CRLF warnings for `AGENTS.md` and `README.md`.
- Android `:android:assembleReldebug` passed.
- Installed `android-reldebug.apk` to the connected Thor with `adb install -r`.
- Launched UPPERS directly from `/storage/2664-21DE/Roms/psvita/Uppers (English v0.97)[vita3k].zip`.
- Verified same-session save/load through the OSD.
- Verified app-restart AVPlayer durable load refuses cleanly and does not crash the app.

## Remaining Work

- Durable app-restart save states need host object serialization, starting with AVPlayer/player queues, decoder position, media buffers, audio state, and kernel wait/sync object state.
- Non-AVPlayer app-restart states still need broader torture testing before they can be called reliable.

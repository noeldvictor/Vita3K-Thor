# UPPERS Render Glitch Double-Buffer Trapping - 2026-05-11 23:00:30

## Capture

- Game: UPPERS (`PCSG00633`) on AYN Thor.
- Symptom: large blue/green stretched geometry in gameplay after the intro/tutorial sequence.
- Live capture: `tmp/thor-fast-uppers-20260511_225529/screen-display0.png`.
- Trace: `tmp/thor-fast-uppers-20260511_225529/logcat.txt`.

## Findings

- The bad frame was using Vulkan memory mapping method `1`, which is the double-buffer path.
- The scene trace showed heavy indexed geometry on a downscaled 704x396 MSAA target before compositing to 960x544.
- The visual failure looks like stale or corrupted vertex/index data, not just a missing texture upload.

## Fix Applied

- Tightened double-buffer dirty tracking in `vita3k/renderer/src/vulkan/scene.cpp`.
- Vertex stream trapping now covers the whole page range so writes into the unaligned head or tail of a trapped buffer cannot be missed.
- Index buffer trapping also covers the whole page range.
- Empty indexed draws now keep `max_index` at zero instead of reading the cached sentinel.

## Verification Plan

- Build Android `reldebug`.
- Install to Thor.
- Reopen UPPERS at the same gameplay spot and compare against the captured screenshot.
- If artifact remains, capture a second render trace with max-index/stream-size instrumentation or add a temporary draw-skip property for binary-searching the exact draw.

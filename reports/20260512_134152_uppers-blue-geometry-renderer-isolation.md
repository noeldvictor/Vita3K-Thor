# UPPERS Blue Geometry Renderer Isolation - 2026-05-12 13:41:52

## Symptom

- Game: UPPERS (`PCSG00633`) on Windows Vita3K-Thor.
- Backend: Vulkan.
- Bad capture: `reports/windows-uppers-live-check_20260512_133638.png`.
- Visible issue: large flat blue geometry covers the camera view in the alley tutorial scene while character/UI/text continue rendering.

## What Was Tried

- Reproduced the same visual corruption on Windows, so the issue is not Android-only and not a Thor screenshot artifact.
- Tested a double-buffer index-path fix that recomputes max index per draw instead of caching it on the trapped buffer.
- Windows `RelWithDebInfo` build succeeded after that patch.
- Rechecked the same alley scene after the patch; the blue geometry was still present, so stale cached `max_index` was not the full root cause.

## Current Isolation

- Started a temporary Windows config with `memory-mapping: disabled` and normal Vulkan accuracy:
  `tmp/vita3k-win-debug/config_nomap.yml`
- Rechecked the same alley scene under no-mapping Vulkan.
- Result: blue geometry remained visible in `reports/windows-uppers-nomap-live-check_20260512_134324.png`.
- Conclusion: double-buffer memory mapping is not the main root cause.

## Current Run

- Started a temporary Windows config with `high-accuracy: true`:
  `tmp/vita3k-win-debug/config_highacc.yml`
- Current capture `reports/windows-uppers-highacc-check_20260512_134514.png` shows the intro/cutscene, not the alley scene yet.
- Goal: get back to the alley scene under high-accuracy Vulkan.

## Next Tests

- Recheck the alley scene with `high-accuracy: true`.
- If high accuracy fixes the scene, focus on texture viewport or direct framebuffer-fetch approximation.
- If high accuracy still reproduces, add a draw-skip/debug filter for UPPERS to binary-search the exact scene/draw/hash producing the large blue surface.

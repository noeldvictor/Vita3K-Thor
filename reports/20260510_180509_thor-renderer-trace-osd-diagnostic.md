# Thor Renderer Trace OSD Diagnostic

## Why

- The graphics corruption reproduced in Vita3K gameplay, so this is now a renderer investigation rather than a screenshot or frontend issue.
- Current observed title context: `PCSG00633`.
- Current Thor config seen over ADB:
  - `backend-renderer: Vulkan`
  - `custom-driver-name: turnip_a8xx`
  - `high-accuracy: false`
  - `disable-surface-sync: true`
  - `resolution-multiplier: 1`
  - `memory-mapping: double-buffer`

## Change

- Added a runtime OSD checkbox named `Renderer Trace`.
- When enabled, the Vulkan renderer emits `ThorRenderTrace` logcat lines:
  - one scene setup line per scene
  - the first 32 draw calls per scene
- Trace data includes render target size, MSAA, macroblock size, surface sync state, memory mapping mode, shader interlock and texture viewport flags, Adreno stock/Turnip flags, color surface address/format/type/stride/downscale, depth/stencil addresses/format/type/load/store, shader hashes, texture and uniform buffer counts, primitive/index formats, depth/stencil state, culling/two-sided state, viewport state, and writing mask.
- Android `:android:assembleReldebug` completed successfully and the APK was installed to AYN Thor device `c3ca0370`.

## How To Use On Thor

- Open a running Vita title.
- Press physical Back to open Thor OSD.
- Enable `Renderer Trace`.
- Resume and reproduce the blue/gray block corruption.
- Pull logcat filtered to `ThorRenderTrace` and attach the matching screenshot.

## Next Triage

- Reproduce once with fast-forward off.
- Reproduce once with current Turnip driver.
- Reproduce once with system driver.
- If the trace shows the bad geometry shares a suspicious color/depth surface address or state transition, patch the corresponding Vulkan surface cache or depth/stencil handling path.

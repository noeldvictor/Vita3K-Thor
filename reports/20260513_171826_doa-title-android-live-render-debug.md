# DOA Title Android Live Render Debug - 2026-05-13 17:18:26

## Current Thor Proof

- Game: `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Package: `org.vita3k.emulator.debug`
- Current screenshot: `tmp/20260513_170951_thor-check-now.png`
- Follow-up screenshot: `tmp/20260513_171324_thor-check-now-10s.png`
- Symptom: title/logo/text render correctly, but the beach/title scene render target is black on the upper half and magenta/pink on the lower half.

## Trace Summary

- Current trace: `tmp/20260513_171135_doa-current-rendertrace-full.txt`
- Final title compositing samples `0x62FF8000` as a 960x544 rendered color surface via `viewport-direct`.
- The source scene repeatedly renders as `rt=960x544`, `msaa=2` (`SCE_GXM_MULTISAMPLE_4X`), `downscale=true`, `color_addr=0x62FF8000`, and `ds_depth=0x631F6000`.
- Existing trace did not expose viewport/scissor values or the real `has_macroblock_sync` boolean, so the next build adds those fields.

## Working Hypothesis

This is no longer a ZIP/cartridge or custom Turnip-driver-only issue. The bad layer is produced inside the Vulkan render target path for a 4x MSAA downscaled scene, then sampled correctly by the final title compositor. The next diagnostic target is identifying whether the magenta lower half comes from the first clear/fullscreen draw, a viewport/scissor mismatch, depth behavior, or a later beach geometry/texture draw.

## Code Direction

- Add live Android system-property controls for Vulkan draw trace, draw skip, stop-after, and dump.
- Add viewport/scissor/res-multiplier fields to `ThorRenderTrace draw` lines.
- Add `macroblock_sync` and `force_full_macroblock` to scene setup trace.
- Initialize render target macroblock dimensions to zero when macroblock sync is not enabled, so trace logs stop showing stale values.

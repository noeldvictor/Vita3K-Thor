# GXM Blend None Renderer Fix

Date: 2026-05-12 18:01:32 America/New_York

## Context

UPPERS `PCSG00633` shows a large blue render artifact on Vulkan in the same scene on Windows and Android. Previous targeted dumps isolated the visible artifact to draws `76-77` on render target `704x396`.

The latest draw dumps showed sane vertex streams, indices, and uniform buffers, so the next likely emulator-side fault was fragment state translation: alpha/discard/blending, texture alpha, or shader output handling.

## Change

- Stored the raw `SceGxmBlendInfo` and fragment program flags in Vulkan fragment-program renderer data for focused draw dumps.
- Added `ThorRenderDump fragment` lines that report program flags, discard/depth/native-color/frag-color/output state, mask-update state, GXM fragment-program mode, raw GXM blend fields, translated Vulkan blend state, write mask, and blend hash.
- Fixed blend translation for both Vulkan and OpenGL when only one of GXM color or alpha blending is enabled.
  - If `colorFunc == SCE_GXM_BLEND_FUNC_NONE`, translated color blending now behaves like source replace: source factor `ONE`, destination factor `ZERO`, op `ADD`.
  - If `alphaFunc == SCE_GXM_BLEND_FUNC_NONE`, translated alpha blending now behaves like source replace: source factor `ONE`, destination factor `ZERO`, op `ADD`.
  - This avoids leaking irrelevant GXM blend factors into a channel that the Vita program marked as not blended.

## Verification

- Built Windows target successfully:
  - `cmake --build build\windows-vs2022 --config RelWithDebInfo --target vita3k -- /m`
- Relaunched UPPERS with:
  - `dump=rt=704x396:draw=76-77`
  - `trace=1`
  - `labels=1`

## Next Check

The emulator is relaunched, but the focused draw has not reappeared in the log yet. Once the user reaches the glitch scene again, inspect the new `ThorRenderDump fragment` lines for draws `76-77` and compare the visible output after the blend correction.

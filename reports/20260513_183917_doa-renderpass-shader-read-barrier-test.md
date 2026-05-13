# DOA render-pass shader-read barrier test

## Context

Dead or Alive Xtreme 3 Venus on AYN Thor showed severe title/menu rendering corruption: black or missing beach/background, magenta lower-frame regions, horizontal split artifacts, and intermittent flicker.

Earlier burst captures and `ThorRenderTrace` logs narrowed the active path to a render-to-texture handoff:

- DOA renders a scene to color surface `0x62FF8000`.
- Later scenes sample `0x62FF8000` as a normal texture.
- Surface cache lookup did hit the expected surface, so the failure was not explained by a missing texture/surface overlap.
- Partial-frame `render_stop_after` captures showed corruption very early in the title/menu pass, pointing toward render-pass synchronization, MSAA/downscale, or attachment visibility rather than one bad late draw.

## Change tested

Updated the Vulkan render-pass external dependency in `vita3k/renderer/src/vulkan/pipeline_cache.cpp` so previous color/depth attachment writes are visible before the next pass reaches fragment shader texture reads.

This adds:

- destination stage: `eFragmentShader`
- destination access: `eShaderRead`

## Why this is plausible

The previous dependency waited for earlier attachment writes before later attachment reads/writes and early depth tests, but did not explicitly make the previous color attachment write visible to fragment shader sampling in the next render pass.

On Adreno/Turnip, DOA's "render scene, then sample scene surface as texture" path may legally observe stale or incomplete contents without the shader-read dependency.

## Verification so far

- `git diff --check` passed.
- Android `:android:assembleReldebug` succeeded.
- APK installed to AYN Thor with `adb install -r`.
- DOA launched in cartridge mode from:
  `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Post-install screenshot reached DOA's autosave prompt and rendered cleanly:
  `tmp/thor-burst/20260513_1828_doa-after-barrier-launch/launch2.png`

## Current blocker

The game is stopped at the autosave "Circle / Next" prompt. ADB `input keyevent` and `input tap` did not advance this specific in-game prompt, so the next visual check needs physical confirm input on Thor before capturing the title/beach scene again.

## Next check

After the prompt is dismissed and the title/beach scene is visible:

1. Capture a 10-12 frame burst.
2. Compare against `20260513_182054_doa-title-nearest-surface-trace` and `20260513_182438_doa-title-msaa-stop-after`.
3. If the beach/flicker improves, keep the barrier fix and commit it.
4. If the failure persists, next isolate MSAA/downscale resolve or attachment layout transitions around surface `0x62FF8000`.

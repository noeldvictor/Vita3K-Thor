# DOA Venus Title Render Debug: BCn Off + Stop-After Fix

## Context

Target game:

`/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`

Device: AYN Thor, package `org.vita3k.emulator.debug`, Turnip path.

Issue: title/beach scene alternates between mostly correct frames and badly corrupted frames with black/purple slabs, missing beach/sky, or large incorrect geometry.

## What Changed

- Fixed Android Vulkan `debug.vita3k.render_stop_after` so it now renders the matching draw and then skips later draws in the same scene.
- Updated `AGENTS.md` to document that behavior and to require burst snapshots for flickering render issues.
- Built and installed `android-reldebug.apk` to Thor after the tooling fix.

## Captures Reviewed

- BCn native path re-enabled:
  - `tmp/20260513_doa-title-bcn-off-after-launch.png`
  - `tmp/thor-burst/20260513_175526_doa-title-bcn-off/`
- Skip tests with BCn off:
  - `tmp/thor-burst/20260513_175643_doa-title-bcn-off-skip-tests/`
  - `tmp/thor-burst/20260513_180843_doa-title-skip-564cd0-bcn-off/`
- Repaired stop-after partial frames:
  - `tmp/thor-stop-after/20260513_180459_doa-title-rt960/`
- Bad-frame render dump:
  - `tmp/thor-bad-frame-dump/20260513_180716_doa-title-early-draws/`

## Findings

BCn force-decompress is not the root cause. With `debug.vita3k.force_bcn_decompress=0`, the title can still alternate between clean and corrupted frames.

Skipping individual fragment hashes did not produce a real fix:

- `fhash=70a54078` skip was a false lead from a lucky single frame.
- `fhash=e29e2948` did not remove the issue.
- `fhash=564cd0f6` still produced bad frames; it affects important beach/offscreen geometry but is not by itself the corruption source.

The bad frame includes an offscreen MSAA scene:

`rt=960x544 msaa=2 color_addr=0x62FF8000 downscale=true ds_depth=0x631F6000 ds_store=true`

That scene repeatedly draws terrain/beach geometry with `fhash=564cd0f6`. Later title/composite scenes also show repeated large-texture surface-cache misses such as:

`reason=no-overlap tex_addr=0x73000000 tex=1024x1024`

Those misses might be expected game textures, but they are now worth instrumenting because the visible failure is frame-to-frame and cache/state-shaped rather than a stable shader compile error.

## Current Read

Most likely suspects:

1. MSAA downscale/resolve or depth-store interaction on Adreno/Turnip for the offscreen 960x544 scene.
2. Surface cache aliasing or stale texture state between offscreen beach render and later title composite.
3. A state transition/barrier issue exposed by the scene alternating between clean and corrupted frames.

Less likely after this pass:

- Native BCn/DXT sampling path.
- A single bad fragment shader hash that can simply be skipped or special-cased.

## Next Debug Step

Use the repaired `render_stop_after` plus burst captures to isolate the first draw range where the offscreen `rt=960x544 msaa=2 downscale=true` scene turns bad. Then test narrow diagnostics:

- force depth clear on `ds_depth=0x631F6000`
- compare depth load/store behavior for that scene
- add temporary trace for nearest color-surface candidate when `reason=no-overlap` fires for 1024x1024 textures
- compare the same scene path against Windows, where the equivalent title render path was not showing this Android-specific black/purple corruption

Do not commit raw screenshots/logs unless they become curated documentation assets.

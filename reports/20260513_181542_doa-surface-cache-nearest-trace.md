# DOA Venus Surface-Cache Miss Trace

## Context

The previous DOA Venus title burst showed repeated `reason=no-overlap` texture-cache misses around large 1024x1024 textures while the visible title/beach scene flickered between clean and corrupted frames.

## Change

Added trace-only detail to `VKSurfaceCache::retrieve_color_surface_as_texture`:

- When a large texture lookup misses with `reason=no-overlap`, the log now includes the nearest previous cached color-surface range.
- If there is no previous surface, it reports the next cached color-surface range instead.
- Rendering behavior is unchanged; this only improves `ThorRenderTrace` diagnostics.

## Build And Install

Built:

`./gradlew.bat :android:assembleReldebug`

Result: success.

Installed to Thor:

`adb -s c3ca0370 install -r android/build/outputs/apk/reldebug/android-reldebug.apk`

Result: success.

## Next Use

Relaunch DOA Venus with `--thor-render-trace`, capture a bad title/beach burst, then check whether the 1024x1024 `no-overlap` misses are near cached render surfaces or clearly unrelated game textures.

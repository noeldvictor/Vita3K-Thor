# DOA Venus MSAA U2F Copy Sampling Fix

## Summary

- Built and installed a Vulkan surface-cache fix for `Dead or Alive Xtreme 3 Venus` on AYN Thor.
- The broken title/background path samples a Vita `U2F10F10F10` color surface at `0x62FF8000` after rendering it as an MSAA-downscaled target on Adreno/Turnip.
- Direct/viewport sampling of that rendered attachment produced black or horizontally corrupted output on Thor.
- The fix records color-surface MSAA/downscale metadata and forces Adreno/Turnip MSAA-downscaled `U2F10F10F10` color surfaces through the existing copied-texture path before sampling.

## Code Change

- `vita3k/renderer/include/renderer/vulkan/surface_cache.h`
  - Added `multisample_mode` and `downscale` metadata to `ColorSurfaceCacheInfo`.
- `vita3k/renderer/src/vulkan/surface_cache.cpp`
  - Updates that metadata whenever a color surface is retrieved for framebuffer use.
  - Keeps the previous same-image feedback safety path.
  - Forces copied sampling when all are true:
    - Android driver is Adreno/Turnip.
    - Requested and stored surface formats match.
    - Surface format is `SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10`.
    - Source surface was rendered with MSAA and downscale enabled.

## Verification

- Built successfully:
  - `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Installed successfully:
  - `adb install -r android\build\outputs\apk\reldebug\android-reldebug.apk`
- Restored the normal Thor config from the high-accuracy diagnostic backup before relaunching.
- Relaunched DOA directly as a virtual cartridge:
  - `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Render trace now shows the problematic surface using copied sampling:
  - `surface texture hit ... mode=casted-copy tex_addr=0x62FF8000 ... fmt=0x41000000`
- Clean intro proof screenshot:
  - `tmp/thor-burst/20260513_203057_doa-msaa-u2f-copy-test/copytest_06.png`
- Relaunch reached the autosave screen afterward:
  - `tmp/thor-burst/20260513_203135_doa-msaa-u2f-copy-title/title_01.png`
- Pressed Circle/O through the Asian-region autosave prompt and captured the clean title loop:
  - `tmp/thor-burst/20260513_203425_doa-msaa-u2f-copy-circle-title/circle_01.png`

## Notes

- This is not a DOA title-id hack. It is scoped to a format, surface state, and Adreno/Turnip path that matches the observed failure.
- The fix may have a cost when affected scenes repeatedly sample MSAA-downscaled `U2F10F10F10` surfaces, because it copies before sampling. That is acceptable for now because the direct path rendered incorrectly on Thor.
- Validation confirmed the original title loop no longer has black background, magenta strips, or the previous horizontal split.

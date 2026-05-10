# Thor Controller OSD And Graphics Capture

## Capture

- Screenshot: `reports/current-graphics-issue-screenshot_20260510_173850.png`
- Logcat tail: `reports/current-graphics-issue-logcat_20260510_173850.txt`
- Foreground package: `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`
- Running title observed in log paths: `PCSG00633`

## Graphics Issue Seen

- The screenshot is now Vita3K in-game, not RPCSX.
- The scene shows large flat blue/gray geometry blocks covering the environment while character and UI text render.
- This looks like an in-game renderer issue, likely around GXM/Vulkan state, surface/depth handling, shader translation, or driver interaction.
- The `FF 200%` overlay was visible, so fast-forward was active during capture. Re-test with fast-forward off before assuming timing is unrelated.
- Logcat repeatedly showed `Unhandled SIGSEGV` entries from the emulated guest path while assets were loading. The app did not crash in Android, but those guest faults may be related to bad game state or missing emulation behavior.

## OSD Mapping Issue

- Thor exposing `KEY_BACK`, `KEY_APPSELECT`, and `BTN_SELECT` as capabilities is normal. The bug was Vita3K treating plain gamepad Select/SDL `GamepadBack` as a short-press OSD opener.
- That effectively made both Android Back and Vita Select open the runtime OSD.
- Fix direction applied: only short Android Back (`AC_BACK`) opens/closes the runtime OSD; gamepad Select remains Vita `SCE_CTRL_SELECT` and only triggers runtime actions when used in explicit chords.
- `Select + R1`, `Select + right-stick down`, and `Select + right-stick up` remain reserved runtime shortcuts.

## Build And Device Push

- Android `:android:assembleReldebug` completed successfully after the mapping fix.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to AYN Thor device `c3ca0370`.
- Launched `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator` after install.
- Startup log confirmed the rebuilt `libVita3K.so` loaded and the selected custom Turnip driver path was active: `/data/data/org.vita3k.emulator.debug/files/driver/turnip_a8xx/vulkan.ad08xx.so`.

## Next Renderer Debug Steps

- Reproduce the screenshot with fast-forward off.
- Capture renderer settings, selected custom driver, resolution multiplier, and whether surface sync / shader interlock options are enabled.
- Test system driver vs selected Turnip driver. If the artifact changes, it is likely a driver/workaround path.
- Enable existing debug options where practical:
  - `--color-surface-debug`
  - `--log-active-shaders`
  - higher log level around SceGxm/renderer
- Add a focused Thor debug toggle to dump per-frame GXM render target, depth/stencil, shader hashes, texture upload metadata, and pipeline state for the bad frame.

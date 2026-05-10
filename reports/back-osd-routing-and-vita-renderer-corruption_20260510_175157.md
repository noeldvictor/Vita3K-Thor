# Back OSD Routing And Vita Renderer Corruption

## Input Routing Finding

- AYN Thor exposes both a real Android Back control and a real Select control.
- SDL's Android controller path can collapse Android `KEYCODE_BACK` and `KEYCODE_BUTTON_SELECT` into the same SDL gamepad Back button before Vita3K sees the event.
- The previous native-only fix kept Select from opening OSD, but it also meant physical Back could be lost as a distinct OSD control when Android/SDL delivered it through the gamepad path.
- Applied fix: `Emulator.dispatchKeyEvent` intercepts `KEYCODE_BACK` before SDL's gamepad mapper and forwards it to SDL's native keyboard path. Vita3K then sees `SDL_SCANCODE_AC_BACK`.
- The bridge logs `Routing KEYCODE_BACK ... through SDL native key path` so physical-device testing can prove whether Android delivered Back to the app.
- Android `:android:assembleReldebug` completed successfully, the APK installed to AYN Thor device `c3ca0370`, and a synthetic Back event produced both down/up bridge log lines in logcat.
- OSD panel opening still needs one physical test while a Vita title is actively running, because the runtime OSD intentionally stays closed when no title is loaded.
- Expected behavior:
  - Physical Back: opens/closes runtime OSD when a Vita title is running.
  - Physical Select: remains Vita Select and hotkey modifier.
  - `Select + R1`: fast-forward toggle.
  - `Select + right-stick down`: save state slot 0.
  - `Select + right-stick up`: load state slot 0.

## Renderer Finding

- The in-game screenshot replication confirms this is a Vita3K gameplay renderer issue, not a one-off screenshot capture issue.
- Captured symptom: large flat blue/gray geometry blocks obscure the environment while the character model and Japanese UI text still render.
- The screenshot had `FF 200%` active, so retest once with fast-forward off, but the repeated repro makes renderer investigation the next real track.
- Current suspect areas:
  - SceGxm render target and depth/stencil state translation.
  - Color/depth surface cache lifetime or aliasing.
  - Shader translator or pipeline state mismatch for this title's draws.
  - Custom Turnip driver interaction, especially if system driver produces a different artifact.

## Next Renderer Work

- Capture the same scene with fast-forward off.
- Capture the same scene on system driver and selected Turnip driver.
- Add a Thor renderer debug toggle that can dump the bad frame's render target, depth/stencil, shader hashes, texture upload metadata, and Vulkan pipeline state.
- Keep title-specific notes for `PCSG00633` until another title shows the same corruption.

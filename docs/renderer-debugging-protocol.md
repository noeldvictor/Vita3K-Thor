# Renderer Debugging Protocol

This fork treats renderer debugging as a controlled experiment loop. A visual improvement is not a fix until it is tied to a hypothesis, reproduced from a baseline, checked for regressions, and recorded in SQLite.

## The Rule

Every renderer change must answer five questions before code is trusted:

1. What exact symptom are we fixing?
2. What single variable changed?
3. What artifact proves the baseline?
4. What artifact proves the result?
5. What neighboring scene/game proves we did not break something else?

Use `tools/renderer_experiment.py` to make those answers explicit.

## Start A Packet

```powershell
python tools\renderer_experiment.py start `
  --case doa-venus-render-corruption `
  --title-id PCSH00250 `
  --platform windows `
  --subsystem surface-cache `
  --hypothesis "One specific renderer hypothesis" `
  --expected "The exact visual/log signal expected if true" `
  --scene "Exact scene, camera/menu state, and how we got there" `
  --baseline-artifact tmp\vita3k-win-debug\<baseline-burst> `
  --regression-target "DOA title loop" `
  --regression-target "UPPERS glitch scene"
```

This creates `tmp/renderer-experiments/<timestamp>_<case>_<subsystem>/` with:

- `manifest.json`: git status, config snippets, Vita3K window/process state, attempt-check output, and planned attempt metadata.
- `outcome.md`: baseline/result/regression template.
- `commands.ps1`: copyable commands for burst capture, analysis, and packet finish.

It also records a `planned` attempt in `reports/debug_knowledge.sqlite` unless `--no-db` is passed.

## Finish A Packet

```powershell
python tools\renderer_experiment.py finish `
  --manifest tmp\renderer-experiments\<packet>\manifest.json `
  --status inconclusive `
  --result "mixed-supports-involvement: hair changed but terrain regressed; not a fix" `
  --artifact tmp\vita3k-win-debug\<result-burst>
```

Allowed finish statuses are `succeeded`, `failed`, `inconclusive`, and `superseded`. Use `inconclusive` for contaminated tests, mixed results, or any run where A/B/A does not return to the original baseline.

## Outcome Labels

- `fixed`: original symptom gone, no obvious neighboring regression, stale toggles cleared, and proof exists on every affected platform.
- `improved`: symptom reduced but still present.
- `unchanged`: no meaningful visual/log change.
- `worse`: original or neighboring scene deteriorated.
- `mixed-supports-involvement`: one thing improved while another broke. This is useful evidence, not a fix.
- `contaminated-inconclusive`: more than one variable changed, scene state changed, debug props/configs were stale, or A/B/A did not return to baseline.

## Regression Matrix

For game-renderer fixes, keep the minimum matrix small but real:

- Original failing scene: burst before and after.
- Neighboring scene in same game: title/menu plus one gameplay or camera-rotation capture when relevant.
- One known sensitive regression title when the subsystem is shared. Current examples are DOA Venus for U2F/BCn/double-buffer behavior and UPPERS for depth/render-pass behavior.
- Android/Thor proof for Android-affecting code, including selected driver, debug props, and APK install time.

If a fix only passes the original screenshot but fails the matrix, the outcome is `mixed-supports-involvement` or `worse`, not fixed.

## Stop Conditions

Stop guessing after two failed or inconclusive attempts in the same subsystem. The next step must be better evidence: pause, draw isolation, surface/texture dump, shader metadata, GXM/API-call-site research, or a smaller repro.

Never leave a renderer turn with mystery state. Record:

- Current commit and dirty state.
- Active debug props/env vars.
- Installed Thor APK status if Android was affected.
- Which packet is open or closed.
- The next single hypothesis.

## Tool Escalation

Use heavier tools when live controls and dumps have stopped answering the question. Escalation is not a new guess; it must answer a specific unresolved question from SQLite.

1. **RenderDoc on Windows**: first escalation for a bad Vulkan frame when the symptom reproduces on desktop. Capture the frame, inspect the event list, pipeline state, bound descriptors, texture contents, render target history, and debug labels. Local helper:

```powershell
.\tools\windows\start-renderdoc-capture.ps1 `
  -TitleId PCSH00250 `
  -CaseSlug doa-statue-renderdoc `
  -BackendRenderer Vulkan `
  -TraceLimit 256 `
  -LogLevel 0 `
  -ApiValidation
```

RenderDoc captures go under `tmp/renderdoc/<case>/`. Use the capture to decide whether the bad pixels first appear in a producer render target, texture upload, shader output, or final composition. Do not use RenderDoc to re-run broad fhash/depth/cull guesses.

2. **GFXReconstruct**: use when we need a portable Vulkan API stream or replay independent of the live game. It is not currently installed in this workspace; install from the Vulkan SDK or LunarG releases before using. Store `.gfxr` files under ignored `tmp/gfxreconstruct/`.

3. **Android GPU Inspector / Snapdragon Profiler**: use after Windows evidence when the issue is Android/Adreno/Turnip-specific or performance-related. They are not currently installed in this workspace. Use AGI for Android frame/system profiling and Snapdragon Profiler for Adreno counters/snapshots when available. Record device, driver, trace settings, and trace path in SQLite.

4. **Ghidra**: use after capture/log evidence produces a concrete Vita-side question, such as which `sceGxm*` calls set a suspicious texture/surface, whether a title uses load/store depth-stencil in a way Vita3K ignores, or which shader constants/material flags feed a bad draw. Do not start from Ghidra just because the image is wrong.

Local Ghidra headless path:

```powershell
$ghidra = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat'
```

When analyzing legally owned local game modules, keep extracted ELFs, projects, scripts, and logs under ignored `tmp/ghidra/<case>/`. Commit only tooling/scripts and SQLite notes, never commercial binaries or decrypted game content.

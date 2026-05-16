---
name: vita3k-render-debug
description: Use when debugging Vita3K Thor renderer problems, flicker, missing geometry, black terrain, Vulkan/OpenGL differences, Windows-first repros, Android/AYN Thor final checks, surface dumps, draw isolation, and game-specific graphics regressions without repeating failed hypotheses.
metadata:
  short-description: Renderer OODA loop for Vita3K Thor
---

# Vita3K Renderer Debug

Use this skill for renderer corruption, flicker, missing geometry, black screens that still have a running game, shader/pipeline regressions, surface-cache bugs, Vulkan/OpenGL differences, or Windows-vs-Android render mismatches.

Pair it with `.agents/skills/vita3k-debug-rag/SKILL.md` for SQLite search/write commands and `.agents/skills/vita3k-input-automation/SKILL.md` for repeatable controller/button sequences.

## Non-Negotiable Order

1. Query `reports/debug_knowledge.sqlite` before touching renderer code.
2. Check the attempt ledger for the exact hypothesis.
3. Create a renderer experiment packet with `tools/renderer_experiment.py start`.
4. Capture a screenshot burst, not a single lucky frame.
5. Stabilize the scene with pause/runtime control when possible.
6. Isolate the frame with live renderer controls before rebuilding.
7. Add instrumentation if evidence is still ambiguous.
8. Patch the smallest emulator subsystem that explains the evidence.
9. Verify on Windows first unless the bug is proven Android-only.
10. Verify on AYN Thor before calling an Android-affecting fix done.
11. Close the experiment packet with `tools/renderer_experiment.py finish`, record the result in SQLite, then commit and push useful checkpoints.

Do not keep replaying long intros manually. Once the user reaches a bad scene, prefer pause, quickstate/save data, burst capture, surface dumps, draw filters, and input automation.

## Experiment Packet Gate

Before a renderer code edit, global Vulkan state change, Android debug property, shader/texture workaround, or config change that could affect another game, create a packet:

```powershell
python tools\renderer_experiment.py start --case doa-venus-render-corruption --title-id PCSH00250 --platform windows --subsystem surface-cache --hypothesis "specific one-variable hypothesis" --expected "what should visibly/logically change" --scene "exact scene and camera/menu state" --baseline-artifact <burst-dir> --regression-target "UPPERS glitch scene" --regression-target "DOA title loop"
```

The packet under `tmp/renderer-experiments/` snapshots git status, selected configs, active Vita3K window state, attempt-check output, and writes `outcome.md` plus `commands.ps1`. Treat this packet as the unit of work. If the packet says the tree was dirty, debug props were stale, or no baseline exists, resolve that before patching unless the dirty state is the experiment.

Close the same packet after verification:

```powershell
python tools\renderer_experiment.py finish --manifest tmp\renderer-experiments\<packet>\manifest.json --status failed --result "unchanged: burst still shows magenta title blocks; no shader hash changed" --artifact <burst-dir>
```

Outcome labels:

- `fixed`: original symptom gone, no visible neighboring regression, stale toggles cleared, proof exists on every affected platform.
- `improved`: symptom reduced but still present.
- `unchanged`: no meaningful visual/log change.
- `worse`: original or neighboring scene deteriorated.
- `mixed-supports-involvement`: one thing improved while another broke; useful evidence, not a fix.
- `contaminated-inconclusive`: more than one variable changed, A/B/A did not return to baseline, scene state changed, or debug props/configs were stale.

## SQLite Gate

Search recent and long-term knowledge:

```powershell
python tools/debug_knowledge.py search "<title id symptom surface shader platform>" --recent-days 30
python tools/debug_knowledge.py search "<title id symptom surface shader platform>" --long-term
```

Check before trying an experiment:

```powershell
python tools/debug_knowledge.py attempt check --case <case-slug> --platform windows --subsystem <renderer-area> --hypothesis "<specific planned change>"
```

If the check finds a failed or inconclusive near-duplicate, do not repeat it unchanged. Either stop, add new instrumentation, or record why the conditions changed enough to supersede the old attempt.

After the test:

```powershell
python tools/debug_knowledge.py attempt add --case <case-slug> --status failed --platform windows --subsystem <renderer-area> --hypothesis "<specific planned change>" --change "<what changed>" --test-command "<exact command>" --result "<what the burst/log proved>" --artifact <path-under-tmp>
```

## Windows First Loop

Use Windows as the primary loop for emulator-core renderer bugs:

```powershell
.\tools\windows\start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug> -BackendRenderer Vulkan -TraceLimit 256 -LogLevel 0
```

If the issue ROM is not already in `roms/issues/<TITLEID>/`, pass `-GameZip <path-to-local-zip>` or copy it with `tools/sync_issue_rom.ps1`. Keep ROMs under ignored `roms/` or `tmp/local-games/`; never commit game content.

Use input automation to reach the scene:

```powershell
.\tools\windows\send-vita3k-input.ps1 -Sequence circle,wait:500,start
```

For Japanese/Asian Vita prompts, Circle/O is often confirm and Cross/X is cancel.

Capture and analyze a burst:

```powershell
.\tools\windows\capture-vita3k-burst.ps1 -Topic <case-slug> -Count 120 -IntervalMs 250
python tools\analyze_screenshot_burst.py <burst-dir>
```

Use 8-12 frames for static proof, 60-120 frames for normal flicker, and longer runs such as 600 frames when the bug appears only after camera/menu rotation.

## Pause And Live Controls

Use the generated render-control file while Vita3K is running:

```powershell
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Action pause
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Action resume
```

Use live filters before rebuilding:

```powershell
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -StopAfter "rt=960x544:draw=5" -TraceLimit 96
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Skip "rt=960x544:draw=5" -TraceLimit 96
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Dump "rt=960x544:draw=0-8" -TraceLimit 64
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Skip "sample=62FF8000:draw=1" -TraceLimit 96
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Dump "addr=62FF8000:draw=20-40" -TraceLimit 64
```

Use `addr=` / `color_addr=` when isolating the producer render target being written. Use `sample=` / `tex=` when isolating consumer draws that read from a suspicious surface. This producer/consumer split is the UPPERS lesson: prove where the bad pixels first appear before changing renderer policy.

If pausing changes or hides the bug, record that in SQLite and switch to burst-based evidence. Timing and presentation bugs can disappear on a still frame.

## Surface And Draw Isolation

When final composition looks wrong, prove whether corruption is already inside an offscreen target:

```powershell
.\tools\windows\start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug> -DumpSurfaceAddr <hex-address> -DumpSurfaceLimit 300 -DumpSurfaceEvery 100 -TraceLimit 256 -LogLevel 0
```

Compare dumped surface PNGs with the window burst. If the surface dump is already corrupted, focus on the producer pass, vertex/pipeline/depth state, render-pass dependency, texture upload, or guest data feeding that target. If the surface dump is clean but presentation is wrong, focus on sampling, final composition, cache visibility, swapchain, or platform presentation.

Use progressive `stop_after` to find the first visible bad family, then test whether `skip` of the same draw actually fixes the live frame. A `stop_after` transition alone does not prove a single draw is the root cause. If a sampled consumer skip does not remove the corruption, immediately compare the producer surface dump with a window burst before trying another presentation/cache change.

## RenderDoc / Replay / Ghidra Escalation

If SQLite shows we are repeating texture/depth/cull/shader guesses, stop live toggles and escalate to evidence tools.

Use RenderDoc first for Windows Vulkan frames that reproduce locally:

```powershell
.\tools\windows\start-renderdoc-capture.ps1 -TitleId <TITLEID> -CaseSlug <case-renderdoc> -BackendRenderer Vulkan -TraceLimit 256 -LogLevel 0 -ApiValidation
```

Capture the bad frame and inspect event history, render target contents, descriptor bindings, sampled textures, shader IO, and debug labels. Record the capture path and the specific answer in SQLite. A RenderDoc capture should produce a question like "texture 0x722A0000 is already magenta before draw 27" or "producer target 0x62FF8000 is clean but consumer swizzle is wrong," not just "looks bad."

Use GFXReconstruct when a replayable Vulkan API stream is needed. It is optional tooling and may not be installed locally; if missing, record that and do not pretend `.gfxr` proof exists.

Use Android GPU Inspector or Snapdragon Profiler only after Windows evidence or for Android-only Adreno/Turnip/performance bugs. Record driver, device, trace settings, and trace path.

Use Ghidra only after the renderer capture/log asks a Vita-side question: imported `sceGxm*` call ordering, texture descriptor setup, scene load/store depth surfaces, material flags, or constants feeding a suspicious draw. Keep extracted ELFs and Ghidra projects under ignored `tmp/ghidra/<case>/`; commit only scripts and SQLite findings.

## Shader And GXM Research RAG

When a renderer bug points at shader translation or Vita GPU architecture, add a SQLite note before patching. Use public sources, local Vita3K source, and per-game artifacts; do not use leaked SDKs, proprietary compiler dumps, or commercial game binaries as committed evidence.

Record three layers:

- Architecture: SGX543MP4+ / Series5XT / USSE2, tile-based deferred rendering, PDS input fetch, USSE vertex/fragment execution, iterators, TAG/TF texture paths, PBE resolve/packing, and memory/cache implications.
- Vita3K implementation: GXP parser, USSE translator, SPIR-V/GLSL generation, shader cache/hash naming, Vulkan vertex input state, render-pass/surface-cache behavior, and feature flags such as shader interlock or scaled/RGB vertex attributes.
- Game evidence: title ID, vhash/fhash, suspicious draw ranges, texture/surface addresses, dumped GXP/SPIR-V/GLSL paths, burst screenshots, surface dumps, and whether the artifact exists before or after final composition.

For suspected shader bugs, first dump and inspect the shader/draw pair:

```powershell
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Dump "rt=960x544:draw=0-8" -TraceLimit 64
```

Then compare the shader program metadata against vertex stream data, texture state, uniform buffers, render target format, and feature flags. Do not assume every visual glitch is a bad shader translator; on a tile-based deferred GPU, wrong depth/load/store/resolve/packing can look like a shader failure.

## Patch Discipline

- Prefer emulator-correct fixes over game-specific hacks.
- Keep diagnostic toggles env-gated or property-gated until converted into a narrow code fix.
- Do not globalize Vulkan state changes such as depth clamp, cull mode, compare op, or clears unless multiple games and the Vita GXM semantics support it.
- Treat OpenGL crashes or differences as evidence, not automatic proof that Vulkan is wrong.
- If two experiments are inconclusive, add better logs/dumps before trying a third guess.
- Keep Android compatibility in mind while patching Windows, especially for Adreno/Turnip visibility, U2F formats, render-pass dependencies, and presentation alpha.

## Android / AYN Thor Gate

After a Windows/core fix is plausible, build/install and verify on Thor:

```powershell
adb devices
adb install -r android\build\outputs\apk\reldebug\android-reldebug.apk
```

Use cartridge ZIP launch rather than installed game copies for normal testing. Remove duplicate installed entries if they confuse the frontend; do not reinstall games just to test rendering.

For Android render checks, use burst and live properties:

```powershell
.\tools\android\capture-thor-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -Count 10 -IntervalMs 350
adb shell setprop debug.vita3k.render_trace 1
adb shell setprop debug.vita3k.render_stop_after "rt=960x544:draw=5"
adb shell setprop debug.vita3k.render_stop_after 0
```

Record device model, driver, build commit, title ID, settings, burst path, logcat path, and whether the Windows result matched Android.

## Active Case Habit

For DOA Venus (`PCSH00250`), do not trust memory. Query `case doa-venus-render-corruption` and the recent attempt list first:

```powershell
python tools/debug_knowledge.py case show doa-venus-render-corruption
python tools/debug_knowledge.py attempt list --case doa-venus-render-corruption --limit 20
```

SQLite owns active case facts. Do not bake volatile DOA conclusions into this skill unless they are durable workflow lessons.

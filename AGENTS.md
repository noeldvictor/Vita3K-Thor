# Vita3K Thor Agent Notes

These notes are for work in Vita3K Thor Experiment, a personal Android-focused Vita3K fork for AYN Thor testing. Keep changes practical, reversible, and clearly scoped to handheld compatibility work.

## Project Goals

- Treat Vita3K Thor as a performance, quality, and usability fork, not only a compatibility fork. Getting a game to boot is not enough if pacing, renderer quality, audio, input, OSD readability, or the debug loop are poor.
- Prefer fixes that improve emulator correctness and long-term quality over one-off game hacks. Game-specific work is acceptable for diagnosis, but it should usually lead to reusable renderer, timing, input, VFS, or tooling improvements.
- Performance work should be measured with logs, screenshots, profiles, frame pacing data, or before/after reports. Do not assume a change is faster or smoother without evidence.
- Debug tooling is part of the product direction. Keep improving the Windows and ADB loops so the user can play while Codex quickly captures evidence, isolates issues, patches, rebuilds, and verifies.
- End-user polish matters: features should be discoverable, controller-first, readable on handheld screens, and explained in plain README language separate from technical implementation notes.
- Stable, fast debugging is a primary project goal. Build and use durable knowledge tooling before chasing renderer/game issues in circles.
- Renderer debugging protocol lives in `docs/renderer-debugging-protocol.md`; keep it aligned with `AGENTS.md`, `.agents/skills/vita3k-render-debug/SKILL.md`, and `tools/renderer_experiment.py`.

## Source Control

- Writable remote is the user's fork: `origin = git@github.com:noeldvictor/Vita3K-Thor.git`.
- Upstream reference is `https://github.com/Vita3K/Vita3K`.
- Always push with SSH; do not switch `origin` to HTTPS.
- Do not push to upstream Vita3K from this checkout.
- Commit and push often. Prefer small pushed checkpoints after a buildable code change, a useful report, an Android/Thor install, a debug-tool improvement, or a confirmed investigation result instead of letting local work pile up.
- Keep Thor-specific changes easy to identify so broadly useful fixes can be proposed upstream separately.
- Do not commit APK outputs, build folders, downloaded driver ZIPs, extracted drivers, caches, SDKs, firmware, license files, saves, shader caches, ELF dumps, screenshots/log dumps, or game content unless the user explicitly requests a narrow proof asset.
- Upstream `master` may include structural rewrites. As of 2026-05-16, `upstream/master` contains the Qt/Android GUI overhaul from `91f533f8`; a direct merge conflicts with Thor Android, ImGui OSD, config/input, audio, kernel, and renderer-adjacent work. Do not merge it straight into `master`. Use a short-lived integration branch, record conflict findings in SQLite, and port Thor features deliberately or cherry-pick narrow upstream fixes when they do not depend on the overhaul.
- If the user wants GitHub's "behind upstream" count cleared after a structural upstream batch has been reviewed and rejected, use an ancestry-only `ours` merge with a clear commit message and SQLite note. This keeps the Thor tree unchanged while recording that the upstream batch was intentionally acknowledged.

## Debug Knowledge Base

- `reports/debug_knowledge.sqlite` is the canonical report and RAG store for emulator/game debugging. Markdown reports are legacy context or human exports only; do not create new durable Markdown reports by default.
- `python tools/debug_knowledge.py case focus` is the current lead-case lock. When a user says one game is still busted, set or verify focus before using filesystem recency, latest screenshots, or old experiment folders. Non-focused game work is allowed only as an explicit regression guard or after changing focus in SQLite.
- Use the committed skill `.agents/skills/vita3k-debug-rag/SKILL.md` for emulator/game issue work. It encodes the expected SQLite-first, Windows-first, Android-final workflow.
- Use the committed skill `.agents/skills/vita3k-render-debug/SKILL.md` for renderer corruption, flicker, black terrain, missing geometry, surface dumps, draw isolation, and Windows-first/Android-final graphics fixes.
- Use the committed skill `.agents/skills/vita3k-input-automation/SKILL.md` for repeatable Windows and Thor button presses during repro setup.
- Use `tools/debug_knowledge.py` before code edits on any recurring bug:

```powershell
python tools/debug_knowledge.py search "doa venus black terrain android 564cd0" --recent-days 30
python tools/debug_knowledge.py search "doa venus black terrain android 564cd0" --long-term
```

- Split reports inside SQLite by `domain`: use `domain=game` with a Vita title ID for game-specific behavior, and `domain=emulator` for renderer/core/tooling architecture that spans games.
- SQLite stores canonical raw report text in `chunks.text` and local deterministic sparse hashed vectors in `chunks.embedding_json`; FTS5 is used when available. Treat these as repo-local recall aids, not external/model embeddings.
- Use the attempt ledger to prevent circular debugging. Before trying a renderer/core hypothesis, run `python tools/debug_knowledge.py attempt check --case <case-slug> --platform <windows|android> --subsystem <area> --hypothesis "<specific planned change>"`; after the test, record it with `attempt add --status succeeded|failed|inconclusive|superseded` plus the build command, burst path, shader hashes, result, and commit if any.
- Prefer updating an existing attempt fingerprint over adding a near-duplicate. If an old failed attempt becomes valid because new evidence changes the conditions, record a new attempt that `--supersedes` the old attempt and says exactly what changed.
- Record observations, decisions, fixes, tests, regression risks, and commit hashes in SQLite. Keep raw screenshots, burst captures, logcat dumps, profile dumps, save experiments, and shader dumps under ignored `tmp/` unless explicitly promoted.
- Record game/platform compatibility checkpoints in SQLite whenever a game becomes known-good, known-bad, regressed, blocked, or partially fixed at a commit. Use `python tools/debug_knowledge.py compat add --title-id <TITLEID> --platform <windows|android-thor> --commit <hash> --status works|regressed|broken|partial|blocked --scene "<exact scene>" --summary "<human result>" --artifact <proof>` and `compat list` before blaming a new change. This ledger is the answer to "which commit did this game work on?"
- Local issue ROMs live under ignored `roms/issues/<TITLEID>/`; regression ROMs live under ignored `roms/regression/<TITLEID>/`. Use `tools/sync_issue_rom.ps1` to copy or pull games there as needed. Never commit `roms/`.
- Current focused lead case is DOA Venus (`PCSH00250`) renderer corruption unless `case focus` says otherwise. UPPERS is currently a regression guard for renderer/depth changes, not the lead case while DOA remains user-visible broken.

## Repo-Local Skills

- Repo workflow skills live under `.agents/skills/` and are committed with this fork. Do not create global/user skills for this repo-specific Vita3K Thor process unless the user explicitly asks.
- Keep skills decomposable. A skill should be a focused operating card with concrete tools, gates, and outputs; it should not become a god-skill that tries to explain the whole emulator.
- Prefer loading the smallest skill or pair of skills needed for the current task. Use `vita3k-debug-rag` for SQLite recall, then add a narrow loop skill only when it matches the next action.
- Current focused skills:
  - `.agents/skills/vita3k-debug-rag/SKILL.md`: SQLite search/read/write and case focus.
  - `.agents/skills/vita3k-render-experiment-gate/SKILL.md`: anti-loop preflight before renderer/core experiments.
  - `.agents/skills/vita3k-windows-render-loop/SKILL.md`: Windows-first launch, input, burst, live-control, and rebuild loop.
  - `.agents/skills/vita3k-thor-android-loop/SKILL.md`: AYN Thor install, launch, ADB props, burst capture, and Android proof.
  - `.agents/skills/vita3k-regression-ledger/SKILL.md`: compatibility checkpoints, "worked before" research, and regression matrix discipline.
  - `.agents/skills/vita3k-perf-profiler/SKILL.md`: measured speed, frame pacing, thermal, and profile work.
  - `.agents/skills/vita3k-ghidra-escalation/SKILL.md`: static-analysis escalation after renderer evidence asks a concrete Vita-side question.
  - `.agents/skills/vita3k-input-automation/SKILL.md`: repeatable Windows and Thor button/control sequences.
  - `.agents/skills/vita3k-render-debug/SKILL.md`: renderer router and deeper graphics reference; use it when a narrow skill is not enough.
- When adding a new skill, also add its `agents/openai.yaml`, keep it repo-local, and mention the concrete scripts/SQLite commands it owns.
- If a skill starts accumulating unrelated topics, split it before adding more content. The split should follow action boundaries such as "capture", "profile", "regression ledger", "Ghidra escalation", or "Android proof".

## Experiment Discipline

- Active-case lock: start every renderer/core turn with `python tools/debug_knowledge.py case focus`. If the next action targets a different case, stop and classify it as either a deliberate focus change or a regression guard. Do not let newest `tmp/` folders, screenshot mtimes, or stale planned attempts choose the active game.
- Hard anti-loop gate: before any renderer/core experiment, read the SQLite case history and produce a short preflight summary in the working notes or user update: current commit, platform, scene, debug props/env, matching prior attempts, what those attempts proved, and the one reason this run is genuinely new. If the only difference is "try it again," stop and choose instrumentation or documentation instead.
- For active game bugs, first run `case show`, `attempt list --limit 20`, `search --recent-days 30`, `search --long-term`, and `attempt check` with the exact shader hash, texture/surface address, prop name, and visual symptom. Do not run a live toggle or rebuild until the result is classified as `new`, `changed-conditions`, or `needs-instrumentation`.
- If SQLite shows the same hypothesis, shader/address, or prop was already failed or inconclusive under comparable conditions, summarize that old result and do not repeat it. A repeat is allowed only when one controlled condition has changed, such as a different commit, fixed stale props, new scene/save, different renderer/backend, or newly added logging that can answer a question the old run could not.
- Planned or open attempts are blockers. Resolve the current planned attempt as `failed`, `inconclusive`, `superseded`, or `succeeded` before starting a nearby experiment, otherwise the ledger becomes a pile of half-memory and Codex starts circling.
- Treat every renderer/core investigation as a named experiment, not a vibe check. Before changing code or live props, write down the current active case, exact hypothesis, subsystem, platform, expected visual/log change, baseline artifact, and rollback path.
- For renderer changes, create an experiment packet before editing or toggling anything risky: `python tools/renderer_experiment.py start --case <case-slug> --title-id <TITLEID> --platform <windows|android|windows-android> --subsystem <renderer-area> --hypothesis "<specific hypothesis>" --expected "<what should change>" --scene "<exact repro scene>"`. The packet under `tmp/renderer-experiments/` snapshots git/config/process context, checks SQLite for similar attempts, records a planned attempt, and writes an outcome/regression template. The packet refuses non-focused cases unless `--regression-guard` or `--allow-non-focus-case` is explicit.
- Close each renderer experiment with `python tools/renderer_experiment.py finish --manifest <packet>/manifest.json --status succeeded|failed|inconclusive|superseded --result "<actual visual/log outcome>" --artifact <burst-or-log-dir>`. If the result has mixed improvement plus new breakage, use `--status inconclusive` and label the result `mixed-supports-involvement`, not fixed.
- Query the case first: `case show`, `attempt list --limit 20`, `search --recent-days 30`, and `attempt check`. If a similar attempt already failed, do not repeat it unless the new attempt explicitly changes one controlled variable and records why the old result no longer answers the question.
- When the user says we are going in circles or redoing work, immediately stop experiments, inspect SQLite/AGENTS, record a process note, and restart from the anti-loop gate. Do not continue renderer toggles in that turn unless the user explicitly tells Codex to resume debugging.
- Use one variable per experiment. Do not combine a renderer code patch, a stale Android property, a shader cache change, a driver switch, and a new save/location in the same test. If multiple things changed, mark the attempt `inconclusive` or `contaminated` and do not use it as proof.
- Clear and record debug props before every Android comparison. Capture `adb shell getprop | Select-String -Pattern 'debug.vita3k'` or the helper equivalent, clear irrelevant props, then name the remaining intentional props in the SQLite attempt body.
- Treat Android debug prop values `0`, `false`, and `off` as disabled, never as hash/address prefixes. When adding any fhash/vhash/address matcher, check the disabled values before prefix matching; otherwise a stale `setprop ... 0` can accidentally match shader hashes beginning with `0` and contaminate later renderer tests.
- Prefer A/B/A checks when a live toggle exists: capture baseline A, enable the experimental toggle and capture B, then disable it and capture A again. If A does not return, the test found state contamination or timing dependence, not a clean fix.
- When an old screenshot shows a new renderer symptom but the active APK/config/props have changed since then, do a same-session default retest before patching. If a fresh baseline no longer reproduces the issue, record it as stale/transient in SQLite and stop; do not write renderer code to fix a ghost frame.
- Stop after two failed or inconclusive guesses in the same subsystem. The next step must be better instrumentation, draw/surface/texture dump evidence, Ghidra/API-call-site research, or a smaller repro. Do not try a third broad Vulkan state toggle just because it is easy.
- Separate experiments from fixes. A diagnostic property, fhash skip, draw skip, forced depth mode, forced texture path, or shader hack is not a fix until it is converted into emulator-semantics code, verified against the original symptom, and regression-checked against at least one neighboring scene/game.
- Renderer outcomes must be described with a stable label: `fixed`, `improved`, `unchanged`, `worse`, `mixed-supports-involvement`, or `contaminated-inconclusive`. `fixed` requires the original symptom gone, no obvious neighboring-scene/game regression, no stale debug toggles, and Windows/Android proof when Android-affecting.
- Keep a compact experiment ledger in SQLite, not memory. Each attempt result must say: what was changed, exact command/build/commit/APK installed, title ID, scene, driver/settings, shader hashes or surface/texture addresses, burst/log artifacts, and whether the hypothesis was falsified, supported, or still ambiguous.
- When an experiment changes visible output but damages adjacent geometry, record it as "supports involvement, not a fix." The next hypothesis should explain both the improvement and the regression.
- Before ending a debug turn, leave the workspace in a knowable state: record current debug props/env vars, whether the installed Thor APK matches the source tree, which local renderer patches are diagnostic only, and the next single hypothesis. Do not leave mystery toggles active for the next agent.

## Input Automation

- Use committed input helpers instead of asking the user to repeatedly press obvious buttons during repro setup.
- Windows desktop debug loop: use `tools/windows/send-vita3k-input.ps1`, which focuses the Vita3K window and sends the default keyboard-mapped Vita controls. Examples: `-Sequence circle,wait:500,start`, `-Sequence down:2,cross`, `-Sequence osd`, `-Sequence fast_forward`, `-Sequence click`.
- Android/AYN Thor loop: use `tools/android/send-thor-input.ps1`. Default `KeyEvent` mode is best for simple prompts; add `-DisplayId 0` when display routing is suspect. `-Mode Sendevent` is best for raw Odin Controller routing, Back/Select conflicts, OSD chords, and cases where Android keyevents do not reach SDL like a real controller; pass `-InputDevicePath /dev/input/eventN` if auto-discovery picks the wrong event.
- Supported common button names include `cross`, `circle`, `square`, `triangle`, `start`, `select`, `back`, `up`, `down`, `left`, `right`, `l1`, `r1`, `l2`, `r2`, `l3`, and `r3`; use `button:count`, `wait:ms`, or `button+button` chords for short sequences.
- Android input helpers also accept `tap:x:y` and `keyevent:<code>` tokens for visible prompt buttons and raw mapping checks. Quoted `'tap:x,y'` works too, but use `tap:x:y` in examples so PowerShell does not split coordinates into two array values.
- Useful aliases are `osd` for L3+R3 and `fast_forward` for Select+R1. Windows also supports `save_state` and `load_state` through the default keyboard right-stick mapping. On Android, prefer runtime control or OSD automation for quickstates until the exact Thor right-stick axis events are captured for the current firmware.
- For Japanese/Asian Vita games, Circle/O can be confirm and Cross/X can be cancel. Prefer `circle` for DOA Venus autosave/title prompts unless a screenshot/log proves Cross is expected.
- Do not conflate system confirm with game input. `sys-button` controls Vita shell/common-dialog O/X behavior; the runtime OSD `Game X/O` control is a per-title game input swap for `sceCtrl` button reads. Japanese-game convenience presets should set both intentionally and persist them through config.
- Use Windows `click` automation for Vita3K/ImGui modal buttons that do not respond to Vita controls. `click:x,y` is window-relative and `click@x,y` is absolute desktop coordinates.
- If a prompt is stuck, use `.agents/skills/vita3k-input-automation/SKILL.md` as the escalation ladder before asking the user to press the same button manually: focus/app responsiveness, normal keyevent, display-routed keyevent, raw Odin `sendevent`, touch fallback, then SDL/input-code investigation.
- After input automation materially changes a repro state, add a SQLite `test` or `observation` entry with the exact script, sequence, platform, and result.

## Safety Scope

- Work only on emulator compatibility, Android handheld UX, controller/touch behavior, renderer settings, driver selection, diagnostics, and build/test documentation.
- Do not add piracy, DRM bypass, license bypass, key distribution, firmware redistribution, online cheating, anti-cheat bypass, or commercial game redistribution support.
- Keep all docs explicit that users must provide their own legally dumped content and homebrew.
- Third-party driver downloads must stay user-initiated, clearly sourced, and stored under app-local custom-driver paths.

## Android And Thor Focus

- Primary target is AYN Thor Base/Pro/Max: Snapdragon 8 Gen 2, Adreno 740, active cooling, LPDDR5X, and UFS4 storage.
- Ignore Thor Lite for defaults and optimization decisions unless the user explicitly asks for Lite work. Thor Lite is a different Snapdragon 865 / Adreno 650 target.
- Prefer Android `arm64-v8a` test paths. Desktop build support should not be broken, but desktop packaging is not this fork's main purpose.
- The Android app label and package id should remain unchanged unless the user explicitly asks to split installs.

## Custom Driver Workflow

- Existing Vita3K custom drivers live under Android internal storage in the `driver/` directory and are selected through `custom_driver_name`.
- Turnip driver download UX should use K11MCH1/AdrenoToolsDrivers as the visible source and should install standard ZIP assets through the same custom-driver extraction path as manual installs.
- The picker should let the user refresh GitHub releases, see which ZIP is recommended for AYN Thor / Adreno 740, download ZIPs, install and select a ZIP, select an already-installed driver, and delete cached downloaded ZIPs.
- Keep downloaded Turnip ZIPs in app-local `driver_downloads/` only. Do not commit or externalize driver ZIPs.
- After installing a driver from the picker, select it immediately in the GPU settings and remind users that emulation must reboot for the renderer change to apply.
- Extract only safe relative ZIP entries; never allow absolute paths or `..` traversal from downloaded archives.
- If a downloaded driver is already installed, selecting the existing copy is acceptable and should not be treated as a fatal error.
- The Thor recommendation is a convenience heuristic, not a compatibility guarantee. Prefer recent Turnip ZIPs that appear Gmem/a7xx-friendly; deprioritize debug/beta/a8xx-specific assets.

## Build Notes

- Local Windows Android builds need Java 21, Android SDK/NDK 27.3.13750724, and vcpkg with the arm64 Android Vita3K dependencies. The known-good local toolchain paths are:

```powershell
$env:JAVA_HOME='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\jdk-21.0.11+10'
$env:ANDROID_HOME='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk'
$env:ANDROID_SDK_ROOT=$env:ANDROID_HOME
$env:ANDROID_NDK_HOME='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk\ndk\27.3.13750724'
$env:VCPKG_ROOT='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\vcpkg'
$env:Path="$env:JAVA_HOME\bin;$env:VCPKG_ROOT;$env:ANDROID_HOME\platform-tools;$env:Path"
```

- Bootstrap and install vcpkg deps if needed:

```powershell
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"
& "$env:VCPKG_ROOT\vcpkg.exe" install boost-system boost-filesystem boost-program-options boost-icl boost-variant openssl zlib --triplet=arm64-android
```

- Stage assets before Gradle:

```powershell
$env:ANDROID_NDK_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\27.3.13750724'
$env:VCPKG_ROOT = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\vcpkg'
Copy-Item -Recurse -Force data android/assets
Copy-Item -Recurse -Force lang android/assets
Copy-Item -Recurse -Force vita3k/shaders-builtin android/assets
.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug
```

- Expected reldebug APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`.
- After a Codex/app restart, do not assume `ANDROID_NDK_HOME` or `VCPKG_ROOT` are still inherited by Gradle. Set them in the same PowerShell session as `gradlew.bat`; otherwise `cmake/vcpkg_android.cmake` fails during configure before compiling native code.
- On Windows, `cmake/vcpkg_android.cmake` must normalize `ANDROID_NDK_HOME` and `VCPKG_ROOT` with `file(TO_CMAKE_PATH ...)`; raw backslashes can break generated CMake files with invalid `\U` escapes.
- If local Android SDK, NDK, Java, vcpkg, or signing setup is missing, do not claim an APK was built.
- For C++ changes, run the lightest practical checks first, such as `git diff --check` and a targeted configure/build when the local toolchain is available.

## Fast Debug Loop Strategy

- Default to the fastest loop that can prove the hypothesis: live renderer controls first, Windows incremental build second, Android APK/native validation third.
- Shader caches, game content, issue saves, screenshots, and profile dumps are not part of the APK and should stay in ignored local storage. Clearing or regenerating a shader cache is faster than rebuilding and reinstalling when the shader translator code did not change.
- Renderer/shader-translator C++ changes currently require at least a process restart and native rebuild. Do not pretend a running Android process can hot-swap already-loaded C++ renderer code unless a debug-only dynamic override loader has been implemented and verified.
- Investigate a debug-only Android native-library fast path for renderer work: build the changed native library, push it to app-local storage, and have debug startup load the override before normal emulator init. Keep it disabled for release builds, require an app restart, and verify it cannot load untrusted external paths.
- Built-in/screen shader text or SPIR-V assets may be candidates for app-local override loading so shader experiments can be pushed without a full APK. Treat this as tooling work with explicit validation on Windows and Thor before relying on it.
- Do not use Android as the primary loop for emulator-core bugs unless the bug is Adreno/Turnip, SurfaceFlinger, Android input, APK packaging, or device-performance specific.
- When live controls and surface dumps stop answering a renderer question, escalate to tools in this order: RenderDoc on Windows for frame/resource inspection, GFXReconstruct for replayable Vulkan API streams when installed, Android GPU Inspector or Snapdragon Profiler for Android/Adreno/performance-only questions, then Ghidra for Vita-side API/material intent. Do not start Ghidra until the capture/log names the exact `sceGxm*`, texture, surface, shader, or material question.
- Local tool inventory on 2026-05-15: RenderDoc is installed at `C:\Program Files\RenderDoc\renderdoccmd.exe`; Ghidra headless is installed at `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat`; GFXReconstruct, Android GPU Inspector, and Snapdragon Profiler were not found on `PATH` or the common local toolchain paths. Use `tools/windows/start-renderdoc-capture.ps1` for the immediate Windows frame-capture loop.

## Playing Without Install

- Vita3K Thor's preferred game flow is ZIP/cartridge mode, not install mode: use `--cartridge <path-to-vpk-or-zip>` to mount game content as a read-only virtual game card for the session instead of adding it to the installed app library.
- Do not install ROM ZIPs/VPKs into `ux0/app` for normal Thor testing. Preserve install code for upstream compatibility and explicit package-management tests only; day-to-day game launch, scan, debug, and frontend work should use virtual cartridges.
- On Android, archive startup should default to virtual cartridge mounting even if a caller forgets `--cartridge`; do not reintroduce install-first handling for ZIP/VPK game launches.
- Android builds shallow-scan `/sdcard/roms/psvita`, `/sdcard/Roms/psvita`, `/storage/emulated/0/roms/psvita`, and `/storage/emulated/0/Roms/psvita` by default when `scan-virtual-cartridges` is enabled. The scanner should also discover removable SD card roots under `/storage/<card>/Roms/psvita`, `/storage/<card>/roms/psvita`, and common Emulation folder variants. Compatible `.zip`/`.vpk` archives in the root or one direct child folder, plus extracted direct child folders containing `sce_sys/param.sfo`, are listed as virtual cartridges in the app grid.
- Virtual cartridge app entries are part of the normal app-list cache. Keep unchanged ZIP/VPK entries by source path, size, and mtime instead of re-opening every archive on startup, and cache archive icon/background assets under app-local cache storage. Invalidate when the source archive/param changes or the scan root no longer covers the source path.
- Some cartridge/NoNpDrm-style ZIPs have readable `param.sfo` but PFS-encrypted app files. Detect these by checking app `eboot.bin` and `sce_sys/icon0.png` headers, show an `E` encrypted-content badge in the app list, and fail launch with a clear diagnostic instead of trying to run encrypted bytes. Do not add DRM, license, or key bypass code.
- Virtual cartridge app entries must launch directly, not through Live Area, because the content is not installed under `ux0/app`.
- On device, the visible launcher path is `File` -> `Play ZIP as Cartridge`; select a `.zip` or `.vpk`, wait for the virtual cartridge cache to mount, then press `Start Cartridge`.
- Android file/front-end launching is supported through `ACTION_VIEW` and `ACTION_SEND` for `.zip`/`.vpk`-style archive intents. The Android bridge converts the incoming file/content URI into `-a true --cartridge <path>`.
- If Android only provides a content URI without a raw filesystem path, copy the URI into app-local `cartridge_launch/` and launch the copied archive from there.
- Do not enable CLI11 Windows-style slash options on Android/Linux. Absolute paths like `/storage/<card>/Roms/psvita/game.zip` must parse as positional `content-path` values for open-with and ADB cartridge launches.
- ZIP/VPK introspection must tolerate translated and nonstandard archives. Normalize separators/case for discovery, score likely game roots (`sce_sys/param.sfo`, `eboot.bin`, title-id folders, `app/` / `ux0/app/`), and deprioritize `patch/`/`rePatch/` roots as launch roots so the base app root wins.
- Cartridge launch now mounts `app0:` directly against the chosen `.zip`/`.vpk` archive and lazily inflates individual file entries on open. When the same archive also contains `patch/<TITLEID>/` or `rePatch/<TITLEID>/` folders, mount them as read-time overlays over the base app root, with `rePatch` applied after `patch` so translated/modded files can override official update files. Do not reintroduce whole-archive extraction for this path unless it is explicitly a fallback.
- The visible menu path should add only a transient in-memory app entry for launch and must not save the cartridge title into the normal installed app cache.
- Normal `content-path` without `--cartridge` remains the upstream install-and-run convenience path for `.vpk`/`.zip` archives or content folders.
- The `--installed-path` / `-r` path runs an already-installed app path from Vita3K storage. A future no-install-like UX would need a new staging, cache, or mount feature and must not bypass ownership or license expectations.
- Cartridge mode should stay read-only from the emulated app side. Do not let games create, delete, or rename files under the virtual card path.
- The direct archive VFS currently reads each requested ZIP entry into memory when opened; this is real no-staging ZIP launch, but not yet compressed random-access streaming.

## Cheats And Runtime Hotkeys

- Cheats are offline single-player only. Do not add online cheating, anti-cheat bypass, DRM bypass, license bypass, or commercial cheat pack redistribution.
- VitaCheat `.psv` files can be detected by title ID from repo/user cheat roots such as `cheats/<TITLEID>.psv`, `cheats/db/<TITLEID>.psv`, shared `cheats/`, `ux0/vitacheat/db/`, and Android shared-storage/SD-card roots like `/storage/<card>/cheats/psvita`, `/storage/<card>/VitaCheat/db`, and `/storage/<card>/Roms/psvita/cheats`.
- Only commit third-party cheat files when their license/source permits redistribution. Otherwise commit importer/conversion tooling and user instructions, not the database itself.
- Use `tools/sync_vitacheat_db.ps1` to clone/update an external VitaCheat DB into ignored `tmp/` storage and push it to the Thor SD card; do not commit the cloned DB.
- Games with detected cheat files show a `C` badge in the app list.
- Runtime cheat support is fail-closed and currently applies only enabled `_V1` VitaCheat writes: `$0000`, `$0100`, `$0200`, ARM/code writes `$A000`, `$A100`, `$A200`, level-1 pointer writes ending in `$3300`, plus simple `$B200` main-module segment-relative base selectors. Unsupported multi-level pointer, condition, block, and button-code formats are skipped and logged.
- `tools/convert_vitacheat.py` converts VitaCheat `.psv` files into JSON metadata for auditing and future UI work. The emulator runtime still reads `.psv` directly.
- Runtime shortcuts reserved for Thor testing: `Select + R1` toggles the currently configured fast-forward speed, `Select + right-stick down` requests save state, and `Select + right-stick up` requests load state.
- `fast-forward-speed-percent` defaults to 200 and is clamped from 101 to 1000 when toggled. The runtime OSD exposes Off, 2x, 3x, and 4x preset buttons; choosing 2x/3x/4x updates `fast-forward-speed-percent` so the `Select + R1` hotkey follows the selected preset. Fast-forward must update display/vblank pacing, kernel wait pacing, and guest clock APIs together; keep `emuenv.display.speed_percent` and `emuenv.kernel.speed_percent` in sync so vblank waits, `sceKernelDelayThread`, kernel timers, wait timeouts, `sceKernelGetProcessTime*`, `sceKernelGetSystemTimeWide`, libc time/gettimeofday, and RTC current tick do not stay at real-time speed.
- SDL fast-forward audio must never raise SDL's stream frequency ratio above `1.0x`; that makes chipmunk audio. Use FFmpeg `atempo` for pitch-preserving tempo changes when available, and otherwise fall back to normal-pitch buffer skipping with light crossfade instead of frequency-ratio speed-up or callback-local grain skipping.
- Do not use Android toast popups for fast-forward, save-state, or load-state feedback. Prefer OSD/overlay state and logs so gameplay is not interrupted.
- Save-state/load-state shortcuts are under a Windows-first stability gate. Current code may capture a disk-backed per-game slot 0 with CPU contexts, allocated guest memory pages, allocator maps, page CRCs, guarded `.tmp`/`.bak` replacement, and checked named metadata sections for timing, kernel objects, IO/VFS, display, and audio. Timing, matched-thread metadata, and matched open-file positions are parsed and have restore code ready behind the gate. The restore-readiness manifest covers section names, kernel object counts, IO/VFS counts, AVPlayer detection, and missing serializers. Restore is intentionally refused by default after Windows crash/hang evidence. Do not re-enable restore, ship Android/Thor quickstate tests, or claim PPSSPP-style reliability until Windows proves repeated restore, post-resume restore, corrupt-file refusal, movie/AVPlayer refusal or serialization, and no process crash across a real game matrix.
- `save-state-dir` can move the state root to a custom directory; relative paths resolve under the shared Vita3K data path, and absolute paths are used as-is. `save-state-compression-level` uses miniz level 0-9 and defaults to 1 for fast compression.
- PPSSPP-level durable save/load is the target, not the current state. Treat save-state work as a serialization subsystem: CPU contexts, guest RAM, allocator maps, kernel thread/object/wait state, GPU/display/renderer state, texture/surface caches, audio state, AVPlayer/movie state, IO/VFS handles, timing state, and per-game metadata all need versioned capture/restore plus refusal paths for unsafe states. Update the restore-readiness manifest whenever one of those layers becomes real so future agents can see exactly what changed.

## Runtime OSD

- The game-running OSD opens from a short Android Back press (`AC_BACK`) on AYN Thor. A long Android Back press should route to the Vita PS/Home path and return to the Vita LiveArea/home flow for the running app. Do not bind plain gamepad Select/`BTN_SELECT`/SDL `GamepadBack` as a second OSD opener; Select must remain Vita `SCE_CTRL_SELECT` and the modifier for `Select + R1`, `Select + right-stick down`, and `Select + right-stick up`.
- L3 + R3 is also a runtime OSD toggle for desktop/Windows controller testing and handhelds with both stick-click buttons. Keep it as a pressed-edge chord so holding both sticks does not repeatedly flicker the OSD, and keep single L3/R3 mapped as normal Vita controls.
- Long Android Back must first restore fast-forward to 100% before routing PS/Home, and virtual-cartridge LiveArea lookups must resolve either source archive path or title ID without null-crashing.
- AYN Thor/Odin controller input may expose Back/Select through multiple Android paths (`KEY_BACK`, `KEY_APPSELECT`, `BTN_SELECT`, and SDL gamepad Back). Back and Select are separate controls: `Emulator.dispatchKeyEvent` forwards `KEYCODE_BACK` directly to SDL's native key path so Vita3K sees `AC_BACK`; `BTN_SELECT`/SDL gamepad Back is Vita Select and Select chords only. When debugging OSD behavior, capture `getevent -lp`, SDL/logcat event traces, and before/after screenshots before changing bindings.
- Opening the OSD pauses guest threads by default. Closing/resuming from the OSD resumes guest threads unless the user explicitly changed pause state in the OSD.
- OSD feedback should replace toast feedback for runtime actions. Fast-forward, save/load quickstate, cheat toggles, and pause/resume should update OSD/overlay status and logs.
- The OSD Controls section should expose two separate choices: `System Confirm` for emulator/Vita shell O/X convention, and `Game X/O` for per-game controller swap. Keep the Japanese-game helper as a preset layered on those controls, not as a hidden global mode.
- OSD first-level actions currently include Resume, Pause/Resume, Settings, Save State slot 0, Load State slot 0, Screenshot, Renderer Trace, Off/2x/3x/4x fast-forward presets, and disabled placeholders for Reset Game and Close Game.
- Keep the OSD readable over bright or glitchy game frames: dim the game behind it, use an opaque high-contrast panel, and size text/buttons for handheld viewing rather than desktop mouse precision.
- Renderer Trace is a runtime diagnostic switch. When enabled it emits `ThorRenderTrace` logcat lines for Vulkan scene setup, the first 32 draws per scene, and texture configure/upload events. Include render target, color/depth surface addresses, formats, depth/stencil state, shader hashes, texture counts, texture address/format/type/stride/upload bytes, mapping mode, surface sync state, and driver flags.
- For ADB-only render/crash investigations, launch with `--thor-render-trace` to enable the same renderer trace at startup, or use `tools/thor_adb_debug_capture.ps1 -GamePath <zip> -RenderTrace` to clear logcat, launch, and capture screenshot/logcat/crash-buffer/window/meminfo artifacts under ignored `tmp/`. Summarize durable findings into `reports/debug_knowledge.sqlite`.
- The Cheats panel lists detected cheats for the current title, shows enabled/disabled state, allows toggling individual cheats, shows unsupported-code counts, and provides a reload-cheat-file action.
- The status area shows title ID, current speed percentage, selected custom driver on Android, quickstate slot status, and whether a matching cheat file was loaded.
- Keep the OSD usable with controller only: D-pad/left stick navigates, Cross/A confirms, Circle/B cancels, Back/Select closes. It should also work with touch/mouse when available. ImGui navigation must remain enabled, and the SDL backend must use real SDL3 gamepad instance IDs/player index instead of assuming gamepad index `0`.
- Keep OSD rendering lightweight and in the existing ImGui path. Do not open the Vita Live Area or normal settings dialog just to perform runtime actions.

## Graphics Debugging And Profiling

- For renderer bugs, invoke the repo skill `.agents/skills/vita3k-render-debug/SKILL.md` before patching. The expected loop is SQLite search, attempt check, burst capture, pause/stabilize when possible, live draw/surface isolation, Windows proof, Android/Thor proof, SQLite attempt entry, then commit/push.
- Always confirm the foreground Android package before attributing a screenshot to Vita3K. `adb shell dumpsys window` should identify whether the visible issue belongs to `org.vita3k.emulator.debug`, Cocoon, RPCSX, or another frontend.
- AYN Thor can report separate focus lines per display; Vita3K may be running on the second screen while the launcher remains focused on another display. Prefer the focus line and screenshot that include `org.vita3k.emulator.debug` before declaring a capture wrong.
- The default fix loop for serious renderer/game failures is Windows first, Android second. First reproduce the game or scene on Windows with the same ZIP/cartridge path, save data, shader logs, and Vulkan trace controls; fix emulator-core, shader translator, CPU, module, or VFS issues there because rebuild/restart cycles are faster and Ghidra/static analysis is practical. Only after the Windows/core behavior is understood should Android/Thor-specific issues be chased, such as Adreno/Turnip behavior, SurfaceFlinger presentation, SurfaceView alpha/composition, Android input routing, and APK asset packaging.
- Treat black screens as a classification problem before editing code. A black screenshot with active shaders and `PC: 0x00000000`/invalid memory reads points toward a guest CPU/module/import/null-function-pointer problem; a valid Windows frame that is black only on Android points toward presentation, driver, or swapchain/composition logic like the UPPERS present-alpha issue.
- A repeatable AI-assisted fix cycle should produce artifacts at every step: Thor screenshot/log/profile dump, Windows repro notes, Ghidra/API-call-site notes when needed, a minimal emulator patch, Windows proof screenshot/log, Android APK install proof, Thor screenshot/log proof, and SQLite entries for observation, decision, fix, and regression risk. Do not skip the proof step just because a hypothesis feels likely.
- For renderer bugs that are not obviously Adreno/Turnip-only, use the Windows desktop loop before rebuilding Android: build `cmake --preset windows-vs2022` and `cmake --build build/windows-vs2022 --config RelWithDebInfo --target vita3k --parallel`, stage the target ZIP under ignored `roms/issues/<TITLEID>/`, mirror only needed Thor firmware/user/save data into the local Windows Vita3K profile, then launch with `tools/windows/start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug>`. This can reproduce cartridge/VFS and many renderer traces in seconds; still verify final fixes on AYN Thor because NVIDIA Vulkan and Adreno/Turnip may diverge.
- Windows debug launches must force the local offline PSN compatibility flag on unless the experiment is explicitly testing signed-out behavior. `tools/windows/start-game-render-debug.ps1` writes `psn-signed-in: 1` into its generated launch config by default so games like DOA Venus do not stop at NetCheck `0x80100C06` before renderer testing. The emulator-side per-game `CurrentConfig` default must also stay signed in so older/missing custom app XML does not silently reintroduce signed-out NetCheck failures. If `--config-location` is used, that explicit file must override the root config even for values equal to compiled defaults.
- PSN emulation in this fork is offline compatibility, not real network login. `sceNpGetServiceState`, `sceNpCheckCallback`, and PSN/PSN_ONLINE `sceNetCheckDialogGetResult` should report local signed-in success so stale configs cannot surface `0x80100C06` during offline single-player testing.
- `--thor-render-trace` also enables debugger import/export logging and loaded ELF dumps for Windows-first diagnosis. Use the generated `elfdumps/` files only as ignored local evidence for Ghidra/API-call-site work; never commit dumped commercial game binaries or decrypted content.
- Windows desktop renderer testing uses a real controller connected to Windows, not the Thor controls over USB/ADB. Prefer an Xbox Wireless/XInput controller paired to Windows; confirm it appears in Windows before blaming Vita3K input. Quick checks: `Get-PnpDevice -PresentOnly | ? FriendlyName -match 'Xbox|XInput|Controller|Gamepad'`, Windows Bluetooth/game controller settings, and Vita3K/SDL logs for `gamepad`/`controller` lines. If the pad is paired after Vita3K is already running, restart Vita3K or verify SDL hotplug sees it.
- For Vita3K graphics bugs, record a SQLite observation with burst screenshot path, title ID, renderer, selected custom driver, resolution multiplier, texture/surface settings, logcat tail path, and whether the issue is in the launcher/OSD or in-game Vita rendering.
- For flicker, intermittent corruption, menus with moving backgrounds, or "check now" render investigations, do not rely on a single screencap. Capture a burst of at least 8-12 screenshots over a few seconds, and use longer bursts such as 60-120 frames when the bug appears only after a camera/menu rotation. Run `python tools/analyze_screenshot_burst.py <burst-dir>` after capture; use the generated `flicker_summary.txt`, `flicker_metrics.csv`, and `flicker_contact_sheet.jpg` to identify the largest visual jumps before deciding what broke. Keep burst PNGs under `tmp/` and summarize the useful frames in SQLite.
- When screenshot bursts are too sparse to catch title-loop flicker, record a short Android `screenrecord`, extract frames with `ffmpeg -vf fps=10`, and run `tools/analyze_screenshot_burst.py` on the extracted frame directory. DOA Venus title verification used this path to separate normal camera shot changes from renderer flicker.
- Single screenshots are allowed only for static UI proof after a burst already exists, or when the user explicitly asks for one image. Emulator/render checks default to burst capture.
- On Windows, use `tools/windows/capture-vita3k-burst.ps1 -Topic <case-slug> -Count 10 -IntervalMs 250` while the Vita3K window is visible and uncovered.
- On Android/AYN Thor, use `tools/android/capture-thor-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -Count 10 -IntervalMs 350`; it captures device-side PNGs, pulls them, and records window focus/logcat tail.
- Serious renderer debugging should be pause-first whenever the scene allows it. Once the user reaches a broken scene, pause guest execution through OSD/runtime control, capture screenshot/log/render state while the frame is stable, then test one diagnostic variable at a time before resuming. Avoid restart-heavy loops unless the code path only initializes at boot.
- Treat emulator pause as a debugging primitive, not only a user feature. Paused evaluation should support screenshot capture, logcat pull, render trace toggles, draw skip/stop-after changes for the next frame, surface/texture cache summaries, save-state attempts, and Ghidra/API-call-site note taking without forcing the user to replay long intros.
- If pausing changes or hides the bug, record that explicitly. Some glitches are timing, presentation, movie, or synchronization dependent; in those cases use burst capture plus a quick pause/resume check rather than assuming a still frame tells the whole story.
- Prefer targeted emulator dumps over guessing: add per-title toggles for GXM call trace, display frame info, surface cache state, shader/GXP translation info, pipeline state, texture upload metadata, and optional frame screenshots.
- For Windows-first renderer debugging, use `VITA3K_RUNTIME_CONTROL_FILE` or the existing `VITA3K_RENDER_CONTROL_FILE` to trigger runtime actions while the game is running. Supported `action=` values include `save_state`, `load_state`, `pause`, `resume`, `toggle_pause`, `open_osd`, and `close_osd`; include a fresh `action_id=` when repeating the same action. `tools/windows/set-render-debug-control.ps1 -Action save_state` writes the shared control file for the common UPPERS debug launch.
- Keep runtime-control polling before any paused-frame wait in the game loop. External `pause` must not strand follow-up `load_state`, `resume`, screenshot, or renderer-toggle actions during Windows-first debugging.
- On Android/AYN Thor, runtime actions can be sent without controller input by setting `debug.vita3k.runtime_action` and, when repeating the same action, a fresh `debug.vita3k.runtime_action_id`. Examples: `adb shell setprop debug.vita3k.runtime_action pause`, `adb shell setprop debug.vita3k.runtime_action resume`, `adb shell setprop debug.vita3k.runtime_action save_state`, or `adb shell setprop debug.vita3k.runtime_action_id 20260515-1315`. Use these to freeze a bad frame before changing `debug.vita3k.render_skip`, `debug.vita3k.render_stop_after`, or other live renderer props. The debug APK polls these Android props every frame; for short resume/pause pulses, set `runtime_action`, then `runtime_action_id`, and confirm the matching `Runtime control android-prop action=... action_id=...` line in logcat before assuming the action landed.
- Apply the UPPERS renderer lesson to new game corruption before patching: split the frame into producer render target, sampled consumer, and final presentation. Live draw filters accept `addr=` / `color_addr=` for the render target being written and `sample=` / `tex=` for textures being read, so trace and skip the producer and consumer independently before changing global Vulkan state.
- UPPERS is a method, not a universal fix. Before carrying an UPPERS-era workaround into another game, prove whether the new game is failing in the producer pass, sampled surface path, texture upload/decode path, or final presentation. If two visual symptoms separate under live toggles, track them as separate bugs.
- Do not enable Vulkan depth clamp globally for GXM pipelines. Vita geometry outside the depth range should clip; global depth clamp can convert off-range geometry into large foreground slabs like the UPPERS 704x396 draw 76/77 glitch.
- For suspected texture upload/render corruption, use `--thor-render-trace` and look for `ThorRenderTrace texture configure` and `ThorRenderTrace texture upload` lines near the bad frame. Compare texture address, format, type, stride, upload bytes, hash, and staging-buffer use before editing renderer cache logic.
- Treat repeated `Unhandled SIGSEGV at pc ...` lines as investigable evidence, not disposable noise. First map the host PC through the active Android process maps and symbolize it against the unstripped `libVita3K.so`; if it resolves to `ArmDynarmicCallback::MemoryWrite<T>`, enable `debug.vita3k.mem_protect_trace=1` before relaunch/capture to classify the protected range as surface cache, buffer trapping, texture cache, or kubridge before changing renderer behavior.
- For render-to-texture corruption where the surface cache correctly hits a prior color surface but later sampling flickers or shows stale/partial contents, inspect Vulkan render-pass dependencies and image visibility before changing texture lookup policy. Attachment writes that are sampled by a later fragment shader need a dependency that reaches `eFragmentShader`/`eShaderRead`, especially on Adreno/Turnip.
- For Adreno/Turnip corruption on Vita `U2F10F10F10` render targets, check whether the sampled source was rendered as an MSAA-downscaled color surface. Direct or texture-viewport sampling can show black, split, or partially stale output on Thor; prefer a copied sampled image for that narrow surface class before broader shader, depth, or game-specific hacks.
- Do not blindly broaden an Android/Turnip render-to-texture workaround to Windows. DOA Venus proved `0x62FF8000` could dump clean while forced copied U2F sampling corrupted the Windows title loop; Windows needed direct viewport sampling, while Thor still needed the Adreno/Turnip copied path. Always test direct-vs-copied sampling per platform before making global surface-cache policy changes.
- DOA Venus corruption had two historical symptoms: native BCn sampling could produce magenta terrain on Adreno/Turnip and Windows, and a later stale/slab flicker was traced to DoubleBuffer mapped data that crossed the guarded mapping boundary. The title loop was fixed first, but post-title gameplay still crossed farther than the original 4 KiB guard and reproduced black/silhouette frames on both Windows and Thor. Commit `38391203` increases the Vulkan DoubleBuffer guard/sync allowance to 64 KiB; Windows and AYN Thor post-title gameplay bursts verified lit scene rendering with no `Buffer at address ... is not completely mapped` errors. Current Vulkan defaults BCn/DXT textures to Vita3K's CPU decompression path on every platform because 2026-05-15 A/B/A bursts showed native BCn still reintroduced stable magenta blocks on the Windows DOA title loop; use `VITA3K_ALLOW_NATIVE_BCN=1` / `debug.vita3k.allow_native_bcn=1` only inside a named experiment.
- DOA Venus statue/cloud/tree/title experiments are now a known high-risk loop. Do not rerun broad BCn, cubemap/reflection, depth-LEQUAL, cull, or shader-skip tests for `18f16721`, `564cd0f6`, `c31c2a24`, `0x722A0000`, `0x73000000`, `0x73400000`, or `0x6065F040` unless the preflight names the prior SQLite result and the new run adds missing instrumentation or a changed commit. If the question is still "which pass is wrong?", the next move is frame/resource capture, surface/texture dump metadata, RenderDoc, or code inspection, not another visual skip pass.
- DOA Venus bedroom black/missing-scene regressions can be caused by stale diagnostic Android props, not only renderer semantics. A prior Thor run had many `debug.vita3k.render_*_fhash=0` and `*_vhash=0` props; before the disabled-value fix those were parsed as prefix `"0"` and could accidentally enable diagnostic depth/cull/stride paths for shaders whose hash began with `0`. Current helpers in `context.cpp`, `pipeline_cache.cpp`, and `scene.cpp` must keep `0`/`false`/`off` disabled. The proof path was a Thor reldebug rebuild/install, DOA cartridge launch, autosave prompt automation with Circle/O, an 8-frame room burst, and a 12-second right-stick rotation screenrecord burst.
- Keep a living research trail for Vita CPU/GPU behavior when bugs point beyond obvious renderer code: GXM draw semantics, PowerVR SGX tiling/deferred rendering behavior, shader patcher/GXP translation, CPU/GPU sync, memory mapping, vertex/index stream lifetimes, and texture/surface formats should be documented in SQLite before risky renderer rewrites.
- For shader/GXM architecture research, store source-backed notes in `reports/debug_knowledge.sqlite` with links to public sources and local code paths. Useful layers are public SGX543MP4+/Series5XT/USSE2 architecture, Vita3K's GXP-to-SPIR-V implementation, and per-game vhash/fhash/draw/surface evidence. Avoid leaked SDK/NDA docs and do not commit commercial shader binaries or game dumps.
- Android profiling should start with non-invasive captures: logcat, `dumpsys SurfaceFlinger`, `dumpsys gfxinfo`, Perfetto/simpleperf when available, and renderer timing counters. Do not clear app data or remove game content just to profile.
- Ghidra is appropriate for legally dumped personal Vita executables/modules when emulator behavior needs to be compared against a game's imported Vita APIs. Use Vita-aware loaders/NID databases, keep findings as notes, and do not commit commercial game binaries or decrypted content.
- Local Ghidra is at `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`; headless is `support\analyzeHeadless.bat`. VitaLoaderRedux 1.09 is installed locally under `Ghidra\Extensions\VitaLoaderRedux` for Vita ELF/PRX work.
- Vita `eboot.bin` files from ZIP/VPK content may start with an `SCE\0` container header even when they contain a readable ELF. If stock Ghidra says "No load spec found", extract the embedded ELF first with `tools\ghidra\ExtractEmbeddedVitaElf.ps1 -InputPath <eboot.bin> -OutputPath tmp\<topic>\<title>.elf`, then run headless analysis on the `.elf`.
- Do not keep manually replaying a long intro for renderer debugging. Once a bad scene is reached, prefer save-state/profile/frame/resource dumps, Ghidra/API call-site evidence, and live renderer-control toggles before any new rebuild/relaunch.
- UPPERS-specific active lead: the game uses depth-heavy 704x396 scene rendering, and Vita3K's `sceGxmBeginSceneEx` currently routes to `sceGxmBeginScene` while ignoring `storeDepthStencilSurface`. Before changing depth compare or clearing rules again, trace `loadDepthStencilSurface` and `storeDepthStencilSurface` fields and decide whether the renderer command path needs separate load/store depth-stencil surfaces.

## Frontend Direction

- The long-term Android UX should move toward emulator-native library patterns like Azahar/Dolphin: a controller-first game grid/list, per-game settings, clean driver selection, compatibility/status badges, and an in-game OSD for runtime actions.
- Vita3K's current frontend is mostly C++/ImGui running inside the SDL surface, so an Android-native launcher rewrite is a larger architecture change than editing XML resources. Treat it as a phased project: first fix layout density and controller behavior, then split out a native Android/Compose launcher if we choose that direction.

## ADB Thor Testing

- For game-specific Thor repros, bypass the Android game list/launcher and start Vita3K directly in cartridge mode. In PowerShell, do not paste Bash-style one-liners or hand-write comma-separated string-array extras; game filenames often contain spaces, brackets, parentheses, and commas.
- Prefer the checked-in PowerShell helpers for launch/capture, for example `.\tools\thor_profile_dump.ps1 -Topic doa-venus-title -TitleId PCSH00250 -GamePath $game -RenderTrace -Adb $adb`. The helpers should own ADB quoting; durable conclusions belong in `reports/debug_knowledge.sqlite`.
- Direct ADB launches must preserve the existing Android `config.yml`; if a cartridge launch falls back to the first-run setup wizard, user picker, or default settings after `adb install -r`, suspect a config merge/regression before debugging the game. Verify `initial-setup: true`, a valid `user-id`, and `show-live-area-screen: false` for automated game-render captures.
- For raw PowerShell commands, invoke executable paths stored in variables with the call operator, for example `& $adb ...`; do not type `$adb ...` as though it were Bash. PowerShell line continuation is a backtick, not `\`, but prefer variables/arrays over fragile multi-line continuations.
- If a raw direct-launch command is needed, build the Vita3K argument list as a PowerShell array, encode it as JSON, base64 the JSON, and pass it through `--es AppStartParametersJsonBase64`. Do not create renamed no-comma duplicate ZIPs just to satisfy ADB quoting:

```powershell
$adb = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk\platform-tools\adb.exe'
$activity = 'org.vita3k.emulator.debug/org.vita3k.emulator.Emulator'
$game = '/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip'
$vitaArgs = @('-a', 'true', '--cartridge', $game, '--log-level', '0', '--thor-render-trace')
$argJson = ConvertTo-Json -Compress -InputObject $vitaArgs
$argJsonBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($argJson))
& $adb shell am force-stop org.vita3k.emulator.debug
& $adb shell am start -n $activity --es AppStartParametersJsonBase64 $argJsonBase64
```
- For Japanese/Asian Vita games, remember the Vita region convention: Circle/O can be confirm and Cross/X can be cancel. On Android `adb shell input keyevent 97` sends Circle/B, while `96` sends Cross/A. Use Circle/O for prompts like DOA Venus autosave notices when Cross does nothing.
- Virtual cartridge scanning de-duplicates by Vita title ID. If a scanned ZIP and an installed `ux0/app/<TITLEID>` entry both exist, prefer the ZIP/cartridge card in the frontend so users do not see duplicate games or accidentally run the installed copy.
- When an APK is built and an AYN Thor is connected, push/install it to the Thor with ADB for real-device testing.
- After every Android-affecting commit/push with a successful APK build, also install the latest APK to the connected Android/AYN Thor with `adb install -r` unless no device is connected or the build failed. Record the result in SQLite.
- Start with `adb devices` and verify the connected device is the user's AYN Thor before installing.
- Prefer non-destructive installs such as `adb install -r path\to\apk`. Do not uninstall the existing app or clear Vita3K data unless the user explicitly accepts data loss.
- For debug/reldebug APKs, expect the `.debug` package slot unless the build config says otherwise.
- After installing, launch through ADB or the device UI, then capture proof with screenshots, `logcat`, selected driver, renderer settings, and any game/title ID tested.
- Save durable test notes and proof summaries in `reports/debug_knowledge.sqlite`; keep bulky raw logs/screenshots under ignored `tmp/` unless the user asks to commit them.
- Prefer `tools/thor_adb_debug_capture.ps1` for repeatable crash/render captures. It should be the first tool for suspected renderer hangs, Android kills, or game-specific startup crashes because it captures normal logcat, crash buffer, current window focus, meminfo, and a screenshot burst together.
- Use `tools/thor_live_debug_stream.ps1` when the user is actively playing and Codex needs a stream of evidence. It writes rolling samples under `tmp/thor-live/<timestamp>_<topic>/` and keeps `latest.txt` and `latest-screen.png` fresh. This is the preferred "play while Codex watches logs/screenshots" workflow; summarize durable findings in SQLite.
- Use `tools/thor_profile_dump.ps1 -Topic <semantic-topic> [-RenderTrace] [-TitleId <TITLEID>]` for a one-shot profile bundle from a running repro. It captures a screenshot burst plus a `screen.png` compatibility copy, logcat, crash buffer, window focus, gfxinfo/frame stats, meminfo, cpuinfo, thermal state, SurfaceFlinger, top threads, device props, and a renderer-trace summary.
- When checking a live render issue from ADB, take a burst snapshot set before and after any live property change. A good default is 10 screenshots at 250-500 ms spacing, plus logcat/window focus, so flicker, alternating surfaces, bad clears, and transient composite failures are visible instead of hidden by one lucky frame.
- For long Android title/menu flicker checks, prefer a 30-60 second `adb shell screenrecord --time-limit <seconds> --bit-rate 12000000 --size 1280x720 /sdcard/<name>.mp4`, then pull it, extract sampled frames with `ffmpeg -vf fps=10`, and analyze those frames. This catches fast temporal flicker better than sparse PNG bursts.
- For camera-rotation bugs such as DOA Venus head/hair or room occlusion, prefer `tools/android/capture-thor-rotation-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -DurationSec 45 -FrameFps 6 -Rotate -Axis ABS_Z`. It holds the Odin Controller analog axis, records a continuous Thor video, extracts frames, writes metadata/logcat/debug props, and runs the burst analyzer. Use this before changing renderer code so a full 360-ish rotation shows whether the bug is angle-specific alpha ordering, flicker, missing geometry, or stale presentation.
- Before comparing Android renderer captures, list `adb shell getprop | Select-String -Pattern 'debug.vita3k'` and clear stale diagnostic props that are not part of the named test. At minimum clear skip, stop-after, trace, dump, U2F-copy override, DoubleBuffer always-copy, BCn override, and depth override props; keep `debug.vita3k.force_bcn_decompress=1` only when the capture name explicitly says the BCn fallback is enabled.
- On Windows PowerShell, avoid `adb exec-out screencap -p > file.png` for proof captures because binary redirection can produce invalid PNG files. Use device-side `screencap -p /sdcard/...png`, then `adb pull`, then remove the temporary device file.
- On Thor Android 13, `screencap -d 0` can fail even when display 0 is active. If `tools/android/capture-thor-burst.ps1 -DisplayId 0` fails, omit `-DisplayId`; default device-side `screencap -p` has captured the Vita3K screen correctly when focus is on `org.vita3k.emulator.debug`.
- Use `tools/thor_save_sync.ps1 -TitleId <TITLEID> -Backup` before risky repro work, and `tools/thor_save_sync.ps1 -TitleId <TITLEID> -InstallPath <folder-or-zip> [-Replace]` only for decrypted Vita savedata exports. Use `-Replace` when restoring an exact save snapshot so stale extra files are removed. Do not commit pulled saves or public/user save archives.
- On Android, renderer trace can be toggled while a game is already running with `adb shell setprop debug.vita3k.thor_render_trace 1` and disabled with `adb shell setprop debug.vita3k.thor_render_trace 0`. `tools/thor_live_debug_stream.ps1 -RenderTrace` sets the property before sampling so Codex can capture `ThorRenderTrace` scene/draw/texture lines without making the user restart the game.
- For Vulkan draw isolation on Android, use live system properties instead of rebuilding when possible: `debug.vita3k.render_trace`, `debug.vita3k.render_trace_limit`, `debug.vita3k.render_skip`, `debug.vita3k.render_stop_after`, and `debug.vita3k.render_dump`. Range specs accept filters such as `rt=960x544:draw=0-4`, `scene=123:draw=8`, `fhash=<prefix>:draw=0`, or `vhash=<prefix>:draw=0`. `render_stop_after` renders the matching draw, then skips later draws in that same scene so partial-frame snapshots can binary-search bad passes. Clear skip/dump/stop-after with value `0`.
- Current live draw filters require an explicit `draw=` range to become active. Specs like `fhash=18f16721` or `tex=0x6065F040` alone update the control file but do not prove anything. Use `draw=0-999:fhash=<prefix>` / `draw=0-999:tex=<addr>` when isolating a shader or texture, then confirm `ThorRenderDebug skip` or `ThorRenderDebug stop-after armed` appears in the log before treating the screenshot as evidence.
- Android live depth experiments also support `debug.vita3k.render_force_depth_clear_ds`, `debug.vita3k.render_force_depth_clear_value`, `debug.vita3k.render_force_depth_always_fhash`, and `debug.vita3k.render_force_depth_lequal_fhash`. Treat these as diagnostics only until the root cause is understood and converted into a narrow code fix.
- Texture experiments support `VITA3K_FORCE_BCN_DECOMPRESS=1` on Windows and `debug.vita3k.force_bcn_decompress=1` on Android to force Vita3K's CPU BCn/DXT decompression path on the next renderer startup. Current Vulkan builds already default BCn/DXT to CPU decompression on Windows and Android; set `VITA3K_ALLOW_NATIVE_BCN=1` or `debug.vita3k.allow_native_bcn=1` only as a regression/diagnostic opt-in, and name the burst accordingly.

## Reporting Thor Results

- Record device model, Android version, Vita3K commit, APK/build type, renderer, selected driver, title ID, game version/update, settings, screenshots, and logs.
- A "works" claim should include proof for boot, rendering, input, audio, save/load, suspend/resume, and exit when those areas matter.
- Do not send Thor-experiment regressions to upstream Vita3K unless the issue is reproduced cleanly on upstream too.
- Write durable reports to `reports/debug_knowledge.sqlite` with `tools/debug_knowledge.py entry add`.
- Use `domain=game` plus `title_id` for game-specific results; use `domain=emulator` for broad emulator/tooling results.
- Entries should briefly state what changed, why, verification performed, regression risk, and any remaining blockers.
- Markdown reports are allowed only as explicit human-readable exports or legacy references; they are not the source of truth.

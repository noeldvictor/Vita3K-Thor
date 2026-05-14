# Vita3K Thor Agent Notes

These notes are for work in Vita3K Thor Experiment, a personal Android-focused Vita3K fork for AYN Thor testing. Keep changes practical, reversible, and clearly scoped to handheld compatibility work.

## Project Goals

- Treat Vita3K Thor as a performance, quality, and usability fork, not only a compatibility fork. Getting a game to boot is not enough if pacing, renderer quality, audio, input, OSD readability, or the debug loop are poor.
- Prefer fixes that improve emulator correctness and long-term quality over one-off game hacks. Game-specific work is acceptable for diagnosis, but it should usually lead to reusable renderer, timing, input, VFS, or tooling improvements.
- Performance work should be measured with logs, screenshots, profiles, frame pacing data, or before/after reports. Do not assume a change is faster or smoother without evidence.
- Debug tooling is part of the product direction. Keep improving the Windows and ADB loops so the user can play while Codex quickly captures evidence, isolates issues, patches, rebuilds, and verifies.
- End-user polish matters: features should be discoverable, controller-first, readable on handheld screens, and explained in plain README language separate from technical implementation notes.
- Stable, fast debugging is a primary project goal. Build and use durable knowledge tooling before chasing renderer/game issues in circles.

## Source Control

- Writable remote is the user's fork: `origin = git@github.com:noeldvictor/Vita3K-Thor.git`.
- Upstream reference is `https://github.com/Vita3K/Vita3K`.
- Always push with SSH; do not switch `origin` to HTTPS.
- Do not push to upstream Vita3K from this checkout.
- Commit and push often. Prefer small pushed checkpoints after a buildable code change, a useful report, an Android/Thor install, a debug-tool improvement, or a confirmed investigation result instead of letting local work pile up.
- Keep Thor-specific changes easy to identify so broadly useful fixes can be proposed upstream separately.
- Do not commit APK outputs, build folders, downloaded driver ZIPs, extracted drivers, caches, SDKs, firmware, license files, saves, shader caches, ELF dumps, screenshots/log dumps, or game content unless the user explicitly requests a narrow proof asset.

## Debug Knowledge Base

- `reports/debug_knowledge.sqlite` is the canonical report and RAG store for emulator/game debugging. Markdown reports are legacy context or human exports only; do not create new durable Markdown reports by default.
- Use the committed skill `.agents/skills/vita3k-debug-rag/SKILL.md` for emulator/game issue work. It encodes the expected SQLite-first, Windows-first, Android-final workflow.
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
- Local issue ROMs live under ignored `roms/issues/<TITLEID>/`; regression ROMs live under ignored `roms/regression/<TITLEID>/`. Use `tools/sync_issue_rom.ps1` to copy or pull games there as needed. Never commit `roms/`.
- Current first active case is DOA Venus (`PCSH00250`) renderer corruption. Pause renderer chasing until the SQLite/debug harness is available and the case has a current observation in the DB.

## Input Automation

- Use committed input helpers instead of asking the user to repeatedly press obvious buttons during repro setup.
- Windows desktop debug loop: use `tools/windows/send-vita3k-input.ps1`, which focuses the Vita3K window and sends the default keyboard-mapped Vita controls. Examples: `-Sequence circle,wait:500,start`, `-Sequence down:2,cross`, `-Sequence osd`, `-Sequence fast_forward`, `-Sequence click`.
- Android/AYN Thor loop: use `tools/android/send-thor-input.ps1`. Default `KeyEvent` mode is best for simple prompts; `-Mode Sendevent` is best for raw Odin Controller routing, Back/Select conflicts, OSD chords, and cases where Android keyevents do not reach SDL like a real controller.
- Supported common button names include `cross`, `circle`, `square`, `triangle`, `start`, `select`, `back`, `up`, `down`, `left`, `right`, `l1`, `r1`, `l2`, `r2`, `l3`, and `r3`; use `button:count`, `wait:ms`, or `button+button` chords for short sequences.
- Useful aliases are `osd` for L3+R3 and `fast_forward` for Select+R1. Windows also supports `save_state` and `load_state` through the default keyboard right-stick mapping. On Android, prefer runtime control or OSD automation for quickstates until the exact Thor right-stick axis events are captured for the current firmware.
- For Japanese/Asian Vita games, Circle/O can be confirm and Cross/X can be cancel. Prefer `circle` for DOA Venus autosave/title prompts unless a screenshot/log proves Cross is expected.
- Use Windows `click` automation for Vita3K/ImGui modal buttons that do not respond to Vita controls. `click:x,y` is window-relative and `click@x,y` is absolute desktop coordinates.
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
Copy-Item -Recurse -Force data android/assets
Copy-Item -Recurse -Force lang android/assets
Copy-Item -Recurse -Force vita3k/shaders-builtin android/assets
.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug
```

- Expected reldebug APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`.
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
- Save-state/load-state shortcuts provide an experimental per-game slot 0 quickstate: wait for guest threads to fully pause, snapshot CPU contexts plus allocated guest memory pages, write compressed `states/<TITLEID>/slot0.thorstate`, and reload same-session states while the host-side emulator objects are still alive. App-restart reloads are attempted only for states that do not contain AVPlayer movie/audio threads; AVPlayer states are refused after restart until host object serialization exists. `save-state-dir` can move the state root to a custom directory; relative paths resolve under the shared Vita3K data path, and absolute paths are used as-is. `save-state-compression-level` uses miniz level 0-9 and defaults to 1 for fast compression.
- PPSSPP-level durable save/load is the target, not the current state. Treat save-state work as a serialization subsystem: CPU contexts, guest RAM, allocator maps, kernel thread/object/wait state, GPU/display/renderer state, texture/surface caches, audio state, AVPlayer/movie state, IO/VFS handles, timing state, and per-game metadata all need versioned capture/restore plus refusal paths for unsafe states.

## Runtime OSD

- The game-running OSD opens from a short Android Back press (`AC_BACK`) on AYN Thor. A long Android Back press should route to the Vita PS/Home path and return to the Vita LiveArea/home flow for the running app. Do not bind plain gamepad Select/`BTN_SELECT`/SDL `GamepadBack` as a second OSD opener; Select must remain Vita `SCE_CTRL_SELECT` and the modifier for `Select + R1`, `Select + right-stick down`, and `Select + right-stick up`.
- L3 + R3 is also a runtime OSD toggle for desktop/Windows controller testing and handhelds with both stick-click buttons. Keep it as a pressed-edge chord so holding both sticks does not repeatedly flicker the OSD, and keep single L3/R3 mapped as normal Vita controls.
- Long Android Back must first restore fast-forward to 100% before routing PS/Home, and virtual-cartridge LiveArea lookups must resolve either source archive path or title ID without null-crashing.
- AYN Thor/Odin controller input may expose Back/Select through multiple Android paths (`KEY_BACK`, `KEY_APPSELECT`, `BTN_SELECT`, and SDL gamepad Back). Back and Select are separate controls: `Emulator.dispatchKeyEvent` forwards `KEYCODE_BACK` directly to SDL's native key path so Vita3K sees `AC_BACK`; `BTN_SELECT`/SDL gamepad Back is Vita Select and Select chords only. When debugging OSD behavior, capture `getevent -lp`, SDL/logcat event traces, and before/after screenshots before changing bindings.
- Opening the OSD pauses guest threads by default. Closing/resuming from the OSD resumes guest threads unless the user explicitly changed pause state in the OSD.
- OSD feedback should replace toast feedback for runtime actions. Fast-forward, save/load quickstate, cheat toggles, and pause/resume should update OSD/overlay status and logs.
- OSD first-level actions currently include Resume, Pause/Resume, Settings, Save State slot 0, Load State slot 0, Screenshot, Renderer Trace, Off/2x/3x/4x fast-forward presets, and disabled placeholders for Reset Game and Close Game.
- Keep the OSD readable over bright or glitchy game frames: dim the game behind it, use an opaque high-contrast panel, and size text/buttons for handheld viewing rather than desktop mouse precision.
- Renderer Trace is a runtime diagnostic switch. When enabled it emits `ThorRenderTrace` logcat lines for Vulkan scene setup, the first 32 draws per scene, and texture configure/upload events. Include render target, color/depth surface addresses, formats, depth/stencil state, shader hashes, texture counts, texture address/format/type/stride/upload bytes, mapping mode, surface sync state, and driver flags.
- For ADB-only render/crash investigations, launch with `--thor-render-trace` to enable the same renderer trace at startup, or use `tools/thor_adb_debug_capture.ps1 -GamePath <zip> -RenderTrace` to clear logcat, launch, and capture screenshot/logcat/crash-buffer/window/meminfo artifacts under ignored `tmp/`. Summarize durable findings into `reports/debug_knowledge.sqlite`.
- The Cheats panel lists detected cheats for the current title, shows enabled/disabled state, allows toggling individual cheats, shows unsupported-code counts, and provides a reload-cheat-file action.
- The status area shows title ID, current speed percentage, selected custom driver on Android, quickstate slot status, and whether a matching cheat file was loaded.
- Keep the OSD usable with controller only: D-pad/left stick navigates, Cross/A confirms, Circle/B cancels, Back/Select closes. It should also work with touch/mouse when available. ImGui navigation must remain enabled, and the SDL backend must use real SDL3 gamepad instance IDs/player index instead of assuming gamepad index `0`.
- Keep OSD rendering lightweight and in the existing ImGui path. Do not open the Vita Live Area or normal settings dialog just to perform runtime actions.

## Graphics Debugging And Profiling

- Always confirm the foreground Android package before attributing a screenshot to Vita3K. `adb shell dumpsys window` should identify whether the visible issue belongs to `org.vita3k.emulator.debug`, Cocoon, RPCSX, or another frontend.
- AYN Thor can report separate focus lines per display; Vita3K may be running on the second screen while the launcher remains focused on another display. Prefer the focus line and screenshot that include `org.vita3k.emulator.debug` before declaring a capture wrong.
- The default fix loop for serious renderer/game failures is Windows first, Android second. First reproduce the game or scene on Windows with the same ZIP/cartridge path, save data, shader logs, and Vulkan trace controls; fix emulator-core, shader translator, CPU, module, or VFS issues there because rebuild/restart cycles are faster and Ghidra/static analysis is practical. Only after the Windows/core behavior is understood should Android/Thor-specific issues be chased, such as Adreno/Turnip behavior, SurfaceFlinger presentation, SurfaceView alpha/composition, Android input routing, and APK asset packaging.
- Treat black screens as a classification problem before editing code. A black screenshot with active shaders and `PC: 0x00000000`/invalid memory reads points toward a guest CPU/module/import/null-function-pointer problem; a valid Windows frame that is black only on Android points toward presentation, driver, or swapchain/composition logic like the UPPERS present-alpha issue.
- A repeatable AI-assisted fix cycle should produce artifacts at every step: Thor screenshot/log/profile dump, Windows repro notes, Ghidra/API-call-site notes when needed, a minimal emulator patch, Windows proof screenshot/log, Android APK install proof, Thor screenshot/log proof, and SQLite entries for observation, decision, fix, and regression risk. Do not skip the proof step just because a hypothesis feels likely.
- For renderer bugs that are not obviously Adreno/Turnip-only, use the Windows desktop loop before rebuilding Android: build `cmake --preset windows-vs2022` and `cmake --build build/windows-vs2022 --config RelWithDebInfo --target vita3k --parallel`, stage the target ZIP under ignored `roms/issues/<TITLEID>/`, mirror only needed Thor firmware/user/save data into the local Windows Vita3K profile, then launch with `tools/windows/start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug>`. This can reproduce cartridge/VFS and many renderer traces in seconds; still verify final fixes on AYN Thor because NVIDIA Vulkan and Adreno/Turnip may diverge.
- `--thor-render-trace` also enables debugger import/export logging and loaded ELF dumps for Windows-first diagnosis. Use the generated `elfdumps/` files only as ignored local evidence for Ghidra/API-call-site work; never commit dumped commercial game binaries or decrypted content.
- Windows desktop renderer testing uses a real controller connected to Windows, not the Thor controls over USB/ADB. Prefer an Xbox Wireless/XInput controller paired to Windows; confirm it appears in Windows before blaming Vita3K input. Quick checks: `Get-PnpDevice -PresentOnly | ? FriendlyName -match 'Xbox|XInput|Controller|Gamepad'`, Windows Bluetooth/game controller settings, and Vita3K/SDL logs for `gamepad`/`controller` lines. If the pad is paired after Vita3K is already running, restart Vita3K or verify SDL hotplug sees it.
- For Vita3K graphics bugs, record a SQLite observation with burst screenshot path, title ID, renderer, selected custom driver, resolution multiplier, texture/surface settings, logcat tail path, and whether the issue is in the launcher/OSD or in-game Vita rendering.
- For flicker, intermittent corruption, menus with moving backgrounds, or "check now" render investigations, do not rely on a single screencap. Capture a burst of at least 8-12 screenshots over a few seconds, then compare the sequence for frame-to-frame changes before deciding what broke. Keep burst PNGs under `tmp/` and summarize the useful frames in SQLite.
- Single screenshots are allowed only for static UI proof after a burst already exists, or when the user explicitly asks for one image. Emulator/render checks default to burst capture.
- On Windows, use `tools/windows/capture-vita3k-burst.ps1 -Topic <case-slug> -Count 10 -IntervalMs 250` while the Vita3K window is visible and uncovered.
- On Android/AYN Thor, use `tools/android/capture-thor-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -Count 10 -IntervalMs 350`; it captures device-side PNGs, pulls them, and records window focus/logcat tail.
- Serious renderer debugging should be pause-first whenever the scene allows it. Once the user reaches a broken scene, pause guest execution through OSD/runtime control, capture screenshot/log/render state while the frame is stable, then test one diagnostic variable at a time before resuming. Avoid restart-heavy loops unless the code path only initializes at boot.
- Treat emulator pause as a debugging primitive, not only a user feature. Paused evaluation should support screenshot capture, logcat pull, render trace toggles, draw skip/stop-after changes for the next frame, surface/texture cache summaries, save-state attempts, and Ghidra/API-call-site note taking without forcing the user to replay long intros.
- If pausing changes or hides the bug, record that explicitly. Some glitches are timing, presentation, movie, or synchronization dependent; in those cases use burst capture plus a quick pause/resume check rather than assuming a still frame tells the whole story.
- Prefer targeted emulator dumps over guessing: add per-title toggles for GXM call trace, display frame info, surface cache state, shader/GXP translation info, pipeline state, texture upload metadata, and optional frame screenshots.
- For Windows-first renderer debugging, use `VITA3K_RUNTIME_CONTROL_FILE` or the existing `VITA3K_RENDER_CONTROL_FILE` to trigger runtime actions while the game is running. Supported `action=` values include `save_state`, `load_state`, `pause`, `resume`, `toggle_pause`, `open_osd`, and `close_osd`; include a fresh `action_id=` when repeating the same action. `tools/windows/set-render-debug-control.ps1 -Action save_state` writes the shared control file for the common UPPERS debug launch.
- Keep runtime-control polling before any paused-frame wait in the game loop. External `pause` must not strand follow-up `load_state`, `resume`, screenshot, or renderer-toggle actions during Windows-first debugging.
- Do not enable Vulkan depth clamp globally for GXM pipelines. Vita geometry outside the depth range should clip; global depth clamp can convert off-range geometry into large foreground slabs like the UPPERS 704x396 draw 76/77 glitch.
- For suspected texture upload/render corruption, use `--thor-render-trace` and look for `ThorRenderTrace texture configure` and `ThorRenderTrace texture upload` lines near the bad frame. Compare texture address, format, type, stride, upload bytes, hash, and staging-buffer use before editing renderer cache logic.
- For render-to-texture corruption where the surface cache correctly hits a prior color surface but later sampling flickers or shows stale/partial contents, inspect Vulkan render-pass dependencies and image visibility before changing texture lookup policy. Attachment writes that are sampled by a later fragment shader need a dependency that reaches `eFragmentShader`/`eShaderRead`, especially on Adreno/Turnip.
- For Adreno/Turnip corruption on Vita `U2F10F10F10` render targets, check whether the sampled source was rendered as an MSAA-downscaled color surface. Direct or texture-viewport sampling can show black, split, or partially stale output on Thor; prefer a copied sampled image for that narrow surface class before broader shader, depth, or game-specific hacks.
- Keep a living research trail for Vita CPU/GPU behavior when bugs point beyond obvious renderer code: GXM draw semantics, PowerVR SGX tiling/deferred rendering behavior, shader patcher/GXP translation, CPU/GPU sync, memory mapping, vertex/index stream lifetimes, and texture/surface formats should be documented in SQLite before risky renderer rewrites.
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
- On Windows PowerShell, avoid `adb exec-out screencap -p > file.png` for proof captures because binary redirection can produce invalid PNG files. Use device-side `screencap -p /sdcard/...png`, then `adb pull`, then remove the temporary device file.
- Use `tools/thor_save_sync.ps1 -TitleId <TITLEID> -Backup` before risky repro work, and `tools/thor_save_sync.ps1 -TitleId <TITLEID> -InstallPath <folder-or-zip> [-Replace]` only for decrypted Vita savedata exports. Use `-Replace` when restoring an exact save snapshot so stale extra files are removed. Do not commit pulled saves or public/user save archives.
- On Android, renderer trace can be toggled while a game is already running with `adb shell setprop debug.vita3k.thor_render_trace 1` and disabled with `adb shell setprop debug.vita3k.thor_render_trace 0`. `tools/thor_live_debug_stream.ps1 -RenderTrace` sets the property before sampling so Codex can capture `ThorRenderTrace` scene/draw/texture lines without making the user restart the game.
- For Vulkan draw isolation on Android, use live system properties instead of rebuilding when possible: `debug.vita3k.render_trace`, `debug.vita3k.render_trace_limit`, `debug.vita3k.render_skip`, `debug.vita3k.render_stop_after`, and `debug.vita3k.render_dump`. Range specs accept filters such as `rt=960x544:draw=0-4`, `scene=123:draw=8`, `fhash=<prefix>:draw=0`, or `vhash=<prefix>:draw=0`. `render_stop_after` renders the matching draw, then skips later draws in that same scene so partial-frame snapshots can binary-search bad passes. Clear skip/dump/stop-after with value `0`.
- Android live depth experiments also support `debug.vita3k.render_force_depth_clear_ds`, `debug.vita3k.render_force_depth_clear_value`, `debug.vita3k.render_force_depth_always_fhash`, and `debug.vita3k.render_force_depth_lequal_fhash`. Treat these as diagnostics only until the root cause is understood and converted into a narrow code fix.
- Android live texture experiments support `debug.vita3k.force_bcn_decompress=1` to disable native BCn/DXT Vulkan sampling and force Vita3K's CPU decompression path on the next renderer startup. Use it only as a diagnostic/comparison switch unless a device-specific compatibility decision is backed by burst screenshots and logs.

## Reporting Thor Results

- Record device model, Android version, Vita3K commit, APK/build type, renderer, selected driver, title ID, game version/update, settings, screenshots, and logs.
- A "works" claim should include proof for boot, rendering, input, audio, save/load, suspend/resume, and exit when those areas matter.
- Do not send Thor-experiment regressions to upstream Vita3K unless the issue is reproduced cleanly on upstream too.
- Write durable reports to `reports/debug_knowledge.sqlite` with `tools/debug_knowledge.py entry add`.
- Use `domain=game` plus `title_id` for game-specific results; use `domain=emulator` for broad emulator/tooling results.
- Entries should briefly state what changed, why, verification performed, regression risk, and any remaining blockers.
- Markdown reports are allowed only as explicit human-readable exports or legacy references; they are not the source of truth.

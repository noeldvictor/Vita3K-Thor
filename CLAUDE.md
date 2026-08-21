# Vita3K Thor — working notes for agents

Thor is a fork of [Vita3K](https://github.com/Vita3K/Vita3K) aimed at the AYN
Thor handheld (Snapdragon 8 Gen 2 / QCS8550). `AGENTS.md` holds the standing
project rules and is the authority when the two disagree; this file covers how
to build, run and automate the thing.

## Layout

| Path | What it is |
|---|---|
| `vita3k/` | the emulator |
| `vita3k/gui-qt/` | upstream's Qt desktop frontend (the one that is built) |
| `vita3k/gui/` | Thor's retired ImGui frontend, kept as a porting reference |
| `vita3k/overlay/` | upstream's in-game overlay |
| `android/app/` | the Android app (upstream's Compose UI) |
| `android/src/` | Thor's retired Android module, kept as a porting reference |
| `android/assets/` | assets packaged into the APK — note this is **not** `android/app/assets` |
| `tools/` | dev tooling, including the MCP server and the knowledge base |
| `docs/reference/arm/` | Arm ARM + Cortex SWOGs (PDFs gitignored) |
| `reports/debug_knowledge.sqlite` | the canonical report store |

## Building

**Windows** needs Qt 6.11+ for the frontend:

```
cmake --preset windows-vs2022 -DVITA3K_ENABLE_QT_GUI=ON
cmake --build build/windows-vs2022 --config RelWithDebInfo -- -m
```

Qt lives at `C:/Qt` on this machine. `aqtinstall` cannot fetch Qt 6.11 — Qt
split its repo per architecture (`qt6_6112/qt6_6112_msvc2022_64/`) and aqt still
looks under `qt6_6112/qt6_6112/`, which 404s. The archives were pulled from that
path directly.

**Android** needs vcpkg and the NDK gradle pins:

```
export VCPKG_ROOT=.../SteamPortableTools/toolchains/vcpkg
export ANDROID_NDK_HOME=.../Android/Sdk/ndk/29.0.14206865
cd android && ./gradlew assembleReldebug -Pandroid.injected.build.abi=arm64-v8a
```

Upstream's `vcpkg.json` puts the project in manifest mode, so the first Android
build compiles ~64 dependencies from source into a per-project
`vcpkg_installed/`. Later builds reuse them.

The APK lands in `android/app/build/intermediates/apk/reldebug/` and is marked
`testOnly`, so install it with `adb install -r -t`. A plain `install -r` fails
with `INSTALL_FAILED_TEST_ONLY`.

## Running a game

Thor's flow is virtual cartridges, not installs. A `.zip`/`.vpk` is mounted
read-only as a game card; nothing is written to `ux0:app`.

* **Desktop:** `Vita3K.exe --cartridge <path>`
* **Android:** open the archive from a file manager (ACTION_VIEW/ACTION_SEND), or
  drop it in a scan root — `/storage/<card>/Roms/psvita` and friends — and it
  appears in the app grid.

A mounted cartridge gets a *transient* app entry so the boot path can find it by
title id. That entry is never written to the apps cache, because the content
lives outside VitaFS.

## Automation: the MCP server

`tools/mcp_server.py` exposes the dev loop over MCP so an agent can build,
install, launch, drive and observe without a human relaying commands.

Turn it on:

```
claude mcp add vita3k-thor -- python tools/mcp_server.py
```

Turn it off:

```
claude mcp remove vita3k-thor
```

Tools: `devices`, `connect`, `build_windows`, `build_android`, `install`,
`launch`, `launch_cartridge`, `stop`, `is_running`, `screenshot`, `logcat`,
`runtime_action`, `knowledge_search`, `knowledge_add`.

`runtime_action` drives a *running* emulator — `save_state`, `load_state`,
`undo_load_state`, `toggle_fast_forward`, `screenshot` — through the runtime
control file. Enable it in `config.yml`:

```yaml
enable-runtime-control: true
runtime-control-file: /path/to/vita3k-control.txt
```

or set `VITA3K_RUNTIME_CONTROL_FILE`. Without one, `runtime_action` tells you so
rather than failing silently.

## Things that will bite you

* **Nothing that runs before SDL is initialised may use `fs_utils::read_data` on
  Android.** It routes through `SDL_IOFromFile` → `Android_JNI_FileOpen` and
  aborts the process with `CallStaticObjectMethod received NULL jclass`. Use
  `std::ifstream` for real filesystem paths. This crashed the cartridge scan.
* **`android/assets`, not `android/app/assets`.** The app module's asset srcDir
  points one level up. Getting this wrong ships an APK with no builtin shaders,
  and the failure surfaces as `vk::Device::createGraphicsPipeline: ErrorUnknown`
  from the *present* pipeline, which reads like a game crash.
* **`io_deinit` must not unmount the current app archive.** Session setup calls
  it, so unmounting there kills a cartridge mounted before boot.
* **A green build is not a working build.** Both of the above compiled fine and
  failed only on device. Install and launch before claiming something works.

## Upstream

`upstream/master` is fetched but the fork has diverged hard: the ImGui frontend
was replaced by Qt, the renderer was rewritten around `FrameHost`, and config,
lang and ngs all changed shape. Update checking is disabled on both frontends —
upstream's releases are not an upgrade path for Thor, and the check only ever
offered to replace it.

Outstanding re-port work is tracked in SQLite:

* `renderer-report-after-upstream-adoption` — 36 renderer commits, 3 applied
* `quickstate-report-after-upstream-adoption` — done, kept for the API notes
* `arm64-spin-backoff-rpcs3-port` — the RPCS3 ARM64 work

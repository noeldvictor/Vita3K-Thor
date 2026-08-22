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
export VCPKG_ROOT=~/Documents/SteamPortableTools/toolchains/vcpkg
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

Build and run: `devices`, `connect`, `build_windows`, `build_android`,
`install`, `launch`, `launch_cartridge`, `stop`, `is_running`, `screenshot`,
`logcat`, `runtime_action`, `knowledge_search`, `knowledge_add`.

Debugging, added because each was hand-rolled over and over while chasing
renderer bugs:

| tool | why it exists |
|---|---|
| `boot_title` | force-stop, clear the log, boot a title id, wait for a log marker - the whole inner loop in one call |
| `wait_for_log` | poll `vita3k.log` on the device with a real sleep, instead of spinning on adb latency |
| `emu_log` | read `vita3k.log` itself, which keeps the full boot trace, rather than whatever survived logcat's ring buffer |
| `foreground` | whose activity is on top. A backgrounded emulator stops stepping and reads exactly like a hang |
| `capture` | screenshot that **refuses** when the emulator is not in front, so you never analyse someone else's app |
| `config_get` / `config_set` | flip a config flag and reboot - the cheapest A/B there is, no rebuild. `disable-surface-sync` was found this way |
| `validation_errors` | Vulkan validation count plus deduplicated samples; a regression check with a number attached |
| `release` | force-stop when done, because the device is shared |



**Driving a game from adb is not the same as driving the Compose UI.** Three
separate things bite here, and each one looks like "input is broken":

* **A tap must be held.** `input tap` injects a down and an up in the same
  instant. The Compose pause menu accepts that; the emulated Vita touchscreen
  ignores it completely and the game sees nothing at all. The MCP `tap` holds
  the contact for 150ms, which is what actually got Chaos Rings III past its
  title screen.
* **There are two touch panels.** The Vita has front and rear, and the emulator
  has one global selector (`set_rear_touchscreen`). A front-panel UI never sees
  a tap while the emulator is switched to the rear. `touch_panel` reads and sets
  it - check it before concluding touch is broken.
* **Some prompts still do not respond**, to held taps, to `input keyevent`, or
  to gamepad-sourced `input gamepad keyevent`. DOA Venus's autosave notice is
  the known case: it renders, the emulator is in the foreground and running at
  30fps, and none of the three advance it. This is unexplained and is the thing
  blocking automated play from reaching real gameplay.

**Debug through the MCP server, not raw `adb`.** If a debugging step needs a
bare `adb shell`, that is a missing tool - add it to `tools/mcp_server.py`
rather than reaching around the server. Everything the server does is
repeatable, logged the same way each time, and safe on a shared device;
a hand-typed `adb` command is none of those. The filesystem, crash, cartridge
and device-state tools all exist because they were first done by hand several
times over.

Particularly worth using rather than reinventing:

| instead of | use |
|---|---|
| `adb shell ls .../vita/ux0/...` | `vita_ls` (takes `ux0:user/00/savedata`) |
| `adb shell mkdir`/`rm` in the guest fs | `vita_mkdir`, `vita_rm` |
| `adb logcat \| grep -i 'Fatal signal'` | `crashes` |
| grepping a title id out of a `.zip` | `cartridges` |
| `adb shell top` + focus checks | `device_state` |
| `adb pull` | `pull` |
| a boot-and-watch loop | `boot_title`, `wait_for_log` |
| a screenshot you then have to sanity-check | `capture` (refuses when we are not in front) |

**The MCP server is a development tool, and is off by default.** It builds,
installs, launches, drives and inspects the emulator on a real device, so it has
no business being registered while doing anything other than working on this
fork - and on a machine where several agents share one AYN Thor, an idle
registration is one more thing that can reach for the device.

```
python tools/mcp_toggle.py status
python tools/mcp_toggle.py on
python tools/mcp_toggle.py off
```

That is a thin wrapper over `claude mcp add|remove|list`; use those directly if
you prefer.

### Cheats, by memory search

The emulator polls a plain text control file, so the whole Cheat Engine loop
works the same on the handheld as on desktop with no debugger attached. Turn it
on once with `runtime_control_enable`, reboot the title, then:

| tool | what it does |
|---|---|
| `mem_search` | first scan of every mapped guest page for a value you can see on screen - HP, gold, a counter. Width 1, 2 or 4 bytes |
| `mem_narrow` | filter the survivors after the value moved. `equal`, `not_equal`, `greater`, `less`, `changed`, `unchanged` - the relative ones need no value, so "take damage, narrow on less" works |
| `mem_read` / `mem_poke` | read or write one address. Poking is how you *confirm* a candidate: poke it and see whether the number on screen changed |
| `mem_list`, `mem_reset` | show survivors, or start over |
| `mem_cheat` | write the survivors out as a `.psv` next to the control file. Nothing is applied automatically |

The engine lives in `vita3k/app/src/memory_search.cpp` and only ever reads pages
`is_valid_addr` vouches for - guest RAM is a 4GiB host reservation of which very
little is committed, so scanning it blindly would fault.

**`runtime_poll_control_file` must be called from whichever loop is running.**
Its call site was lost in the upstream merge, which silently made the control
file - and every `runtime_action` in the MCP server - a no-op. It is now called
from both `main_android.cpp` and `gui-qt/src/main_window.cpp`. If a runtime
action ever stops working, check that first.`runtime_action` drives a *running* emulator — `save_state`, `load_state`,
`undo_load_state`, `toggle_fast_forward`, `screenshot` — through the runtime
control file. Enable it in `config.yml`:

```yaml
enable-runtime-control: true
runtime-control-file: /path/to/vita3k-control.txt
```

or set `VITA3K_RUNTIME_CONTROL_FILE`. Without one, `runtime_action` tells you so
rather than failing silently.

## The AYN Thor is shared

Several agents work on emulators for this device at once, and there is one
device. It is not yours for the duration of a task.

* **Expect to be interrupted.** Another agent will launch its own emulator,
  and Android will background yours. A backgrounded emulator stops stepping,
  so its log goes quiet and its last frame persists. That looks exactly like a
  hang, and it is not one. Before calling anything a hang, check that your
  activity is still `topResumedActivity` for the whole window you measured.
* **Close the emulator when you are done with it** -
  `adb shell am force-stop org.vita3k.emulator.debug`. Leaving it resident
  makes the next agent fight it for the foreground and the GPU.
* **A busy device is not a reason to stop.** Most of the work here does not
  need hardware: reading upstream, porting commits, comparing patches against
  what upstream already fixed, building both targets, writing up findings.
  Do that while you wait, and batch the on-device verification into one pass
  at the end.
* **Screenshots are of whatever is on top**, which may be someone else's app.
  Check focus before reading a screenshot as evidence about Vita3K.
* Do not force-stop, uninstall, or reconfigure the other emulators. They
  belong to work in progress elsewhere.

## Runtime speed and the OSD

Fast forward scales four clocks - kernel, audio, threadmgr and **the vblank**.
That last one is the one that matters and the one that was missing: nearly every
game blocks on `sceDisplayWaitVblankStart`, so `display.speed_percent` is what
actually caps the frame rate. It used to be written and never read, which is why
fast forward looked like it did nothing at all. If a speed change ever appears to
have no effect again, check `vblank_sync_thread` in `vita3k/display/src/display.cpp`
before anything else.

The speed badge is rendered by `overlay::perf_overlay`, and `State::update_overlays`
deliberately creates that overlay when fast forward is on *even if the performance
overlay is disabled* - leaving a game silently running at 3x is worse than an
unwanted glyph. Keep that behaviour if you touch the gating.

Controller input reaches the pause OSD through `Emulator.dispatchKeyEvent`, which
remaps the pad onto what Compose understands (A -> DPAD_CENTER, B -> back) only
while the menu is up, and swallows the rest so it cannot leak into the running
game. Compose navigates on DPAD_* by itself; it does not know `KEYCODE_BUTTON_A`.

## Things that will bite you

* **`vfs::read_app_file` is a footgun for cartridges.** It resolves under
  `ux0:app/<app path>/`, which is where an *installed* game lives - and a
  virtual cartridge is never installed. Anything reading game content through
  it silently gets nothing for every cartridge in the library. This has now
  been fixed three separate times, in `module_parent.cpp` (module loading),
  `_sceAppMgrLoadExec` (games that chain to a second executable, e.g.
  Uncharted) and `load_app` (param.sfo, and with it SAVEDATA_MAX_SIZE,
  ATTRIBUTE2 and APP_VER). The pattern to copy:

  ```cpp
  vfs::current_app_archive_mounted(emuenv.io)
      ? vfs::read_current_app_file(buf, emuenv.io, emuenv.vita_fs_path, relative)
      : vfs::read_app_file(buf, emuenv.vita_fs_path, emuenv.io.app_path, relative)
  ```

  `read_app_file` is still correct where the installed path is genuinely what
  is wanted - `apps_list.cpp`'s `read_app_info` scans ux0:app on purpose, since
  cartridges come from the scanner instead.

* **`disable-surface-sync` causes garbage geometry on Vulkan.** Upstream
  defaults it to true; Thor defaults it to false. With it on, and memory
  mapping enabled, `handle_transfer_copy` and `handle_transfer_downscale` skip
  the Vulkan surface cache and do a CPU copy out of guest memory - which is
  stale for any surface the GPU rendered and never wrote back. Chaos Rings III
  shows this as coloured streaks and black blocks over its 3D title scenes,
  and turning sync on visibly clears them. The performance cost has not been
  measured; if a game needs the speed, the flag is still per-game.
* **Nothing heavy may run on the Android UI thread from the pause menu.** A
  quickstate capture is hundreds of megabytes - Chaos Rings III is 373 MiB - and
  calling it inline from a Compose `onClick` blocks input long enough for Android
  to kill the app with an ANR. `EmulationSessionViewModel.runtimeAction` goes
  through `viewModelScope` + `Dispatchers.IO` for exactly this reason; keep any
  new runtime action on that path.
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

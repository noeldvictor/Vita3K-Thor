# Vita3K Thor — working notes for agents

Thor is a fork of [Vita3K](https://github.com/Vita3K/Vita3K) aimed at the AYN
Thor handheld (Snapdragon 8 Gen 2 / QCS8550).

**[`AGENTS.md`](./AGENTS.md) is the rulebook and wins whenever the two
disagree.** This file is the map: how to build, run and automate the thing, and
the traps that cost a day each. Where a topic has a section in `AGENTS.md`,
this file points at it instead of repeating it.

## Where the rules live

| Topic | `AGENTS.md` section |
|---|---|
| What the fork is for, what is out of scope | Project Goals, Safety Scope, Android And Thor Focus, Frontend Direction |
| Remotes, SSH push, commit cadence, how upstream batches are taken | Source Control |
| SQLite knowledge base, cases, the attempt ledger, compat checkpoints | Debug Knowledge Base, Repo-Local Skills |
| Renderer/core experiments: the anti-loop gate, one variable per run, A/B/A | Experiment Discipline, Graphics Debugging And Profiling |
| Scripted button presses on Windows and the Thor | Input Automation |
| Turnip driver picker | Custom Driver Workflow |
| Toolchain paths, vcpkg, the Gradle invocation | Build Notes, Fast Debug Loop Strategy |
| Cartridges instead of installs, archive layout rules, encrypted content | Playing Without Install |
| Cheats, hotkeys, fast forward, quickstates and their harnesses | Cheats And Runtime Hotkeys, Runtime OSD |
| Sharing the one device, ADB conventions | ADB Thor Testing |
| What a "works" claim needs | Reporting Thor Results |

Start every game or renderer task with `python tools/debug_knowledge.py case
focus` and a search of `reports/debug_knowledge.sqlite`; the rules for that are
in Debug Knowledge Base and Experiment Discipline.

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
| `tools/` | dev tooling: the MCP server, the knowledge base, the cartridge converter |
| `docs/reference/arm/` | Arm ARM + Cortex SWOGs (PDFs gitignored) |
| `reports/debug_knowledge.sqlite` | the canonical report store |

## Building

Both commands, with the toolchain paths from `AGENTS.md` → Build Notes:

```
# Windows (needs Qt 6.11+ at C:/Qt; the configure needs Qt6_ROOT)
export Qt6_ROOT=C:/Qt
cmake --preset windows-vs2022 -DVITA3K_ENABLE_QT_GUI=ON
cmake --build build/windows-vs2022 --config RelWithDebInfo -- -m

# Android (VCPKG_ROOT and ANDROID_NDK_HOME must be set in the same shell)
export VCPKG_ROOT=~/Documents/SteamPortableTools/toolchains/vcpkg
export ANDROID_NDK_HOME=~/AppData/Local/Android/Sdk/ndk/29.0.14206865
cd android && ./gradlew assembleReldebug -Pandroid.injected.build.abi=arm64-v8a
```

The NDK is the one `android/app/build.gradle` pins (`ndkVersion`). `aqtinstall`
cannot fetch Qt 6.11 — Qt split its repo per architecture and aqt looks in the
old place, which 404s — so the archives were pulled from
`qt6_6112/qt6_6112_msvc2022_64/` by hand.

The APK lands in `android/app/build/intermediates/apk/reldebug/` and is marked
`testOnly`, so install it with `adb install -r -t`; a plain `install -r` fails
with `INSTALL_FAILED_TEST_ONLY`. The first Android build compiles the vcpkg
manifest dependencies from source; later builds reuse them.

**Build both targets before committing shared code.** The desktop build sat
broken for two weeks because a declaration was left Android-only.

## Running a game

Thor's flow is virtual cartridges, not installs (`AGENTS.md` → Playing Without
Install has the layout rules). Three kinds of cartridge exist and they are not
equal:

* **A stored zip** (method 0, no compression) — the user's preferred form:
  one file per game. Mounted read-only through miniz, and every member over
  64 MiB is served in place through a windowed `FileStats` onto the zip
  (`open_file`, `resolve_archive_data_offset`). No cache. The library was
  migrated to this with `tools/pack_cartridges.py` on 2026-09-07.
* **An extracted folder** — `Roms/psvita/<Game>/sce_sys/param.sfo`. Mounted
  straight from the card (`app0_host_path`), no cache either.
* **A deflated zip** — same mount, but a deflated member over 64 MiB is
  unpacked once into the cartridge cache on internal storage, because a
  deflate stream cannot be read at an offset. The scanner sums those members
  into `AppEntry::unpack_bytes` and the grid shows the amber badge.

`patch/<id>/` and `rePatch/<id>/` inside a zip are folded over the content root
at read time.

Launching:

* **Desktop:** `Vita3K.exe --cartridge <zip or folder>`
* **Android:** drop it in a scan root (`/storage/<card>/Roms/psvita` and
  friends) and it appears in the app grid, or open an archive from a file
  manager (ACTION_VIEW / ACTION_SEND).

A cartridge gets a *transient* app entry so the boot path can find it by title
id; it is never written to the apps cache because the content lives outside
VitaFS. Both boot paths (`apps_list.cpp` `set_app_info` and `interface.cpp`
`load_app`) re-mount from the recorded source through
`vfs::mount_current_app_source`, which handles a folder and an archive alike —
before 2026-09-07 they assumed an archive, and a folder died on "failed finding
central directory".

## Automation: the MCP server

`tools/mcp_server.py` exposes the dev loop over MCP so an agent can build,
install, launch, drive and observe without a human relaying commands. It is a
development tool and **off by default** (`python tools/mcp_toggle.py on|off|status`,
a thin wrapper over `claude mcp add|remove|list`): it reaches for a shared
device, so it has no business being registered outside work on this fork.

Build and run: `devices`, `connect`, `build_windows`, `build_android`,
`install`, `launch`, `launch_cartridge`, `stop`, `is_running`, `screenshot`,
`logcat`, `runtime_action`, `knowledge_search`, `knowledge_add`.

Debugging, each added because it kept being hand-rolled:

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
| `vita_ls`, `vita_mkdir`, `vita_rm`, `pull` | the guest filesystem without a raw `adb shell` |
| `crashes`, `cartridges`, `device_state` | native crashes and ANRs; title ids read out of every archive; CPU, memory and who owns each display |

**Android input injection cannot drive a game. Use `press` and `touch`.**
`adb shell input` events carry no InputDevice, so SDL drops them instead of
matching them to an opened joystick - and the Thor always has a real controller
open. That is why `adb shell input keyevent` can drive the Compose pause menu
and never the game. The MCP `press` and `touch` tools write straight into the
emulator's own pad and touch state, below SDL, through the runtime control
file, so a game cannot tell the difference:

```
press  button=circle hold_ms=150      cross/a, circle/b, square, triangle,
                                      up/down/left/right, start, select,
                                      l1, r1, l3, r3
touch  x=500 y=850 hold_ms=150        permille of the screen, so 500/500 is
                                      the centre
```

Both holds expire on a deadline rather than needing a matching release. Known
gap: an injected `start` did not advance the Trails Evolution title screens on
2026-09-07 while DOA Venus reacts fine; see the `automated-input-debug-loop`
case before touching the injection.

The Android-level `tap` still matters for the Compose UI: a tap must be held
(`input tap` sends down and up in the same instant, which the emulated panel
ignores), and there are two touch panels (`touch_panel` reads and sets
front/rear; a front-panel UI never sees a touch while the emulator is switched
to the rear).

Two Windows-side traps when calling the server's functions directly from
Python rather than over MCP: Git Bash rewrites a leading `/storage/...` or
`/sdcard/...` argument into `C:/Program Files/Git/storage/...` before adb sees
it (set `MSYS_NO_PATHCONV=1`), and printing a Japanese game title from
`python -c` dies with a cp1252 `UnicodeEncodeError` (set
`PYTHONIOENCODING=utf-8`). Over MCP neither applies.

**Debug through the MCP server, not raw `adb`.** If a debugging step needs a
bare `adb shell`, that is a missing tool - add it to `tools/mcp_server.py`.
Everything the server does is repeatable, logged the same way each time, and
safe on a shared device; a hand-typed `adb` command is none of those.

### Cheats, by memory search

The emulator polls a plain text control file, so the whole Cheat Engine loop
works the same on the handheld as on desktop with no debugger attached. Turn it
on once with `runtime_control_enable`, reboot the title, then `mem_search`,
`mem_narrow`, `mem_read` / `mem_poke`, `mem_list`, `mem_reset` and `mem_cheat`
(writes the survivors out as a `.psv`; nothing is applied automatically). The
engine lives in `vita3k/app/src/memory_search.cpp` and only ever reads pages
`is_valid_addr` vouches for - guest RAM is a 4 GiB host reservation of which
very little is committed.

**`runtime_poll_control_file` must be called from whichever loop is running.**
Its call site was lost in an upstream merge once, which silently made the
control file - and every `runtime_action` - a no-op. It is called from both
`main_android.cpp` and `gui-qt/src/main_window.cpp`; if a runtime action ever
stops working, check that first. `runtime_action` needs
`enable-runtime-control: true` and `runtime-control-file: <path>` in
`config.yml` (or `VITA3K_RUNTIME_CONTROL_FILE`); without one it says so rather
than failing silently. The code paths and the hotkeys are described in
`AGENTS.md` → Cheats And Runtime Hotkeys.

## The AYN Thor is shared

Several agents work on emulators for this device at once, and the user picks it
up and plays whenever they like. It is not yours for the duration of a task
(`AGENTS.md` → ADB Thor Testing has the conventions).

* **Expect to be interrupted.** Another app will take the foreground and
  Android will background yours; a backgrounded emulator stops stepping, so its
  log goes quiet and its last frame persists. That looks exactly like a hang
  and is not one. Check `foreground` for the whole window you measured.
* **Close the emulator when you are done** (`release`). Leaving it resident
  makes the next agent fight it for the foreground and the GPU.
* **A busy device is not a reason to stop.** Read upstream, port, build both
  targets, write up findings, and batch the on-device verification.
* **Screenshots are of whatever is on top.** `capture` refuses when that is
  not us.
* Do not force-stop, uninstall, or reconfigure the other emulators.

## Runtime speed and the OSD

Fast forward scales four clocks - kernel, audio, threadmgr and **the vblank**.
Nearly every game blocks on `sceDisplayWaitVblankStart`, so
`display.speed_percent` is what actually caps the frame rate; it used to be
written and never read. If a speed change ever appears to have no effect, check
`vblank_sync_thread` in `vita3k/display/src/display.cpp` first.

The speed badge is rendered by `overlay::perf_overlay`, and `State::update_overlays`
deliberately creates that overlay when fast forward is on *even if the
performance overlay is disabled* - a game silently running at 3x is worse than
an unwanted glyph. Keep that behaviour.

Controller input reaches the pause OSD through `Emulator.dispatchKeyEvent`,
which remaps the pad onto what Compose understands (A -> DPAD_CENTER, B -> back)
only while the menu is up, and swallows the rest so it cannot leak into the
running game.

## Things that will bite you

* **Anything that resolves a game file under `ux0:app/<app path>/` is a footgun
  for cartridges.** A cartridge is never installed there, so such a check
  silently gets nothing for every cartridge in the library. Fixed four times so
  far: `module_parent.cpp` (module loading), `_sceAppMgrLoadExec` (games that
  chain to a second executable, e.g. Uncharted), `load_app` (param.sfo, and
  with it SAVEDATA_MAX_SIZE, ATTRIBUTE2 and APP_VER) and `interface.cpp`'s
  preload list, which decides whether `libc` and `libfios2` come from the game
  or from vs0. That last one is how the Trails Evolution games ended up on a
  black screen: they ship their own `libfios2`, the firmware one rejects their
  `sceFiosInitialize` params (`Unsupported paramsSize (96, most recent is 116)`
  via `sceClibPrintf`), no PSARC ever mounts, and every read fails as
  `Cannot find device for path: /arc/...`. The pattern to copy:

  ```cpp
  vfs::current_app_source_mounted(emuenv.io)
      ? vfs::current_app_file_exists(emuenv.io, relative)   // or read_current_app_file
      : fs::exists(emuenv.vita_fs_path / "ux0/app" / emuenv.io.app_path / relative)
  ```

  `read_app_file` is still right where the installed path is genuinely what is
  wanted - `apps_list.cpp`'s `read_app_info` scans ux0:app on purpose. Grep
  for `"ux0/app"` before trusting any new path check.

* **A game that opens `/arc/...` paths is using FIOS2 overlays**, not a broken
  device table. The Trails engine mounts `app0:/gamedata/data.psarc` and
  `data%d.psarc` at `/arc%d` inside the LLE `libfios2` and layers `/arc` over
  them through `sceFiosOverlayAddForProcess02` (HLE in
  `SceDriverUser/SceFios2User.cpp`, backed by `create_overlay`/`resolve_path`
  in `io.cpp`). Reads that libfios2 serves from an archive never reach
  `sceIoOpen`; a raw `/arc/...` there means libfios2 fell back to native IO
  because the file was in none of its archives. In Trails 3rd those are benign
  probes (`map4/e1110.mc3` exists nowhere; the map really is `map2/e1110.it3`),
  so check the PSARC manifest before blaming IO. Overlay adds, removes and the
  first 48 resolves are logged at info level.

* **`disable-surface-sync` causes garbage geometry on Vulkan.** Upstream
  defaults it to true; Thor defaults it to false. With it on, and memory
  mapping enabled, `handle_transfer_copy` and `handle_transfer_downscale` skip
  the Vulkan surface cache and do a CPU copy out of guest memory - stale for
  any surface the GPU rendered and never wrote back. Chaos Rings III shows this
  as coloured streaks and black blocks over its 3D title scenes.
* **Nothing heavy may run on the Android UI thread from the pause menu.** A
  quickstate capture is hundreds of megabytes and calling it inline from a
  Compose `onClick` blocks input long enough for an ANR.
  `EmulationSessionViewModel.runtimeAction` goes through `viewModelScope` +
  `Dispatchers.IO` for exactly this reason; keep any new runtime action there.
* **Nothing that runs before SDL is initialised may use `fs_utils::read_data` on
  Android.** It routes through `SDL_IOFromFile` → `Android_JNI_FileOpen` and
  aborts the process with `CallStaticObjectMethod received NULL jclass`. Use
  `std::ifstream` for real filesystem paths. This crashed the cartridge scan.
* **`android/assets`, not `android/app/assets`.** Getting this wrong ships an
  APK with no builtin shaders, and the failure surfaces as
  `vk::Device::createGraphicsPipeline: ErrorUnknown` from the *present*
  pipeline, which reads like a game crash.
* **`io_deinit` must not unmount the current app archive or folder.** Session
  setup calls it, so unmounting there kills a cartridge mounted before boot.
* **Declarations guarded by `#ifdef __ANDROID__` break the desktop build** when
  shared code calls them - the runtime control file's touch-panel switch did.
* **A green build is not a working build.** Install and launch before claiming
  something works.

## Known gaps, left on purpose (2026-09-07 review)

* `_sceIoPread` is upstream's `UNIMPLEMENTED()`. No game in the library calls
  it. Implement it the first time a log shows `Unimplemented _sceIoPread` for
  a game that matters, and take the argument layout (64-bit offset on 32-bit
  ARM, possibly an option struct like `_sceIoLseek`) from that game's call site
  in Ghidra rather than guessing.
* `resolve_archive_data_offset` re-reads a stored member's 30-byte local
  header on every open. A few opens per boot; not worth caching.
* Upstream's `FileStats::read` pre-touches every page of the requested buffer
  (its guard against faults in write-protected guest pages), also for a short
  windowed read. Harmless.
* `tools/android/Packer.java` reads each file twice, once for the CRC a stored
  entry must carry up front. 531 MB in 8 s on the Thor; a data descriptor
  would need reader support too.

## The cartridge cache, and why a deflated zip gets unpacked

A deflated zip entry cannot be read at an arbitrary offset, and a game seeks
inside its PSARCs constantly. So `open_file` unpacks any *deflated* member over
64 MiB once into
`<vita>/cache/cartridge_archive/<TITLEID>/<archive key>/<relative path>` and
serves it from there. Trails FC costs about 2.8 GB of that. Nothing evicts it:
on 2026-09-07 the Thor's internal storage was at 100% with 24.9 GB of cache.
A *stored* member is served in place and never touches the cache.

What exists for it:

* Settings → Emulator → **Cartridge Cache** lists the cache per title with
  sizes and free space and deletes one title or all of them.
* `python tools/pack_cartridges.py [--delete]` turns game folders on the card
  into stored zips, on the device: `tools/android/Packer.java` (compiled to
  `tools/android/packer.jar`, run through `app_process` with
  `ANDROID_DATA=/data/local/tmp`) walks the folder and writes STORED members
  with precomputed CRCs; the driver checks `unzip -t` and the member count
  before deleting the folder. About a minute per game on UFS.
* `python tools/unpack_cartridges.py [--delete] [--purge-cache]` is the
  reverse for deflated zips: unzip to a temp folder, check every member's size
  against the listing, fold `patch/`/`rePatch/` over the content root (keeping
  the game's own `param.sfo`, since the scanner rejects a patch's `gp`
  category), park `addcont/` DLC under `Roms/psvita/addcont/<id>/`, then
  delete the zip. Both tools take `--dry-run`.

The Thor often shows up on adb twice (USB `c3ca0370` and Wi-Fi
`192.168.1.5:5555`). The MCP helpers pick the USB transport when several
devices are attached and no serial was given; raw `adb` calls still need
`ANDROID_SERIAL=c3ca0370` or `-s`.

## Upstream

`upstream/master` is fetched but the fork has diverged hard: the ImGui frontend
was replaced by Qt, the renderer was rewritten around `FrameHost`, and config,
lang and ngs all changed shape. Update checking is disabled on both frontends —
upstream's releases are not an upgrade path for Thor.

Since the big adoption, upstream merges are cheap: the 2026-09-07 merge of 16
commits only conflicted on the three GitHub workflows Thor deleted (keep them
deleted), the README download table (keep Thor's note) and the SDL submodule
(take upstream's, then `git submodule update --init external/sdl`). Do it on an
`upstream-sync-<date>` branch, build both targets before committing, and
fast-forward `master`. The rules for acknowledging a rejected batch are in
`AGENTS.md` → Source Control.

Outstanding re-port work is tracked in SQLite:

* `renderer-report-after-upstream-adoption` — 36 renderer commits, 3 applied
* `quickstate-report-after-upstream-adoption` — done, kept for the API notes
* `arm64-spin-backoff-rpcs3-port` — the RPCS3 ARM64 work
* `trails-evolution-cartridge-fios` — FC fixed; the 3rd's black prologue map
  turned out to be the same bug (it renders with the game's own libfios2)

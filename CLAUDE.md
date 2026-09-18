# Vita3K Thor: rules and working notes for agents

## Writing standard

Every piece of English written for this project follows these rules. This
includes this file, `README.md`, commit messages, code comments, SQLite
entries, reports, UI strings, and replies to the user.

- Use literal, plain, direct language.
- Do not use metaphors, similes, analogies, or idioms. Examples of banned
  wording: "journey", "tapestry", "navigating", "beacon", "dive in",
  "landscape", "footgun", "bite", "chase", "going in circles", "under the
  hood".
- Do not use AI buzzwords, hype words, or decorative adjectives. State facts
  and concepts exactly as they are.
- Keep sentences short. Put one idea in each sentence. Order sentences
  logically.
- Prefer clarity and precision over style.
- When you edit existing text that breaks these rules, rewrite it so that it
  complies.

## What this file is

Thor is a fork of [Vita3K](https://github.com/Vita3K/Vita3K) for the AYN Thor
handheld (Snapdragon 8 Gen 2 / QCS8550).

This file is the only rulebook for the fork. `AGENTS.md` exists for tools
that look for that filename; it points here and holds no rules of its own. If
two sections of this file disagree, fix the file so that only one statement
remains.

- Part 1, working notes: how to build, run and automate the emulator, and
  the known failure causes.
- Part 2, rules: goals and scope, source control, the SQLite knowledge base,
  experiment discipline, device sharing, and what a "works" claim needs.

Start every game or renderer task with `python tools/debug_knowledge.py case
focus` and a search of `reports/debug_knowledge.sqlite`. The rules for that
are in Debug Knowledge Base and Experiment Discipline (Part 2).

## Contents

| Topic | Sections |
|---|---|
| What the fork is for, what is out of scope | Project Goals, Safety Scope, Android And Thor Focus, Frontend Direction |
| Remotes, SSH, commit cadence, how upstream and Vita3K-Plus batches are taken | Source Control, Upstream and reference remotes |
| SQLite knowledge base, cases, the attempt ledger, compat checkpoints | Debug Knowledge Base, Repo-Local Skills |
| Renderer/core experiments: the anti-loop gate, one variable per run, A/B/A | Experiment Discipline, Graphics Debugging And Profiling |
| Scripted button presses on Windows and the Thor | Input Automation, Automation: the MCP server |
| Turnip driver picker | Custom Driver Workflow |
| Toolchain paths, vcpkg, the Gradle invocation | Building, Build Notes, Fast Debug Loop Strategy |
| Cartridges instead of installs, archive layout rules, encrypted content | Running a game, Playing Without Install, The cartridge cache |
| Cheats, hotkeys, fast forward, quickstates and their harnesses | Cheats And Runtime Hotkeys, Runtime OSD, Runtime speed and the OSD |
| Sharing the one device, ADB conventions | The AYN Thor is shared, ADB Thor Testing |
| What a "works" claim needs | Reporting Thor Results |
| Defects that have already cost time | Known failure causes, Known gaps left on purpose |

# Part 1: Working notes

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
| `cheats/` | the bundled FinalCheat/VitaCheat database (`db/`, 677 files), its `index.json` and README |
| `docs/reference/arm/` | Arm ARM + Cortex SWOGs (PDFs gitignored) |
| `reports/debug_knowledge.sqlite` | the canonical report store |

## Building

Both commands. The toolchain paths are in Build Notes (Part 2):

```
# Windows (needs Qt 6.11+ at C:/Qt; the configure needs Qt6_ROOT)
export Qt6_ROOT=C:/Qt
cmake --preset windows-vs2022 -DVITA3K_ENABLE_QT_GUI=ON
cmake --build build/windows-vs2022 --config RelWithDebInfo -- -m

# Android (VCPKG_ROOT and ANDROID_NDK_HOME must be set in the same shell)
export VCPKG_ROOT=~/Documents/SteamPortableTools/toolchains/vcpkg
export ANDROID_NDK_HOME=~/AppData/Local/Android/Sdk/ndk/29.0.14206865
# stage the bundled assets first; android/assets is ignored and packaged as it is
mkdir -p android/assets/cheats/db && cp -r data lang vita3k/shaders-builtin android/assets/ \
  && cp cheats/db/*.psv android/assets/cheats/db/ && cp cheats/index.json android/assets/cheats/
cd android && ./gradlew assembleReldebug -Pandroid.injected.build.abi=arm64-v8a
```

The NDK is the one `android/app/build.gradle` pins (`ndkVersion`). `aqtinstall`
cannot fetch Qt 6.11 — Qt split its repo per architecture and aqt looks in the
old place, which 404s — so the archives were downloaded manually from
`qt6_6112/qt6_6112_msvc2022_64/`.

The APK is written to `android/app/build/intermediates/apk/reldebug/` and is marked
`testOnly`, so install it with `adb install -r -t`; a plain `install -r` fails
with `INSTALL_FAILED_TEST_ONLY`. The first Android build compiles the vcpkg
manifest dependencies from source; later builds reuse them.

**Build both targets before committing shared code.** The desktop build was
broken for two weeks because a declaration was left Android-only.

## Running a game

Thor's flow is virtual cartridges, not installs (the layout rules are in Playing Without Install, Part 2). Three kinds of cartridge exist and they behave differently:

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
* **Android:** copy it into a scan root (`/storage/<card>/Roms/psvita` and
  friends) and it appears in the app grid, or open an archive from a file
  manager (ACTION_VIEW / ACTION_SEND).

A cartridge gets a *transient* app entry so the boot path can find it by title
id; it is never written to the apps cache because the content lives outside
VitaFS. Both boot paths (`apps_list.cpp` `set_app_info` and `interface.cpp`
`load_app`) re-mount from the recorded source through
`vfs::mount_current_app_source`, which handles a folder and an archive alike —
before 2026-09-07 they assumed an archive, and a folder failed with "failed finding
central directory".

## Automation: the MCP server

`tools/mcp_server.py` exposes the dev loop over MCP so an agent can build,
install, launch, drive and observe without a human relaying commands. It is a
development tool and **off by default** (`python tools/mcp_toggle.py on|off|status`,
a thin wrapper over `claude mcp add|remove|list`): it connects to a shared
device, so it must not be registered outside work on this fork.

Build and run: `devices`, `connect`, `build_windows`, `build_android`,
`install`, `launch`, `launch_cartridge`, `stop`, `is_running`, `screenshot`,
`logcat`, `runtime_action`, `knowledge_search`, `knowledge_add`.

Debugging tools, each added because the same commands were repeatedly typed by hand:

| tool | why it exists |
|---|---|
| `boot_title` | force-stop, clear the log, boot a title id, wait for a log marker - the whole inner loop in one call |
| `wait_for_log` | poll `vita3k.log` on the device with a real sleep, instead of spinning on adb latency |
| `emu_log` | read `vita3k.log` itself, which keeps the full boot trace, rather than whatever survived logcat's ring buffer |
| `foreground` | whose activity is on top. A backgrounded emulator stops stepping and has the same symptoms as a hang |
| `capture` | screenshot that **refuses** when the emulator is not in front, so you never analyse someone else's app |
| `config_get` / `config_set` | flip a config flag and reboot - the fastest A/B test, no rebuild. `disable-surface-sync` was found this way |
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

Two Windows-side problems when calling the server's functions directly from
Python rather than over MCP: Git Bash rewrites a leading `/storage/...` or
`/sdcard/...` argument into `C:/Program Files/Git/storage/...` before adb sees
it (set `MSYS_NO_PATHCONV=1`), and printing a Japanese game title from
`python -c` fails with a cp1252 `UnicodeEncodeError` (set
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
engine lives in `vita3k/app/src/memory_search.cpp` and only reads pages that
`is_valid_addr` reports as valid - guest RAM is a 4 GiB host reservation of which
very little is committed.

**`runtime_poll_control_file` must be called from whichever loop is running.**
Its call site was lost in an upstream merge once, which silently made the
control file - and every `runtime_action` - a no-op. It is called from both
`main_android.cpp` and `gui-qt/src/main_window.cpp`; if a runtime action ever
stops working, check that first. `runtime_action` needs
`enable-runtime-control: true` and `runtime-control-file: <path>` in
`config.yml` (or `VITA3K_RUNTIME_CONTROL_FILE`); without one it says so rather
than failing silently. The code paths and the hotkeys are described in
Cheats And Runtime Hotkeys (Part 2).

## The AYN Thor is shared

Several agents work on emulators for this device at once, and the user picks it
up and plays whenever they like. It is not yours for the duration of a task
(the conventions are in ADB Thor Testing, Part 2).

* **Expect to be interrupted.** Another app will take the foreground and
  Android will background yours; a backgrounded emulator stops stepping, so its
  log goes quiet and its last frame persists. That has the same symptoms as a hang
  and is not one. Check `foreground` for the whole window you measured.
* **Close the emulator when you are done** (`release`). Leaving it resident
  makes the next agent compete with it for the foreground and the GPU.
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
only while the menu is up, and discards the rest so it does not reach the
running game.

## Known failure causes

* **Anything that resolves a game file under `ux0:app/<app path>/` is wrong
  for cartridges.** A cartridge is never installed there, so such a check
  silently gets nothing for every cartridge in the library. Fixed four times so
  far: `module_parent.cpp` (module loading), `_sceAppMgrLoadExec` (games that
  chain to a second executable, e.g. Uncharted), `load_app` (param.sfo, and
  with it SAVEDATA_MAX_SIZE, ATTRIBUTE2 and APP_VER) and `interface.cpp`'s
  preload list, which decides whether `libc` and `libfios2` come from the game
  or from vs0. That last one caused the Trails Evolution games to show a
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
  for `"ux0/app"` before accepting any new path check.

* **A game that opens `/arc/...` paths is using FIOS2 overlays**, not a broken
  device table. The Trails engine mounts `app0:/gamedata/data.psarc` and
  `data%d.psarc` at `/arc%d` inside the LLE `libfios2` and layers `/arc` over
  them through `sceFiosOverlayAddForProcess02` (HLE in
  `SceDriverUser/SceFios2User.cpp`, backed by `create_overlay`/`resolve_path`
  in `io.cpp`). Reads that libfios2 serves from an archive never reach
  `sceIoOpen`; a raw `/arc/...` there means libfios2 fell back to native IO
  because the file was in none of its archives. In Trails 3rd those are benign
  probes (`map4/e1110.mc3` exists nowhere; the map really is `map2/e1110.it3`),
  so check the PSARC manifest before attributing the failure to IO. Overlay adds, removes and the
  first 48 resolves are logged at info level.

* **`disable-surface-sync` causes incorrect geometry on Vulkan.** Upstream
  defaults it to true; Thor defaults it to false. With it on, and memory
  mapping enabled, `handle_transfer_copy` and `handle_transfer_downscale` skip
  the Vulkan surface cache and do a CPU copy out of guest memory - stale for
  any surface the GPU rendered and never wrote back. Chaos Rings III shows this
  as coloured streaks and black blocks over its 3D title scenes.
* **No long-running work may run on the Android UI thread from the pause menu.** A
  quickstate capture is hundreds of megabytes and calling it inline from a
  Compose `onClick` blocks input long enough for an ANR.
  `EmulationSessionViewModel.runtimeAction` goes through `viewModelScope` +
  `Dispatchers.IO` for exactly this reason; keep any new runtime action there.
* **Nothing that runs before SDL is initialised may use `fs_utils::read_data` on
  Android.** It routes through `SDL_IOFromFile` → `Android_JNI_FileOpen` and
  aborts the process with `CallStaticObjectMethod received NULL jclass`. Use
  `std::ifstream` for real filesystem paths. This crashed the cartridge scan.
* **`android/assets`, not `android/app/assets`.** Getting this wrong ships an
  APK with no builtin shaders, and the failure appears as
  `vk::Device::createGraphicsPipeline: ErrorUnknown` from the *present*
  pipeline, which has the same symptoms as a game crash.
* **`io_deinit` must not unmount the current app archive or folder.** Session
  setup calls it, so unmounting there removes a cartridge that was mounted before boot.
* **Declarations guarded by `#ifdef __ANDROID__` break the desktop build** when
  shared code calls them - the runtime control file's touch-panel switch did.
* **Android does not run `app_init.cpp`'s `init_paths`.** The Compose app's
  `native_bootstrap.cpp` builds its own `Root` paths and hands them to
  `app::init`. A new root path added only to `init_paths` (the way upstream's
  cheat PR added `cheat_path`) stays empty on Android, and any code that joins
  a file name onto it writes into the working directory or, as the cheat
  engine did on 2026-09-17, into the bundled database. Set new paths in both
  places.
* **A successful compile is not a working build.** Install and launch before claiming
  something works.

## Known gaps left on purpose (2026-09-07 review)

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
serves it from there. Trails FC uses about 2.8 GB of that. Nothing evicts it:
on 2026-09-07 the Thor's internal storage was at 100% with 24.9 GB of cache.
A *stored* member is served in place and never touches the cache.

What exists for it:

* Settings → Emulator → **Cartridge Cache** lists the cache per title with
  sizes and free space and deletes one title or all of them.
* `python tools/pack_cartridges.py [--delete]` converts game folders on the card
  into stored zips, on the device: `tools/android/Packer.java` (compiled to
  `tools/android/packer.jar`, run through `app_process` with
  `ANDROID_DATA=/data/local/tmp`) walks the folder and writes STORED members
  with precomputed CRCs; the driver checks `unzip -t` and the member count
  before deleting the folder. About a minute per game on UFS.
* `python tools/unpack_cartridges.py [--delete] [--purge-cache]` is the
  reverse for deflated zips: unzip to a temp folder, check every member's size
  against the listing, fold `patch/`/`rePatch/` over the content root (keeping
  the game's own `param.sfo`, since the scanner rejects a patch's `gp`
  category), move `addcont/` DLC to `Roms/psvita/addcont/<id>/`, then
  delete the zip. Both tools take `--dry-run`.

The Thor often appears on adb twice (USB `c3ca0370` and Wi-Fi
`192.168.1.5:5555`). The MCP helpers pick the USB transport when several
devices are attached and no serial was given; raw `adb` calls still need
`ANDROID_SERIAL=c3ca0370` or `-s`.

## Upstream and reference remotes

### upstream (Vita3K/Vita3K)

`upstream/master` is fetched over SSH. The fork adopted upstream's Qt
frontend, the `FrameHost` renderer and the config, lang and ngs changes on
2026-08-20, so the two trees are close again. Update checking is disabled on
both frontends; upstream's releases are not an upgrade path for Thor.

Upstream merges are now small. Do them on an `upstream-sync-<date>` branch,
build both targets before committing, and fast-forward `master`. The same
conflicts recur, and the answers are fixed:

| Conflict | Resolution |
|---|---|
| `.github/workflows/*.yml` (deleted in Thor, modified upstream) | keep them deleted: `git rm` the file |
| `README.md` download table | keep Thor's note |
| `external/sdl`, `external/ffmpeg` or another submodule | `git checkout upstream/master -- <path>`, then `git submodule update --init <path>`, confirm `git submodule status <path>` shows upstream's commit, and only then `git add <path>`. `git add` on a submodule records the commit that is checked out in its worktree, so adding before the worktree moved records the old commit |

History: 2026-09-07 merged 16 commits (conflicts: workflows, README, SDL
submodule). 2026-09-17 merged 5 commits (ffmpeg-core bump, GL nearest and
bicubic filters, spdlog Android tag, vcpkg gitignore, CI); conflicts were the
`c-cpp.yml` workflow and the ffmpeg submodule.

The rules for acknowledging a rejected batch are in Source Control (Part 2).

Outstanding re-port work is tracked in SQLite:

* `renderer-report-after-upstream-adoption` — 36 renderer commits, 3 applied
* `quickstate-report-after-upstream-adoption` — done, kept for the API notes
* `arm64-spin-backoff-rpcs3-port` — the RPCS3 ARM64 work
* `trails-evolution-cartridge-fios` — FC fixed; the 3rd's black prologue map
  turned out to be the same bug (it renders with the game's own libfios2)

### plus (nckstwrt/Vita3K-Plus)

`plus` is a read-only reference remote added on 2026-09-17:
[nckstwrt/Vita3K-Plus](https://github.com/nckstwrt/Vita3K-Plus), a fork of
upstream with game compatibility and rendering fixes, releases v1.0 and v1.1
(2026-09-13). `Tekushiki/Vita3K-Plus` is a fork of it and is not tracked.

What is on it, as of 2026-09-17:

* `plus/master` is upstream plus README, screenshot and issue-template
  commits. It has no code changes.
* `plus/all-enhancements` is the code branch and the one the releases are
  tagged on (`v1.1` = `89496b8a`). It is 117 commits ahead of
  `upstream/master` and 40 behind it. The diff against its merge base touches
  216 files, about 13,500 lines added. The largest changes are in
  `vita3k/renderer/src/vulkan/surface_cache.cpp`, `renderer.cpp`,
  `vita3k/shader/src/spirv_recompiler.cpp` and the USSE translator, plus
  kernel thread scheduling, page-table memory mode as the Android default, a
  deadlock breaker for waits, Mali and Qualcomm-driver fallbacks, and
  per-game fixes (Killzone, Gundam Breaker 3, MGS3, Madden, Tearaway,
  LittleBigPlanet, DOA5, Silent Hill, Ys Celceta).
* `plus/fix/hd-typeless-copies` is a small renderer fix branch.

How to use it:

* Read it first. When a game is broken on Thor and works on Vita3K-Plus, run
  `git log plus/all-enhancements --grep=<game>` and read the commit before
  designing a fix.
* Cherry-pick one commit at a time with `git cherry-pick -x`, build both
  targets, test on the Thor, and record the outcome in SQLite under the
  `vita3k-plus-reference` case. Its commits are large and mix several fixes;
  expect to split them.
* Never merge `plus/all-enhancements` into `master`, and never push to
  `plus` (its push URL is `no_push`).
* Refresh with `git fetch plus`. `plus` and `upstream` share history but not
  intent; do not merge either one without reading the batch.

# Part 2: Rules

These rules apply to all work in Vita3K Thor, a personal Android-focused
Vita3K fork for AYN Thor testing. Keep changes practical, reversible, and
clearly scoped to handheld compatibility work. Part 1 and the repo skills
refer to the section names below; keep the names stable.

## Project Goals

- Treat Vita3K Thor as a performance, quality, and usability fork, not only a compatibility fork. Getting a game to boot is not enough if pacing, renderer quality, audio, input, OSD readability, or the debug loop are poor.
- Prefer fixes that improve emulator correctness and long-term quality over one-off game hacks. Game-specific work is acceptable for diagnosis, but it should usually lead to reusable renderer, timing, input, VFS, or tooling improvements.
- Performance work should be measured with logs, screenshots, profiles, frame pacing data, or before/after reports. Do not assume a change is faster or smoother without evidence.
- Debug tooling is part of the product direction. Keep improving the Windows and ADB loops so the user can play while the agent quickly captures evidence, isolates issues, patches, rebuilds, and verifies.
- End-user polish matters: features should be discoverable, controller-first, readable on handheld screens, and explained in plain README language separate from technical implementation notes.
- Stable, fast debugging is a primary project goal. Build and use durable knowledge tooling instead of repeating failed renderer or game experiments.
- Renderer debugging protocol lives in `docs/renderer-debugging-protocol.md`; keep it aligned with this file, `.agents/skills/vita3k-render-debug/SKILL.md`, and `tools/renderer_experiment.py`.

## Source Control

- Use git over SSH for every remote operation. Never use the GitHub CLI (`gh`), the GitHub desktop app, or HTTPS remotes, and do not add a GitHub credential helper.
- Remotes in this checkout:
  - `origin = git@github.com:noeldvictor/Vita3K-Thor.git`: the user's fork and the only writable remote.
  - `upstream = git@github.com:Vita3K/Vita3K.git`: upstream Vita3K, fetch only. Its push URL is set to `no_push`.
  - `plus = git@github.com:nckstwrt/Vita3K-Plus.git`: Vita3K-Plus, a fork of upstream with game compatibility and rendering fixes, fetch only. Its push URL is set to `no_push`. What is on it and how to use it is in Upstream and reference remotes (Part 1).
- Do not push to upstream Vita3K or to Vita3K-Plus from this checkout.
- Commit and push often. Prefer small pushed checkpoints after a buildable code change, a useful report, an Android/Thor install, a debug-tool improvement, or a confirmed investigation result instead of letting local work accumulate.
- Keep Thor-specific changes easy to identify so broadly useful fixes can be proposed upstream separately.
- Do not commit APK outputs, build folders, downloaded driver ZIPs, extracted drivers, caches, SDKs, firmware, license files, saves, shader caches, ELF dumps, screenshots/log dumps, or game content unless the user explicitly requests a narrow proof asset.
- Take upstream batches on a branch named `upstream-sync-<date>`, build both targets before committing the merge, and fast-forward `master`. Record the batch, the conflicts and their resolutions in SQLite. The conflicts that recur and their fixed answers are listed in Upstream and reference remotes (Part 1).
- Upstream `master` may include structural rewrites. The Qt/Android GUI overhaul from `91f533f8` was adopted on 2026-08-20; before that, a direct merge conflicted with Thor Android, ImGui OSD, config/input, audio, kernel, and renderer-adjacent work. If a future batch is structural again, use a short-lived integration branch, record conflict findings in SQLite, and port Thor features deliberately or cherry-pick narrow upstream fixes when they do not depend on the rewrite.
- If the user wants GitHub's "behind upstream" count cleared after a structural upstream batch has been reviewed and rejected, use an ancestry-only `ours` merge with a clear commit message and SQLite note. This keeps the Thor tree unchanged while recording that the upstream batch was intentionally acknowledged.
- Vita3K-Plus is a source to read and cherry-pick from, never a merge source. Take one commit at a time with `git cherry-pick -x`, build both targets, and record the result in SQLite under the `vita3k-plus-reference` case.

## Debug Knowledge Base

- `reports/debug_knowledge.sqlite` is the canonical report and RAG store for emulator/game debugging. Markdown reports are legacy context or human exports only; do not create new durable Markdown reports by default.
- `python tools/debug_knowledge.py case focus` is the current lead-case lock. When a user says one game is still broken, set or verify focus before using filesystem recency, latest screenshots, or old experiment folders. Non-focused game work is allowed only as an explicit regression guard or after changing focus in SQLite.
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
- Record game/platform compatibility checkpoints in SQLite whenever a game becomes known-good, known-bad, regressed, blocked, or partially fixed at a commit. Use `python tools/debug_knowledge.py compat add --title-id <TITLEID> --platform <windows|android-thor> --commit <hash> --status works|regressed|broken|partial|blocked --scene "<exact scene>" --summary "<human result>" --artifact <proof>` and `compat list` before attributing a regression to a new change. This ledger is the answer to "which commit did this game work on?"
- Local issue ROMs live under ignored `roms/issues/<TITLEID>/`; regression ROMs live under ignored `roms/regression/<TITLEID>/`. Use `tools/sync_issue_rom.ps1` to copy or pull games there as needed. Never commit `roms/`.
- Current focused lead case is DOA Venus (`PCSH00250`) renderer corruption unless `case focus` says otherwise. UPPERS is currently a regression guard for renderer/depth changes, not the lead case while DOA remains user-visible broken.

## Repo-Local Skills

- Repo workflow skills live under `.agents/skills/` and are committed with this fork. Do not create global/user skills for this repo-specific Vita3K Thor process unless the user explicitly asks.
- Keep skills decomposable. A skill should be a focused instruction set with concrete tools, gates, and outputs; it should not become a single skill that tries to explain the whole emulator.
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
- Planned or open attempts are blockers. Resolve the current planned attempt as `failed`, `inconclusive`, `superseded`, or `succeeded` before starting a nearby experiment, otherwise the ledger becomes incomplete and the agent repeats work.
- Treat every renderer/core investigation as a named experiment, not an informal check. Before changing code or live props, write down the current active case, exact hypothesis, subsystem, platform, expected visual/log change, baseline artifact, and rollback path.
- For renderer changes, create an experiment packet before editing or toggling anything risky: `python tools/renderer_experiment.py start --case <case-slug> --title-id <TITLEID> --platform <windows|android|windows-android> --subsystem <renderer-area> --hypothesis "<specific hypothesis>" --expected "<what should change>" --scene "<exact repro scene>"`. The packet under `tmp/renderer-experiments/` snapshots git/config/process context, checks SQLite for similar attempts, records a planned attempt, and writes an outcome/regression template. The packet refuses non-focused cases unless `--regression-guard` or `--allow-non-focus-case` is explicit.
- Close each renderer experiment with `python tools/renderer_experiment.py finish --manifest <packet>/manifest.json --status succeeded|failed|inconclusive|superseded --result "<actual visual/log outcome>" --artifact <burst-or-log-dir>`. If the result has mixed improvement plus new breakage, use `--status inconclusive` and label the result `mixed-supports-involvement`, not fixed.
- Query the case first: `case show`, `attempt list --limit 20`, `search --recent-days 30`, and `attempt check`. If a similar attempt already failed, do not repeat it unless the new attempt explicitly changes one controlled variable and records why the old result no longer answers the question.
- When the user says work is being repeated, immediately stop experiments, inspect SQLite and this file, record a process note, and restart from the anti-loop gate. Do not continue renderer toggles in that turn unless the user explicitly tells the agent to resume debugging.
- Use one variable per experiment. Do not combine a renderer code patch, a stale Android property, a shader cache change, a driver switch, and a new save/location in the same test. If multiple things changed, mark the attempt `inconclusive` or `contaminated` and do not use it as proof.
- Clear and record debug props before every Android comparison. Capture `adb shell getprop | Select-String -Pattern 'debug.vita3k'` or the helper equivalent, clear irrelevant props, then name the remaining intentional props in the SQLite attempt body.
- Treat Android debug prop values `0`, `false`, and `off` as disabled, never as hash/address prefixes. When adding any fhash/vhash/address matcher, check the disabled values before prefix matching; otherwise a stale `setprop ... 0` can accidentally match shader hashes beginning with `0` and contaminate later renderer tests.
- Prefer A/B/A checks when a live toggle exists: capture baseline A, enable the experimental toggle and capture B, then disable it and capture A again. If A does not return, the test found state contamination or timing dependence, not a clean fix.
- When an old screenshot shows a new renderer symptom but the active APK/config/props have changed since then, do a same-session default retest before patching. If a fresh baseline no longer reproduces the issue, record it as stale/transient in SQLite and stop; do not write renderer code for a symptom that no longer reproduces.
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
- If a prompt is stuck, use `.agents/skills/vita3k-input-automation/SKILL.md` as the escalation sequence before asking the user to press the same button manually: focus/app responsiveness, normal keyevent, display-routed keyevent, raw Odin `sendevent`, touch fallback, then SDL/input-code investigation.
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

- Local Windows Android builds need Java 17 or newer, the Android SDK with the NDK that `android/app/build.gradle` pins (`29.0.14206865` as of 2026-09-07; `ANDROID_NDK_HOME` must point at it), and vcpkg with the arm64 Android Vita3K dependencies. The known-good local toolchain paths are:

```powershell
$env:JAVA_HOME='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\jdk-21.0.11+10'
$env:ANDROID_HOME='C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk'
$env:ANDROID_SDK_ROOT=$env:ANDROID_HOME
$env:ANDROID_NDK_HOME=Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\29.0.14206865'
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
$env:ANDROID_NDK_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\29.0.14206865'
$env:VCPKG_ROOT = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\vcpkg'
Copy-Item -Recurse -Force data android/assets
Copy-Item -Recurse -Force lang android/assets
Copy-Item -Recurse -Force vita3k/shaders-builtin android/assets
.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand assembleReldebug -Pandroid.injected.build.abi=arm64-v8a
```

- Expected reldebug APK: `android/app/build/intermediates/apk/reldebug/app-reldebug.apk` (about 106 MB, marked `testOnly`, so install it with `adb install -r -t`).
- After an agent or app restart, do not assume `ANDROID_NDK_HOME` or `VCPKG_ROOT` are still inherited by Gradle. Set them in the same PowerShell session as `gradlew.bat`; otherwise `cmake/vcpkg_android.cmake` fails during configure before compiling native code.
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
- Some cartridge/NoNpDrm-style ZIPs have readable `param.sfo` but PFS-encrypted app files. Detect these by checking app `eboot.bin` and `sce_sys/icon0.png` headers, show an Encrypted badge in the app list, and fail launch with a clear diagnostic instead of trying to run encrypted bytes. Do not add DRM, license, or key bypass code.
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
- The cheat engine is upstream's `vita3k/cheat` module (Vita3K PR 4107, cherry-picked on 2026-09-17). It reads FinalCheat/VitaCheat `.psv` files as they are, runs the codes once per vblank from `display.cpp`, and handles code types `$0` write, `$3` pointer, `$4` serial, `$5` copy, `$7` pointer serial, `$8` pointer copy, `$A` ARM write (restored when the cheat is turned off), `$B` module-relative base, `$C` button condition and `$D` value condition. The parser and engine have googletest coverage in `vita3k/cheat/tests`.
- File resolution (`cheat::resolve_cheat_file`), in order: 1. the user folder `cheat_path` (`<shared>/cheats`); 2. the Thor roots from `vita3k/util/src/cheat_paths.cpp` (`<shared>/cheats/db`, SD card roots such as `/storage/<card>/cheats/psvita/db`, `ux0:/vitacheat/db`); 3. `<static assets>/cheats/db` next to the desktop executable. A per-title file found outside the user folder is copied there first, so the on/off choices that `save` writes back (`_V1` means on at boot) never touch the bundled database or the SD card.
- The bundled database reaches each platform differently. The desktop build copies `cheats/db` and `cheats/index.json` next to the executable in a CMake post-build step. The APK carries them under `assets/cheats`, staged from `cheats/` into `android/assets/cheats` before Gradle (see Building, Part 1); `CheatDatabase.ensureExtracted` copies the `.psv` files to `<storage>/cheats/db` on the first launch of each installed build, before the native app scan sets the Cheats badges.
- `enable-cheats` in `config.yml` is the master switch; Thor's older `cheats-enabled` key was dropped. Off stops every cheat and restores the code the ARM writes replaced.
- UI. Desktop: Manage > Cheats and the app list context menu open the Qt cheats dialog (toggle, save, reload, open file). Android: the top bar of the games grid has a Cheat catalog icon that opens `CheatCatalogScreen` (every database game grouped by title with region chips, search, region and in-library filters, expandable cheat names, an In library mark, and a Cheats button for library games); the long-press menu of a game with a cheat file has a Cheats entry; the Session tab of the pause menu has a Cheats card. All three open `CheatsSheet`: one switch per cheat, the master switch, All on, All off and Reload, through the JNI bridge `vita3k/android/jni/native_cheats.cpp`. While the title runs the switches act on the live engine; otherwise they edit the file and take effect at boot. Every change is saved at once.
- The FinalCheat/VitaCheat database is committed under `cheats/db` (user decision of 2026-09-17; the source repositories publish no license file, and each file keeps its author header). Refresh it as `cheats/README.md` describes and rebuild `cheats/index.json` with `tools/build_cheat_index.py`. Do not add codes for online play.
- `tools/sync_vitacheat_db.ps1` clones the source database into ignored `tmp/` and can push it to the Thor SD card. The committed copy under `cheats/db` is the one that ships.
- Games with a cheat file show a Cheats badge in the app list (`cheat::has_cheat_file`, the same lookup without the copy).
- `tools/convert_vitacheat.py` converts `.psv` files into JSON for review and reports the code lines outside a given subset. `tools/build_cheat_index.py` rebuilds `cheats/index.json`; run it after any change under `cheats/db`.
- Runtime shortcuts reserved for Thor testing: `Select + R1` toggles the currently configured fast-forward speed, `Select + right-stick down` requests save state, and `Select + right-stick up` requests load state.
- `fast-forward-speed-percent` defaults to 200 and is clamped from 101 to 1000 when toggled. The runtime OSD exposes Off, 2x, 3x, and 4x preset buttons; choosing 2x/3x/4x updates `fast-forward-speed-percent` so the `Select + R1` hotkey follows the selected preset. Fast-forward must update display/vblank pacing, kernel wait pacing, and guest clock APIs together; keep `emuenv.display.speed_percent` and `emuenv.kernel.speed_percent` in sync so vblank waits, `sceKernelDelayThread`, kernel timers, wait timeouts, `sceKernelGetProcessTime*`, `sceKernelGetSystemTimeWide`, libc time/gettimeofday, and RTC current tick do not stay at real-time speed.
- SDL fast-forward audio must never raise SDL's stream frequency ratio above `1.0x`; that raises the pitch of the audio. Use FFmpeg `atempo` for pitch-preserving tempo changes when available, and otherwise fall back to normal-pitch buffer skipping with light crossfade instead of frequency-ratio speed-up or callback-local grain skipping.
- Do not use Android toast popups for fast-forward, save-state, or load-state feedback. Prefer OSD/overlay state and logs so gameplay is not interrupted.
- Save-state/load-state shortcuts are under a Windows-first stability gate. Current code captures a disk-backed per-game slot 0 with CPU contexts, allocated guest memory pages, allocator maps, page CRCs, guarded `.tmp`/`.bak` replacement, full readback validation before slot promotion, primary-slot corruption fallback to the retained backup, a separate `slot0.thorstate.undo` pre-load rescue slot, and checked named metadata sections for timing, kernel objects, Sysmem memblock UID tables, SceFiber host CPU contexts, SceSharedFb framebuffer metadata, IO/VFS, display, audio, Audiodec decoder handles, AVPlayer/movie, Jpeg/MJPEG decoder state, Videodec/H.264 decoder handles, host-service state, and NGS host state. Restore is enabled only when the manifest says mandatory layers are present. The `slot0.thorstate.txt` sidecar must describe the durable disk file after a successful save, not the temporary live same-session slot that remains in memory. Windows proof currently includes UPPERS (`PCSG00633`) and DOA Venus (`PCSH00250`) passing a five-cycle durable restart load, save-again, same-session load, undo-load, and restart-again load soak without process crashes. Current canary slots exercise IO file handles, nonzero SceFiber snapshots, mandatory zero-state SceSharedFb metadata, GXM render-target/program host rebuilds, DOA NP/Net/NetCtl state, AAC Audiodec and Videodec movie decoder shells, and deferred lwmutex waiter rebuilds. SharedFb still needs a nonzero real-game proof. Directory-handle and FIOS-overlay capture/restore is versioned, identity-checked, and fail-closed, but still needs a nonzero real-game proof before being treated as broadly proven. Do not ship Android/Thor quickstate claims until the same matrix is explicitly run on device.
- The durable restore path recreates saved thread identities, removes extra current boot threads, rebuilds saved kernel callback objects and display vblank callback registrations by UID, rebuilds mismatched IO file handles, remaps saved timer/rwlock identities by UID/name/attr, clears boot-time simple-event/timer/rwlock queues when the saved state has no live waiters, rebuilds restorable deferred mutex/lwmutex waiters from snapshot metadata, rebuilds SceFiber host pointers inside restored guest fiber structs, restores SceSharedFb framebuffer metadata, rebuilds missing or mismatched audio output ports, restores Audiodec AT9/MP3/AAC decoder tables, restores AVPlayer/movie runtime scalars when present, restores NP/Net/NetCtl host-service state, restores NGS state after guest memory, lazily rebuilds GXM render targets/programs/context host objects after process restart, keeps guest CPUs paused until host state is rebuilt, and uses a stricter quiescence barrier so active HLE imports cannot run through half-restored state. The pause window is deliberately longer than before so early boot GXM/msgpipe imports can unwind instead of false-failing.
- Do not hide allocator ownership bugs during quickstate restore. Extra current boot threads removed during durable restore must release stack/TLS `Block` ownership before the saved allocation map replaces the current map; `Freeing unallocated page` criticals after restore are failures to investigate, not log noise to suppress.
- Quickstate allocation-map restore must treat the bitmap allocator as the page-level allocation truth, and thread contexts as the stack/TLS ownership truth. Normalize saved thread stack/TLS ranges before capture/restore, commit and unprotect allocated bitmap ranges rather than only `alloc_table` block starts, and never accept stale `alloc_table` starts that leave owned thread memory uncommitted.
- GXM/display queue restore is part of the quickstate contract. Queue reset must hold the queue lock, wake producers/consumers, and use the display-queue restore generation to skip stale callback follow-ups from the pre-restore world.
- Before committing save-state/load-state changes, run the Windows harness when issue ROMs and slot0 states are available: `.\tools\windows\run-quickstate-regression.ps1 -TitleId PCSH00250,PCSG00633 -CasePrefix <semantic-slug> -Cycles 2 -SkipBuild -StopExisting -ExerciseUndoLoad -StartupSeconds 12 -AfterActionSeconds 5 -MarkerTimeoutSeconds 180 -TraceLimit 0`. It launches each title, performs durable load, save-again, same-session load, undo-load, restart load, checks fresh marker files, asserts successful restore markers and the durable state sidecar say `Restore enabled: yes` and `Missing serializers: none`, appends separate marker digests for durable, same-session, undo, and restart restores, and fails on crash/restore poison such as `Freeing unallocated page`, `VirtualAlloc failed`, `SIGSEGV`, stale GXM host pointers, or sync identity mismatches.
- After the Windows quickstate gate passes and a reldebug APK is installed on AYN Thor, run the Android harness before claiming device reliability: `.\tools\android\run-thor-quickstate-regression.ps1 -TitleId PCSH00250,PCSG00633 -CasePrefix <semantic-slug> -Cycles 1 -ExerciseUndoLoad -StartupSeconds 12 -AfterActionSeconds 5 -MarkerTimeoutSeconds 180`. It launches cartridge ZIPs from `tools/android/thor-render-regression-matrix.json`, creates a fresh state on device, verifies `slot0.thorstate.txt`, same-session load, optional undo-load, force-stop/relaunch durable disk load, marker freshness, and logcat poison patterns. Keep raw artifacts under `tmp/thor-quickstate/` and write durable conclusions into SQLite.
- Before claiming primary-slot corruption resilience, rerun the harness with `-ExerciseCorruptPrimaryFallback`. It corrupts `slot0.thorstate` before run 2, requires the log to show `.bak` fallback, verifies a successful durable restore marker, and restores the original primary state file afterward so later tests do not inherit a poisoned slot.
- Before claiming custom save-state directory or compression behavior, rerun the harness with `-SaveStateDir <absolute-temp-dir> -SaveStateCompressionLevel <0-9>`. The harness writes a temporary launch config, seeds the custom root from the default known-good slots if needed, and verifies marker digests from the configured root.
- Scheduler-managed wait classes currently include display vblank waits without callbacks, simple-event waits, timer event waits, semaphore waits, eventflag waits, mutex/lwmutex waits, condvar/lwcondvar waits, rwlock waits, thread-end waits, GXM notification waits, and message-pipe sender/receiver waits with buffered bytes. Timed semaphores, timed simple-event waits, timed timer waits, timed eventflags, timed mutex/lwmutex waits, timed condvar/lwcondvar waits, timed rwlock waits, timed message-pipe sender/receiver waits, and timer event wakeups now use a kernel-owned scheduler plus per-wait/per-event generation guards, so stale pre-load jobs cannot complete waiters rebuilt after restore. Future work should move any remaining host-side wait callbacks into the same centrally owned scheduling model before broad game-matrix claims. Treat this as the pattern for PPSSPP-level durability, not the end state. Kernel callback lifecycle and display vblank callback registration now serialize/restore and fail closed when metadata is incomplete, but still need a nonzero real-game callback proof before broad claims. GXM notification waits plus GXM display queue entries/waiters/pending callbacks serialize/restore, but still need nonzero real-game proof before broad claims. Renderer texture/surface/framebuffer runtime caches are reset during durable restore so host GPU resources are rebuilt from restored RAM. Sysmem snapshots now use `thor.sysmem.v1` for memblock UID tables, VM block membership, allocation counters, and `next_uid`; missing Sysmem sections must fail closed because guest handles can outlive process restart. SceFiber snapshots now use `thor.fiber.v1` for tracked guest fiber structs and their host `CPUContext` pointers; old states without this section must fail closed because those guest structs otherwise point at stale process memory after restart. SceSharedFb snapshots now use `thor.sharedfb.v1` for shared-framebuffer service metadata; old states without this section must fail closed because the service keeps active guest framebuffer addresses in host state. Audiodec snapshots now use `thor.audiodec.v1` for AT9/MP3/AAC decoder handles and stream parameters; unknown decoder kinds must fail closed instead of leaving guest handles pointed at missing host objects. Jpeg/MJPEG snapshots now use `thor.jpeg.v1` so initialized decoder service state is rebuilt after process restart instead of leaving later guest calls pointed at a missing host object. AVPlayer snapshots now use `thor.avplayer.v2` cursor metadata and seek-prime the next decoded movie frame after durable restore; native `libsceavplayer` movie windows also need `thor.videodec` H.264 decoder handle snapshots because they can have AVPlayer threads with zero HLE AVPlayer players. Old active media states without the needed audio/movie/decoder sections must fail closed rather than pretending to be exact. The restore-readiness manifest must use the same wait-queue capability rules as restore itself: unsupported timed deferred waits should report `kernel-wait-queues`, not `Missing serializers: none`.
- `save-state-dir` can move the state root to a custom directory; relative paths resolve under the shared Vita3K data path, and absolute paths are used as-is. `save-state-compression-level` uses miniz level 0-9 and defaults to 1 for fast compression.
- PPSSPP-level durable save/load is the target, not the current state. Treat save-state work as a serialization subsystem: CPU contexts, guest RAM, allocator maps, kernel thread/object/wait state, GPU/display/renderer state, texture/surface caches, audio output state, audio decoder state, AVPlayer/movie state, IO/VFS handles, timing state, and per-game metadata all need versioned capture/restore plus refusal paths for unsafe states. Update the restore-readiness manifest whenever one of those layers becomes real so future agents can see exactly what changed.

## Runtime OSD

- The game-running OSD opens from a short Android Back press (`AC_BACK`) on AYN Thor. A long Android Back press should route to the Vita PS/Home path and return to the Vita LiveArea/home flow for the running app. Do not bind plain gamepad Select/`BTN_SELECT`/SDL `GamepadBack` as a second OSD opener; Select must remain Vita `SCE_CTRL_SELECT` and the modifier for `Select + R1`, `Select + right-stick down`, and `Select + right-stick up`.
- L3 + R3 is also a runtime OSD toggle for desktop/Windows controller testing and handhelds with both stick-click buttons. Keep it as a pressed-edge chord so holding both sticks does not repeatedly flicker the OSD, and keep single L3/R3 mapped as normal Vita controls.
- Long Android Back must first restore fast-forward to 100% before routing PS/Home, and virtual-cartridge LiveArea lookups must resolve either source archive path or title ID without null-crashing.
- AYN Thor/Odin controller input may expose Back/Select through multiple Android paths (`KEY_BACK`, `KEY_APPSELECT`, `BTN_SELECT`, and SDL gamepad Back). Back and Select are separate controls: `Emulator.dispatchKeyEvent` forwards `KEYCODE_BACK` directly to SDL's native key path so Vita3K sees `AC_BACK`; `BTN_SELECT`/SDL gamepad Back is Vita Select and Select chords only. When debugging OSD behavior, capture `getevent -lp`, SDL/logcat event traces, and before/after screenshots before changing bindings.
- Opening the OSD pauses guest threads by default. Closing/resuming from the OSD resumes guest threads unless the user explicitly changed pause state in the OSD.
- OSD feedback should replace toast feedback for runtime actions. Fast-forward, save/load quickstate, cheat toggles, and pause/resume should update OSD/overlay status and logs.
- The OSD Controls section should expose two separate choices: `System Confirm` for emulator/Vita shell O/X convention, and `Game X/O` for per-game controller swap. Keep the Japanese-game helper as a preset layered on those controls, not as a hidden global mode.
- OSD first-level actions currently include Resume, Pause/Resume, Settings, Save State slot 0, Load State slot 0, Undo Load, Screenshot, Renderer Trace, Off/2x/3x/4x fast-forward presets, and disabled placeholders for Reset Game and Close Game.
- Keep the OSD readable over bright or glitchy game frames: dim the game behind it, use an opaque high-contrast panel, and size text/buttons for handheld viewing rather than desktop mouse precision.
- Renderer Trace is a runtime diagnostic switch. When enabled it emits `ThorRenderTrace` logcat lines for Vulkan scene setup, the first 32 draws per scene, and texture configure/upload events. Include render target, color/depth surface addresses, formats, depth/stencil state, shader hashes, texture counts, texture address/format/type/stride/upload bytes, mapping mode, surface sync state, and driver flags.
- For ADB-only render/crash investigations, launch with `--thor-render-trace` to enable the same renderer trace at startup, or use `tools/thor_adb_debug_capture.ps1 -GamePath <zip> -RenderTrace` to clear logcat, launch, and capture screenshot/logcat/crash-buffer/window/meminfo artifacts under ignored `tmp/`. Summarize durable findings into `reports/debug_knowledge.sqlite`.
- The Cheats card of the pause menu (Session tab) shows how many cheats are on for the running title and opens `CheatsSheet`, where each cheat has a switch, codes the engine does not understand are marked, and the file can be reloaded.
- The status area shows title ID, current speed percentage, selected custom driver on Android, quickstate slot status, and whether a matching cheat file was loaded.
- Keep the OSD usable with controller only: D-pad/left stick navigates, Cross/A confirms, Circle/B cancels, Back/Select closes. It should also work with touch/mouse when available. ImGui navigation must remain enabled, and the SDL backend must use real SDL3 gamepad instance IDs/player index instead of assuming gamepad index `0`.
- Keep OSD rendering lightweight and in the existing ImGui path. Do not open the Vita Live Area or normal settings dialog just to perform runtime actions.

## Graphics Debugging And Profiling

- For renderer bugs, invoke the repo skill `.agents/skills/vita3k-render-debug/SKILL.md` before patching. The expected loop is SQLite search, attempt check, burst capture, pause/stabilize when possible, live draw/surface isolation, Windows proof, Android/Thor proof, SQLite attempt entry, then commit/push.
- Always confirm the foreground Android package before attributing a screenshot to Vita3K. `adb shell dumpsys window` should identify whether the visible issue belongs to `org.vita3k.emulator.debug`, Cocoon, RPCSX, or another frontend.
- AYN Thor can report separate focus lines per display; Vita3K may be running on the second screen while the launcher remains focused on another display. Prefer the focus line and screenshot that include `org.vita3k.emulator.debug` before declaring a capture wrong.
- The default fix loop for serious renderer/game failures is Windows first, Android second. First reproduce the game or scene on Windows with the same ZIP/cartridge path, save data, shader logs, and Vulkan trace controls; fix emulator-core, shader translator, CPU, module, or VFS issues there because rebuild/restart cycles are faster and Ghidra/static analysis is practical. Only after the Windows/core behavior is understood should Android/Thor-specific issues be investigated, such as Adreno/Turnip behavior, SurfaceFlinger presentation, SurfaceView alpha/composition, Android input routing, and APK asset packaging.
- Treat black screens as a classification problem before editing code. A black screenshot with active shaders and `PC: 0x00000000`/invalid memory reads points toward a guest CPU/module/import/null-function-pointer problem; a valid Windows frame that is black only on Android points toward presentation, driver, or swapchain/composition logic like the UPPERS present-alpha issue.
- A repeatable AI-assisted fix cycle should produce artifacts at every step: Thor screenshot/log/profile dump, Windows repro notes, Ghidra/API-call-site notes when needed, a minimal emulator patch, Windows proof screenshot/log, Android APK install proof, Thor screenshot/log proof, and SQLite entries for observation, decision, fix, and regression risk. Do not skip the proof step just because a hypothesis feels likely.
- For renderer bugs that are not obviously Adreno/Turnip-only, use the Windows desktop loop before rebuilding Android: build `cmake --preset windows-vs2022` and `cmake --build build/windows-vs2022 --config RelWithDebInfo --target vita3k --parallel`, stage the target ZIP under ignored `roms/issues/<TITLEID>/`, mirror only needed Thor firmware/user/save data into the local Windows Vita3K profile, then launch with `tools/windows/start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug>`. This can reproduce cartridge/VFS and many renderer traces in seconds; still verify final fixes on AYN Thor because NVIDIA Vulkan and Adreno/Turnip may diverge.
- Windows debug launches must force the local offline PSN compatibility flag on unless the experiment is explicitly testing signed-out behavior. `tools/windows/start-game-render-debug.ps1` writes `psn-signed-in: 1` into its generated launch config by default so games like DOA Venus do not stop at NetCheck `0x80100C06` before renderer testing. The emulator-side per-game `CurrentConfig` default must also stay signed in so older/missing custom app XML does not silently reintroduce signed-out NetCheck failures. If `--config-location` is used, that explicit file must override the root config even for values equal to compiled defaults.
- PSN emulation in this fork is offline compatibility, not real network login. `sceNpGetServiceState`, `sceNpCheckCallback`, and PSN/PSN_ONLINE `sceNetCheckDialogGetResult` should report local signed-in success so stale configs cannot surface `0x80100C06` during offline single-player testing.
- `--thor-render-trace` also enables debugger import/export logging and loaded ELF dumps for Windows-first diagnosis. Use the generated `elfdumps/` files only as ignored local evidence for Ghidra/API-call-site work; never commit dumped commercial game binaries or decrypted content.
- Windows desktop renderer testing uses a real controller connected to Windows, not the Thor controls over USB/ADB. Prefer an Xbox Wireless/XInput controller paired to Windows; confirm it appears in Windows before attributing the problem to Vita3K input. Quick checks: `Get-PnpDevice -PresentOnly | ? FriendlyName -match 'Xbox|XInput|Controller|Gamepad'`, Windows Bluetooth/game controller settings, and Vita3K/SDL logs for `gamepad`/`controller` lines. If the pad is paired after Vita3K is already running, restart Vita3K or verify SDL hotplug sees it.
- For Vita3K graphics bugs, record a SQLite observation with burst screenshot path, title ID, renderer, selected custom driver, resolution multiplier, texture/surface settings, logcat tail path, and whether the issue is in the launcher/OSD or in-game Vita rendering.
- For flicker, intermittent corruption, menus with moving backgrounds, or "check now" render investigations, do not rely on a single screencap. Capture a burst of at least 8-12 screenshots over a few seconds, and use longer bursts such as 60-120 frames when the bug appears only after a camera/menu rotation. Run `python tools/analyze_screenshot_burst.py <burst-dir>` after capture; use the generated `flicker_summary.txt`, `flicker_metrics.csv`, and `flicker_contact_sheet.jpg` to identify the largest visual jumps before deciding what broke. Keep burst PNGs under `tmp/` and summarize the useful frames in SQLite.
- When screenshot bursts are too sparse to catch title-loop flicker, record a short Android `screenrecord`, extract frames with `ffmpeg -vf fps=10`, and run `tools/analyze_screenshot_burst.py` on the extracted frame directory. DOA Venus title verification used this path to separate normal camera shot changes from renderer flicker.
- Single screenshots are allowed only for static UI proof after a burst already exists, or when the user explicitly asks for one image. Emulator/render checks default to burst capture.
- On Windows, use `tools/windows/capture-vita3k-burst.ps1 -Topic <case-slug> -Count 10 -IntervalMs 250` while the Vita3K window is visible and uncovered.
- On Android/AYN Thor, use `tools/android/capture-thor-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -Count 10 -IntervalMs 350`; it captures device-side PNGs, pulls them, and records window focus/logcat tail.
- Serious renderer debugging should be pause-first whenever the scene allows it. Once the user reaches a broken scene, pause guest execution through OSD/runtime control, capture screenshot/log/render state while the frame is stable, then test one diagnostic variable at a time before resuming. Avoid restart-heavy loops unless the code path only initializes at boot.
- Treat emulator pause as a debugging primitive, not only a user feature. Paused evaluation should support screenshot capture, logcat pull, render trace toggles, draw skip/stop-after changes for the next frame, surface/texture cache summaries, save-state attempts, and Ghidra/API-call-site note taking without forcing the user to replay long intros.
- If pausing changes or hides the bug, record that explicitly. Some glitches are timing, presentation, movie, or synchronization dependent; in those cases use burst capture plus a quick pause/resume check rather than assuming a still frame contains all the information.
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
- Do not broaden an Android/Turnip render-to-texture workaround to Windows without testing it there. DOA Venus proved `0x62FF8000` could dump clean while forced copied U2F sampling corrupted the Windows title loop; Windows needed direct viewport sampling, while Thor still needed the Adreno/Turnip copied path. Always test direct-vs-copied sampling per platform before making global surface-cache policy changes.
- DOA Venus corruption had two historical symptoms: native BCn sampling could produce magenta terrain on Adreno/Turnip and Windows, and a later stale/slab flicker was traced to DoubleBuffer mapped data that crossed the guarded mapping boundary. The title loop was fixed first, but post-title gameplay still crossed farther than the original 4 KiB guard and reproduced black/silhouette frames on both Windows and Thor. Commit `38391203` increases the Vulkan DoubleBuffer guard/sync allowance to 64 KiB; Windows and AYN Thor post-title gameplay bursts verified lit scene rendering with no `Buffer at address ... is not completely mapped` errors. Current Vulkan defaults BCn/DXT textures to Vita3K's CPU decompression path on every platform because 2026-05-15 A/B/A bursts showed native BCn still reintroduced stable magenta blocks on the Windows DOA title loop; use `VITA3K_ALLOW_NATIVE_BCN=1` / `debug.vita3k.allow_native_bcn=1` only inside a named experiment.
- DOA Venus statue/cloud/tree/title experiments are now a known high-risk loop. Do not rerun broad BCn, cubemap/reflection, depth-LEQUAL, cull, or shader-skip tests for `18f16721`, `564cd0f6`, `c31c2a24`, `0x722A0000`, `0x73000000`, `0x73400000`, or `0x6065F040` unless the preflight names the prior SQLite result and the new run adds missing instrumentation or a changed commit. If the question is still "which pass is wrong?", the next move is frame/resource capture, surface/texture dump metadata, RenderDoc, or code inspection, not another visual skip pass.
- DOA Venus bedroom black/missing-scene regressions can be caused by stale diagnostic Android props, not only renderer semantics. A prior Thor run had many `debug.vita3k.render_*_fhash=0` and `*_vhash=0` props; before the disabled-value fix those were parsed as prefix `"0"` and could accidentally enable diagnostic depth/cull/stride paths for shaders whose hash began with `0`. Current helpers in `context.cpp`, `pipeline_cache.cpp`, and `scene.cpp` must keep `0`/`false`/`off` disabled. The proof path was a Thor reldebug rebuild/install, DOA cartridge launch, autosave prompt automation with Circle/O, an 8-frame room burst, and a 12-second right-stick rotation screenrecord burst.
- Keep a maintained research record of Vita CPU/GPU behavior when bugs point beyond obvious renderer code: GXM draw semantics, PowerVR SGX tiling/deferred rendering behavior, shader patcher/GXP translation, CPU/GPU sync, memory mapping, vertex/index stream lifetimes, and texture/surface formats should be documented in SQLite before risky renderer rewrites.
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
- Use `tools/thor_live_debug_stream.ps1` when the user is actively playing and the agent needs a stream of evidence. It writes rolling samples under `tmp/thor-live/<timestamp>_<topic>/` and keeps `latest.txt` and `latest-screen.png` fresh. This is the preferred "play while the agent watches logs/screenshots" workflow; summarize durable findings in SQLite.
- Use `tools/thor_profile_dump.ps1 -Topic <semantic-topic> [-RenderTrace] [-TitleId <TITLEID>]` for a one-shot profile bundle from a running repro. It captures a screenshot burst plus a `screen.png` compatibility copy, logcat, crash buffer, window focus, gfxinfo/frame stats, meminfo, cpuinfo, thermal state, SurfaceFlinger, top threads, device props, and a renderer-trace summary.
- When checking a live render issue from ADB, take a burst snapshot set before and after any live property change. A good default is 10 screenshots at 250-500 ms spacing, plus logcat/window focus, so flicker, alternating surfaces, bad clears, and transient composite failures are visible instead of hidden by a single frame that happened to look correct.
- For long Android title/menu flicker checks, prefer a 30-60 second `adb shell screenrecord --time-limit <seconds> --bit-rate 12000000 --size 1280x720 /sdcard/<name>.mp4`, then pull it, extract sampled frames with `ffmpeg -vf fps=10`, and analyze those frames. This catches fast temporal flicker better than sparse PNG bursts.
- For camera-rotation bugs such as DOA Venus head/hair or room occlusion, prefer `tools/android/capture-thor-rotation-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -DurationSec 45 -FrameFps 6 -Rotate -Axis ABS_Z`. It holds the Odin Controller analog axis, records a continuous Thor video, extracts frames, writes metadata/logcat/debug props, and runs the burst analyzer. Use this before changing renderer code so a full 360-ish rotation shows whether the bug is angle-specific alpha ordering, flicker, missing geometry, or stale presentation.
- Before comparing Android renderer captures, list `adb shell getprop | Select-String -Pattern 'debug.vita3k'` and clear stale diagnostic props that are not part of the named test. At minimum clear skip, stop-after, trace, dump, U2F-copy override, DoubleBuffer always-copy, BCn override, and depth override props; keep `debug.vita3k.force_bcn_decompress=1` only when the capture name explicitly says the BCn fallback is enabled.
- On Windows PowerShell, avoid `adb exec-out screencap -p > file.png` for proof captures because binary redirection can produce invalid PNG files. Use device-side `screencap -p /sdcard/...png`, then `adb pull`, then remove the temporary device file.
- On Thor Android 13, `screencap -d 0` can fail even when display 0 is active. If `tools/android/capture-thor-burst.ps1 -DisplayId 0` fails, omit `-DisplayId`; default device-side `screencap -p` has captured the Vita3K screen correctly when focus is on `org.vita3k.emulator.debug`.
- Use `tools/thor_save_sync.ps1 -TitleId <TITLEID> -Backup` before risky repro work, and `tools/thor_save_sync.ps1 -TitleId <TITLEID> -InstallPath <folder-or-zip> [-Replace]` only for decrypted Vita savedata exports. Use `-Replace` when restoring an exact save snapshot so stale extra files are removed. Do not commit pulled saves or public/user save archives.
- On Android, renderer trace can be toggled while a game is already running with `adb shell setprop debug.vita3k.thor_render_trace 1` and disabled with `adb shell setprop debug.vita3k.thor_render_trace 0`. `tools/thor_live_debug_stream.ps1 -RenderTrace` sets the property before sampling so the agent can capture `ThorRenderTrace` scene/draw/texture lines without making the user restart the game.
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

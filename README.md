# Vita3K Thor Experiment

<p align="center">
  <img src="docs/media/branding/vita3k-thor-experiment-banner.svg" alt="Vita3K Thor Experiment banner">
</p>

<p align="center">
  <img src="docs/media/branding/no-support-fork-it.svg" alt="No support. Fork it and own the result.">
</p>

This is a personal Android fork of [Vita3K](https://github.com/Vita3K/Vita3K) for the AYN Thor. It is tuned around handheld testing, renderer/input experiments, and local proof logs. It is not upstream Vita3K, not an official release channel, and not trying to be a general-purpose support project.

> [!WARNING]
> This fork is vibe coded with AI assistance. That is intentional and disclosed. If AI-assisted code, docs, or generated assets bother you, this repo is not for you. Use upstream Vita3K or another fork.

> [!CAUTION]
> Personal-use experiment. No guarantee of stability, compatibility, correctness, performance, support, or future updates. No games, license files, firmware, keys, or copyrighted game content are included. Use your own legally dumped content and homebrew.

## What This Is

- Android-focused Vita3K fork for AYN Thor Base/Pro/Max testing.
- A place for Thor renderer, graphics-driver, input, touch, scaling, and suspend/resume experiments.
- Built around Android `arm64-v8a` APK checks, not desktop release packaging.
- Uses upstream Vita3K documentation as the baseline for emulator setup and build expectations.
- Uses Thor proof logs, screenshots, and short videos when a game or setting is called working.

## Target Hardware

Optimization work assumes AYN Thor Base/Pro/Max hardware: Snapdragon 8 Gen 2, Adreno 740, active cooling, LPDDR5X, and UFS4 storage. Thor Lite is a different Snapdragon 865 / Adreno 650 target and should not drive defaults unless explicitly called out.

## What This Is Not

- Not upstream Vita3K.
- Not a supported emulator distribution.
- Not a compatibility reporting project.
- Not a place to request games, licenses, firmware, keys, copyrighted files, or piracy help.
- Not a promise that any game, renderer setting, controller mapping, or performance tweak will work for your copy of a game.

## Support And Issues

Do not open issues expecting support for this experiment. Fork it, test it, patch it, and own the result. If the AI/vibe-coded nature of this fork is a problem, look elsewhere.

The upstream Vita3K project has its own rules, standards, and support expectations. Do not send Thor-experiment problems to upstream Vita3K.

## Major Feature Differences From Upstream

This fork keeps its differences from stock Vita3K visible and testable:

- README and repo branding identify this as the `Vita3K Thor Experiment`.
- Writable fork remote is `git@github.com:noeldvictor/Vita3K-Thor.git`.
- Android testing targets the existing `android/` Gradle project and `arm64-v8a` native build path.
- GPU settings include an in-app Turnip driver picker that can download K11MCH1 AdrenoToolsDrivers ZIP assets, install them, mark a Thor/Adreno 740 recommendation, select the installed custom driver, and delete cached driver ZIPs.
- Virtual cartridge mode can launch `.zip`/`.vpk` archives directly with `--cartridge`, from the Android UI, or from Android open-with/front-end intents without installing them into the normal Vita3K app library.
- Archive introspection understands common nonstandard Vita ZIP layouts, including `app/<TITLEID>`, `patch/<TITLEID>`, and `rePatch/<TITLEID>` overlays used by translated game packs.
- Direct ZIP cartridge mode now caches very large deflated archive members, such as multi-gigabyte `.psarc` files, to app-local storage on first open instead of inflating them into RAM every time. This keeps large games like `PCSG00490`/Sora no Kiseki the 3rd Evolution from blowing up Android native heap during startup.
- Android virtual-cartridge scanning includes common internal and removable SD card roots such as `/storage/<card>/Roms/psvita`.
- The runtime OSD opens from short Back while a game is running, supports controller navigation, and exposes pause/resume, save/load quickstate slot 0, fast-forward, screenshots, renderer trace, and cheat toggles.
- Long Back maps to the Vita PS/Home path for returning to the LiveArea/home flow instead of opening the OSD.
- Runtime hotkeys are tuned for Thor: `Select + R1` fast-forward toggle, `Select + right-stick down` save quickstate, and `Select + right-stick up` load quickstate.
- Fast-forward defaults to 200% and adjusts display/vblank pacing plus guest kernel clock/wait timing together.
- Experimental same-session quickstate support snapshots guest CPU contexts and allocated memory pages for slot 0. It is not yet a durable full emulator save-state format.
- VitaCheat `.psv` files are detected by title ID from repo, app storage, emulated `ux0/vitacheat`, and Android SD-card cheat roots. The OSD can show loaded cheats and toggle individual entries, while unsupported VitaCheat code types fail closed.
- `tools/sync_vitacheat_db.ps1` can clone/update a third-party VitaCheat database into ignored local scratch storage and push it to the Thor SD card without committing unlicensed cheat files.
- `--thor-render-trace` enables the Thor renderer GXM trace at startup for ADB/front-end launches, and `tools/thor_adb_debug_capture.ps1` captures logcat, crash buffer, window focus, meminfo, screenshot, and a Markdown report for repeatable device debugging.
- Thor test APKs prefer debug-friendly builds while renderer, cartridge, input, and OSD experiments are still moving.
- Compatibility claims should include commit, APK/build type, renderer, graphics driver, title ID, settings, screenshots, and logs.
- Thor-only behavior should be guarded behind settings, build flags, device detection, or clearly named code paths.
- The Android app label and package id are intentionally unchanged unless a future patch says otherwise.
- Broad Vita3K fixes should stay clean enough to propose upstream separately.
- No game files, firmware, license files, or commercial content should be committed here.

## Questions This Fork Should Answer

- Which Vita3K Android renderer and graphics-driver settings behave best on AYN Thor?
- Which games are useful Thor canaries for fast regression testing?
- Can physical controls, front touch, rear touch, analog input, audio, save/load, and suspend/resume all survive real handheld use?
- Which performance changes are general Vita3K improvements, and which ones should stay Thor-only?
- What proof is enough before calling a game or setting working: screenshot, logcat, frame pacing, controller proof, save proof, or all of them?

## Thor Screenshot

No live Thor screenshot is checked in yet. When one is added, use a real device screenshot from this fork and make clear that no games are bundled with this repository.

Suggested path:

```text
docs/media/screenshots/vita3k-thor-android.png
```

## Build Locally

This fork is currently aimed at Android/AYN Thor APK experiments:

```powershell
git submodule update --init --recursive
Copy-Item -Recurse -Force data android/assets
Copy-Item -Recurse -Force lang android/assets
Copy-Item -Recurse -Force vita3k/shaders-builtin android/assets
.\gradlew.bat assembleReldebug
```

APK output:

```text
android/build/outputs/apk/reldebug/android-reldebug.apk
```

Desktop builds still follow the upstream-style instructions in [`building.md`](./building.md).

## Content And Firmware

Vita3K-Thor does not include PlayStation Vita games, firmware, licenses, keys, or copyrighted game content. Use legally dumped content and homebrew only.

Useful upstream/user resources:

- [Vita3K quickstart](https://vita3k.org/quickstart)
- [Vita3K compatibility list](https://vita3k.org/compatibility.html)
- [Vita3K homebrew compatibility list](https://vita3k.org/compatibility-homebrew.html)
- [VitaDB homebrew](https://www.rinnegatamante.eu/vitadb/#/)

## Remotes

- Writable fork: `git@github.com:noeldvictor/Vita3K-Thor.git`
- Upstream reference: `https://github.com/Vita3K/Vita3K`

Clone this fork with SSH:

```sh
git clone --recursive git@github.com:noeldvictor/Vita3K-Thor.git
cd Vita3K-Thor
```

## Upstream Credit

Vita3K is an open-source PlayStation Vita emulator. This fork exists because upstream Vita3K and many emulator contributors did the real foundational work.

This repository remains under the upstream license terms. See [`COPYING.txt`](./COPYING.txt).

PlayStation, PlayStation Vita, and PlayStation Network are registered trademarks of Sony Interactive Entertainment Inc. This fork is not related to or endorsed by Sony, Vita3K upstream, or any commercial game publisher.

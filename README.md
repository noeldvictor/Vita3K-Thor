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

## User-Facing Differences

The short version: this fork is Vita3K tuned for testing on AYN Thor.

- Turnip driver manager: download GitHub Turnip ZIPs from K11MCH1/AdrenoToolsDrivers, see the Thor/Adreno 740 recommendation, install one, select it, and delete cached ZIP downloads.
- Play ZIP as Cartridge: launch `.zip` and `.vpk` archives directly from the Android UI, Android open-with/front-end intents, or `--cartridge` without installing them into the normal Vita3K library.
- Smarter ZIP scanning: translated and nonstandard archives with `app/<TITLEID>`, `patch/<TITLEID>`, and `rePatch/<TITLEID>` are detected, and cached icons/backgrounds avoid full rescans every launch.
- Clear library badges: encrypted/PFS-style virtual cartridge entries get an `E` badge and fail with a clear message; games with matching VitaCheat files get a `C` badge.
- Runtime OSD: short Back opens a controller-friendly in-game menu for pause/resume, save/load quickstate slot 0, fast-forward speed, screenshots, renderer trace, and cheats.
- Thor hotkeys: `Select + R1` toggles fast-forward, `Select + right-stick down` saves state, and `Select + right-stick up` loads state.
- Fast-forward presets: Off, 2x, 3x, and 4x are available from the OSD; the selected speed also drives the hotkey.
- Experimental quickstates: per-game slot 0 saves compressed state files under `states/<TITLEID>/slot0.thorstate`. Same-session save/load works; app-restart durability is still incomplete and is being expanded toward PPSSPP-style reliability.
- VitaCheat support: `.psv` files can be loaded from repo/user/app/SD-card cheat roots and toggled per game in the OSD. Unsupported code types fail closed.
- Thor debug capture: one-shot and live ADB tools collect logs, screenshots, focus, meminfo, gfxinfo, crash buffers, and renderer trace evidence while testing on device.

## Technical Differences And Goals

- Android `arm64-v8a` APKs and real AYN Thor testing are the main path; desktop builds still follow upstream expectations.
- Direct ZIP cartridge mode mounts `app0:` against the archive and applies read-time `patch`/`rePatch` overlays. It does not bypass encryption, licenses, or ownership checks.
- Large deflated archive members, including multi-gigabyte `.psarc` files, are cached to app-local storage instead of inflated into RAM.
- Fast-forward changes display/vblank pacing and guest kernel clock/wait timing together.
- Quickstates currently serialize CPU contexts, allocated guest memory pages, and allocator maps. The next durability targets are kernel objects, GPU/display state, audio, AVPlayer/movie state, IO/VFS handles, and renderer cache state.
- `--thor-render-trace` adds GXM/Vulkan trace logs, including scene, draw, surface, and texture upload details for renderer debugging.
- Thor-only behavior should stay behind settings, build flags, device checks, or clearly named code paths.
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

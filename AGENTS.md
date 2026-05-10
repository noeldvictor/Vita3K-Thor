# Vita3K Thor Agent Notes

These notes are for work in Vita3K Thor Experiment, a personal Android-focused Vita3K fork for AYN Thor testing. Keep changes practical, reversible, and clearly scoped to handheld compatibility work.

## Source Control

- Writable remote is the user's fork: `origin = git@github.com:noeldvictor/Vita3K-Thor.git`.
- Upstream reference is `https://github.com/Vita3K/Vita3K`.
- Always push with SSH; do not switch `origin` to HTTPS.
- Do not push to upstream Vita3K from this checkout.
- Keep Thor-specific changes easy to identify so broadly useful fixes can be proposed upstream separately.
- Do not commit APK outputs, build folders, downloaded driver ZIPs, extracted drivers, caches, SDKs, firmware, license files, or game content.

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

## Playing Without Install

- Vita3K Thor now has an experimental cartridge launch path for archives: use `--cartridge <path-to-vpk-or-zip>` to mount game content as a read-only virtual game card for the session instead of adding it to the installed app library.
- Android builds shallow-scan `/sdcard/roms/psvita` and `/storage/emulated/0/roms/psvita` by default when `scan-virtual-cartridges` is enabled. Compatible `.zip`/`.vpk` archives in the root or one direct child folder, plus extracted direct child folders containing `sce_sys/param.sfo`, are listed as virtual cartridges in the app grid.
- Virtual cartridge app entries must launch directly, not through Live Area, because the content is not installed under `ux0/app`.
- On device, the visible launcher path is `File` -> `Play ZIP as Cartridge`; select a `.zip` or `.vpk`, wait for the virtual cartridge cache to mount, then press `Start Cartridge`.
- Android file/front-end launching is supported through `ACTION_VIEW` and `ACTION_SEND` for `.zip`/`.vpk`-style archive intents. The Android bridge converts the incoming file/content URI into `-a true --cartridge <path>`.
- If Android only provides a content URI without a raw filesystem path, copy the URI into app-local `cartridge_launch/` and launch the copied archive from there.
- Cartridge launch now mounts `app0:` directly against the chosen `.zip`/`.vpk` archive and lazily inflates individual file entries on open. Do not reintroduce whole-archive extraction for this path unless it is explicitly a fallback.
- The visible menu path should add only a transient in-memory app entry for launch and must not save the cartridge title into the normal installed app cache.
- Normal `content-path` without `--cartridge` remains the upstream install-and-run convenience path for `.vpk`/`.zip` archives or content folders.
- The `--installed-path` / `-r` path runs an already-installed app path from Vita3K storage. A future no-install-like UX would need a new staging, cache, or mount feature and must not bypass ownership or license expectations.
- Cartridge mode should stay read-only from the emulated app side. Do not let games create, delete, or rename files under the virtual card path.
- The direct archive VFS currently reads each requested ZIP entry into memory when opened; this is real no-staging ZIP launch, but not yet compressed random-access streaming.

## Cheats And Runtime Hotkeys

- Cheats are offline single-player only. Do not add online cheating, anti-cheat bypass, DRM bypass, license bypass, or commercial cheat pack redistribution.
- VitaCheat `.psv` files can be detected by title ID from repo/user cheat roots such as `cheats/<TITLEID>.psv`, `cheats/db/<TITLEID>.psv`, shared `cheats/`, or `ux0/vitacheat/db/`.
- Only commit third-party cheat files when their license/source permits redistribution. Otherwise commit importer/conversion tooling and user instructions, not the database itself.
- Games with detected cheat files show a `C` badge in the app list.
- Runtime cheat support is fail-closed and currently applies only enabled `_V1` VitaCheat static writes: `$0000`, `$0100`, `$0200`, plus simple `$B200` main-module segment-relative base selectors. Unsupported pointer, condition, block, and button-code formats are skipped and logged.
- `tools/convert_vitacheat.py` converts VitaCheat `.psv` files into JSON metadata for auditing and future UI work. The emulator runtime still reads `.psv` directly.
- Runtime shortcuts reserved for Thor testing: `Select + R1` toggles 200% fast-forward, `Select + right-stick down` requests save state, and `Select + right-stick up` requests load state.
- Save-state/load-state shortcuts reserve per-game slot 0 under shared `states/<TITLEID>/`, but serialization is still a placeholder until CPU, memory, kernel thread, GPU/display, IO, and audio state capture is implemented and validated together.

## ADB Thor Testing

- When an APK is built and an AYN Thor is connected, push/install it to the Thor with ADB for real-device testing.
- Start with `adb devices` and verify the connected device is the user's AYN Thor before installing.
- Prefer non-destructive installs such as `adb install -r path\to\apk`. Do not uninstall the existing app or clear Vita3K data unless the user explicitly accepts data loss.
- For debug/reldebug APKs, expect the `.debug` package slot unless the build config says otherwise.
- After installing, launch through ADB or the device UI, then capture proof with screenshots, `logcat`, selected driver, renderer settings, and any game/title ID tested.
- Save durable test notes in `reports/YYYYMMDD_HHMMSS.md`; keep bulky raw logs out of git unless the user asks to commit them.

## Reporting Thor Results

- Record device model, Android version, Vita3K commit, APK/build type, renderer, selected driver, title ID, game version/update, settings, screenshots, and logs.
- A "works" claim should include proof for boot, rendering, input, audio, save/load, suspend/resume, and exit when those areas matter.
- Do not send Thor-experiment regressions to upstream Vita3K unless the issue is reproduced cleanly on upstream too.
- Write repo work reports as timestamped Markdown files under `reports/`, using names like `YYYYMMDD_HHMMSS.md`.
- Reports should briefly state what changed, why, verification performed, and any remaining blockers.

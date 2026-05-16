---
name: vita3k-thor-android-loop
description: Use for AYN Thor Android Vita3K APK install, cartridge launch, ADB debug props, screenshot bursts, logcat/profile capture, and Android proof after Windows renderer work.
metadata:
  short-description: AYN Thor Android proof loop
---

# Vita3K Thor Android Loop

Use this skill when testing on the user's AYN Thor. Android is the final gate for Turnip/Adreno behavior, input routing, APK packaging, and handheld performance.

## Device And Install

Verify the device first:

```powershell
$adb = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk\platform-tools\adb.exe'
& $adb devices
```

Use non-destructive installs:

```powershell
& $adb install -r android\build\outputs\apk\reldebug\android-reldebug.apk
```

Do not uninstall, clear data, or remove saves unless the user explicitly accepts data loss.

## Launch

Prefer direct cartridge ZIP launch for game repros. Keep the checked-in helpers responsible for PowerShell/ADB quoting. Raw launches should pass Vita3K args through `AppStartParametersJsonBase64`, not hand-written comma-sensitive strings.

For prompt input:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial <serial> -Sequence circle,wait:500,start
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial <serial> -Mode Sendevent -Sequence osd
```

Circle/O is often confirm for Japanese/Asian Vita titles.

## Capture

Clear stale renderer props before comparisons:

```powershell
& $adb shell getprop | Select-String -Pattern 'debug.vita3k'
```

Capture bursts by default:

```powershell
.\tools\android\capture-thor-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -Count 12 -IntervalMs 350
python tools\analyze_screenshot_burst.py <burst-dir>
```

For camera-rotation or flicker bugs, prefer:

```powershell
.\tools\android\capture-thor-rotation-burst.ps1 -Adb $adb -Serial <serial> -Topic <case-slug> -DurationSec 45 -FrameFps 6 -Rotate
```

Use `tools/thor_profile_dump.ps1` or `tools/thor_adb_debug_capture.ps1` when crashes, ANRs, black screens, or performance issues need logs/profile context.

## Runtime Props

Use Android props for live renderer and pause actions when available:

```powershell
& $adb shell setprop debug.vita3k.runtime_action pause
& $adb shell setprop debug.vita3k.runtime_action_id 20260516-0001
& $adb shell setprop debug.vita3k.render_trace 1
& $adb shell setprop debug.vita3k.render_stop_after "rt=960x544:draw=5"
& $adb shell setprop debug.vita3k.render_stop_after 0
```

Treat `0`, `false`, and `off` as disabled. Record intentional props in SQLite with the burst/log path and installed APK commit.

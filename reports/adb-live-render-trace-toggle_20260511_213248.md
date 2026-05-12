# ADB Live Render Trace Toggle - 2026-05-11 21:32:48

## Summary

Added an Android ADB property hook so Codex can enable or disable renderer trace while a game is already running on AYN Thor.

## Commands

Enable trace:

```powershell
adb shell setprop debug.vita3k.thor_render_trace 1
```

Disable trace:

```powershell
adb shell setprop debug.vita3k.thor_render_trace 0
```

The live debug stream now sets the property automatically when `-RenderTrace` is passed.

## Why

The UPPERS render issue is intermittent and scene-dependent. Restarting the game or asking the player to use the OSD can disturb the exact bad frame. This lets Codex switch trace on for a short capture while the user keeps playing.

## Validation

- `git diff --check` passed with CRLF warnings only.
- PowerShell syntax parsing passed for the updated capture scripts.
- Android `:android:assembleReldebug` passed.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to the connected AYN Thor with `adb install -r`.
- Reset `debug.vita3k.thor_render_trace` to `0` after install so normal play is not flooded with trace logs.

## Related UPPERS Captures

- `reports/uppers-render-weird-check_20260511_212607.md`
- `reports/uppers-render-weird-recheck_20260511_212753.md`

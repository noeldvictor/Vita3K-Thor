# Windows UPPERS Render Debug Loop - 2026-05-12 09:59:16

## Goal

Check whether UPPERS renderer work can be debugged on Windows instead of doing every experiment through the slower Android build/install/play loop.

## Result

- Built the Windows desktop target successfully at `build/windows-vs2022/bin/RelWithDebInfo/Vita3K.exe`.
- Pulled the UPPERS archive from Thor into ignored local storage:
  - `tmp/local-games/Uppers (English v0.97)[vita3k].zip`
- Mirrored only the needed Thor Vita3K system/profile pieces into the local Windows Vita3K profile:
  - `os0`, `sa0`, `vs0`, `vd0`, `pd0`
  - `ux0/user/00/user.xml`
  - `ux0/user/00/savedata/PCSG00633`
- Created an ignored disposable Windows debug config at `tmp/vita3k-win-debug/config.yml` with first-run setup skipped and user `00` auto-selected.
- Verified UPPERS boots locally from the ZIP in cartridge mode with renderer trace enabled:
  - Window title: `UPPERS (PCSG00633) | Vulkan | 29 FPS (35 ms) | 960x544 | Bilinear`
  - Renderer: Windows NVIDIA Vulkan, `NVIDIA GeForce RTX 3060`
  - Trace: `ThorRenderTrace` scene/draw/texture-upload lines written to `build/windows-vs2022/bin/RelWithDebInfo/vita3k.log`

## Why This Helps

The local loop avoids rebuilding and reinstalling Android for every renderer instrumentation change. It can quickly validate cartridge ZIP mounting, trace output, surface/texture behavior, and many renderer invariants. Final fixes still need Thor validation because Adreno/Turnip behavior can differ from NVIDIA Vulkan.

## Code Fix Needed

Windows desktop compilation hit a Win32 macro collision in `archive_install_dialog.cpp` because `ERROR` is defined by Windows headers and conflicts with `host::dialog::filesystem::Result::ERROR`. Added a local `#undef ERROR`, matching the pattern already used in other GUI files.

## Useful Command

```powershell
$exeDir = Resolve-Path .\build\windows-vs2022\bin\RelWithDebInfo
$exe = Join-Path $exeDir 'Vita3K.exe'
$cfg = Resolve-Path .\tmp\vita3k-win-debug\config.yml
$game = Resolve-Path -LiteralPath '.\tmp\local-games\Uppers (English v0.97)[vita3k].zip'
Start-Process -FilePath $exe -WorkingDirectory $exeDir -ArgumentList ('--config-location "' + $cfg + '" --cartridge --thor-render-trace --backend-renderer Vulkan --log-level 0 "' + $game + '"')
```

## Remaining Work

- Drive or automate the Windows build to the exact UPPERS scene that glitches on Thor and compare the local `ThorRenderTrace` against the Android/Turnip trace.
- If Windows reproduces the corruption, iterate renderer fixes locally first.
- If Windows does not reproduce it, keep using Windows for instrumentation sanity checks, then validate the fix path with Thor `tools/thor_live_debug_stream.ps1` or `tools/thor_profile_dump.ps1 -RenderTrace`.

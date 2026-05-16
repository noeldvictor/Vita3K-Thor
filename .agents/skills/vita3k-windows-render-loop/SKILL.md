---
name: vita3k-windows-render-loop
description: Use for Windows-first Vita3K game renderer repros, local launches, screenshot bursts, live renderer controls, input automation, and incremental rebuild checks before Android validation.
metadata:
  short-description: Windows renderer debug loop
---

# Vita3K Windows Render Loop

Use this skill when a renderer/game bug should be reproduced and isolated on Windows before building the Android APK.

## Launch

Keep issue ROMs under ignored `roms/issues/<TITLEID>/`, normally as `game.zip`.

```powershell
.\tools\windows\start-game-render-debug.ps1 -TitleId <TITLEID> -CaseSlug <case-slug> -BackendRenderer Vulkan -TraceLimit 256 -LogLevel 0
```

If needed, pass `-GameZip <path>` or copy with `tools/sync_issue_rom.ps1`. Never commit game content, saves, shader caches, logs, or screenshots unless explicitly promoted.

## Drive And Capture

Automate prompt/menu input:

```powershell
.\tools\windows\send-vita3k-input.ps1 -Sequence circle,wait:500,start
.\tools\windows\send-vita3k-input.ps1 -Sequence osd
.\tools\windows\send-vita3k-input.ps1 -Sequence click
```

Capture bursts, not single lucky frames:

```powershell
.\tools\windows\capture-vita3k-burst.ps1 -Topic <case-slug> -Count 12 -IntervalMs 250
python tools\analyze_screenshot_burst.py <burst-dir>
```

Use 60-120 frames for flicker or camera rotation symptoms.

## Live Controls

Use the generated render-control file before rebuilding:

```powershell
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Action pause
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -StopAfter "rt=960x544:draw=5" -TraceLimit 96
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Skip "addr=62FF8000:draw=20-40" -TraceLimit 96
.\tools\windows\set-render-debug-control.ps1 -ControlFile <render-control.txt> -Dump "sample=62FF8000:draw=0-20" -TraceLimit 64
```

Use `addr=` or `color_addr=` for producer render targets. Use `sample=` or `tex=` for consumer draws that read a suspicious surface.

## Build

Prefer targeted Windows builds for renderer fixes:

```powershell
cmake --build build/windows-vs2022 --config RelWithDebInfo --target vita3k --parallel
```

After a code change, capture the same scene again and record the commit/diff state, command, artifacts, and result in SQLite.

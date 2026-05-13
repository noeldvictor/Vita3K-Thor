---
name: vita3k-input-automation
description: Use when automating Vita3K Thor button presses on Windows or Android during debugging, including pressing Circle/O, Cross/X, Start, Select, Back, L3+R3 OSD, fast-forward/save/load chords, or repeatable menu navigation sequences for renderer/game repros.
metadata:
  short-description: Automate Vita3K Windows/Thor input
---

# Vita3K Input Automation

Use this skill when a debug loop needs repeatable button presses instead of manual user input. Prefer automation for prompts, title screens, OSD checks, fast-forward toggles, and short navigation sequences.

## General Rules

- Use scripts, not hand-typed one-off ADB commands, whenever possible.
- Keep sequences short and observable. Capture a screenshot/log after meaningful automation.
- Record useful automation steps in `reports/debug_knowledge.sqlite` with `tools/debug_knowledge.py entry add`.
- Do not use input automation to bypass ownership, login, DRM, online systems, anti-cheat, or account gates.

## Button Names

Common names: `cross`, `circle`, `square`, `triangle`, `start`, `select`, `back`, `up`, `down`, `left`, `right`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`.

Useful aliases:

- `osd`: L3 + R3 chord.
- `fast_forward`: Select + R1.
- `save_state`: Select + right-stick down on Windows keyboard mapping.
- `load_state`: Select + right-stick up on Windows keyboard mapping.
- `wait:500`: wait 500 ms.
- `circle:3`: press Circle three times.
- `select+r1`: explicit chord syntax.

## Windows

Use `tools/windows/send-vita3k-input.ps1`. It focuses the newest window whose title contains `Vita3K` and sends Vita3K's default keyboard-mapped controls.

```powershell
.\tools\windows\send-vita3k-input.ps1 -Sequence circle,wait:500,start
.\tools\windows\send-vita3k-input.ps1 -Sequence down:2,cross
.\tools\windows\send-vita3k-input.ps1 -Sequence osd
.\tools\windows\send-vita3k-input.ps1 -Sequence fast_forward
```

If the user changed Vita3K keyboard bindings, update the script mapping or pass manual keyboard input; do not assume the defaults still apply.

## Android / AYN Thor

Use `tools/android/send-thor-input.ps1`.

`KeyEvent` mode is the default and is best for simple game/UI prompts:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Sequence circle,wait:500,start
```

`Sendevent` mode uses the Odin Controller input device and is best for controller-routing bugs:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence back
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence osd
```

For Asian/Japanese Vita games, Circle/O is often confirm and Cross/X is cancel. For DOA Venus prompts, try `circle` before `cross`.

For Android quickstate actions, prefer runtime control or OSD automation over fake right-stick axis events until the exact Thor axis codes are captured for the current firmware. `send-thor-input.ps1` intentionally handles buttons only.

## SQLite Note

After automation materially changes a repro state, record it:

```powershell
python tools/debug_knowledge.py entry add --case <case-slug> --type test --platform android --summary "Automated prompt advance" --body "Used tools/android/send-thor-input.ps1 -Sequence circle,wait:500,start. Result: reached title/menu without manual input."
```

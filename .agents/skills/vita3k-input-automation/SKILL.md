---
name: vita3k-input-automation
description: Use when automating or debugging Vita3K Thor controller, keyboard, touch, or ADB input on Windows or Android, including stuck prompts, Circle/O vs Cross/X confirm, display-routed Android keyevents, Odin Controller sendevent, L3+R3 OSD, fast-forward/save/load chords, and repeatable repro navigation.
metadata:
  short-description: Automate and debug Vita3K controller input
---

# Vita3K Input Automation

Use this skill when a debug loop needs repeatable button presses instead of manual user input, or when automation gets stuck at a prompt. Prefer automation for prompts, title screens, OSD checks, fast-forward toggles, and short navigation sequences.

## General Rules

- Use scripts, not hand-typed one-off ADB commands, whenever possible.
- Keep sequences short and observable. After meaningful automation, capture a screenshot burst/log for emulator or render state changes; use a single screenshot only for static UI proof.
- Record useful automation steps in `reports/debug_knowledge.sqlite` with `tools/debug_knowledge.py entry add`.
- If input does not work, treat it as an input-routing bug before asking the user to press the same obvious button. Check focus, display id, event device, and app responsiveness in that order.
- Change one input route per probe. Name the burst/log after the route, such as `doa-autosave-keyevent-display0` or `doa-autosave-sendevent-odin`.
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
- `tap:1815:1005`: Android touchscreen tap in logical display coordinates. Quoted `'tap:1815,1005'` also works, but unquoted commas split PowerShell arrays.
- `keyevent:97`: Android raw keyevent code, useful when checking a mapping.

## Windows

Use `tools/windows/send-vita3k-input.ps1`. It focuses the newest window whose title contains `Vita3K` and sends Vita3K's default keyboard-mapped controls.

```powershell
.\tools\windows\send-vita3k-input.ps1 -Sequence circle,wait:500,start
.\tools\windows\send-vita3k-input.ps1 -Sequence down:2,cross
.\tools\windows\send-vita3k-input.ps1 -Sequence osd
.\tools\windows\send-vita3k-input.ps1 -Sequence fast_forward
.\tools\windows\send-vita3k-input.ps1 -Sequence click
```

If the user changed Vita3K keyboard bindings, update the script mapping or pass manual keyboard input; do not assume the defaults still apply.
Use `click` for Vita3K/ImGui modal buttons that do not respond to Vita controls. `click:x,y` clicks window-relative coordinates; `click@x,y` clicks absolute desktop coordinates.

After reaching a render repro on Windows, capture a burst:

```powershell
.\tools\windows\capture-vita3k-burst.ps1 -Topic <case-slug> -Count 10 -IntervalMs 250
```

## Android / AYN Thor

Use `tools/android/send-thor-input.ps1`.

Before diagnosing a stuck prompt, capture the input/focus state:

```powershell
$adb = 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\android-sdk\platform-tools\adb.exe'
& $adb devices
& $adb shell dumpsys window displays | Select-String -Pattern 'mCurrentFocus|mFocusedApp|displayId|org.vita3k|cocoon|odin'
& $adb shell dumpsys input | Select-String -Pattern 'FocusedDisplayId|Odin Controller|Touch Input Mapper|org.vita3k'
```

Vita3K should usually be focused on display 0. If `tools/android/capture-thor-burst.ps1 -DisplayId 0` fails on Thor Android 13, omit `-DisplayId`; default `screencap -p` can still capture the focused Vita3K screen.

`KeyEvent` mode is the default and is best for simple game/UI prompts:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Sequence circle,wait:500,start
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -DisplayId 0 -Sequence circle,wait:500,keyevent:97
```

`Sendevent` mode uses the Odin Controller input device and is best for controller-routing bugs:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence back
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence osd
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -InputDevicePath /dev/input/event9 -Sequence circle
```

Touch fallback is acceptable for game prompts that show a visible on-screen command:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -DisplayId 0 -Sequence tap:1815:1005
```

For Asian/Japanese Vita games, Circle/O is often confirm and Cross/X is cancel. For DOA Venus prompts, try `circle` before `cross`.

For Android quickstate actions, prefer runtime control or OSD automation over fake right-stick axis events until the exact Thor axis codes are captured for the current firmware. `send-thor-input.ps1` intentionally handles buttons only.

After reaching a render repro on Thor, capture a burst:

```powershell
.\tools\android\capture-thor-burst.ps1 -Adb $adb -Serial c3ca0370 -Topic <case-slug> -Count 10 -IntervalMs 350
```

### Stuck Prompt Ladder

Use this ladder when the same prompt keeps blocking renderer/debug work:

1. Verify focus and responsiveness with `dumpsys window displays`, `dumpsys input`, and a short logcat tail. If the activity is not responsive, record that as an emulator/app hang instead of continuing input probes.
2. Try `KeyEvent` with no display id, then `KeyEvent -DisplayId 0`. For DOA/Asian prompts, send `circle`/keyevent 97 before `cross`/96.
3. Try `Sendevent` against the Odin Controller. If auto-discovery picks the wrong event, pass `-InputDevicePath` from `adb shell getevent -lp`.
4. Try touch with display 0 logical coordinates, then rotated/native coordinates if dumpsys shows a rotated touch mapper.
5. Capture a 2-3 frame burst after each route and record the first route that changed state. If none work, stop and inspect Vita3K SDL/input handling or add a debug-only input injection hook; do not keep spamming buttons.

## SQLite Note

After automation materially changes a repro state, record it:

```powershell
python tools/debug_knowledge.py entry add --case <case-slug> --type test --platform android --summary "Automated prompt advance" --body "Used tools/android/send-thor-input.ps1 -Sequence circle,wait:500,start. Result: reached title/menu without manual input."
```

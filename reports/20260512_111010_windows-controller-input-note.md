# Windows Controller Input Note - 2026-05-12 11:10:10

## Context

During the Windows UPPERS renderer test loop, controller input was not working. The intended controller is an Xbox wireless controller connected to the Windows PC, not the AYN Thor's built-in controls.

## Findings

- Vita3K was running UPPERS on Windows normally.
- Windows saw the AYN Thor as `AYN Thor` / `ADB Interface`, which is useful for ADB/MTP but does not expose Thor built-in controls as a PC gamepad.
- The Windows device checks did not show an Xbox/XInput controller at the time of testing.
- Vita3K logs did not show useful SDL controller/gamepad detection lines.

## Agent Note Added

`AGENTS.md` now says Windows desktop renderer testing should use a real Windows-visible controller, preferably Xbox Wireless/XInput. Before debugging Vita3K input, confirm Windows sees the pad with device settings or:

```powershell
Get-PnpDevice -PresentOnly | ? FriendlyName -match 'Xbox|XInput|Controller|Gamepad'
```

If the controller is paired after Vita3K starts, restart Vita3K or verify SDL hotplug detected it.

param(
    [string[]]$Sequence,
    [string[]]$Button,
    [int]$Repeat = 1,
    [string]$WindowTitle = "Vita3K",
    [int]$PressMs = 90,
    [int]$HoldMs = 0,
    [int]$GapMs = 90,
    [int]$ClickYFromBottom = 125,
    [switch]$NoFocus,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if (-not $Sequence -and $Button) {
    $Sequence = @()
    foreach ($name in $Button) {
        $Sequence += "$name`:$Repeat"
    }
}

if (-not $Sequence) {
    throw "Provide -Sequence or -Button."
}

if ($HoldMs -gt 0) {
    $PressMs = $HoldMs
}

if (-not ('Vita3KInputWin32' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Vita3KInputWin32 {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern UInt32 SendInput(UInt32 nInputs, INPUT[] pInputs, Int32 cbSize);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out Vita3KInputRect lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(int dwFlags, int dx, int dy, int dwData, UIntPtr dwExtraInfo);

    public static UInt32 SendScan(UInt16 scan, bool extended, bool keyUp) {
        const UInt32 INPUT_KEYBOARD = 1;
        const UInt32 KEYEVENTF_EXTENDEDKEY = 0x0001;
        const UInt32 KEYEVENTF_KEYUP = 0x0002;
        const UInt32 KEYEVENTF_SCANCODE = 0x0008;

        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].U.ki.wVk = 0;
        inputs[0].U.ki.wScan = scan;
        inputs[0].U.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (extended) {
            inputs[0].U.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        if (keyUp) {
            inputs[0].U.ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        inputs[0].U.ki.time = 0;
        inputs[0].U.ki.dwExtraInfo = IntPtr.Zero;
        return SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Vita3KInputRect {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}

[StructLayout(LayoutKind.Sequential)]
public struct INPUT {
    public UInt32 type;
    public InputUnion U;
}

[StructLayout(LayoutKind.Explicit)]
public struct InputUnion {
    [FieldOffset(0)]
    public MOUSEINPUT mi;
    [FieldOffset(0)]
    public KEYBDINPUT ki;
    [FieldOffset(0)]
    public HARDWAREINPUT hi;
}

[StructLayout(LayoutKind.Sequential)]
public struct MOUSEINPUT {
    public Int32 dx;
    public Int32 dy;
    public UInt32 mouseData;
    public UInt32 dwFlags;
    public UInt32 time;
    public IntPtr dwExtraInfo;
}

[StructLayout(LayoutKind.Sequential)]
public struct KEYBDINPUT {
    public UInt16 wVk;
    public UInt16 wScan;
    public UInt32 dwFlags;
    public UInt32 time;
    public IntPtr dwExtraInfo;
}

[StructLayout(LayoutKind.Sequential)]
public struct HARDWAREINPUT {
    public UInt32 uMsg;
    public UInt16 wParamL;
    public UInt16 wParamH;
}
"@
}

$MOUSEEVENTF_LEFTDOWN = 0x0002
$MOUSEEVENTF_LEFTUP = 0x0004
$SW_RESTORE = 9
$script:TargetWindow = [IntPtr]::Zero

$KeyMap = @{
    "cross" = @{ Scan = 0x2D; Extended = $false }; "x" = @{ Scan = 0x2D; Extended = $false }; "confirm" = @{ Scan = 0x2D; Extended = $false }
    "circle" = @{ Scan = 0x2E; Extended = $false }; "o" = @{ Scan = 0x2E; Extended = $false }; "cancel" = @{ Scan = 0x2E; Extended = $false }
    "square" = @{ Scan = 0x2C; Extended = $false }; "triangle" = @{ Scan = 0x2F; Extended = $false }
    "start" = @{ Scan = 0x1C; Extended = $false }; "enter" = @{ Scan = 0x1C; Extended = $false }
    "select" = @{ Scan = 0x36; Extended = $false }; "rshift" = @{ Scan = 0x36; Extended = $false }
    "ps" = @{ Scan = 0x19; Extended = $false }; "home" = @{ Scan = 0x19; Extended = $false }
    "back" = @{ Scan = 0x01; Extended = $false }; "escape" = @{ Scan = 0x01; Extended = $false }; "esc" = @{ Scan = 0x01; Extended = $false }
    "up" = @{ Scan = 0x48; Extended = $true }; "dpad_up" = @{ Scan = 0x48; Extended = $true }
    "right" = @{ Scan = 0x4D; Extended = $true }; "dpad_right" = @{ Scan = 0x4D; Extended = $true }
    "down" = @{ Scan = 0x50; Extended = $true }; "dpad_down" = @{ Scan = 0x50; Extended = $true }
    "left" = @{ Scan = 0x4B; Extended = $true }; "dpad_left" = @{ Scan = 0x4B; Extended = $true }
    "l1" = @{ Scan = 0x10; Extended = $false }; "r1" = @{ Scan = 0x12; Extended = $false }
    "l2" = @{ Scan = 0x16; Extended = $false }; "r2" = @{ Scan = 0x18; Extended = $false }
    "l3" = @{ Scan = 0x21; Extended = $false }; "r3" = @{ Scan = 0x23; Extended = $false }
    "left_up" = @{ Scan = 0x11; Extended = $false }; "left_down" = @{ Scan = 0x1F; Extended = $false }; "left_left" = @{ Scan = 0x1E; Extended = $false }; "left_right" = @{ Scan = 0x20; Extended = $false }
    "right_up" = @{ Scan = 0x17; Extended = $false }; "right_down" = @{ Scan = 0x25; Extended = $false }; "right_left" = @{ Scan = 0x24; Extended = $false }; "right_right" = @{ Scan = 0x26; Extended = $false }
}

$ChordAliases = @{
    "osd" = @("l3", "r3")
    "fast_forward" = @("select", "r1")
    "save_state" = @("select", "right_down")
    "load_state" = @("select", "right_up")
}

function Normalize-Token([string]$Token) {
    return ($Token.Trim().ToLowerInvariant() -replace "-", "_")
}

function Focus-Vita3KWindow {
    if ($NoFocus) {
        return
    }

    $proc = Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object {
        $_.MainWindowHandle -ne 0
    } | Sort-Object StartTime -Descending | Select-Object -First 1

    if (-not $proc) {
        # Avoid focusing browser tabs or repo pages whose title happens to
        # contain "Vita3K"; the emulator runtime title starts with Vita3K v.
        $proc = Get-Process | Where-Object {
            $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "$WindowTitle v*"
        } | Sort-Object StartTime -Descending | Select-Object -First 1
    }

    if (-not $proc) {
        throw "No foreground-capable window found with title containing '$WindowTitle'."
    }

    [Vita3KInputWin32]::ShowWindow($proc.MainWindowHandle, $SW_RESTORE) | Out-Null
    [Vita3KInputWin32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    $script:TargetWindow = $proc.MainWindowHandle
    Start-Sleep -Milliseconds 180
}

function Resolve-Buttons([string]$Token) {
    $normalized = Normalize-Token $Token
    if ($ChordAliases.ContainsKey($normalized)) {
        return @($ChordAliases[$normalized])
    }
    if ($normalized.Contains("+")) {
        return @($normalized.Split("+") | ForEach-Object { Normalize-Token $_ } | Where-Object { $_ })
    }
    return @($normalized)
}

function Send-ButtonDown([string]$Button) {
    if (-not $KeyMap.ContainsKey($Button)) {
        throw "Unknown Windows Vita3K button '$Button'."
    }
    $info = $KeyMap[$Button]
    if ($DryRun) {
        Write-Host ("down {0} scan=0x{1:X2}" -f $Button, $info.Scan)
        return
    }
    $sent = [Vita3KInputWin32]::SendScan([UInt16]$info.Scan, [bool]$info.Extended, $false)
    if ($sent -ne 1) {
        throw ("SendInput key-down failed for {0} scan=0x{1:X2}" -f $Button, $info.Scan)
    }
}

function Send-ButtonUp([string]$Button) {
    if (-not $KeyMap.ContainsKey($Button)) {
        throw "Unknown Windows Vita3K button '$Button'."
    }
    $info = $KeyMap[$Button]
    if ($DryRun) {
        Write-Host ("up   {0} scan=0x{1:X2}" -f $Button, $info.Scan)
        return
    }
    $sent = [Vita3KInputWin32]::SendScan([UInt16]$info.Scan, [bool]$info.Extended, $true)
    if ($sent -ne 1) {
        throw ("SendInput key-up failed for {0} scan=0x{1:X2}" -f $Button, $info.Scan)
    }
}

function Invoke-Press([string]$Token, [int]$Repeat) {
    $buttons = Resolve-Buttons $Token
    foreach ($button in $buttons) {
        Send-ButtonDown $button
    }
    Start-Sleep -Milliseconds $PressMs
    $releaseButtons = @($buttons)
    [array]::Reverse($releaseButtons)
    foreach ($button in $releaseButtons) {
        Send-ButtonUp $button
    }
    if ($Repeat -gt 1) {
        Start-Sleep -Milliseconds $GapMs
    }
}

function Invoke-Click([string]$Token) {
    if ($script:TargetWindow -eq [IntPtr]::Zero) {
        Focus-Vita3KWindow
    }

    $rect = New-Object Vita3KInputRect
    [Vita3KInputWin32]::GetWindowRect($script:TargetWindow, [ref]$rect) | Out-Null
    $x = [int](($rect.Left + $rect.Right) / 2)
    $y = [int]($rect.Bottom - $ClickYFromBottom)

    if ($Token -match "^click@(?<x>-?\d+),(?<y>-?\d+)$") {
        $x = [int]$Matches["x"]
        $y = [int]$Matches["y"]
    } elseif ($Token -match "^click:(?<x>-?\d+),(?<y>-?\d+)$") {
        $x = $rect.Left + [int]$Matches["x"]
        $y = $rect.Top + [int]$Matches["y"]
    } elseif ($Token -match "^click[@:]") {
        throw "Invalid click token '$Token'. Use click:x,y, click@x,y, or quote coordinate tokens in PowerShell."
    }

    if ($DryRun) {
        Write-Host "click $x,$y"
        return
    }

    [Vita3KInputWin32]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 40
    [Vita3KInputWin32]::mouse_event($MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $PressMs
    [Vita3KInputWin32]::mouse_event($MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
}

function Normalize-Sequence([string[]]$Items) {
    $normalized = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Items.Count; $i++) {
        $item = $Items[$i]
        if ($item -match "^click[@:]-?\d+$" -and ($i + 1) -lt $Items.Count -and $Items[$i + 1] -match "^-?\d+$") {
            $normalized.Add("$item,$($Items[$i + 1])")
            $i++
            continue
        }
        $normalized.Add($item)
    }
    return $normalized.ToArray()
}

Focus-Vita3KWindow

foreach ($item in (Normalize-Sequence $Sequence)) {
    $token = $item
    $repeatCount = 1
    if ($item -notmatch "^click[@:]" -and $item -match "^(?<name>[^:]+):(?<value>\d+)$") {
        $token = $Matches["name"]
        $value = [int]$Matches["value"]
        if ((Normalize-Token $token) -in @("wait", "sleep")) {
            Write-Host "wait $value ms"
            Start-Sleep -Milliseconds $value
            continue
        }
        $repeatCount = [Math]::Max($value, 1)
    }

    Write-Host "press $token x$repeatCount"
    if ((Normalize-Token $token) -eq "click" -or $token -match "^click[@:]") {
        for ($i = 0; $i -lt $repeatCount; $i++) {
            Invoke-Click $token
            Start-Sleep -Milliseconds $GapMs
        }
    } else {
        for ($i = 0; $i -lt $repeatCount; $i++) {
            Invoke-Press $token 1
            Start-Sleep -Milliseconds $GapMs
        }
    }
}

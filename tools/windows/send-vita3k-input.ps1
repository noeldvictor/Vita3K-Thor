param(
    [Parameter(Mandatory = $true)]
    [string[]]$Sequence,
    [string]$WindowTitle = "Vita3K",
    [int]$PressMs = 90,
    [int]$GapMs = 90,
    [int]$ClickYFromBottom = 125,
    [switch]$NoFocus,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Vita3KInputWin32 {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, int dwFlags, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(int dwFlags, int dx, int dy, int dwData, UIntPtr dwExtraInfo);
}

[StructLayout(LayoutKind.Sequential)]
public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}
"@

$KEYEVENTF_KEYUP = 0x0002
$MOUSEEVENTF_LEFTDOWN = 0x0002
$MOUSEEVENTF_LEFTUP = 0x0004
$SW_RESTORE = 9
$script:TargetWindow = [IntPtr]::Zero

$KeyMap = @{
    "cross" = 0x58; "x" = 0x58; "confirm" = 0x58
    "circle" = 0x43; "o" = 0x43; "cancel" = 0x43
    "square" = 0x5A; "triangle" = 0x56
    "start" = 0x0D; "enter" = 0x0D
    "select" = 0xA1; "rshift" = 0xA1
    "ps" = 0x50; "home" = 0x50
    "back" = 0x1B; "escape" = 0x1B; "esc" = 0x1B
    "up" = 0x26; "dpad_up" = 0x26
    "right" = 0x27; "dpad_right" = 0x27
    "down" = 0x28; "dpad_down" = 0x28
    "left" = 0x25; "dpad_left" = 0x25
    "l1" = 0x51; "r1" = 0x45
    "l2" = 0x55; "r2" = 0x4F
    "l3" = 0x46; "r3" = 0x48
    "left_up" = 0x57; "left_down" = 0x53; "left_left" = 0x41; "left_right" = 0x44
    "right_up" = 0x49; "right_down" = 0x4B; "right_left" = 0x4A; "right_right" = 0x4C
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

    $proc = Get-Process | Where-Object {
        $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "*$WindowTitle*"
    } | Sort-Object StartTime -Descending | Select-Object -First 1

    if (-not $proc) {
        throw "No foreground-capable window found with title containing '$WindowTitle'."
    }

    [Vita3KInputWin32]::ShowWindow($proc.MainWindowHandle, $SW_RESTORE) | Out-Null
    [Vita3KInputWin32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    $script:TargetWindow = $proc.MainWindowHandle
    Start-Sleep -Milliseconds 120
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
    $vk = [byte]$KeyMap[$Button]
    if ($DryRun) {
        Write-Host "down $Button vk=$vk"
        return
    }
    [Vita3KInputWin32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
}

function Send-ButtonUp([string]$Button) {
    if (-not $KeyMap.ContainsKey($Button)) {
        throw "Unknown Windows Vita3K button '$Button'."
    }
    $vk = [byte]$KeyMap[$Button]
    if ($DryRun) {
        Write-Host "up   $Button vk=$vk"
        return
    }
    [Vita3KInputWin32]::keybd_event($vk, 0, $KEYEVENTF_KEYUP, [UIntPtr]::Zero)
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

    $rect = New-Object RECT
    [Vita3KInputWin32]::GetWindowRect($script:TargetWindow, [ref]$rect) | Out-Null
    $x = [int](($rect.Left + $rect.Right) / 2)
    $y = [int]($rect.Bottom - $ClickYFromBottom)

    if ($Token -match "^click@(?<x>-?\d+),(?<y>-?\d+)$") {
        $x = [int]$Matches["x"]
        $y = [int]$Matches["y"]
    } elseif ($Token -match "^click:(?<x>-?\d+),(?<y>-?\d+)$") {
        $x = $rect.Left + [int]$Matches["x"]
        $y = $rect.Top + [int]$Matches["y"]
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

Focus-Vita3KWindow

foreach ($item in $Sequence) {
    $token = $item
    $repeat = 1
    if ($item -match "^(?<name>[^:]+):(?<value>\d+)$") {
        $token = $Matches["name"]
        $value = [int]$Matches["value"]
        if ((Normalize-Token $token) -in @("wait", "sleep")) {
            Write-Host "wait $value ms"
            Start-Sleep -Milliseconds $value
            continue
        }
        $repeat = [Math]::Max($value, 1)
    }

    for ($i = 0; $i -lt $repeat; $i++) {
        Write-Host "press $token"
        if ((Normalize-Token $token) -eq "click" -or $token -match "^click[@:]") {
            Invoke-Click $token
        } else {
            Invoke-Press $token $repeat
        }
        Start-Sleep -Milliseconds $GapMs
    }
}

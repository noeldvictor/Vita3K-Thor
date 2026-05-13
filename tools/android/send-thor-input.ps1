param(
    [Parameter(Mandatory = $true)]
    [string[]]$Sequence,
    [string]$Adb = "adb",
    [string]$Serial = "",
    [ValidateSet("KeyEvent", "Sendevent")]
    [string]$Mode = "KeyEvent",
    [string]$InputDeviceName = "Odin Controller",
    [int]$PressMs = 90,
    [int]$GapMs = 90,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Invoke-Adb([string[]]$AdbArgs) {
    $fullArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($Serial)) {
        $fullArgs += @("-s", $Serial)
    }
    $fullArgs += $AdbArgs
    if ($DryRun) {
        Write-Host "adb $($fullArgs -join ' ')"
        return @()
    }
    return & $Adb @fullArgs
}

function Normalize-Token([string]$Token) {
    return ($Token.Trim().ToLowerInvariant() -replace "-", "_")
}

$AndroidKeyMap = @{
    "cross" = 96; "x" = 96
    "circle" = 97; "o" = 97
    "square" = 99
    "triangle" = 100
    "l1" = 102; "r1" = 103
    "l2" = 104; "r2" = 105
    "l3" = 106; "r3" = 107
    "start" = 108
    "select" = 109
    "up" = 19; "dpad_up" = 19
    "down" = 20; "dpad_down" = 20
    "left" = 21; "dpad_left" = 21
    "right" = 22; "dpad_right" = 22
    "back" = 4
    "app_switch" = 187; "appselect" = 187
}

$LinuxKeyMap = @{
    "cross" = 304; "x" = 304
    "circle" = 305; "o" = 305
    "triangle" = 307
    "square" = 308
    "l1" = 310; "r1" = 311
    "l2" = 312; "r2" = 313
    "select" = 314
    "start" = 315
    "l3" = 317; "r3" = 318
    "back" = 158
    "up" = 544; "dpad_up" = 544
    "down" = 545; "dpad_down" = 545
    "left" = 546; "dpad_left" = 546
    "right" = 547; "dpad_right" = 547
}

$ChordAliases = @{
    "osd" = @("l3", "r3")
    "fast_forward" = @("select", "r1")
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

function Resolve-InputEventPath {
    $text = Invoke-Adb @("shell", "getevent", "-lp")
    $current = ""
    foreach ($line in $text) {
        if ($line -match "add device .*: (?<event>/dev/input/event\d+)") {
            $current = $Matches["event"]
            continue
        }
        if ($current -and $line -match "name:\s+`"(?<name>.*)`"") {
            if ($Matches["name"] -like "*$InputDeviceName*") {
                return $current
            }
            $current = ""
        }
    }
    throw "Could not find Android input device named '$InputDeviceName'."
}

function Send-KeyEventPress([string[]]$Buttons) {
    foreach ($button in $Buttons) {
        if (-not $AndroidKeyMap.ContainsKey($button)) {
            throw "Unknown Android keyevent button '$button'."
        }
        Invoke-Adb @("shell", "input", "keyevent", "$($AndroidKeyMap[$button])") | Out-Null
        Start-Sleep -Milliseconds $GapMs
    }
}

function Send-SendeventChord([string]$EventPath, [string[]]$Buttons) {
    $codes = @()
    foreach ($button in $Buttons) {
        if (-not $LinuxKeyMap.ContainsKey($button)) {
            throw "Unknown Linux sendevent button '$button'."
        }
        $codes += [int]$LinuxKeyMap[$button]
    }

    foreach ($code in $codes) {
        Invoke-Adb @("shell", "sendevent", $EventPath, "1", "$code", "1") | Out-Null
    }
    Invoke-Adb @("shell", "sendevent", $EventPath, "0", "0", "0") | Out-Null
    Start-Sleep -Milliseconds $PressMs
    [array]::Reverse($codes)
    foreach ($code in $codes) {
        Invoke-Adb @("shell", "sendevent", $EventPath, "1", "$code", "0") | Out-Null
    }
    Invoke-Adb @("shell", "sendevent", $EventPath, "0", "0", "0") | Out-Null
}

$eventPath = ""
if ($Mode -eq "Sendevent") {
    $eventPath = if ($DryRun) { "/dev/input/eventX" } else { Resolve-InputEventPath }
    Write-Host "Using Android input device: $eventPath ($InputDeviceName)"
}

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
        $buttons = Resolve-Buttons $token
        Write-Host "press $token"
        if ($Mode -eq "KeyEvent") {
            Send-KeyEventPress $buttons
        } else {
            Send-SendeventChord $eventPath $buttons
        }
        Start-Sleep -Milliseconds $GapMs
    }
}

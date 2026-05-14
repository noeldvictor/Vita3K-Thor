param(
    [string]$Topic = "thor-rotation",
    [int]$DurationSec = 45,
    [int]$FrameFps = 6,
    [int]$BitRate = 12000000,
    [string]$Size = "1280x720",
    [string]$Adb = "adb",
    [string]$Serial = "",
    [string]$Ffmpeg = "ffmpeg",
    [string]$OutDir = "tmp/thor-video",
    [string]$InputDeviceName = "Odin Controller",
    [switch]$Rotate,
    [ValidateSet("ABS_X", "ABS_Y", "ABS_Z", "ABS_RZ")]
    [string]$Axis = "ABS_Z",
    [int]$AxisValue = 32767,
    [int]$WarmupMs = 300,
    [int]$LogcatLines = 2000,
    [switch]$NoAnalyze,
    [switch]$NoLogcat
)

$ErrorActionPreference = "Stop"

$AxisCodes = @{
    "ABS_X" = 0
    "ABS_Y" = 1
    "ABS_Z" = 2
    "ABS_RZ" = 5
}

function Require-Command($Name) {
    if (Test-Path -LiteralPath $Name) {
        return
    }
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "thor-rotation"
    }
    return $slug
}

function Get-AdbPrefix {
    if ([string]::IsNullOrWhiteSpace($Serial)) {
        return @()
    }
    return @("-s", $Serial)
}

function Invoke-AdbChecked([string[]]$AdbArgs) {
    $allArgs = @()
    $allArgs += Get-AdbPrefix
    $allArgs += $AdbArgs
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $Adb @allArgs 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "adb $($allArgs -join ' ') failed:`n$($output -join "`n")"
    }
    return $output
}

function Write-AdbText($Path, [string[]]$AdbArgs) {
    $output = Invoke-AdbChecked $AdbArgs
    $output | Set-Content -Encoding UTF8 -Path $Path
}

function Resolve-InputEventPath {
    $text = Invoke-AdbChecked @("shell", "getevent", "-lp")
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

function Set-AxisValue([string]$EventPath, [string]$AxisName, [int]$Value) {
    $code = $AxisCodes[$AxisName]
    Invoke-AdbChecked @("shell", "sendevent", $EventPath, "3", "$code", "$Value") | Out-Null
    Invoke-AdbChecked @("shell", "sendevent", $EventPath, "0", "0", "0") | Out-Null
}

Require-Command $Adb
Require-Command $Ffmpeg

$devices = & $Adb devices -l
$connected = @($devices | Select-String -Pattern "\bdevice\b" | Where-Object { $_.Line -notmatch "^List of devices" })
if ($connected.Count -eq 0) {
    throw "No adb device connected."
}
if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $serialPattern = [Regex]::Escape($Serial)
    $matched = @($connected | Where-Object { $_.Line -match "^$serialPattern\s+device\b" })
    if ($matched.Count -eq 0) {
        throw "ADB device '$Serial' is not connected."
    }
}

$DurationSec = [Math]::Max($DurationSec, 1)
$FrameFps = [Math]::Max($FrameFps, 1)
$BitRate = [Math]::Max($BitRate, 1000000)
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$topicSlug = Slug $Topic
$sessionDir = Join-Path $OutDir "$($stamp)_$topicSlug"
$frameDir = Join-Path $sessionDir "frames"
New-Item -ItemType Directory -Force -Path $frameDir | Out-Null

$remoteVideo = "/sdcard/vita3k-thor-$stamp-$topicSlug.mp4"
$localVideo = Join-Path $sessionDir "screenrecord.mp4"
$windowPath = Join-Path $sessionDir "window.txt"
$focusPath = Join-Path $sessionDir "focus.txt"
$propPath = Join-Path $sessionDir "debug-vita3k-props.txt"
$logPath = Join-Path $sessionDir "logcat-tail.txt"
$eventPath = ""

Write-AdbText $windowPath @("shell", "dumpsys", "window")
$focus = @(Select-String -Path $windowPath -Pattern "mCurrentFocus=|mFocusedApp=" | Select-Object -First 8)
if ($focus.Count -eq 0) {
    "focus unavailable" | Set-Content -Encoding UTF8 -Path $focusPath
} else {
    $focus | ForEach-Object { $_.Line.Trim() } | Set-Content -Encoding UTF8 -Path $focusPath
}
Write-AdbText $propPath @("shell", "getprop")

Invoke-AdbChecked @("shell", "rm", "-f", $remoteVideo) | Out-Null

try {
    if ($Rotate) {
        $eventPath = Resolve-InputEventPath
        Write-Host "Using Android input device: $eventPath ($InputDeviceName)"
        Write-Host "Holding $Axis at $AxisValue for $DurationSec seconds"
        Set-AxisValue $eventPath $Axis $AxisValue
        if ($WarmupMs -gt 0) {
            Start-Sleep -Milliseconds $WarmupMs
        }
    }

    Write-Host "Recording Thor screen for $DurationSec seconds: $remoteVideo"
    Invoke-AdbChecked @(
        "shell",
        "screenrecord",
        "--time-limit",
        "$DurationSec",
        "--bit-rate",
        "$BitRate",
        "--size",
        $Size,
        $remoteVideo
    ) | Out-Null
} finally {
    if ($Rotate -and -not [string]::IsNullOrWhiteSpace($eventPath)) {
        Write-Host "Releasing $Axis"
        Set-AxisValue $eventPath $Axis 0
    }
}

Invoke-AdbChecked @("pull", $remoteVideo, $localVideo) | Out-Null
Invoke-AdbChecked @("shell", "rm", "-f", $remoteVideo) | Out-Null

if (-not $NoLogcat) {
    Write-AdbText $logPath @("logcat", "-d", "-v", "threadtime", "-t", "$LogcatLines")
}

$framePattern = Join-Path $frameDir "frame_%04d.png"
Write-Host "Extracting $FrameFps fps to $frameDir"
& $Ffmpeg -hide_banner -loglevel error -y -i $localVideo -vf "fps=$FrameFps" $framePattern
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg frame extraction failed."
}

$frameCount = @(
    Get-ChildItem -LiteralPath $frameDir -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @(".png", ".jpg", ".jpeg") }
).Count
if ($frameCount -lt 2) {
    throw "Expected extracted frames in $frameDir, found $frameCount."
}

$metadata = @()
$metadata += "Topic: $Topic"
$metadata += "Started: $stamp"
$metadata += "ADB: $Adb"
$metadata += "Serial: $(if ([string]::IsNullOrWhiteSpace($Serial)) { 'default' } else { $Serial })"
$metadata += "Duration seconds: $DurationSec"
$metadata += "Frame fps: $FrameFps"
$metadata += "Bitrate: $BitRate"
$metadata += "Size: $Size"
$metadata += "Rotate: $Rotate"
$metadata += "Input device name: $InputDeviceName"
$metadata += "Input event path: $(if ([string]::IsNullOrWhiteSpace($eventPath)) { 'none' } else { $eventPath })"
$metadata += "Axis: $Axis"
$metadata += "Axis value: $AxisValue"
$metadata += "Video: $localVideo"
$metadata += "Frames: $frameDir"
$metadata += "Window dump: $windowPath"
$metadata += "Focus: $focusPath"
$metadata += "Debug props: $propPath"
if (-not $NoLogcat) {
    $metadata += "Logcat tail: $logPath"
}
$metadata | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDir "metadata.txt")

if (-not $NoAnalyze) {
    Write-Host "Analyzing extracted frames"
    python tools\analyze_screenshot_burst.py $sessionDir --contact-sheet-width 320 --top 40
}

Write-Host "Wrote Thor rotation burst: $sessionDir"

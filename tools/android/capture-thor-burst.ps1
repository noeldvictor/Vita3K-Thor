param(
    [string]$Topic = "thor-burst",
    [int]$Count = 10,
    [int]$IntervalMs = 350,
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$Adb = "adb",
    [string]$Serial = "",
    [string]$OutDir = "tmp/thor-burst",
    [int]$LogcatLines = 1200,
    [string]$DisplayId = "",
    [switch]$NoLogcat
)

$ErrorActionPreference = "Stop"

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
        return "thor-burst"
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
        $output = & $Adb @allArgs
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

Require-Command $Adb

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

$Count = [Math]::Max($Count, 1)
$IntervalMs = [Math]::Max($IntervalMs, 0)
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$topicSlug = Slug $Topic
$sessionDir = Join-Path $OutDir "$($stamp)_$topicSlug"
$frameDir = Join-Path $sessionDir "frames"
New-Item -ItemType Directory -Force -Path $frameDir | Out-Null

$windowPath = Join-Path $sessionDir "window.txt"
$focusPath = Join-Path $sessionDir "focus.txt"
$logPath = Join-Path $sessionDir "logcat-tail.txt"
Write-AdbText $windowPath @("shell", "dumpsys", "window")

$focus = @(Select-String -Path $windowPath -Pattern "mCurrentFocus=|mFocusedApp=" | Select-Object -First 8)
if ($focus.Count -eq 0) {
    "focus unavailable" | Set-Content -Encoding UTF8 -Path $focusPath
} else {
    $focus | ForEach-Object { $_.Line.Trim() } | Set-Content -Encoding UTF8 -Path $focusPath
}

$frames = @()
for ($i = 1; $i -le $Count; $i++) {
    $remoteScreenPath = "/sdcard/vita3k-thor-burst-$stamp-$('{0:D4}' -f $i).png"
    $screenPath = Join-Path $frameDir ("frame_{0:D4}.png" -f $i)
    $screenArgs = @("shell", "screencap")
    if (-not [string]::IsNullOrWhiteSpace($DisplayId)) {
        $screenArgs += @("-d", $DisplayId)
    }
    $screenArgs += @("-p", $remoteScreenPath)

    Invoke-AdbChecked $screenArgs | Out-Null
    Invoke-AdbChecked @("pull", $remoteScreenPath, $screenPath) | Out-Null
    Invoke-AdbChecked @("shell", "rm", $remoteScreenPath) | Out-Null
    $frames += $screenPath
    Write-Host ("Captured Thor frame {0}/{1}: {2}" -f $i, $Count, $screenPath)
    if ($i -lt $Count -and $IntervalMs -gt 0) {
        Start-Sleep -Milliseconds $IntervalMs
    }
}

$latestPath = Join-Path $sessionDir "latest-screen.png"
Copy-Item -Force -LiteralPath $frames[-1] -Destination $latestPath

if (-not $NoLogcat) {
    Write-AdbText $logPath @("logcat", "-d", "-v", "threadtime", "-t", "$LogcatLines")
}

$metadata = @()
$metadata += "Topic: $Topic"
$metadata += "Started: $stamp"
$metadata += "Package: $Package"
$metadata += "ADB: $Adb"
$metadata += "Serial: $(if ([string]::IsNullOrWhiteSpace($Serial)) { 'default' } else { $Serial })"
$metadata += "Display id: $(if ([string]::IsNullOrWhiteSpace($DisplayId)) { 'default' } else { $DisplayId })"
$metadata += "Count: $Count"
$metadata += "Interval ms: $IntervalMs"
$metadata += "Window dump: $windowPath"
$metadata += "Focus: $focusPath"
if (-not $NoLogcat) {
    $metadata += "Logcat tail: $logPath"
}
$metadata += "Latest: $latestPath"
$metadata += "Frames:"
$metadata += $frames
$metadata | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDir "metadata.txt")

Write-Host "Wrote Thor burst: $sessionDir"

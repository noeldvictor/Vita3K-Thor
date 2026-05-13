param(
    [string]$Topic = "thor-live-debug",
    [int]$IntervalSeconds = 5,
    [int]$DurationSeconds = 0,
    [int]$LogcatLines = 600,
    [string]$GamePath = "",
    [switch]$RenderTrace,
    [int]$LogLevel = 0,
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$Activity = "org.vita3k.emulator.Emulator",
    [string]$Adb = "adb",
    [string]$OutDir = "tmp/thor-live",
    [string]$ReportDir = "tmp/thor-live-reports",
    [switch]$NoClearLogcat,
    [switch]$NoScreenshots
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Quote-AndroidShell($Value) {
    return "'" + ($Value -replace "'", "'\''") + "'"
}

function Slug($Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "thor-live-debug"
    }
    return $slug
}

function Get-FocusSummary($WindowPath) {
    $focus = Select-String -Path $WindowPath -Pattern "mCurrentFocus=|mFocusedApp=" | Select-Object -First 4
    if ($focus.Count -eq 0) {
        return "focus unavailable"
    }
    return ($focus | ForEach-Object { $_.Line.Trim() }) -join " | "
}

Require-Command $Adb

$devices = & $Adb devices -l
$connected = @($devices | Select-String -Pattern "\bdevice\b" | Where-Object { $_.Line -notmatch "^List of devices" })
if ($connected.Count -eq 0) {
    throw "No adb device connected."
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$topicSlug = Slug $Topic
$sessionDir = Join-Path $OutDir "$($stamp)_$topicSlug"
New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

if (-not $NoClearLogcat) {
    & $Adb logcat -c | Out-Null
}

if ($RenderTrace) {
    & $Adb shell setprop debug.vita3k.thor_render_trace 1 | Out-Null
    Write-Host "Requested running renderer trace via debug.vita3k.thor_render_trace=1"
}

if (-not [string]::IsNullOrWhiteSpace($GamePath)) {
    $vitaArgs = @("-a", "true", "--cartridge", $GamePath, "--log-level", "$LogLevel")
    if ($RenderTrace) {
        $vitaArgs += "--thor-render-trace"
    }
    $argJson = ConvertTo-Json -Compress -InputObject $vitaArgs
    $argJsonBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($argJson))
    $component = "$Package/$Activity"
    $remoteStart = "am start -n $component --es AppStartParametersJsonBase64 $argJsonBase64"
    Write-Host "Launching $GamePath"
    Write-Host "ADB args JSON: $argJson"
    & $Adb shell $remoteStart | Tee-Object -FilePath (Join-Path $sessionDir "launch.txt")
}

$started = Get-Date
$sample = 0
$maxSamples = 0
if ($DurationSeconds -gt 0) {
    $maxSamples = [Math]::Max([Math]::Ceiling($DurationSeconds / [double]$IntervalSeconds), 1)
}

Write-Host "Writing live Thor debug samples to $sessionDir"
Write-Host "Press Ctrl+C to stop when DurationSeconds is 0."

while ($true) {
    $sample++
    $sampleStamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $prefix = Join-Path $sessionDir ("sample_{0:D4}_$sampleStamp" -f $sample)
    $logPath = "$prefix-logcat.txt"
    $crashPath = "$prefix-crashbuffer.txt"
    $windowPath = "$prefix-window.txt"
    $gfxPath = "$prefix-gfxinfo.txt"
    $memPath = "$prefix-meminfo.txt"
    $screenPath = "$prefix-screen.png"

    & $Adb logcat -d -v threadtime -t $LogcatLines | Set-Content -Encoding UTF8 -Path $logPath
    & $Adb logcat -b crash -d -v threadtime -t 200 | Set-Content -Encoding UTF8 -Path $crashPath
    & $Adb shell dumpsys window | Set-Content -Encoding UTF8 -Path $windowPath
    & $Adb shell dumpsys gfxinfo $Package | Set-Content -Encoding UTF8 -Path $gfxPath
    & $Adb shell dumpsys meminfo $Package | Set-Content -Encoding UTF8 -Path $memPath

    if (-not $NoScreenshots) {
        $remoteScreenPath = "/sdcard/vita3k-thor-live-$sampleStamp.png"
        & $Adb shell screencap -p $remoteScreenPath | Out-Null
        & $Adb pull $remoteScreenPath $screenPath | Out-Null
        & $Adb shell rm $remoteScreenPath | Out-Null
        Copy-Item -Force -LiteralPath $screenPath -Destination (Join-Path $sessionDir "latest-screen.png")
    }

    $expectedPaths = @($logPath, $crashPath, $windowPath, $gfxPath, $memPath)
    if (-not $NoScreenshots) {
        $expectedPaths += $screenPath
    }
    foreach ($path in $expectedPaths) {
        if (-not (Test-Path -LiteralPath $path)) {
            New-Item -ItemType File -Path $path | Out-Null
        }
    }

    $focusSummary = Get-FocusSummary $windowPath
    $keyLines = @(Select-String -Path $logPath -Pattern @(
        "ThorRenderTrace",
        "texture configure",
        "texture upload",
        "Vita3K",
        "ERROR",
        "CRITICAL",
        "FATAL EXCEPTION",
        "AndroidRuntime",
        "SIGSEGV",
        "SIGABRT",
        "lowmemorykiller",
        "Killed"
    ) | Select-Object -Last 80)

    $latest = @()
    $latest += "Thor live debug: $Topic"
    $latest += "Started: $($started.ToString('yyyy-MM-dd HH:mm:ss'))"
    $latest += "Sample: $sample"
    $latest += "Sample time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $latest += "Focus: $focusSummary"
    $latest += "Logcat: $logPath"
    $latest += "Crash buffer: $crashPath"
    $latest += "Window: $windowPath"
    $latest += "Gfxinfo: $gfxPath"
    $latest += "Meminfo: $memPath"
    if (-not $NoScreenshots) {
        $latest += "Screenshot: $screenPath"
    }
    $latest += ""
    $latest += "Key lines:"
    if ($keyLines.Count -eq 0) {
        $latest += "No matching key lines found."
    } else {
        foreach ($line in $keyLines) {
            $latest += $line.Line
        }
    }
    $latest | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDir "latest.txt")

    Write-Host ("[{0}] sample {1}: {2}" -f (Get-Date -Format "HH:mm:ss"), $sample, $focusSummary)

    if ($maxSamples -gt 0 -and $sample -ge $maxSamples) {
        break
    }

    Start-Sleep -Seconds $IntervalSeconds
}

$reportPath = Join-Path $ReportDir "$($stamp)_$topicSlug.md"
$report = @()
$report += "# $Topic - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += ""
$report += "## Session"
$report += ""
$report += "- Package: ``$Package``"
$report += "- Output directory: ``$sessionDir``"
$report += "- Samples: ``$sample``"
$report += "- Interval seconds: ``$IntervalSeconds``"
$report += "- Duration seconds: ``$DurationSeconds``"
$report += "- Logcat lines per sample: ``$LogcatLines``"
$report += "- Game path: ``$GamePath``"
$report += "- Render trace: ``$RenderTrace``"
$report += "- ADB trace property: ``debug.vita3k.thor_render_trace=$(if ($RenderTrace) { '1' } else { 'unchanged' })``"
$report += ""
$report += "## How To Use"
$report += ""
$report += "Keep this script running while reproducing a graphics, crash, input, quickstate, or frontend issue on the Thor. The newest screenshot and key log summary are always in ``$sessionDir/latest-screen.png`` and ``$sessionDir/latest.txt``."
$report += ""
$report += "## Notes"
$report += ""
$report += "- Generated by ``tools/thor_live_debug_stream.ps1``."
$report += "- Raw logs/screenshots stay under ``tmp/thor-live`` by default; durable conclusions belong in ``reports/debug_knowledge.sqlite``."
$report | Set-Content -Encoding UTF8 -Path $reportPath

Write-Host "Wrote transient report: $reportPath"
Write-Host "Latest sample: $(Join-Path $sessionDir 'latest.txt')"

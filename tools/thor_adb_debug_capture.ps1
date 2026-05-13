param(
    [Parameter(Mandatory = $true)]
    [string]$GamePath,

    [string]$Topic = "thor-adb-debug",
    [int]$Seconds = 20,
    [switch]$RenderTrace,
    [int]$LogLevel = 0,
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$Activity = "org.vita3k.emulator.Emulator",
    [string]$Adb = "adb",
    [string]$OutDir = "tmp/thor-adb-debug",
    [int]$ScreenshotBurstCount = 10,
    [int]$ScreenshotIntervalMs = 350,
    [string]$DisplayId = "",
    [switch]$NoClearLogcat,
    [switch]$NoForceStop
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
        return "thor-adb-debug"
    }
    return $slug
}

function Invoke-AdbNoStop([string[]]$AdbArgs) {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $Adb @AdbArgs 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "adb $($AdbArgs -join ' ') failed:`n$($output -join "`n")"
    }
    return $output
}

Require-Command $Adb

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$topicSlug = Slug $Topic
$prefix = Join-Path $OutDir "$($stamp)_$topicSlug"
$logPath = "$prefix-logcat.txt"
$crashPath = "$prefix-crashbuffer.txt"
$windowPath = "$prefix-window.txt"
$memPath = "$prefix-meminfo.txt"
$screenPath = "$prefix-screen.png"
$screenBurstDir = "$prefix-screen-burst"
$reportPath = "$prefix.md"

$devices = & $Adb devices -l
$connected = @($devices | Select-String -Pattern "\bdevice\b" | Where-Object { $_.Line -notmatch "^List of devices" })
if ($connected.Count -eq 0) {
    throw "No adb device connected."
}

if (-not $NoForceStop) {
    & $Adb shell am force-stop $Package | Out-Null
}
if (-not $NoClearLogcat) {
    & $Adb logcat -c | Out-Null
}

if ($RenderTrace) {
    & $Adb shell setprop debug.vita3k.thor_render_trace 1 | Out-Null
}

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
& $Adb shell $remoteStart

Start-Sleep -Seconds $Seconds

& $Adb logcat -d -v threadtime | Set-Content -Encoding UTF8 -Path $logPath
& $Adb logcat -b crash -d -v threadtime | Set-Content -Encoding UTF8 -Path $crashPath
& $Adb shell dumpsys window | Set-Content -Encoding UTF8 -Path $windowPath
& $Adb shell dumpsys meminfo $Package | Set-Content -Encoding UTF8 -Path $memPath
$ScreenshotBurstCount = [Math]::Max($ScreenshotBurstCount, 1)
$ScreenshotIntervalMs = [Math]::Max($ScreenshotIntervalMs, 0)
New-Item -ItemType Directory -Force -Path $screenBurstDir | Out-Null
$screenFrames = @()
for ($i = 1; $i -le $ScreenshotBurstCount; $i++) {
    $remoteScreenPath = "/sdcard/vita3k-thor-debug-$stamp-$('{0:D4}' -f $i).png"
    $framePath = Join-Path $screenBurstDir ("frame_{0:D4}.png" -f $i)
    $screenArgs = @("shell", "screencap")
    if (-not [string]::IsNullOrWhiteSpace($DisplayId)) {
        $screenArgs += @("-d", $DisplayId)
    }
    $screenArgs += @("-p", $remoteScreenPath)
    Invoke-AdbNoStop $screenArgs | Out-Null
    Invoke-AdbNoStop @("pull", $remoteScreenPath, $framePath) | Out-Null
    Invoke-AdbNoStop @("shell", "rm", $remoteScreenPath) | Out-Null
    $screenFrames += $framePath
    if ($i -lt $ScreenshotBurstCount -and $ScreenshotIntervalMs -gt 0) {
        Start-Sleep -Milliseconds $ScreenshotIntervalMs
    }
}
Copy-Item -Force -LiteralPath $screenFrames[-1] -Destination $screenPath
foreach ($path in @($logPath, $crashPath, $windowPath, $memPath, $screenPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType File -Path $path | Out-Null
    }
}

$patterns = @(
    "Native arguments",
    "input-content-path",
    "Thor renderer GXM trace",
    "ThorRenderTrace",
    "mounted directly",
    "Loaded VitaCheat file",
    "Failed",
    "ERROR",
    "CRITICAL",
    "FATAL EXCEPTION",
    "AndroidRuntime",
    "crash_dump",
    "backtrace",
    "signal ",
    "SIGSEGV",
    "SIGABRT",
    "lowmemorykiller",
    "low on memory",
    "Killed",
    "sceIoOpen"
)

$keyLines = @(Select-String -Path $logPath -Pattern $patterns | Select-Object -Last 160)

$report = @()
$report += "# $Topic - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += ""
$report += "## Launch"
$report += ""
$report += "- Package: ``$Package``"
$report += "- Game path: ``$GamePath``"
$report += "- Seconds captured: ``$Seconds``"
$report += "- Log level: ``$LogLevel``"
$report += "- Render trace: ``$RenderTrace``"
$report += "- ADB trace property: ``debug.vita3k.thor_render_trace=$(if ($RenderTrace) { '1' } else { 'unchanged' })``"
$report += "- Screenshot burst count: ``$ScreenshotBurstCount``"
$report += "- Screenshot burst interval ms: ``$ScreenshotIntervalMs``"
$report += ""
$report += "## Artifacts"
$report += ""
$report += "- Logcat: ``$logPath``"
$report += "- Crash buffer: ``$crashPath``"
$report += "- Window dump: ``$windowPath``"
$report += "- Meminfo: ``$memPath``"
$report += "- Screenshot compatibility copy: ``$screenPath``"
$report += "- Screenshot burst: ``$screenBurstDir``"
$report += ""
$report += "## Key Lines"
$report += ""
if ($keyLines.Count -eq 0) {
    $report += "No matching key lines found."
} else {
    $report += '```text'
    foreach ($line in $keyLines) {
        $report += $line.Line
    }
    $report += '```'
}
$report += ""
$report += "## Notes"
$report += ""
$report += "- Generated by ``tools/thor_adb_debug_capture.ps1``."
$report += "- Keep bulky raw logs/screenshots out of git unless they are specifically requested."
$report += "- Durable conclusions belong in ``reports/debug_knowledge.sqlite``."

$report | Set-Content -Encoding UTF8 -Path $reportPath

Write-Host "Wrote transient report: $reportPath"
Write-Host "Wrote logcat: $logPath"

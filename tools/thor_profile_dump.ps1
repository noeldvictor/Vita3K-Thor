param(
    [string]$Topic = "thor-profile",
    [int]$LogcatLines = 12000,
    [int]$Seconds = 3,
    [string]$TitleId = "",
    [string]$GamePath = "",
    [switch]$RenderTrace,
    [switch]$KeepRenderTrace,
    [int]$LogLevel = 0,
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$Activity = "org.vita3k.emulator.Emulator",
    [string]$Adb = "adb",
    [string]$OutDir = "tmp/thor-profile",
    [string]$ReportDir = "tmp/thor-profile-reports",
    [string]$KnowledgeDb = "reports/debug_knowledge.sqlite",
    [string]$CaseSlug = "",
    [ValidateSet("game", "emulator")]
    [string]$Domain = "game",
    [switch]$NoKnowledgeEntry,
    [switch]$NoClearLogcat,
    [switch]$NoForceStop,
    [switch]$NoScreenshot,
    [int]$ScreenshotBurstCount = 10,
    [int]$ScreenshotIntervalMs = 350,
    [string]$DisplayId = ""
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Slug($Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "thor-profile"
    }
    return $slug
}

function Quote-AndroidShell($Value) {
    return "'" + ($Value -replace "'", "'\''") + "'"
}

function Write-AdbOutput($Path, [string[]]$AdbArgs) {
    $output = & $Adb @AdbArgs 2>&1
    $output | Set-Content -Encoding UTF8 -Path $Path
}

function Get-AdbText([string[]]$AdbArgs) {
    $output = & $Adb @AdbArgs 2>&1
    return (($output | Out-String).Trim())
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

function Get-FocusSummary($WindowPath, $Package) {
    $focus = @(Select-String -Path $WindowPath -Pattern "mCurrentFocus=|mFocusedApp=" | Select-Object -First 4)
    if ($focus.Count -eq 0) {
        return "focus unavailable"
    }
    $packageFocus = @($focus | Where-Object { $_.Line -match [Regex]::Escape($Package) })
    if ($packageFocus.Count -gt 0) {
        return ($packageFocus | ForEach-Object { $_.Line.Trim() }) -join " | "
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
    Write-Host "Enabled running renderer trace with debug.vita3k.thor_render_trace=1"
}

if (-not [string]::IsNullOrWhiteSpace($GamePath)) {
    if (-not $NoForceStop) {
        & $Adb shell am force-stop $Package | Out-Null
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
    (& $Adb shell $remoteStart 2>&1) | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDir "launch.txt")
}

if ($Seconds -gt 0) {
    Start-Sleep -Seconds $Seconds
}

$appPid = Get-AdbText @("shell", "pidof", $Package)
$appPid = ($appPid -split "\s+" | Select-Object -First 1)

$logPath = Join-Path $sessionDir "logcat.txt"
$crashPath = Join-Path $sessionDir "crashbuffer.txt"
$windowPath = Join-Path $sessionDir "window.txt"
$gfxPath = Join-Path $sessionDir "gfxinfo.txt"
$frameStatsPath = Join-Path $sessionDir "gfxinfo-framestats.txt"
$memPath = Join-Path $sessionDir "meminfo.txt"
$cpuPath = Join-Path $sessionDir "cpuinfo.txt"
$thermalPath = Join-Path $sessionDir "thermalservice.txt"
$surfacePath = Join-Path $sessionDir "surfaceflinger.txt"
$topPath = Join-Path $sessionDir "top-threads.txt"
$propsPath = Join-Path $sessionDir "device-props.txt"
$saveListPath = Join-Path $sessionDir "savedata-list.txt"
$screenPath = Join-Path $sessionDir "screen.png"
$screenBurstDir = Join-Path $sessionDir "screen-burst"

Write-AdbOutput $logPath @("logcat", "-d", "-v", "threadtime", "-t", "$LogcatLines")
Write-AdbOutput $crashPath @("logcat", "-b", "crash", "-d", "-v", "threadtime", "-t", "400")
Write-AdbOutput $windowPath @("shell", "dumpsys", "window")
Write-AdbOutput $gfxPath @("shell", "dumpsys", "gfxinfo", $Package)
Write-AdbOutput $frameStatsPath @("shell", "dumpsys", "gfxinfo", $Package, "framestats")
Write-AdbOutput $memPath @("shell", "dumpsys", "meminfo", $Package)
Write-AdbOutput $cpuPath @("shell", "dumpsys", "cpuinfo")
Write-AdbOutput $thermalPath @("shell", "dumpsys", "thermalservice")
Write-AdbOutput $surfacePath @("shell", "dumpsys", "SurfaceFlinger")
Write-AdbOutput $propsPath @("shell", "getprop")

if (-not [string]::IsNullOrWhiteSpace($appPid)) {
    Write-AdbOutput $topPath @("shell", "top", "-b", "-n", "1", "-H", "-p", $appPid)
} else {
    "Package PID not found for $Package" | Set-Content -Encoding UTF8 -Path $topPath
}

if (-not [string]::IsNullOrWhiteSpace($TitleId)) {
    $remoteSave = "/sdcard/Android/data/$Package/files/vita/ux0/user/00/savedata/$TitleId"
    Write-AdbOutput $saveListPath @("shell", "ls", "-la", $remoteSave)
} else {
    "No TitleId provided." | Set-Content -Encoding UTF8 -Path $saveListPath
}

if (-not $NoScreenshot) {
    $ScreenshotBurstCount = [Math]::Max($ScreenshotBurstCount, 1)
    $ScreenshotIntervalMs = [Math]::Max($ScreenshotIntervalMs, 0)
    New-Item -ItemType Directory -Force -Path $screenBurstDir | Out-Null
    $screenFrames = @()
    for ($i = 1; $i -le $ScreenshotBurstCount; $i++) {
        $remoteScreenPath = "/sdcard/vita3k-thor-profile-$stamp-$('{0:D4}' -f $i).png"
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
}

if ($RenderTrace -and -not $KeepRenderTrace) {
    & $Adb shell setprop debug.vita3k.thor_render_trace 0 | Out-Null
}

$focusSummary = Get-FocusSummary $windowPath $Package
$renderLines = @(Select-String -Path $logPath -Pattern "ThorRenderTrace")
$sceneLines = @($renderLines | Where-Object { $_.Line -match "ThorRenderTrace scene" })
$textureLines = @($renderLines | Where-Object { $_.Line -match "texture configure|texture upload" })
$noColorLines = @($sceneLines | Where-Object { $_.Line -match "color_addr=0x00000000" })
$weirdMacroblockLines = @($sceneLines | Where-Object { $_.Line -match "macroblock=([1-9][0-9]{3,}x|[0-9]+x[1-9][0-9]{3,})" })
$largeDrawLines = @(Select-String -Path $logPath -Pattern "ThorRenderTrace draw" | Where-Object { $_.Line -match "count=([2-9][0-9]{3,}|[1-9][0-9]{4,})" })
$errorLines = @(Select-String -Path $logPath -Pattern @("ERROR", "CRITICAL", "FATAL EXCEPTION", "AndroidRuntime", "SIGSEGV", "SIGABRT", "Killed", "lowmemorykiller") | Select-Object -Last 80)

$reportPath = Join-Path $ReportDir "$($stamp)_$topicSlug.md"
$report = @()
$report += "# $Topic - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += ""
$report += "## Session"
$report += ""
$report += "- Package: ``$Package``"
$report += "- PID: ``$(if ([string]::IsNullOrWhiteSpace($appPid)) { 'not found' } else { $appPid })``"
$report += "- Title ID: ``$TitleId``"
$report += "- Game path: ``$GamePath``"
$report += "- Output directory: ``$sessionDir``"
$report += "- Logcat lines: ``$LogcatLines``"
$report += "- Warmup seconds: ``$Seconds``"
$report += "- Render trace requested: ``$RenderTrace``"
$report += "- Render trace left enabled: ``$(if ($RenderTrace -and $KeepRenderTrace) { 'true' } else { 'false' })``"
$report += "- Screenshot burst count: ``$(if ($NoScreenshot) { 0 } else { $ScreenshotBurstCount })``"
$report += "- Screenshot burst interval ms: ``$ScreenshotIntervalMs``"
$report += "- Focus: ``$focusSummary``"
$report += ""
$report += "## Profile Summary"
$report += ""
$report += "- Render trace lines: ``$($renderLines.Count)``"
$report += "- Scene lines: ``$($sceneLines.Count)``"
$report += "- Texture lines: ``$($textureLines.Count)``"
$report += "- No-color scene lines: ``$($noColorLines.Count)``"
$report += "- Suspicious macroblock scene lines: ``$($weirdMacroblockLines.Count)``"
$report += "- Large draw lines: ``$($largeDrawLines.Count)``"
$report += "- Error/crash key lines: ``$($errorLines.Count)``"
$report += ""
$report += "## Artifacts"
$report += ""
$report += "- Logcat: ``$logPath``"
$report += "- Crash buffer: ``$crashPath``"
$report += "- Window dump: ``$windowPath``"
$report += "- Gfxinfo: ``$gfxPath``"
$report += "- Frame stats: ``$frameStatsPath``"
$report += "- Meminfo: ``$memPath``"
$report += "- CPU info: ``$cpuPath``"
$report += "- Thermal: ``$thermalPath``"
$report += "- SurfaceFlinger: ``$surfacePath``"
$report += "- Top threads: ``$topPath``"
$report += "- Device props: ``$propsPath``"
$report += "- Save list: ``$saveListPath``"
if (-not $NoScreenshot) {
    $report += "- Screenshot compatibility copy: ``$screenPath``"
    $report += "- Screenshot burst: ``$screenBurstDir``"
}
$report += ""
$report += "## Suspicious Render Lines"
$report += ""
$interesting = @(($weirdMacroblockLines + $noColorLines + $largeDrawLines) | Select-Object -First 60)
if ($interesting.Count -eq 0) {
    $report += "No suspicious render-trace lines matched the built-in filters."
} else {
    $report += '```text'
    foreach ($line in $interesting) {
        $report += $line.Line
    }
    $report += '```'
}
$report += ""
$report += "## Error Lines"
$report += ""
if ($errorLines.Count -eq 0) {
    $report += "No error/crash lines matched the built-in filters."
} else {
    $report += '```text'
    foreach ($line in $errorLines) {
        $report += $line.Line
    }
    $report += '```'
}
$report += ""
$report += "## Notes"
$report += ""
$report += "- Generated by ``tools/thor_profile_dump.ps1``."
$report += "- Keep bulky raw artifacts under ``tmp/thor-profile`` unless a specific artifact is explicitly requested."
$report += "- Durable conclusions should be stored in ``$KnowledgeDb``."

$report | Set-Content -Encoding UTF8 -Path $reportPath

if (-not $NoKnowledgeEntry) {
    try {
        $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
        $debugKnowledge = Join-Path $repoRoot "tools\debug_knowledge.py"
        $caseRef = if ([string]::IsNullOrWhiteSpace($CaseSlug)) { $topicSlug } else { $CaseSlug }
        $knowledgeDbPath = Join-Path $repoRoot $KnowledgeDb
        $entryBody = @(
            "ADB profile capture for $Topic."
            "Package: $Package"
            "PID: $(if ([string]::IsNullOrWhiteSpace($appPid)) { 'not found' } else { $appPid })"
            "Title ID: $TitleId"
            "Game path: $GamePath"
            "Focus: $focusSummary"
            "Render trace lines: $($renderLines.Count)"
            "Scene lines: $($sceneLines.Count)"
            "Texture lines: $($textureLines.Count)"
            "No-color scene lines: $($noColorLines.Count)"
            "Suspicious macroblock scene lines: $($weirdMacroblockLines.Count)"
            "Large draw lines: $($largeDrawLines.Count)"
            "Error/crash key lines: $($errorLines.Count)"
        ) -join "`n"

        $caseArgs = @(
            $debugKnowledge, "--db", $knowledgeDbPath, "case", "upsert",
            "--domain", $Domain,
            "--slug", $caseRef,
            "--status", "active",
            "--severity", "normal",
            "--platform-scope", "android"
        )
        if (-not [string]::IsNullOrWhiteSpace($TitleId)) {
            $caseArgs += @("--title-id", $TitleId)
        }
        $caseArgs += @("--summary", $Topic, "--hypothesis", "ADB profile evidence captured; inspect artifacts and update hypothesis.")
        & python @caseArgs | Out-Host

        $entryArgs = @(
            $debugKnowledge, "--db", $knowledgeDbPath, "entry", "add",
            "--case", $caseRef,
            "--type", "observation",
            "--platform", "android",
            "--summary", "Thor profile capture: $Topic",
            "--body", $entryBody,
            "--artifact", $sessionDir,
            "--artifact", $reportPath
        )
        if (-not $NoScreenshot) {
            $entryArgs += @("--artifact", $screenPath)
            $entryArgs += @("--artifact", $screenBurstDir)
        }
        & python @entryArgs | Out-Host
    } catch {
        Write-Warning "Could not write SQLite knowledge entry: $_"
    }
}

Write-Host "Wrote transient profile report: $reportPath"
Write-Host "Wrote profile artifacts: $sessionDir"

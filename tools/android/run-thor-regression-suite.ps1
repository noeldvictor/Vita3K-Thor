param(
    [string]$MatrixPath = "tools/android/thor-render-regression-matrix.json",
    [string[]]$Game = @(),
    [string]$Adb = "adb",
    [string]$Serial = "c3ca0370",
    [string]$OutDir = "tmp/regression-runs",
    [string]$KnowledgeDb = "reports/debug_knowledge.sqlite",
    [switch]$NoKnowledgeEntry,
    [switch]$NoAnalyze,
    [switch]$NoForceStop,
    [switch]$KeepRunning,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\..")
$sendInputScript = Join-Path $scriptDir "send-thor-input.ps1"
$captureBurstScript = Join-Path $scriptDir "capture-thor-burst.ps1"
$captureRotationScript = Join-Path $scriptDir "capture-thor-rotation-burst.ps1"

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
        return "regression"
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
    if ($DryRun) {
        Write-Host "adb $($allArgs -join ' ')"
        return @()
    }

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

function Get-AdbText([string[]]$AdbArgs) {
    $output = Invoke-AdbChecked $AdbArgs
    return (($output | Out-String).Trim())
}

function Invoke-ToolCapture([scriptblock]$Call, [string]$LogPath) {
    if ($DryRun) {
        "dry-run" | Set-Content -Encoding UTF8 -Path $LogPath
        return @("dry-run")
    }

    $output = & $Call 2>&1
    $output | Set-Content -Encoding UTF8 -Path $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "Tool call failed. See $LogPath"
    }
    return $output
}

function Clear-Vita3KDebugProps([string]$Package) {
    $props = @(
        "debug.vita3k.render_skip",
        "debug.vita3k.render_trace",
        "debug.vita3k.render_dump",
        "debug.vita3k.render_stop_after",
        "debug.vita3k.render_disable_culled_discard_back_depth_fhash",
        "debug.vita3k.render_use_back_depth_write_fhash",
        "debug.vita3k.render_force_depth_always_fhash",
        "debug.vita3k.render_force_depth_lequal_fhash",
        "debug.vita3k.render_force_cull_none_fhash",
        "debug.vita3k.render_use_gl_cull_fhash",
        "debug.vita3k.render_flip_cull_fhash",
        "debug.vita3k.render_force_cull_front_fhash",
        "debug.vita3k.render_force_cull_back_fhash",
        "debug.vita3k.render_front_face_cw_fhash",
        "debug.vita3k.decompressed_bcn_abgr_swizzle",
        "debug.vita3k.thor_render_trace"
    )
    foreach ($prop in $props) {
        Invoke-AdbChecked @("shell", "setprop", $prop, "0") | Out-Null
    }
    $actionId = "regression-resume-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action", "resume") | Out-Null
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action_id", $actionId) | Out-Null
    Invoke-AdbChecked @("logcat", "-c") | Out-Null
}

function Launch-Game($Config, $GameConfig, [string]$GameDir) {
    $package = if ($Config.package) { [string]$Config.package } else { "org.vita3k.emulator.debug" }
    $activity = if ($Config.activity) { [string]$Config.activity } else { "org.vita3k.emulator.Emulator" }
    $logLevel = if ($null -ne $Config.logLevel) { [string]$Config.logLevel } else { "0" }
    $component = "$package/$activity"
    $vitaArgs = @("-a", "true", "--cartridge", [string]$GameConfig.path, "--log-level", $logLevel)
    if ($GameConfig.renderTrace) {
        $vitaArgs += "--thor-render-trace"
    }

    $argJson = ConvertTo-Json -Compress -InputObject $vitaArgs
    $argJsonBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($argJson))
    $argJson | Set-Content -Encoding UTF8 -Path (Join-Path $GameDir "launch-args.json")

    if (-not $NoForceStop) {
        Invoke-AdbChecked @("shell", "am", "force-stop", $package) | Out-Null
        Start-Sleep -Milliseconds 700
    }

    Invoke-AdbChecked @("shell", "am", "start", "-n", $component, "--es", "AppStartParametersJsonBase64", $argJsonBase64) |
        Set-Content -Encoding UTF8 -Path (Join-Path $GameDir "launch.txt")
}

function Get-PhysicalSize {
    if ($DryRun) {
        return @{ Width = 1080; Height = 1920 }
    }
    $text = Get-AdbText @("shell", "wm", "size")
    if ($text -notmatch "(?<w>\d+)x(?<h>\d+)") {
        throw "Could not parse wm size output: $text"
    }
    return @{ Width = [int]$Matches["w"]; Height = [int]$Matches["h"] }
}

function Clamp-Int([int]$Value, [int]$Min, [int]$Max) {
    return [Math]::Max($Min, [Math]::Min($Max, $Value))
}

function Convert-LandscapeTap([int]$X, [int]$Y, [string]$Strategy, [int]$ScreenshotWidth, [int]$ScreenshotHeight, $PhysicalSize) {
    $pw = [int]$PhysicalSize.Width
    $ph = [int]$PhysicalSize.Height
    switch ($Strategy) {
        "direct" {
            $tx = $X
            $ty = $Y
        }
        "portrait_cw" {
            $tx = $Y
            $ty = $ph - $X
        }
        "portrait_ccw" {
            $tx = $pw - $Y
            $ty = $X
        }
        "normalized" {
            $tx = [int][Math]::Round(($X / [double]$ScreenshotWidth) * $pw)
            $ty = [int][Math]::Round(($Y / [double]$ScreenshotHeight) * $ph)
        }
        "normalized_swapped" {
            $tx = [int][Math]::Round(($Y / [double]$ScreenshotHeight) * $pw)
            $ty = [int][Math]::Round(($X / [double]$ScreenshotWidth) * $ph)
        }
        default {
            throw "Unknown tap strategy '$Strategy'."
        }
    }
    return @{
        X = Clamp-Int $tx 0 ($pw - 1)
        Y = Clamp-Int $ty 0 ($ph - 1)
        Strategy = $Strategy
    }
}

function Invoke-LandscapeTap($Step, [string]$GameDir) {
    $physicalSize = Get-PhysicalSize
    $screenshotWidth = if ($Step.screenshotWidth) { [int]$Step.screenshotWidth } else { 1920 }
    $screenshotHeight = if ($Step.screenshotHeight) { [int]$Step.screenshotHeight } else { 1080 }
    $strategies = @($Step.strategies)
    if ($strategies.Count -eq 0) {
        $strategies = @("normalized", "portrait_cw", "portrait_ccw", "normalized_swapped")
    }
    $waitMs = if ($Step.waitMs) { [int]$Step.waitMs } else { 500 }
    $tapLog = Join-Path $GameDir ("tap-{0}.txt" -f (Slug ([string]$Step.name)))
    $lines = @()
    foreach ($strategy in $strategies) {
        $tap = Convert-LandscapeTap -X ([int]$Step.x) -Y ([int]$Step.y) -Strategy ([string]$strategy) -ScreenshotWidth $screenshotWidth -ScreenshotHeight $screenshotHeight -PhysicalSize $physicalSize
        $lines += ("{0}: screenshot=({1},{2}) physical=({3},{4}) size={5}x{6}" -f $tap.Strategy, [int]$Step.x, [int]$Step.y, $tap.X, $tap.Y, $physicalSize.Width, $physicalSize.Height)
        Invoke-AdbChecked @("shell", "input", "tap", "$($tap.X)", "$($tap.Y)") | Out-Null
        if ($waitMs -gt 0) {
            Start-Sleep -Milliseconds $waitMs
        }
    }
    $lines | Set-Content -Encoding UTF8 -Path $tapLog
}

function Get-ResponsivenessSnapshot([string]$GameDir, [string]$Package) {
    $logPath = Join-Path $GameDir "responsiveness-logcat-tail.txt"
    $windowPath = Join-Path $GameDir "responsiveness-window.txt"
    $log = Get-AdbText @("logcat", "-d", "-v", "threadtime", "-t", "600")
    $window = Get-AdbText @("shell", "dumpsys", "window")
    $log | Set-Content -Encoding UTF8 -Path $logPath
    $window | Set-Content -Encoding UTF8 -Path $windowPath
    $patterns = @("not responsive", "Input dispatching timed out", "ANR in", "Application Not Responding")
    $matched = @()
    foreach ($pattern in $patterns) {
        if ($log -match [Regex]::Escape($pattern)) {
            $matched += $pattern
        }
    }
    $focusOk = ($window -match [Regex]::Escape($Package))
    return @{
        LogPath = $logPath
        WindowPath = $windowPath
        IsResponsive = ($matched.Count -eq 0)
        Matched = $matched
        FocusContainsPackage = $focusOk
    }
}

function Add-KnowledgeEntry($GameConfig, [string]$Status, [string]$Commit, [string[]]$Artifacts, [string]$Body) {
    if ($NoKnowledgeEntry -or [string]::IsNullOrWhiteSpace([string]$GameConfig.case)) {
        return
    }

    $summary = "Automated Thor regression $Status - $($GameConfig.name)"
    $args = @(
        "tools/debug_knowledge.py", "entry", "add",
        "--db", $KnowledgeDb,
        "--case", [string]$GameConfig.case,
        "--type", "regression",
        "--platform", "android-thor",
        "--summary", $summary,
        "--body", $Body,
        "--commit", $Commit,
        "--source", "tools/android/run-thor-regression-suite.ps1"
    )
    foreach ($artifact in $Artifacts) {
        $args += @("--artifact", $artifact)
    }

    if ($DryRun) {
        Write-Host "python $($args -join ' ')"
        return
    }
    & python @args | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to add SQLite knowledge entry for $($GameConfig.name)."
    }
}

function Get-GitCommit {
    if ($DryRun) {
        return "dry-run"
    }
    $commit = (& git rev-parse --short HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) {
        return "unknown"
    }
    return (($commit | Out-String).Trim())
}

Require-Command $Adb
Require-Command $sendInputScript
Require-Command $captureBurstScript
Require-Command $captureRotationScript

Push-Location $repoRoot
try {
    if (-not (Test-Path -LiteralPath $MatrixPath)) {
        throw "Matrix file not found: $MatrixPath"
    }

    $matrix = Get-Content -Raw -Path $MatrixPath | ConvertFrom-Json
    $package = if ($matrix.package) { [string]$matrix.package } else { "org.vita3k.emulator.debug" }
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $runSlug = Slug $matrix.name
    $runDir = Join-Path $OutDir "$($stamp)_$runSlug"
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $commit = Get-GitCommit
    $results = @()

    $selected = @($matrix.games)
    if ($Game.Count -gt 0) {
        $wanted = @($Game | ForEach-Object { Slug $_ })
        $selected = @($selected | Where-Object {
            $wanted -contains (Slug ([string]$_.name)) -or
            $wanted -contains (Slug ([string]$_.slug)) -or
            $wanted -contains (Slug ([string]$_.titleId))
        })
    }
    if ($selected.Count -eq 0) {
        throw "No games selected from matrix."
    }

    foreach ($gameConfig in $selected) {
        $gameSlug = if ($gameConfig.slug) { Slug ([string]$gameConfig.slug) } else { Slug ([string]$gameConfig.name) }
        $gameDir = Join-Path $runDir $gameSlug
        New-Item -ItemType Directory -Force -Path $gameDir | Out-Null
        $artifacts = New-Object System.Collections.Generic.List[string]
        $status = "passed"
        $notes = New-Object System.Collections.Generic.List[string]
        $notes.Add("Game: $($gameConfig.name)")
        $notes.Add("Title ID: $($gameConfig.titleId)")
        $notes.Add("Path: $($gameConfig.path)")
        $notes.Add("Commit: $commit")
        if ($gameConfig.expected) {
            $notes.Add("Expected: $($gameConfig.expected)")
        }
        if ($gameConfig.knownBlocker) {
            $notes.Add("Known blocker: $($gameConfig.knownBlocker)")
        }

        try {
            Clear-Vita3KDebugProps $package
            Launch-Game -Config $matrix -GameConfig $gameConfig -GameDir $gameDir

            foreach ($step in @($gameConfig.steps)) {
                $stepName = if ($step.name) { [string]$step.name } else { [string]$step.type }
                Write-Host "[$($gameConfig.name)] $stepName"
                switch ([string]$step.type) {
                    "wait" {
                        $seconds = if ($step.seconds) { [int]$step.seconds } else { 1 }
                        Start-Sleep -Seconds ([Math]::Max($seconds, 0))
                    }
                    "input" {
                        $mode = if ($step.mode) { [string]$step.mode } else { "Sendevent" }
                        $sequence = [string[]]@($step.sequence)
                        $logPath = Join-Path $gameDir ("input-{0}.txt" -f (Slug $stepName))
                        Invoke-ToolCapture -LogPath $logPath -Call {
                            & $sendInputScript -Adb $Adb -Serial $Serial -Mode $mode -Sequence $sequence
                        } | Out-Null
                    }
                    "tapLandscape" {
                        Invoke-LandscapeTap -Step $step -GameDir $gameDir
                    }
                    "burst" {
                        $topic = if ($step.topic) { [string]$step.topic } else { "$gameSlug-burst" }
                        $count = if ($step.count) { [int]$step.count } else { 10 }
                        $intervalMs = if ($step.intervalMs) { [int]$step.intervalMs } else { 350 }
                        $logPath = Join-Path $gameDir ("burst-{0}.txt" -f (Slug $topic))
                        $output = Invoke-ToolCapture -LogPath $logPath -Call {
                            & $captureBurstScript -Adb $Adb -Serial $Serial -Topic $topic -Count $count -IntervalMs $intervalMs
                        }
                        $burstLine = @($output | Where-Object { $_ -match "Wrote Thor burst:\s*(?<path>.+)$" } | Select-Object -Last 1)
                        if ($burstLine.Count -gt 0) {
                            $artifact = ($burstLine[-1] -replace "^.*Wrote Thor burst:\s*", "").Trim()
                            $artifacts.Add($artifact)
                            if (-not $NoAnalyze -and -not $DryRun) {
                                & python tools/analyze_screenshot_burst.py $artifact | Tee-Object -FilePath (Join-Path $gameDir ("analyze-{0}.txt" -f (Slug $topic))) | Out-Host
                            }
                        }
                    }
                    "rotation" {
                        $topic = if ($step.topic) { [string]$step.topic } else { "$gameSlug-rotation" }
                        $durationSec = if ($step.durationSec) { [int]$step.durationSec } else { 20 }
                        $frameFps = if ($step.frameFps) { [int]$step.frameFps } else { 2 }
                        $axis = if ($step.axis) { [string]$step.axis } else { "ABS_Z" }
                        $axisValue = if ($step.axisValue) { [int]$step.axisValue } else { 32767 }
                        $logPath = Join-Path $gameDir ("rotation-{0}.txt" -f (Slug $topic))
                        $output = Invoke-ToolCapture -LogPath $logPath -Call {
                            & $captureRotationScript -Adb $Adb -Serial $Serial -Topic $topic -DurationSec $durationSec -FrameFps $frameFps -Axis $axis -AxisValue $axisValue -Rotate
                        }
                        $rotationLine = @($output | Where-Object { $_ -match "Wrote Thor rotation burst:\s*(?<path>.+)$" } | Select-Object -Last 1)
                        if ($rotationLine.Count -gt 0) {
                            $artifact = ($rotationLine[-1] -replace "^.*Wrote Thor rotation burst:\s*", "").Trim()
                            $artifacts.Add($artifact)
                        }
                    }
                    default {
                        throw "Unknown regression step type '$($step.type)' in $($gameConfig.name)."
                    }
                }
            }

            $responsive = Get-ResponsivenessSnapshot -GameDir $gameDir -Package $package
            if (-not $responsive.IsResponsive) {
                $status = "blocked"
                $notes.Add("Responsiveness blocker: $($responsive.Matched -join ', ')")
            }
        } catch {
            $status = "failed"
            $notes.Add("Failure: $($_.Exception.Message)")
            ($_ | Out-String) | Set-Content -Encoding UTF8 -Path (Join-Path $gameDir "error.txt")
        } finally {
            if (-not $KeepRunning -and -not $NoForceStop) {
                try {
                    Invoke-AdbChecked @("shell", "am", "force-stop", $package) | Out-Null
                } catch {
                    $notes.Add("Force-stop cleanup failed: $($_.Exception.Message)")
                }
            }
        }

        $notes.Add("Status: $status")
        $notes.Add("Artifacts:")
        foreach ($artifact in $artifacts) {
            $notes.Add("- $artifact")
        }
        $body = ($notes -join "`n")
        $body | Set-Content -Encoding UTF8 -Path (Join-Path $gameDir "result.md")

        Add-KnowledgeEntry -GameConfig $gameConfig -Status $status -Commit $commit -Artifacts ([string[]]$artifacts.ToArray()) -Body $body

        $results += [pscustomobject]@{
            name = [string]$gameConfig.name
            slug = $gameSlug
            titleId = [string]$gameConfig.titleId
            case = [string]$gameConfig.case
            status = $status
            artifacts = @($artifacts.ToArray())
            resultFile = (Join-Path $gameDir "result.md")
        }
    }

    $results | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -Path (Join-Path $runDir "results.json")
    $summary = @()
    $summary += "# Thor Regression Run"
    $summary += ""
    $summary += "- Matrix: ``$MatrixPath``"
    $summary += "- Commit: ``$commit``"
    $summary += "- Output: ``$runDir``"
    $summary += ""
    foreach ($result in $results) {
        $summary += "- ``$($result.status)`` - $($result.name) ($($result.titleId)): $($result.resultFile)"
    }
    $summary | Set-Content -Encoding UTF8 -Path (Join-Path $runDir "summary.md")
    Write-Host "Wrote Thor regression run: $runDir"
    $results | Format-Table -AutoSize | Out-Host
} finally {
    Pop-Location
}

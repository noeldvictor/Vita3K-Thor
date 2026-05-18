param(
    [string[]]$TitleId = @("PCSH00250", "PCSG00633"),
    [string]$CasePrefix = "thor-quickstate-regression",
    [string]$MatrixPath = "tools/android/thor-render-regression-matrix.json",
    [string]$Adb = "adb",
    [string]$Serial = "c3ca0370",
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$Activity = "org.vita3k.emulator.Emulator",
    [string]$StateRoot = "",
    [int]$LogLevel = 2,
    [int]$StartupSeconds = 12,
    [int]$AfterActionSeconds = 5,
    [int]$MarkerTimeoutSeconds = 180,
    [ValidateRange(1, 25)]
    [int]$Cycles = 1,
    [switch]$RequireExistingDurableFirst,
    [switch]$ExerciseUndoLoad,
    [switch]$KeepRunning,
    [switch]$NoKnowledgeEntry,
    [string]$KnowledgeDb = "reports/debug_knowledge.sqlite",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\..")
$matrixFullPath = Resolve-Path (Join-Path $repoRoot $MatrixPath)

function Get-Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "thor-quickstate"
    }
    return $slug
}

function Get-RepoPath([string]$Path) {
    return Join-Path $repoRoot $Path
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

function Get-DeviceStateRoot {
    if (-not [string]::IsNullOrWhiteSpace($StateRoot)) {
        return $StateRoot.TrimEnd("/")
    }
    return "/sdcard/Android/data/$Package/files/states"
}

function Get-DeviceStateDir([string]$Title) {
    return "$(Get-DeviceStateRoot)/$Title"
}

function Get-DeviceStateFile([string]$Title, [string]$Name) {
    return "$(Get-DeviceStateDir $Title)/$Name"
}

function Clear-Vita3KDebugProps {
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
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action", "clear") | Out-Null
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action_id", "clear-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')") | Out-Null
}

function Remove-DeviceFile([string]$Path) {
    Invoke-AdbChecked @("shell", "rm", "-f", $Path) | Out-Null
}

function Wait-DeviceFile([string]$Path, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        # Pass the whole shell expression as one adb argument. Windows adb can
        # otherwise split `sh -c` scripts in a way that makes existing files
        # report as missing.
        $result = Get-AdbText @("shell", "test -f '$Path' && echo yes || echo no")
        if ($result -match "yes") {
            return
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for device file: $Path"
}

function Read-DeviceFile([string]$Path) {
    return Get-AdbText @("shell", "cat", $Path)
}

function Pull-DeviceFile([string]$DevicePath, [string]$LocalDir) {
    New-Item -ItemType Directory -Force -Path $LocalDir | Out-Null
    $name = Split-Path -Leaf $DevicePath
    $localPath = Join-Path $LocalDir $name
    Invoke-AdbChecked @("pull", $DevicePath, $localPath) | Out-Null
    return $localPath
}

function Assert-MarkerSuccess([string]$Text, [string]$Path, [string]$ExpectedMode = "") {
    if ($Text -notmatch "(?m)^Result:\s+success\s*$") {
        throw "Marker did not report success: $Path`n$Text"
    }
    if ($ExpectedMode -and ($Text -notmatch "(?m)^Mode:\s+$([regex]::Escape($ExpectedMode))\s*$")) {
        throw "Marker did not report Mode: ${ExpectedMode}: $Path`n$Text"
    }
    if (($Text -match "(?m)^Restore enabled:") -and ($Text -notmatch "(?m)^Restore enabled:\s+yes\s*$")) {
        throw "Successful restore marker did not report Restore enabled: yes: $Path`n$Text"
    }
    if (($Text -match "(?m)^Missing serializers:") -and ($Text -notmatch "(?m)^Missing serializers:\s+none\s*$")) {
        throw "Successful restore marker did not report Missing serializers: none: $Path`n$Text"
    }
}

function Assert-StateMarkerReady([string]$Text, [string]$Path) {
    if ($Text -notmatch "(?m)^Restore enabled:\s+yes\s*$") {
        throw "State marker did not report Restore enabled: yes: $Path`n$Text"
    }
    if ($Text -notmatch "(?m)^Missing serializers:\s+none\s*$") {
        throw "State marker did not report Missing serializers: none: $Path`n$Text"
    }
    if ($Text -notmatch "(?m)^Block reason:\s+all mandatory quickstate serializers are present\s*$") {
        throw "State marker did not report the durable-ready block reason: $Path`n$Text"
    }
}

function Assert-CleanLog([string]$Text, [string]$Context) {
    $pattern = "Freeing unallocated page|VirtualAlloc failed|mprotect failed|commit failed|Unhandled SIGSEGV|SIGSEGV|Refused quickstate slot 0 restore|Failed to restore quickstate slot 0|restore failed|does not match the saved identity|not responsive|Input dispatching timed out|ANR in"
    $hits = @($Text -split "`n" | Where-Object { $_ -match $pattern } | Select-Object -First 16)
    if ($hits.Count -gt 0) {
        throw "Critical quickstate pattern found during ${Context}:`n$($hits -join "`n")"
    }
}

function Invoke-RuntimeAction([string]$Action, [string]$Title, [string]$ExpectedMarker, [string]$ExpectedMode, [string]$RunDir) {
    $restoreMarker = Get-DeviceStateFile $Title "slot0.thorstate.restore.txt"
    $captureMarker = Get-DeviceStateFile $Title "slot0.thorstate.capture.txt"
    $stateMarker = Get-DeviceStateFile $Title "slot0.thorstate.txt"

    if ($ExpectedMarker -eq "restore") {
        Remove-DeviceFile $restoreMarker
    } elseif ($ExpectedMarker -eq "capture") {
        Remove-DeviceFile $captureMarker
        Remove-DeviceFile $stateMarker
    }

    Invoke-AdbChecked @("logcat", "-c") | Out-Null
    $actionId = "$Action-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action", $Action) | Out-Null
    Invoke-AdbChecked @("shell", "setprop", "debug.vita3k.runtime_action_id", $actionId) | Out-Null

    if ($ExpectedMarker -eq "restore") {
        Wait-DeviceFile $restoreMarker $MarkerTimeoutSeconds
        $text = Read-DeviceFile $restoreMarker
        Assert-MarkerSuccess $text $restoreMarker $ExpectedMode
        Pull-DeviceFile $restoreMarker $RunDir | Out-Null
    } elseif ($ExpectedMarker -eq "capture") {
        Wait-DeviceFile $captureMarker $MarkerTimeoutSeconds
        Wait-DeviceFile $stateMarker $MarkerTimeoutSeconds
        $captureText = Read-DeviceFile $captureMarker
        $stateText = Read-DeviceFile $stateMarker
        Assert-MarkerSuccess $captureText $captureMarker
        Assert-StateMarkerReady $stateText $stateMarker
        Pull-DeviceFile $captureMarker $RunDir | Out-Null
        Pull-DeviceFile $stateMarker $RunDir | Out-Null
    }

    Start-Sleep -Seconds $AfterActionSeconds
    $log = Get-AdbText @("logcat", "-d", "-v", "threadtime", "-t", "1200")
    $logPath = Join-Path $RunDir ("logcat-$Action.txt")
    $log | Set-Content -Encoding UTF8 -Path $logPath
    Assert-CleanLog $log $Action
}

function Launch-Game($GameConfig, [string]$RunDir) {
    $component = "$Package/$Activity"
    $vitaArgs = @("-a", "true", "--cartridge", [string]$GameConfig.path, "--log-level", "$LogLevel")
    $argJson = ConvertTo-Json -Compress -InputObject $vitaArgs
    $argJsonBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($argJson))
    $argJson | Set-Content -Encoding UTF8 -Path (Join-Path $RunDir "launch-args.json")
    Invoke-AdbChecked @("shell", "am", "force-stop", $Package) | Out-Null
    Start-Sleep -Milliseconds 700
    Invoke-AdbChecked @("shell", "am", "start", "-n", $component, "--es", "AppStartParametersJsonBase64", $argJsonBase64) |
        Set-Content -Encoding UTF8 -Path (Join-Path $RunDir "launch.txt")
}

function Get-GameMap {
    $matrix = Get-Content -LiteralPath $matrixFullPath -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($game in $matrix.games) {
        if ($game.titleId) {
            $map[[string]$game.titleId] = $game
        }
        if ($game.slug) {
            $map[[string]$game.slug] = $game
        }
    }
    return $map
}

function Add-KnowledgeEntry([string]$Case, [string]$Title, [string]$Name, [string]$Status, [string]$Commit, [string]$Body, [string[]]$Artifacts) {
    if ($NoKnowledgeEntry -or [string]::IsNullOrWhiteSpace($Case)) {
        return
    }
    $args = @(
        "tools/debug_knowledge.py", "--db", $KnowledgeDb, "entry", "add",
        "--case", $Case,
        "--type", "test",
        "--platform", "android-thor",
        "--summary", "Thor quickstate $Status - $Name",
        "--body", $Body,
        "--commit", $Commit,
        "--source", "tools/android/run-thor-quickstate-regression.ps1"
    )
    foreach ($artifact in $Artifacts) {
        $args += @("--artifact", $artifact)
    }
    if ($DryRun) {
        Write-Host "python $($args -join ' ')"
        return
    }
    Push-Location $repoRoot
    try {
        & python @args | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "debug_knowledge.py exited with $LASTEXITCODE while recording $Case"
        }
    } catch {
        Write-Warning "Failed to record SQLite knowledge for ${Case}: $($_.Exception.Message)"
    } finally {
        Pop-Location
    }
}

$outRoot = Get-RepoPath ("tmp\thor-quickstate\" + (Get-Slug $CasePrefix))
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
Clear-Vita3KDebugProps
$gameMap = Get-GameMap
$commit = (git -C $repoRoot rev-parse --short HEAD).Trim()
$summary = [System.Collections.Generic.List[string]]::new()
$summary.Add("Vita3K Thor Android quickstate regression")
$summary.Add("Started: $((Get-Date).ToString("o"))")
$summary.Add("Commit: $commit")
$summary.Add("Titles: $($TitleId -join ", ")")
$summary.Add("Cycles: $Cycles")
$summary.Add("State root: $(Get-DeviceStateRoot)")
$summary.Add("Require existing durable first: $RequireExistingDurableFirst")
$summary.Add("Exercise undo load: $ExerciseUndoLoad")
$summary.Add("")

$failures = [System.Collections.Generic.List[string]]::new()

foreach ($cycle in 1..$Cycles) {
    foreach ($title in $TitleId) {
        $game = $gameMap[$title]
        if (-not $game) {
            $failures.Add("${title} cycle ${cycle}: no game path in $MatrixPath")
            continue
        }

        $runSlug = "$(Get-Slug $CasePrefix)-cycle$cycle-$(Get-Slug $title)"
        $runDir = Join-Path $outRoot $runSlug
        New-Item -ItemType Directory -Force -Path $runDir | Out-Null
        $summary.Add("[$title cycle $cycle/$Cycles] $($game.name)")
        $artifacts = [System.Collections.Generic.List[string]]::new()

        try {
            Launch-Game $game $runDir
            Start-Sleep -Seconds $StartupSeconds

            if ($RequireExistingDurableFirst) {
                Invoke-RuntimeAction "load_state" $title "restore" "durable-disk" $runDir
                $summary.Add("  existing durable load: pass")
            }

            Invoke-RuntimeAction "save_state" $title "capture" "" $runDir
            $summary.Add("  save fresh state: pass")

            Invoke-RuntimeAction "load_state" $title "restore" "same-session" $runDir
            $summary.Add("  same-session load: pass")

            if ($ExerciseUndoLoad) {
                Invoke-RuntimeAction "undo_load_state" $title "restore" "durable-undo" $runDir
                $summary.Add("  undo load: pass")
            }

            Invoke-AdbChecked @("shell", "am", "force-stop", $Package) | Out-Null
            Start-Sleep -Seconds 2
            Launch-Game $game $runDir
            Start-Sleep -Seconds $StartupSeconds
            Invoke-RuntimeAction "load_state" $title "restore" "durable-disk" $runDir
            $summary.Add("  restart durable load: pass")

            $logPath = Join-Path $runDir "final-logcat.txt"
            $log = Get-AdbText @("logcat", "-d", "-v", "threadtime", "-t", "1600")
            $log | Set-Content -Encoding UTF8 -Path $logPath
            Assert-CleanLog $log "$title final"

            $artifacts.Add($runDir)
            Add-KnowledgeEntry `
                -Case ([string]$game.case) `
                -Title $title `
                -Name ([string]$game.name) `
                -Status "passed" `
                -Commit $commit `
                -Body "Android Thor quickstate cycle $cycle passed: fresh save marker ready, same-session load restored, undo-load exercised=$ExerciseUndoLoad, restart durable load restored from disk. State root $(Get-DeviceStateRoot)." `
                -Artifacts $artifacts.ToArray()
        } catch {
            $message = $_.Exception.Message
            $summary.Add("  FAILED: $message")
            $failures.Add("${title} cycle ${cycle}: $message")
            try {
                $logPath = Join-Path $runDir "failure-logcat.txt"
                Get-AdbText @("logcat", "-d", "-v", "threadtime", "-t", "2000") |
                    Set-Content -Encoding UTF8 -Path $logPath
                $artifacts.Add($logPath)
            } catch {
            }
            Add-KnowledgeEntry `
                -Case ([string]$game.case) `
                -Title $title `
                -Name ([string]$game.name) `
                -Status "failed" `
                -Commit $commit `
                -Body "Android Thor quickstate cycle $cycle failed: $message" `
                -Artifacts $artifacts.ToArray()
        } finally {
            if (-not $KeepRunning) {
                Invoke-AdbChecked @("shell", "am", "force-stop", $Package) | Out-Null
            }
        }
    }
}

$summary.Add("")
if ($failures.Count -gt 0) {
    $summary.Add("Result: FAILED")
    foreach ($failure in $failures) {
        $summary.Add("- $failure")
    }
} else {
    $summary.Add("Result: PASSED")
}

$summaryPath = Join-Path $outRoot "quickstate-regression-summary.txt"
$summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "Thor quickstate regression summary: $summaryPath"

if ($failures.Count -gt 0) {
    throw "Thor quickstate regression failed with $($failures.Count) issue(s)."
}

Write-Host "Thor quickstate regression passed for: $($TitleId -join ", ") ($Cycles cycle(s))"

param(
    [string[]]$TitleId = @("PCSG00633", "PCSH00250"),
    [string]$CasePrefix = "quickstate-regression",
    [string]$ConfigPath = "",
    [ValidateSet("Vulkan", "OpenGL")]
    [string]$BackendRenderer = "Vulkan",
    [int]$LogLevel = 2,
    [int]$TraceLimit = 0,
    [int]$StartupSeconds = 10,
    [int]$AfterActionSeconds = 4,
    [int]$MarkerTimeoutSeconds = 90,
    [ValidateRange(1, 100)]
    [int]$Cycles = 1,
    [switch]$SkipBuild,
    [switch]$StopExisting,
    [switch]$ExerciseUndoLoad,
    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"

function Get-Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "quickstate-regression"
    }
    return $slug
}

function Get-Vita3KProcesses {
    @(Get-Process -Name Vita3K -ErrorAction SilentlyContinue)
}

function Stop-Vita3KProcess([System.Diagnostics.Process]$Process) {
    if (-not $Process) {
        return
    }

    try {
        $Process.Refresh()
        if ($Process.HasExited) {
            return
        }
    } catch {
        return
    }

    try {
        if ($Process.CloseMainWindow()) {
            Start-Sleep -Seconds 2
            $Process.Refresh()
            if ($Process.HasExited) {
                return
            }
        }
    } catch {
    }

    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
}

function Wait-FreshFile([string]$Path, [DateTime]$AfterUtc, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($item -and $item.LastWriteTimeUtc -ge $AfterUtc) {
            return $item
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for fresh marker: $Path"
}

function Assert-MarkerSuccess([string]$Path, [string]$ExpectedMode = "") {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Marker not found: $Path"
    }

    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -notmatch "(?m)^Result:\s+success\s*$") {
        throw "Marker did not report success: $Path`n$text"
    }
    if ($ExpectedMode -and ($text -notmatch "(?m)^Mode:\s+$([regex]::Escape($ExpectedMode))\s*$")) {
        throw "Marker did not report Mode: ${ExpectedMode}: $Path`n$text"
    }
    if (($text -match "(?m)^Restore enabled:") -and ($text -notmatch "(?m)^Restore enabled:\s+yes\s*$")) {
        throw "Successful restore marker did not report Restore enabled: yes: $Path`n$text"
    }
    if (($text -match "(?m)^Missing serializers:") -and ($text -notmatch "(?m)^Missing serializers:\s+none\s*$")) {
        throw "Successful restore marker did not report Missing serializers: none: $Path`n$text"
    }
    return $text
}

function Assert-StateMarkerReady([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "State marker not found: $Path"
    }

    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -notmatch "(?m)^Restore enabled:\s+yes\s*$") {
        throw "State marker did not report Restore enabled: yes: $Path`n$text"
    }
    if ($text -notmatch "(?m)^Missing serializers:\s+none\s*$") {
        throw "State marker did not report Missing serializers: none: $Path`n$text"
    }
    if ($text -notmatch "(?m)^Block reason:\s+all mandatory quickstate serializers are present\s*$") {
        throw "State marker did not report the durable-ready block reason: $Path`n$text"
    }
    return $text
}

function Assert-HealthyProcess([System.Diagnostics.Process]$Process, [string]$Context) {
    if (-not $Process) {
        throw "No Vita3K process for $Context"
    }
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "Vita3K exited during $Context"
    }
    if (-not $Process.Responding) {
        throw "Vita3K is not responding during $Context"
    }
}

function Assert-CleanLog([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $pattern = "Freeing unallocated page|VirtualAlloc failed|mprotect failed|commit failed|Unhandled SIGSEGV|SIGSEGV|Refused quickstate slot 0 restore|Failed to restore quickstate slot 0|restore failed|does not match the saved identity"
    $hits = Select-String -LiteralPath $Path -Pattern $pattern -CaseSensitive:$false -ErrorAction SilentlyContinue
    if ($hits) {
        $snippet = ($hits | Select-Object -First 12 | ForEach-Object { "$($_.LineNumber): $($_.Line)" }) -join [Environment]::NewLine
        throw "Critical quickstate pattern found in $Path`n$snippet"
    }
}

function Add-MarkerDigest([System.Collections.Generic.List[string]]$Summary, [string]$Label, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $Summary.Add("[$Label] marker digest: missing $Path")
        return
    }

    $patterns = @(
        "^Result:",
        "^Mode:",
        "^Detail:",
        "^Restore enabled:",
        "^Same paused-session restore:",
        "^Same-session live host restore:",
        "^Block reason:",
        "^Missing serializers:",
        "^Sync wait queue entries:",
        "^Sync wait queue metadata:",
        "^Message-pipe buffered bytes:",
        "^IO memory-backed file handles:",
        "^Open files:",
        "^Open std files:",
        "^Open directories:",
        "^FIOS overlays:",
        "^Display vblank waits:",
        "^Display vblank callbacks:",
        "^GXM display queue entries:",
        "^GXM display queue waiters:",
        "^GXM pending display callbacks:",
        "^GXM notification waits:",
        "^Audiodec decoders:",
        "^AVPlayer players:",
        "^Videodec decoders:",
        "^Timers:",
        "^Semaphores:",
        "^Condvars:",
        "^Lightweight condvars:",
        "^Mutexes:",
        "^Lightweight mutexes:",
        "^RW locks:",
        "^Event flags:",
        "^Message pipes:",
        "^Callbacks:",
        "^Simple events:"
    )
    $hits = Select-String -LiteralPath $Path -Pattern $patterns -ErrorAction SilentlyContinue
    if (-not $hits) {
        $Summary.Add("[$Label] marker digest: no selected coverage lines")
        return
    }

    $Summary.Add("[$Label] marker digest:")
    foreach ($hit in $hits) {
        $Summary.Add("  $($hit.Line.Trim())")
    }
}

function Invoke-ControlAction([string]$ControlFile, [string]$Action, [int]$TraceLimit) {
    & (Join-Path $PSScriptRoot "set-render-debug-control.ps1") `
        -ControlFile $ControlFile `
        -Action $Action `
        -TraceLimit $TraceLimit `
        -NoTrace `
        -NoLabels | Out-Null
}

function Start-TitleRun([string]$Title, [string]$CaseSlug, [string]$ConfigPath, [string]$BackendRenderer, [int]$TraceLimit, [int]$LogLevel) {
    $beforeIds = @(Get-Vita3KProcesses | ForEach-Object { $_.Id })

    $launchParams = @{
        TitleId = $Title
        CaseSlug = $CaseSlug
        BackendRenderer = $BackendRenderer
        TraceLimit = $TraceLimit
        LogLevel = $LogLevel
        NoLabels = $true
    }
    if ($ConfigPath) {
        $launchParams.ConfigPath = $ConfigPath
    }
    & (Join-Path $PSScriptRoot "start-game-render-debug.ps1") @launchParams | Out-Host

    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        $newProcess = Get-Vita3KProcesses |
            Where-Object { $beforeIds -notcontains $_.Id } |
            Sort-Object StartTime -Descending |
            Select-Object -First 1
        if ($newProcess) {
            return $newProcess
        }
        Start-Sleep -Milliseconds 250
    }

    return Get-Vita3KProcesses |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$binDir = Join-Path $repoRoot "build\windows-vs2022\bin\RelWithDebInfo"
$stateRoot = Join-Path $binDir "states"
$summaryRoot = Join-Path $repoRoot ("tmp\vita3k-win-debug\" + (Get-Slug $CasePrefix))
New-Item -ItemType Directory -Force -Path $summaryRoot | Out-Null
$summaryPath = Join-Path $summaryRoot "quickstate-regression-summary.txt"
$summary = [System.Collections.Generic.List[string]]::new()
$summary.Add("Vita3K Thor Windows quickstate regression")
$summary.Add("Started: $((Get-Date).ToString("o"))")
$summary.Add("Commit: $(git -C $repoRoot rev-parse --short HEAD)")
$summary.Add("Titles: $($TitleId -join ", ")")
$summary.Add("Cycles: $Cycles")
$summary.Add("Exercise undo load: $ExerciseUndoLoad")
$summary.Add("")

if ($StopExisting) {
    Get-Vita3KProcesses | ForEach-Object { Stop-Vita3KProcess $_ }
} elseif (Get-Vita3KProcesses) {
    throw "Vita3K is already running. Close it first, or pass -StopExisting for a harness-owned run."
}

if (-not $SkipBuild) {
    $summary.Add("Build: cmake --build build\windows-vs2022 --config RelWithDebInfo --target vita3k --parallel 4")
    cmake --build (Join-Path $repoRoot "build\windows-vs2022") --config RelWithDebInfo --target vita3k --parallel 4
}

$failures = [System.Collections.Generic.List[string]]::new()

$runs = @()
for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
    foreach ($title in $TitleId) {
        $runs += [pscustomobject]@{
            Title = $title
            Cycle = $cycle
        }
    }
}

foreach ($run in $runs) {
    $title = $run.Title
    $cycle = [int]$run.Cycle
    $titleSlug = Get-Slug $title
    $prefixSlug = Get-Slug $CasePrefix
    $cycleSlug = if ($Cycles -gt 1) { "$prefixSlug-cycle$cycle" } else { $prefixSlug }
    $stateFile = Join-Path $stateRoot "$title\slot0.thorstate"
    $stateMarker = Join-Path $stateRoot "$title\slot0.thorstate.txt"
    $captureMarker = Join-Path $stateRoot "$title\slot0.thorstate.capture.txt"
    $restoreMarker = Join-Path $stateRoot "$title\slot0.thorstate.restore.txt"
    if (-not (Test-Path -LiteralPath $stateFile)) {
        $failures.Add("${title} cycle ${cycle}: missing baseline state file $stateFile")
        continue
    }

    $case1 = "$cycleSlug-$titleSlug-durable-1"
    $case2 = "$cycleSlug-$titleSlug-durable-2"
    $control1 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case1\render-control.txt"
    $control2 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case2\render-control.txt"
    $stdout1 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case1\vita3k.stdout.log"
    $stdout2 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case2\vita3k.stdout.log"

    $summary.Add("[$title cycle $cycle/$Cycles] durable run 1: $case1")
    $process = $null
    try {
        $process = Start-TitleRun $title $case1 $ConfigPath $BackendRenderer $TraceLimit $LogLevel
        Start-Sleep -Seconds $StartupSeconds
        Assert-HealthyProcess $process "$title startup durable run 1"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "load_state" $TraceLimit
        Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $restoreMarker "durable-disk" | Out-Null
        Add-MarkerDigest $summary "$title cycle $cycle run 1 durable restore" $restoreMarker
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title durable load"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "save_state" $TraceLimit
        Wait-FreshFile $captureMarker $before $MarkerTimeoutSeconds | Out-Null
        Wait-FreshFile $stateMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $captureMarker | Out-Null
        Assert-StateMarkerReady $stateMarker | Out-Null
        Add-MarkerDigest $summary "$title cycle $cycle run 1 state" $stateMarker
        Add-MarkerDigest $summary "$title cycle $cycle run 1 capture" $captureMarker
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title save-again"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "load_state" $TraceLimit
        Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $restoreMarker "same-session" | Out-Null
        Add-MarkerDigest $summary "$title cycle $cycle run 1 same-session restore" $restoreMarker
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title same-session load"

        if ($ExerciseUndoLoad) {
            $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
            Invoke-ControlAction $control1 "undo_load_state" $TraceLimit
            Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
            Assert-MarkerSuccess $restoreMarker "durable-undo" | Out-Null
            Add-MarkerDigest $summary "$title cycle $cycle run 1 undo restore" $restoreMarker
            Start-Sleep -Seconds $AfterActionSeconds
            Assert-HealthyProcess $process "$title undo load"
        }
    } catch {
        $failures.Add("$title cycle $cycle run 1: $($_.Exception.Message)")
    } finally {
        if (-not $KeepRunning) {
            Stop-Vita3KProcess $process
        }
    }

    try {
        Assert-CleanLog $stdout1
    } catch {
        $failures.Add("$title cycle $cycle run 1 log: $($_.Exception.Message)")
    }

    if (-not $KeepRunning) {
        Start-Sleep -Seconds 2
    }

    $summary.Add("[$title cycle $cycle/$Cycles] durable run 2: $case2")
    $process = $null
    try {
        $process = Start-TitleRun $title $case2 $ConfigPath $BackendRenderer $TraceLimit $LogLevel
        Start-Sleep -Seconds $StartupSeconds
        Assert-HealthyProcess $process "$title startup durable run 2"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control2 "load_state" $TraceLimit
        Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $restoreMarker "durable-disk" | Out-Null
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title restart durable load"
        Add-MarkerDigest $summary "$title cycle $cycle run 2 restore" $restoreMarker
    } catch {
        $failures.Add("$title cycle $cycle run 2: $($_.Exception.Message)")
    } finally {
        if (-not $KeepRunning) {
            Stop-Vita3KProcess $process
        }
    }

    try {
        Assert-CleanLog $stdout2
    } catch {
        $failures.Add("$title cycle $cycle run 2 log: $($_.Exception.Message)")
    }
}

$summary.Add("")
if ($failures.Count -gt 0) {
    $summary.Add("Result: FAILED")
    foreach ($failure in $failures) {
        $summary.Add("- $failure")
    }
    $summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    Write-Host "Quickstate regression summary: $summaryPath"
    throw "Windows quickstate regression failed with $($failures.Count) issue(s)."
}

$summary.Add("Result: PASSED")
$summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "Quickstate regression summary: $summaryPath"
Write-Host "Windows quickstate regression passed for: $($TitleId -join ", ") ($Cycles cycle(s))"

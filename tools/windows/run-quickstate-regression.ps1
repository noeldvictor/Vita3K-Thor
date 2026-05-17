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
    [switch]$SkipBuild,
    [switch]$StopExisting,
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

foreach ($title in $TitleId) {
    $titleSlug = Get-Slug $title
    $stateFile = Join-Path $stateRoot "$title\slot0.thorstate"
    $captureMarker = Join-Path $stateRoot "$title\slot0.thorstate.capture.txt"
    $restoreMarker = Join-Path $stateRoot "$title\slot0.thorstate.restore.txt"
    if (-not (Test-Path -LiteralPath $stateFile)) {
        $failures.Add("${title}: missing baseline state file $stateFile")
        continue
    }

    $case1 = "$(Get-Slug $CasePrefix)-$titleSlug-durable-1"
    $case2 = "$(Get-Slug $CasePrefix)-$titleSlug-durable-2"
    $control1 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case1\render-control.txt"
    $control2 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case2\render-control.txt"
    $stdout1 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case1\vita3k.stdout.log"
    $stdout2 = Join-Path $repoRoot "tmp\vita3k-win-debug\$case2\vita3k.stdout.log"

    $summary.Add("[$title] durable run 1: $case1")
    $process = $null
    try {
        $process = Start-TitleRun $title $case1 $ConfigPath $BackendRenderer $TraceLimit $LogLevel
        Start-Sleep -Seconds $StartupSeconds
        Assert-HealthyProcess $process "$title startup durable run 1"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "load_state" $TraceLimit
        Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $restoreMarker "durable-disk" | Out-Null
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title durable load"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "save_state" $TraceLimit
        Wait-FreshFile $captureMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $captureMarker | Out-Null
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title save-again"

        $before = (Get-Date).ToUniversalTime().AddMilliseconds(-500)
        Invoke-ControlAction $control1 "load_state" $TraceLimit
        Wait-FreshFile $restoreMarker $before $MarkerTimeoutSeconds | Out-Null
        Assert-MarkerSuccess $restoreMarker | Out-Null
        Start-Sleep -Seconds $AfterActionSeconds
        Assert-HealthyProcess $process "$title same-session load"
    } catch {
        $failures.Add("$title run 1: $($_.Exception.Message)")
    } finally {
        if (-not $KeepRunning) {
            Stop-Vita3KProcess $process
        }
    }

    try {
        Assert-CleanLog $stdout1
    } catch {
        $failures.Add("$title run 1 log: $($_.Exception.Message)")
    }

    if (-not $KeepRunning) {
        Start-Sleep -Seconds 2
    }

    $summary.Add("[$title] durable run 2: $case2")
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
    } catch {
        $failures.Add("$title run 2: $($_.Exception.Message)")
    } finally {
        if (-not $KeepRunning) {
            Stop-Vita3KProcess $process
        }
    }

    try {
        Assert-CleanLog $stdout2
    } catch {
        $failures.Add("$title run 2 log: $($_.Exception.Message)")
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
Write-Host "Windows quickstate regression passed for: $($TitleId -join ", ")"

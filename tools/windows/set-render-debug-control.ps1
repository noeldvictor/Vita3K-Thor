param(
    [string]$Skip = "",
    [string]$StopAfter = "",
    [int]$TraceLimit = 512,
    [switch]$NoTrace,
    [switch]$NoLabels,
    [string]$ControlFile = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $ControlFile) {
    $ControlFile = Join-Path $RepoRoot "tmp\vita3k-win-debug\render-control.txt"
}

New-Item -ItemType Directory -Force -Path (Split-Path $ControlFile) | Out-Null

@(
    "# Edit while Vita3K is running; values update live."
    "trace=$([int](-not $NoTrace))"
    "trace_limit=$TraceLimit"
    "labels=$([int](-not $NoLabels))"
    "skip=$Skip"
    "stop_after=$StopAfter"
) | Set-Content -LiteralPath $ControlFile -Encoding UTF8

Write-Host "Updated renderer control:"
Write-Host "  file:       $ControlFile"
Write-Host "  trace:      $(-not $NoTrace)"
Write-Host "  traceLimit: $TraceLimit"
Write-Host "  labels:     $(-not $NoLabels)"
Write-Host "  skip:       $Skip"
Write-Host "  stopAfter:  $StopAfter"

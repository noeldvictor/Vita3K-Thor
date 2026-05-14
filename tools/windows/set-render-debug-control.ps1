param(
    [string]$Skip = "",
    [string]$StopAfter = "",
    [string]$Dump = "",
    [string]$Action = "",
    [string]$ActionId = "",
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
if ($Action -and -not $ActionId) {
    $ActionId = Get-Date -Format "yyyyMMdd_HHmmss_ffff"
}

$controlLines = @(
    "# Edit while Vita3K is running; values update live."
    "trace=$([int](-not $NoTrace))"
    "trace_limit=$TraceLimit"
    "labels=$([int](-not $NoLabels))"
    "skip=$Skip"
    "stop_after=$StopAfter"
    "dump=$Dump"
    "action=$Action"
    "action_id=$ActionId"
)

$controlText = ($controlLines -join [Environment]::NewLine) + [Environment]::NewLine
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
        [System.IO.File]::WriteAllText($ControlFile, $controlText, $utf8NoBom)
        break
    } catch {
        if ($attempt -eq 5) {
            throw
        }
        Start-Sleep -Milliseconds (50 * $attempt)
    }
}

Write-Host "Updated renderer control:"
Write-Host "  file:       $ControlFile"
Write-Host "  trace:      $(-not $NoTrace)"
Write-Host "  traceLimit: $TraceLimit"
Write-Host "  labels:     $(-not $NoLabels)"
Write-Host "  skip:       $Skip"
Write-Host "  stopAfter:  $StopAfter"
Write-Host "  dump:       $Dump"
Write-Host "  action:     $Action"
Write-Host "  actionId:   $ActionId"

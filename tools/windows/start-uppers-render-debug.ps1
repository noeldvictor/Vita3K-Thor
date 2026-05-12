param(
    [string]$ConfigPath = "",
    [string]$GameZip = "",
    [string]$Skip = "",
    [string]$StopAfter = "",
    [string]$Dump = "",
    [string]$ControlFile = "",
    [int]$TraceLimit = 256,
    [int]$LogLevel = 2,
    [switch]$NoLabels
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ExePath = Join-Path $RepoRoot "build\windows-vs2022\bin\RelWithDebInfo\Vita3K.exe"

if (-not $ConfigPath) {
    $ConfigPath = Join-Path $RepoRoot "tmp\vita3k-win-debug\config_highacc.yml"
}
if (-not $GameZip) {
    $GameZip = Join-Path $RepoRoot "tmp\local-games\Uppers (English v0.97)[vita3k].zip"
}
if (-not $ControlFile) {
    $ControlFile = Join-Path $RepoRoot "tmp\vita3k-win-debug\render-control.txt"
}

$LaunchConfigPath = $ConfigPath
if ($LogLevel -ge 0) {
    $debugConfigDir = Join-Path $RepoRoot "tmp\vita3k-win-debug"
    New-Item -ItemType Directory -Force -Path $debugConfigDir | Out-Null
    $LaunchConfigPath = Join-Path $debugConfigDir "config_render_debug.yml"
    $configText = Get-Content -LiteralPath $ConfigPath -Raw
    if ($configText -match '(?m)^log-level:\s*\d+') {
        $configText = $configText -replace '(?m)^log-level:\s*\d+', "log-level: $LogLevel"
    } else {
        $configText += "`nlog-level: $LogLevel`n"
    }
    Set-Content -LiteralPath $LaunchConfigPath -Value $configText -Encoding UTF8
}

$env:VITA3K_RENDER_DEBUG = "1"
$env:VITA3K_RENDER_TRACE = "1"
$env:VITA3K_RENDER_TRACE_LIMIT = [string]$TraceLimit
$env:VITA3K_RENDER_CONTROL_FILE = $ControlFile
if (-not (Test-Path -LiteralPath $ControlFile)) {
    @(
        "# Edit while Vita3K is running; values update live."
        "trace=1"
        "trace_limit=$TraceLimit"
        "labels=$([int](-not $NoLabels))"
        "skip="
        "stop_after="
        "dump="
    ) | Set-Content -LiteralPath $ControlFile -Encoding UTF8
}
if ($NoLabels) {
    Remove-Item Env:\VITA3K_RENDER_LABELS -ErrorAction SilentlyContinue
} else {
    $env:VITA3K_RENDER_LABELS = "1"
}

if ($Skip) {
    $env:VITA3K_RENDER_SKIP = $Skip
} else {
    Remove-Item Env:\VITA3K_RENDER_SKIP -ErrorAction SilentlyContinue
}

if ($StopAfter) {
    $env:VITA3K_RENDER_STOP_AFTER = $StopAfter
} else {
    Remove-Item Env:\VITA3K_RENDER_STOP_AFTER -ErrorAction SilentlyContinue
}

if ($Dump) {
    $env:VITA3K_RENDER_DUMP = $Dump
} else {
    Remove-Item Env:\VITA3K_RENDER_DUMP -ErrorAction SilentlyContinue
}

$argumentLine = "--config-location `"$LaunchConfigPath`" --cartridge --backend-renderer Vulkan --log-level $LogLevel --thor-render-trace `"$GameZip`""

Write-Host "Starting Vita3K render debug:"
Write-Host "  exe:        $ExePath"
Write-Host "  config:     $LaunchConfigPath"
Write-Host "  game:       $GameZip"
Write-Host "  labels:     $(-not $NoLabels)"
Write-Host "  traceLimit: $TraceLimit"
Write-Host "  logLevel:   $LogLevel"
Write-Host "  control:    $ControlFile"
Write-Host "  skip:       $Skip"
Write-Host "  stopAfter:  $StopAfter"
Write-Host "  dump:       $Dump"

Start-Process -FilePath $ExePath -ArgumentList $argumentLine -WorkingDirectory (Split-Path $ExePath)

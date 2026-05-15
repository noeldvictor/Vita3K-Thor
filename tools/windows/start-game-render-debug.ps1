param(
    [string]$TitleId = "PCSH00250",
    [string]$CaseSlug = "",
    [string]$ConfigPath = "",
    [string]$GameZip = "",
    [string]$Skip = "",
    [string]$StopAfter = "",
    [string]$Dump = "",
    [string]$DumpSurfaceAddr = "",
    [string]$DumpSurfaceDir = "",
    [int]$DumpSurfaceLimit = 24,
    [int]$DumpSurfaceEvery = 1,
    [string]$ControlFile = "",
    [ValidateSet("Vulkan", "OpenGL")]
    [string]$BackendRenderer = "Vulkan",
    [int]$TraceLimit = 256,
    [int]$LogLevel = 2,
    [bool]$PsnSignedIn = $true,
    [switch]$NoLabels,
    [switch]$NoStart
)

$ErrorActionPreference = "Stop"

function Get-Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "render-debug"
    }
    return $slug
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ExePath = Join-Path $RepoRoot "build\windows-vs2022\bin\RelWithDebInfo\Vita3K.exe"

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Windows Vita3K build not found: $ExePath"
}

if (-not $CaseSlug) {
    $CaseSlug = Get-Slug $TitleId
}

if (-not $ConfigPath) {
    $ConfigPath = Join-Path $RepoRoot "tmp\vita3k-win-debug\config_highacc.yml"
}
if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Config not found: $ConfigPath"
}

if (-not $GameZip) {
    $issueDir = Join-Path $RepoRoot "roms\issues\$TitleId"
    $GameZip = Get-ChildItem -LiteralPath $issueDir -Filter *.zip -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $GameZip -or -not (Test-Path -LiteralPath $GameZip)) {
    throw "Game ZIP not found. Put the issue ROM under roms\issues\$TitleId\ or pass -GameZip."
}
$GameZip = (Resolve-Path -LiteralPath $GameZip).Path

$debugRoot = Join-Path $RepoRoot "tmp\vita3k-win-debug\$CaseSlug"
New-Item -ItemType Directory -Force -Path $debugRoot | Out-Null

if (-not $ControlFile) {
    $ControlFile = Join-Path $debugRoot "render-control.txt"
}

$LaunchConfigPath = $ConfigPath
if ($LogLevel -ge 0) {
    $LaunchConfigPath = Join-Path $debugRoot "config_render_debug.yml"
    $configText = Get-Content -LiteralPath $ConfigPath -Raw
    if ($configText -match '(?m)^backend-renderer:\s*\S+') {
        $configText = $configText -replace '(?m)^backend-renderer:\s*\S+', "backend-renderer: $BackendRenderer"
    } else {
        $configText += "`nbackend-renderer: $BackendRenderer`n"
    }
    if ($configText -match '(?m)^log-level:\s*\d+') {
        $configText = $configText -replace '(?m)^log-level:\s*\d+', "log-level: $LogLevel"
    } else {
        $configText += "`nlog-level: $LogLevel`n"
    }
    $psnSignedInValue = if ($PsnSignedIn) { "1" } else { "0" }
    if ($configText -match '(?m)^psn-signed-in:\s*\d+') {
        $configText = $configText -replace '(?m)^psn-signed-in:\s*\d+', "psn-signed-in: $psnSignedInValue"
    } else {
        $configText += "`npsn-signed-in: $psnSignedInValue`n"
    }
    Set-Content -LiteralPath $LaunchConfigPath -Value $configText -Encoding UTF8
}

$env:VITA3K_RENDER_DEBUG = "1"
$env:VITA3K_RENDER_TRACE = "1"
$env:VITA3K_RENDER_TRACE_LIMIT = [string]$TraceLimit
$env:VITA3K_RENDER_CONTROL_FILE = $ControlFile
$env:VITA3K_RUNTIME_CONTROL_FILE = $ControlFile

if (-not (Test-Path -LiteralPath $ControlFile)) {
    @(
        "# Edit while Vita3K is running; values update live."
        "trace=1"
        "trace_limit=$TraceLimit"
        "labels=$([int](-not $NoLabels))"
        "skip="
        "stop_after="
        "dump="
        "action="
        "action_id="
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

if ($DumpSurfaceAddr) {
    if (-not $DumpSurfaceDir) {
        $DumpSurfaceDir = Join-Path $debugRoot "surface-dumps"
    }
    New-Item -ItemType Directory -Force -Path $DumpSurfaceDir | Out-Null
    $env:VITA3K_RENDER_DUMP_SURFACE_ADDR = $DumpSurfaceAddr
    $env:VITA3K_RENDER_DUMP_SURFACE_DIR = $DumpSurfaceDir
    $env:VITA3K_RENDER_DUMP_SURFACE_LIMIT = [string]$DumpSurfaceLimit
    $env:VITA3K_RENDER_DUMP_SURFACE_EVERY = [string]([Math]::Max(1, $DumpSurfaceEvery))
} else {
    Remove-Item Env:\VITA3K_RENDER_DUMP_SURFACE_ADDR -ErrorAction SilentlyContinue
    Remove-Item Env:\VITA3K_RENDER_DUMP_SURFACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\VITA3K_RENDER_DUMP_SURFACE_LIMIT -ErrorAction SilentlyContinue
    Remove-Item Env:\VITA3K_RENDER_DUMP_SURFACE_EVERY -ErrorAction SilentlyContinue
}

$argumentLine = "--config-location `"$LaunchConfigPath`" --cartridge --backend-renderer $BackendRenderer --log-level $LogLevel --thor-render-trace `"$GameZip`""

Write-Host "Vita3K Windows render debug:"
Write-Host "  exe:        $ExePath"
Write-Host "  title:      $TitleId"
Write-Host "  case:       $CaseSlug"
Write-Host "  config:     $LaunchConfigPath"
Write-Host "  game:       $GameZip"
Write-Host "  backend:    $BackendRenderer"
Write-Host "  control:    $ControlFile"
Write-Host "  traceLimit: $TraceLimit"
Write-Host "  logLevel:   $LogLevel"
Write-Host "  psnLocal:   $PsnSignedIn"
Write-Host "  labels:     $(-not $NoLabels)"
Write-Host "  skip:       $Skip"
Write-Host "  stopAfter:  $StopAfter"
Write-Host "  dump:       $Dump"
Write-Host "  surface:    $DumpSurfaceAddr"
Write-Host "  surfaceDir: $DumpSurfaceDir"

if (-not $NoStart) {
    Start-Process -FilePath $ExePath -ArgumentList $argumentLine -WorkingDirectory (Split-Path $ExePath)
}

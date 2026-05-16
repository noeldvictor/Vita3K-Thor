param(
    [string]$TitleId = "PCSH00250",
    [string]$CaseSlug = "",
    [string]$ConfigPath = "",
    [string]$GameZip = "",
    [ValidateSet("Vulkan", "OpenGL")]
    [string]$BackendRenderer = "Vulkan",
    [int]$TraceLimit = 256,
    [int]$LogLevel = 0,
    [bool]$PsnSignedIn = $true,
    [switch]$NoLabels,
    [string]$ControlFile = "",
    [string]$RenderDocPath = "C:\Program Files\RenderDoc\renderdoccmd.exe",
    [string]$CaptureTemplate = "",
    [switch]$ApiValidation,
    [switch]$CaptureCallstacks,
    [switch]$RefAllResources,
    [switch]$WaitForExit,
    [switch]$NoStart
)

$ErrorActionPreference = "Stop"

function Get-Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "renderdoc-capture"
    }
    return $slug
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ExePath = Join-Path $RepoRoot "build\windows-vs2022\bin\RelWithDebInfo\Vita3K.exe"
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Windows Vita3K build not found: $ExePath"
}
if (-not (Test-Path -LiteralPath $RenderDocPath)) {
    throw "RenderDoc command line tool not found: $RenderDocPath"
}

if (-not $CaseSlug) {
    $CaseSlug = Get-Slug "$TitleId-renderdoc"
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
$captureRoot = Join-Path $RepoRoot "tmp\renderdoc\$CaseSlug"
New-Item -ItemType Directory -Force -Path $debugRoot | Out-Null
New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null

if (-not $ControlFile) {
    $ControlFile = Join-Path $debugRoot "render-control.txt"
}
if (-not $CaptureTemplate) {
    $CaptureTemplate = Join-Path $captureRoot "$CaseSlug"
}

$LaunchConfigPath = Join-Path $debugRoot "config_renderdoc.yml"
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
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($LaunchConfigPath, $configText, $utf8NoBom)

$controlLines = @(
    "# Edit while Vita3K is running; values update live."
    "trace=1"
    "trace_limit=$TraceLimit"
    "labels=$([int](-not $NoLabels))"
    "skip="
    "stop_after="
    "dump="
    "action="
    "action_id="
)
[System.IO.File]::WriteAllText($ControlFile, (($controlLines -join [Environment]::NewLine) + [Environment]::NewLine), $utf8NoBom)

$env:VITA3K_RENDER_DEBUG = "1"
$env:VITA3K_RENDER_TRACE = "1"
$env:VITA3K_RENDER_TRACE_LIMIT = [string]$TraceLimit
$env:VITA3K_RENDER_CONTROL_FILE = $ControlFile
$env:VITA3K_RUNTIME_CONTROL_FILE = $ControlFile
if ($NoLabels) {
    Remove-Item Env:\VITA3K_RENDER_LABELS -ErrorAction SilentlyContinue
} else {
    $env:VITA3K_RENDER_LABELS = "1"
}

$renderDocArgs = @(
    "capture",
    "-d", (Split-Path $ExePath),
    "-c", $CaptureTemplate
)
if ($WaitForExit) {
    $renderDocArgs += "-w"
}
if ($ApiValidation) {
    $renderDocArgs += "--opt-api-validation"
}
if ($CaptureCallstacks) {
    $renderDocArgs += "--opt-capture-callstacks"
}
if ($RefAllResources) {
    $renderDocArgs += "--opt-ref-all-resources"
}

$vitaArgs = @(
    $ExePath,
    "--config-location", $LaunchConfigPath,
    "--cartridge",
    "--backend-renderer", $BackendRenderer,
    "--log-level", [string]$LogLevel,
    "--thor-render-trace",
    $GameZip
)
$renderDocArgs += $vitaArgs

Write-Host "Vita3K RenderDoc capture launch:"
Write-Host "  renderdoc: $RenderDocPath"
Write-Host "  exe:       $ExePath"
Write-Host "  title:     $TitleId"
Write-Host "  case:      $CaseSlug"
Write-Host "  config:    $LaunchConfigPath"
Write-Host "  game:      $GameZip"
Write-Host "  control:   $ControlFile"
Write-Host "  captures:  $CaptureTemplate"
Write-Host "  backend:   $BackendRenderer"
Write-Host "  trace:     $TraceLimit"
Write-Host "  labels:    $(-not $NoLabels)"
Write-Host ""
Write-Host "Use RenderDoc's capture hotkey while the target frame is visible. Captures are written under tmp\renderdoc."

if (-not $NoStart) {
    & $RenderDocPath @renderDocArgs
}

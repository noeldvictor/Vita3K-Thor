param(
    [Parameter(Mandatory = $true)]
    [string]$TitleId,
    [string]$SourcePath = "",
    [string]$DevicePath = "",
    [string]$Adb = "adb",
    [string]$Name = "game.zip",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SourcePath) -and [string]::IsNullOrWhiteSpace($DevicePath)) {
    throw "Provide either -SourcePath for a local file or -DevicePath for an ADB pull."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$safeTitleId = $TitleId.ToUpperInvariant() -replace "[^A-Z0-9_-]", ""
if ([string]::IsNullOrWhiteSpace($safeTitleId)) {
    throw "Invalid TitleId: $TitleId"
}

$destDir = Join-Path $repoRoot "roms\issues\$safeTitleId"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
$destPath = Join-Path $destDir $Name

if ((Test-Path -LiteralPath $destPath) -and -not $Force) {
    throw "Destination already exists: $destPath. Pass -Force to replace it."
}

if (-not [string]::IsNullOrWhiteSpace($SourcePath)) {
    if (-not (Test-Path -LiteralPath $SourcePath)) {
        throw "Source file not found: $SourcePath"
    }
    Copy-Item -LiteralPath $SourcePath -Destination $destPath -Force:$Force
} else {
    $devices = & $Adb devices -l
    $connected = @($devices | Select-String -Pattern "\bdevice\b" | Where-Object { $_.Line -notmatch "^List of devices" })
    if ($connected.Count -eq 0) {
        throw "No adb device connected."
    }
    & $Adb pull $DevicePath $destPath
}

$item = Get-Item -LiteralPath $destPath
$manifest = @{
    title_id = $safeTitleId
    local_path = $destPath
    source_path = $SourcePath
    device_path = $DevicePath
    bytes = $item.Length
    copied_at = (Get-Date).ToString("o")
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $destDir "manifest.json")

Write-Host "Issue ROM staged locally:"
Write-Host "  $destPath"
Write-Host "This directory is ignored by git."

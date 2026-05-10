param(
    [string]$RepoUrl = "https://github.com/r0ah/vitacheat.git",
    [string]$WorkDir = "tmp/vitacheat-db",
    [string]$DeviceCheatRoot = "/storage/2664-21DE/cheats/psvita",
    [string]$Adb = "adb",
    [switch]$SkipPush
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

Require-Command git
if (-not $SkipPush) {
    Require-Command $Adb
}

$repoPath = Resolve-Path -LiteralPath "." | Select-Object -ExpandProperty Path
$workPath = Join-Path $repoPath $WorkDir

if (Test-Path -LiteralPath (Join-Path $workPath ".git")) {
    git -C $workPath pull --ff-only
} else {
    $parent = Split-Path -Parent $workPath
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    git clone --depth 1 $RepoUrl $workPath
}

$dbPath = Join-Path $workPath "db"
if (-not (Test-Path -LiteralPath $dbPath)) {
    throw "VitaCheat db folder not found at $dbPath"
}

$psvFiles = @(Get-ChildItem -LiteralPath $dbPath -Filter "*.psv" -File)
Write-Host "VitaCheat db ready: $($psvFiles.Count) .psv files at $dbPath"

if ($SkipPush) {
    return
}

$devices = & $Adb devices -l
$connected = @($devices | Select-String -Pattern "\bdevice\b" | Where-Object { $_.Line -notmatch "^List of devices" })
if ($connected.Count -eq 0) {
    throw "No adb device connected."
}

& $Adb shell "mkdir -p '$DeviceCheatRoot'"
& $Adb push $dbPath "$DeviceCheatRoot/"

Write-Host "Pushed VitaCheat db to $DeviceCheatRoot/db"

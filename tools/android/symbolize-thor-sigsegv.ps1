param(
    [string]$Adb = "adb",
    [string]$Serial = "",
    [string]$Package = "org.vita3k.emulator.debug",
    [string]$LibPath = "",
    [string]$Addr2Line = "",
    [string]$LogPath = "",
    [int]$TailLines = 5000
)

$ErrorActionPreference = "Stop"

function Invoke-ThorAdb {
    param([string[]]$AdbArgs)
    if ($Serial) {
        & $Adb -s $Serial @AdbArgs
    } else {
        & $Adb @AdbArgs
    }
}

if (-not $LibPath) {
    $libs = @(Get-ChildItem -Path "android\build\intermediates\cxx" -Recurse -Filter "libVita3K.so" -ErrorAction SilentlyContinue |
        Sort-Object Length -Descending)
    if (-not $libs) {
        throw "Could not find unstripped libVita3K.so. Build Android reldebug first or pass -LibPath."
    }
    $LibPath = $libs[0].FullName
}

if (-not $Addr2Line) {
    $ndkHome = $env:ANDROID_NDK_HOME
    if (-not $ndkHome -and $env:ANDROID_HOME) {
        $ndks = @(Get-ChildItem -Path (Join-Path $env:ANDROID_HOME "ndk") -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending)
        if ($ndks) {
            $ndkHome = $ndks[0].FullName
        }
    }
    if ($ndkHome) {
        $candidate = Join-Path $ndkHome "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-addr2line.exe"
        if (Test-Path $candidate) {
            $Addr2Line = $candidate
        }
    }
    if (-not $Addr2Line) {
        $cmd = Get-Command llvm-addr2line.exe -ErrorAction SilentlyContinue
        if ($cmd) {
            $Addr2Line = $cmd.Source
        }
    }
    if (-not $Addr2Line) {
        throw "Could not find llvm-addr2line.exe. Set ANDROID_NDK_HOME or pass -Addr2Line."
    }
}

$appPid = ((Invoke-ThorAdb @("shell", "pidof", $Package)) | Select-Object -First 1).Trim()
if (-not $appPid) {
    throw "Package $Package is not running."
}

$maps = Invoke-ThorAdb @("shell", "run-as", $Package, "cat", "/proc/$appPid/maps")
$libLine = $maps | Where-Object { $_ -match "libVita3K\.so" -and $_ -match "\sr-xp\s" } | Select-Object -First 1
if (-not $libLine) {
    throw "Could not find executable libVita3K.so mapping for pid $appPid."
}

if ($libLine -notmatch "^([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+\S+\s+([0-9a-fA-F]+)") {
    throw "Could not parse libVita3K.so map line: $libLine"
}

$base = [Convert]::ToUInt64($Matches[1], 16)
$fileOffset = [Convert]::ToUInt64($Matches[3], 16)

if ($LogPath) {
    $logLines = Get-Content -Path $LogPath
} else {
    $logLines = Invoke-ThorAdb @("logcat", "-d", "-v", "threadtime") | Select-Object -Last $TailLines
}

$counts = @{}
foreach ($line in $logLines) {
    if ($line -match "Unhandled SIGSEGV at pc 0x([0-9a-fA-F]+)") {
        $pc = $Matches[1].ToLowerInvariant()
        if (-not $counts.ContainsKey($pc)) {
            $counts[$pc] = 0
        }
        $counts[$pc]++
    }
}

if ($counts.Count -eq 0) {
    Write-Host "No 'Unhandled SIGSEGV at pc' lines found."
    return
}

foreach ($item in ($counts.GetEnumerator() | Sort-Object Value -Descending)) {
    $pcValue = [Convert]::ToUInt64($item.Key, 16)
    $offset = $pcValue - $base + $fileOffset
    $offsetText = "0x{0:x}" -f $offset
    $symbol = & $Addr2Line -f -C -e $LibPath $offsetText
    [pscustomobject]@{
        Count = $item.Value
        Pc = "0x$($item.Key)"
        Offset = $offsetText
        Function = $symbol[0]
        Location = $symbol[1]
    }
}

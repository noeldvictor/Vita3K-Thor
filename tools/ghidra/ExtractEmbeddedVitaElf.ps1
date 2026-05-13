param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath
)

$resolvedInput = (Resolve-Path -LiteralPath $InputPath).Path
$bytes = [System.IO.File]::ReadAllBytes($resolvedInput)

$elfOffset = -1
for ($i = 0; $i -le $bytes.Length - 4; $i++) {
    if ($bytes[$i] -eq 0x7F -and $bytes[$i + 1] -eq 0x45 -and $bytes[$i + 2] -eq 0x4C -and $bytes[$i + 3] -eq 0x46) {
        $elfOffset = $i
        break
    }
}

if ($elfOffset -lt 0) {
    throw "No embedded ELF magic found in $resolvedInput"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $dir = Split-Path -Parent $resolvedInput
    $base = [System.IO.Path]::GetFileNameWithoutExtension($resolvedInput)
    $OutputPath = Join-Path $dir "$base.elf"
}

$targetPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath
} else {
    Join-Path (Get-Location) $OutputPath
}

$resolvedOutputParent = Split-Path -Parent $targetPath
if (![string]::IsNullOrWhiteSpace($resolvedOutputParent)) {
    New-Item -ItemType Directory -Force -Path $resolvedOutputParent | Out-Null
}

$slice = New-Object byte[] ($bytes.Length - $elfOffset)
[Array]::Copy($bytes, $elfOffset, $slice, 0, $slice.Length)
[System.IO.File]::WriteAllBytes($targetPath, $slice)

[pscustomobject]@{
    Input        = $resolvedInput
    Output       = (Resolve-Path -LiteralPath $targetPath).Path
    ElfOffset    = ("0x{0:X}" -f $elfOffset)
    BytesWritten = $slice.Length
}

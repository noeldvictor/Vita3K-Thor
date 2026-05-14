param(
    [string]$Topic = "vita3k-burst",
    [int]$Count = 10,
    [int]$IntervalMs = 300,
    [string]$WindowTitle = "Vita3K",
    [string]$OutDir = "tmp/vita3k-win-debug",
    [switch]$Desktop,
    [switch]$NoFocus
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ('Vita3KCaptureWin32' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Vita3KCaptureWin32 {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out Vita3KCaptureRect lpRect);
}

[StructLayout(LayoutKind.Sequential)]
public struct Vita3KCaptureRect {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}
"@
}

$SW_RESTORE = 9

function Slug([string]$Value) {
    $slug = ($Value.ToLowerInvariant() -replace "[^a-z0-9]+", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "vita3k-burst"
    }
    return $slug
}

function Find-Vita3KWindow {
    $process = Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object {
        $_.MainWindowHandle -ne 0
    } | Sort-Object StartTime -Descending | Select-Object -First 1
    if ($process) {
        return $process
    }

    # Avoid false positives from browser tabs or repo pages whose title contains
    # "Vita3K". The emulator's runtime window starts with the product name.
    return Get-Process | Where-Object {
        $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "$WindowTitle v*"
    } | Sort-Object StartTime -Descending | Select-Object -First 1
}

function Get-DesktopBounds {
    return [System.Windows.Forms.SystemInformation]::VirtualScreen
}

function Get-CaptureBounds {
    if ($Desktop) {
        return @{
            Mode = "desktop"
            Title = "virtual desktop"
            Bounds = Get-DesktopBounds
        }
    }

    $proc = Find-Vita3KWindow
    if (-not $proc) {
        Write-Warning "No window found with title containing '$WindowTitle'; capturing the virtual desktop instead."
        return @{
            Mode = "desktop-fallback"
            Title = "virtual desktop"
            Bounds = Get-DesktopBounds
        }
    }

    if (-not $NoFocus) {
        [Vita3KCaptureWin32]::ShowWindow($proc.MainWindowHandle, $SW_RESTORE) | Out-Null
        [Vita3KCaptureWin32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 120
    }

    $rect = New-Object Vita3KCaptureRect
    $ok = [Vita3KCaptureWin32]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)
    if (-not $ok -or $rect.Right -le $rect.Left -or $rect.Bottom -le $rect.Top) {
        Write-Warning "Could not read Vita3K window bounds; capturing the virtual desktop instead."
        return @{
            Mode = "desktop-fallback"
            Title = "virtual desktop"
            Bounds = Get-DesktopBounds
        }
    }

    $virtual = Get-DesktopBounds
    $left = [Math]::Max($rect.Left, $virtual.Left)
    $top = [Math]::Max($rect.Top, $virtual.Top)
    $right = [Math]::Min($rect.Right, $virtual.Right)
    $bottom = [Math]::Min($rect.Bottom, $virtual.Bottom)
    if ($right -le $left -or $bottom -le $top) {
        Write-Warning "Vita3K window is outside the visible desktop; capturing the virtual desktop instead."
        return @{
            Mode = "desktop-fallback"
            Title = "virtual desktop"
            Bounds = $virtual
        }
    }

    return @{
        Mode = "window"
        Title = $proc.MainWindowTitle
        Bounds = [System.Drawing.Rectangle]::FromLTRB($left, $top, $right, $bottom)
    }
}

function Capture-Png($Path, [System.Drawing.Rectangle]$Bounds) {
    $bitmap = [System.Drawing.Bitmap]::new($Bounds.Width, $Bounds.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($Bounds.Left, $Bounds.Top, 0, 0, $bitmap.Size, [System.Drawing.CopyPixelOperation]::SourceCopy)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$Count = [Math]::Max($Count, 1)
$IntervalMs = [Math]::Max($IntervalMs, 0)
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$topicSlug = Slug $Topic
$topicDir = Join-Path $OutDir $topicSlug
$sessionDir = Join-Path $topicDir "$($stamp)_burst"
$frameDir = Join-Path $sessionDir "frames"
New-Item -ItemType Directory -Force -Path $frameDir | Out-Null

$capture = Get-CaptureBounds
$bounds = [System.Drawing.Rectangle]$capture.Bounds
$frames = @()

for ($i = 1; $i -le $Count; $i++) {
    $framePath = Join-Path $frameDir ("frame_{0:D4}.png" -f $i)
    Capture-Png $framePath $bounds
    $frames += $framePath
    Write-Host ("Captured frame {0}/{1}: {2}" -f $i, $Count, $framePath)
    if ($i -lt $Count -and $IntervalMs -gt 0) {
        Start-Sleep -Milliseconds $IntervalMs
    }
}

$latestPath = Join-Path $sessionDir "latest-screen.png"
Copy-Item -Force -LiteralPath $frames[-1] -Destination $latestPath

$metadata = @()
$metadata += "Topic: $Topic"
$metadata += "Started: $stamp"
$metadata += "Capture mode: $($capture.Mode)"
$metadata += "Window title: $($capture.Title)"
$metadata += "Window title filter: $WindowTitle"
$metadata += "Count: $Count"
$metadata += "Interval ms: $IntervalMs"
$metadata += "Bounds: left=$($bounds.Left) top=$($bounds.Top) width=$($bounds.Width) height=$($bounds.Height)"
$metadata += "Latest: $latestPath"
$metadata += "Frames:"
$metadata += $frames
$metadata | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDir "metadata.txt")

Write-Host "Wrote Windows Vita3K burst: $sessionDir"

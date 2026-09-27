# Takes a screenshot of the 86Box window. For diagnostics, when the guest does not do what it should.
#
#   powershell -File ref86box\screenshot.ps1 -Out shot.png
#
# Without -ProcId it shoots the first window whose title starts with "86Box".
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$ProcId = 0
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rr, B; }
}
"@

$proc = if ($ProcId) { Get-Process -Id $ProcId } else {
    Get-Process | Where-Object { $_.MainWindowTitle -like "86Box*" } | Select-Object -First 1
}
if (-not $proc) { Write-Output "86Box window not found"; exit 1 }

$h = $proc.MainWindowHandle
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400

$r = New-Object W+R
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.Rr - $r.L
$ht = $r.B - $r.T

$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "saved $Out ($w x $ht), title: $($proc.MainWindowTitle)"

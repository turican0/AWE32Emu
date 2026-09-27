# ---------------------------------------------------------------------------
# Starts our build of 86Box with the EMU8000 port-write trace and closes it
# after the given time.
#
#   powershell -File ref86box\run_trace.ps1 -Trace tests\out\dos_init.trace `
#              -Mode dos -Seconds 90
#
# The CMOS of the guest may be invalid for the current configuration, and the
# BIOS then waits on "Press <F1> for Setup, <ESC> to Boot". So the script
# sends ESC to the window after a few seconds. -NoEsc suppresses it.
# ---------------------------------------------------------------------------
param(
    [Parameter(Mandatory = $true)][string]$Trace,
    # Where to save the output of the EMU8000 itself as .wav. It is taken from
    # the end of emu8k_update(), i.e. before the card mixer - directly
    # comparable with what our player produces.
    [string]$Wav = "",
    [ValidateSet("dos", "win95", "mc2")][string]$Mode = "dos",
    [int]$Seconds = 90,
    [int]$EscAfter = 8,
    # CPU state and a memory window at EBX/EBP on every access to the EMU8000
    # ports (awe32_trace.c). EBX points to the voice parameter block, so the
    # driver's **intermediate values** can be read from it, not only the
    # written registers. The decoder is tests/patch_struct.py.
    [string]$CpuTrace = "",
    [int]$CpuTraceMem = 160,
    [switch]$NoEsc
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
# The mc2 mode has its own virtual machine, so the DOS environment does not
# mix with the Windows one - its own disk image and configuration.
# The VM images and the built 86Box are not published; they lie in the data
# directory next to this folder.
$data = Split-Path $here -Parent
$vm   = Join-Path $data ($(if ($Mode -eq "mc2") { "ref86box\vmdos" } else { "ref86box\vm" }))
# The instruction mode of the tracer needs a build without the dynarec - in
# translated blocks the hook is missed. Switched with the variable AWE32_BUILD.
$build = if ($env:AWE32_BUILD) { $env:AWE32_BUILD } else { "build86box" }
$exe  = Join-Path $data ("ref86box\" + $build + "\src\86Box.exe")
$roms = "C:\prenos\86BoxWipeout2\roms"

$Trace = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Trace))
if (Test-Path $Trace) { Remove-Item $Trace }

$env:EMU8K_TRACE = $Trace
if ($Wav -ne "") {
    $Wav = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Wav))
    $env:AWE32_WAV = $Wav
    Write-Output "wav:   $Wav"
} else {
    Remove-Item Env:AWE32_WAV -ErrorAction SilentlyContinue
}
if ($CpuTrace -ne "") {
    $CpuTrace = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $CpuTrace))
    if (Test-Path $CpuTrace) { Remove-Item $CpuTrace }
    $env:AWE32_TRACE_FILE = $CpuTrace
    $env:AWE32_TRACE_MEM  = "$CpuTraceMem"
    Write-Output "cpu:   $CpuTrace (mem $CpuTraceMem B)"
} else {
    Remove-Item Env:AWE32_TRACE_FILE -ErrorAction SilentlyContinue
}
# The guest's CMOS. After a hard kill of 86Box its contents are stale or
# half-written and the BIOS then stops on "CMOS Checksum Invalid / Press <F1>
# for Setup, <ESC> to Boot" - and waits there for a key that SendKeys may not
# deliver.
#
# What does NOT work (verified 2026-08-25):
#  - copying a `thor.nvr` of another machine - it is for another
#    configuration, the checksum does not match anyway
#  - truncating the file to 128 B - 86Box writes it back at 256 B on start,
#    so the length was never the problem
#
# What works: keeping **a known good copy for this machine** aside and
# restoring it before every run. It is made automatically by the first run
# that ends cleanly (then 86Box finishes writing the CMOS and the checksum
# matches).
function Cmos-Checksum([byte[]]$b) {
    if ($b.Length -lt 0x30) { return $false }
    $sum = 0
    for ($i = 0x10; $i -lt 0x2E; $i++) { $sum += $b[$i] }
    $sum = $sum -band 0xFFFF
    return ($sum -eq ((([int]$b[0x2E]) -shl 8) -bor [int]$b[0x2F]))
}
function Repair-Cmos([string]$dir) {
    $f    = Join-Path $dir "nvr\thor.nvr"
    $good = "$f.good"
    if (-not (Test-Path $f)) { return }
    $b = [System.IO.File]::ReadAllBytes($f)
    if (Cmos-Checksum $b) {
        Copy-Item $f $good -Force
        Write-Output "CMOS fine, backup refreshed ($($b.Length) B)"
    } elseif (Test-Path $good) {
        Copy-Item $good $f -Force
        Write-Output "CMOS had a bad checksum, restored from the backup"
    } else {
        Write-Output "CAREFUL: the CMOS has a bad checksum and there is no backup."
        Write-Output "         Boot the guest by hand, press F1 -> Save & Exit in the BIOS,"
        Write-Output "         close 86Box with the window's X and run this script again."
    }
}
Repair-Cmos $vm

Write-Output "mode:  $Mode"
Write-Output "trace: $Trace"

$p = Start-Process -PassThru -FilePath $exe -ArgumentList `
    '-P', $vm, '-R', $roms, `
    '-C', $(if ($Mode -eq "mc2") { Join-Path $vm "86box.cfg" } else { Join-Path $vm "86box-$Mode.cfg" }), `
    '-L', (Join-Path $vm "86box.log")

if (-not $NoEsc) {
    Start-Sleep -Seconds $EscAfter
    $ws = New-Object -ComObject WScript.Shell
    if ($ws.AppActivate($p.Id)) {
        $ws.SendKeys("{ESC}")
        Write-Output "sent ESC (skipping the CMOS message)"
    } else {
        Write-Output "warning: could not activate the 86Box window"
    }
    $Seconds = $Seconds - $EscAfter
}

Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) {
    # First close the window cleanly. A hard kill (`Stop-Process -Force`)
    # leaves a broken CMOS in nvr\thor.nvr (256 B instead of 128 B) and on the
    # next start the BIOS stops on a prompt nobody answers - every run sawed
    # off the branch the next one sat on.
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(15000)) {
        Stop-Process -Id $p.Id -Force
        Write-Output "86Box had to be killed - check nvr"
    } else {
        Write-Output "86Box closed"
    }
}
Start-Sleep -Seconds 2

if ($CpuTrace -ne "" -and (Test-Path $CpuTrace)) {
    Write-Output ("cpu trace: " + (Get-Item $CpuTrace).Length + " B")
}
if (Test-Path $Trace) {
    $len = (Get-Item $Trace).Length
    $lines = (Get-Content $Trace | Measure-Object -Line).Lines
    Write-Output "trace: $len B, $lines lines"
} else {
    Write-Output "NO TRACE WAS CREATED"
}

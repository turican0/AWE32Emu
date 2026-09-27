@echo off
rem ---------------------------------------------------------------------------
rem Starts our build of 86Box with the EMU8000 port-write trace switched on.
rem
rem   run_vm.bat <output-trace> [dos|win95]
rem
rem The guest is ref86box\vm (a Windows 95 image; not published). The card is
rem sbawe32 (without PnP), so in DOS it sits at A220/E620 and CTCM is not
rem needed.
rem ---------------------------------------------------------------------------
setlocal

if "%~1"=="" (
    echo usage: run_vm.bat ^<output-trace^> [dos^|win95]
    echo   dos   - boots to DOS, AUTOEXEC runs AWEUTIL /S       (card sbawe32^)
    echo   win95 - boots Windows 95, driver SBAWE32.DRV           (card sbawe32_pnp^)
    exit /b 1
)

set MODE=%~2
if "%MODE%"=="" set MODE=dos

set EMU8K_TRACE=%~f1
set VM=%~dp0vm
set ROMS=C:\prenos\86BoxWipeout2\roms

echo Mode: %MODE%
echo Trace: %EMU8K_TRACE%
"%~dp0build86box\src\86Box.exe" -P "%VM%" -R "%ROMS%" -C "%VM%\86box-%MODE%.cfg" -L "%VM%\86box.log"

endlocal

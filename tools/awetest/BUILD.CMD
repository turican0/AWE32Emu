@echo off
cd /d "%~dp0"
rem ---------------------------------------------------------------------------
rem  SUPERSEDED - use BUILD32.CMD instead.
rem
rem  This 16-bit build still compiles, but the capture now collects a whole
rem  cycle in memory before saving it, and real mode leaves only a few
rem  hundred kB for that - a few seconds of audio per file. Kept only for
rem  comparison; the program that goes to testers is the 32-bit one.
rem
rem  Build AWETEST.EXE with Open Watcom.
rem
rem  16-bit real mode, LARGE model. The program is small and has no reason to
rem  need a DOS extender, so this build produces a plain DOS executable - one
rem  file to copy into the game directory and nothing else. A missing
rem  DOS4GW.EXE is an easy way to waste somebody's afternoon.
rem
rem  WATCOM points at an unpacked Open Watcom snapshot, AWESDK at a copy of
rem  the Creative AWE32 DOS SDK on a path WITHOUT SPACES - Watcom cannot cope
rem  with a space in -i= and reports "more than one file to compile".
rem ---------------------------------------------------------------------------
if "%WATCOM%"=="" set WATCOM=C:\prenos\OpenWatcom
if "%AWESDK%"=="" set AWESDK=C:\prenos\awesdk

set PATH=%WATCOM%\binnt64;%WATCOM%\binnt;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h
set EDPATH=%WATCOM%\eddat

echo === compiling (16-bit, large model) ===
wcc -ml -bt=dos -oilms -DSBKLIB -i=%AWESDK%\LIB\SBKLIB\INCLUDE -fo=awetest.obj AWETEST.C
if errorlevel 1 goto done

echo === linking ===
wlink system dos file awetest.obj library %AWESDK%\LIB\SBKLIB\LARGE\RAWE32L.LIB name AWETEST.EXE
if errorlevel 1 goto done

echo.
echo Done. AWETEST.EXE is a plain DOS program - no extender needed.
:done

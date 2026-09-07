@echo off
cd /d "%~dp0"
rem ---------------------------------------------------------------------------
rem  Build AWETEST.EXE as a 32-bit protected mode program with the DOS
rem  extender bound into the executable, so it stays a single file to copy.
rem
rem  Why protected mode: the capture has to live in memory instead of being
rem  streamed to a disk that cannot keep up, and 640 kB is nowhere near
rem  enough for that. The SDK ships a flat model library (PAWE32.LIB) next
rem  to the real mode ones, so the AWE32 calls are available here as well.
rem
rem  DOS/32A is bound in as the executable's stub - that is what makes it a
rem  single file. It has to be DOS/32A and not DOS/4GW: DOS4GW.EXE is itself
rem  an LE program behind an MZ loader, so the linker embeds only its 62 kB
rem  MZ part and the extender never starts. DOS32A.EXE is a plain MZ program
rem  and goes in whole. Do not use WBIND either - that binds the Windows 386
rem  supervisor, not a DOS extender.
rem ---------------------------------------------------------------------------
if "%WATCOM%"=="" set WATCOM=C:\prenos\OpenWatcom
if "%AWESDK%"=="" set AWESDK=C:\prenos\awesdk

set PATH=%WATCOM%\binnt64;%WATCOM%\binnt;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h
set EDPATH=%WATCOM%\eddat

echo === compiling (32-bit flat) ===
wcc386 -bt=dos -oilms -DSBKLIB -i=%AWESDK%\LIB\SBKLIB\INCLUDE -fo=awetest.obj AWETEST.C
if errorlevel 1 goto done

echo === linking, extender bound in as the stub ===
wlink system dos4g option stub=%WATCOM%\binw\dos32a.exe file awetest.obj library %AWESDK%\LIB\SBKLIB\FLAT\PAWE32.LIB name AWETEST.EXE
if errorlevel 1 goto done

echo.
echo Done. AWETEST.EXE carries its own extender - still one file to copy.
:done

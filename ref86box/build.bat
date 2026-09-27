@echo off
rem Builds the standalone 86Box EMU8000 reference renderer emu8k_ref.exe.
rem Compiles the chip from the 86Box tree (the VM copy). AWE32Emu has its own
rem copy in AWE32Emu\src\86box; chipcheck.py compares the two outputs.
rem `upstream\snd_emu8k.c` stays an untouched copy of 86Box master for diffs.
rem The 86Box tree is expected in ..\docs\86box-src\master-full (see build_86box.sh).
setlocal
set AWE32EMU_DATA=%~dp0..
set BUILD=%AWE32EMU_DATA%\ref86box\build
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%BUILD%" mkdir "%BUILD%"
rem Careful: a backslash right before a quote would escape it in cmd,
rem so a forward slash is used here - cl accepts it the same.
cl /nologo /O2 /MD /Fo:"%BUILD%/" /Fe:"%BUILD%/emu8k_ref.exe" ^
   /I "%~dp0include" ^
   /D_CRT_SECURE_NO_WARNINGS ^
   /wd4244 /wd4267 /wd4996 ^
   "%AWE32EMU_DATA%\docs\86box-src\master-full\src\sound\snd_emu8k.c" "%~dp0harness.c"
endlocal

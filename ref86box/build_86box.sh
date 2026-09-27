#!/bin/sh
# ---------------------------------------------------------------------------
# Builds 86Box master with our EMU8000 instrumentation (port-write trace,
# instruction tracer, WAV hook, state dump - see 86box-patch/).
#
# Runs in the MSYS2 / MINGW64 environment. 86Box is written for GCC (it uses
# e.g. __attribute__((always_inline))), so it cannot be built with MSVC -
# the official Windows builds go through MinGW and so do we.
#
# Dependencies are installed as ready packages, nothing is built from source:
#
#   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
#       mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf \
#       mingw-w64-x86_64-SDL2 mingw-w64-x86_64-freetype \
#       mingw-w64-x86_64-libpng mingw-w64-x86_64-libsndfile \
#       mingw-w64-x86_64-libslirp mingw-w64-x86_64-zlib
#
# Call from Windows:
#   C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=MINGW64 /c/prenos/AWE32EmuData/ref86box/build_86box.sh"
#
# The 86Box source tree is a plain clone of 86Box at the commit in
# 86box-patch/86box-commit.txt with 86box-patch/awe32emu.patch applied and the
# new files of 86box-patch/src/ copied in.
# ---------------------------------------------------------------------------
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
# The 86Box source and the build are not in the repository - they lie in the
# data directory next to this folder.
DATA="$HERE/.."
SRC="$DATA/docs/86box-src/master-full"
BUILD="${AWE32_BUILDDIR:-$DATA/ref86box/build86box}"

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DRELEASE=ON \
    -DDYNAREC=${AWE32_DYNAREC:-ON} \
    -DQT=OFF \
    -DSDL2=ON \
    -DOPENAL=OFF \
    -DRTMIDI=OFF \
    -DFLUIDSYNTH=OFF \
    -DMUNT=OFF \
    -DSOUNDCANVAS=OFF \
    -DDISCORD=OFF \
    -DVNC=OFF

cmake --build "$BUILD"

echo
echo "Done: $BUILD/86Box.exe"

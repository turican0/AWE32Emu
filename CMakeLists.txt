cmake_minimum_required(VERSION 3.16)
project(AWE32Emu LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Build without Visual Studio - for CI and Linux.
#
# On Windows `AWE32Emu.sln` stays the main path; this CMake builds the same
# binary:
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# The chip from 86Box (`--chip 86box`) is in the repository
# (AWE32Emu/src/86box, GPL-2.0-or-later); -DAWE32EMU_WITH_86BOX=OFF builds
# without it and `--chip 86box` then reports that it is missing.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AWE32EMU_WITH_86BOX "Include the chip core from 86Box (--chip 86box)" ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/AWE32Emu/src)

set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/Emu8000.cpp
    ${SRC_DIR}/Emu8000Box.cpp
    ${SRC_DIR}/MidiFile.cpp
    ${SRC_DIR}/Sequencer.cpp
    ${SRC_DIR}/SoundFont.cpp
    ${SRC_DIR}/SoundFontExport.cpp
    ${SRC_DIR}/Synth.cpp
    ${SRC_DIR}/XmiFile.cpp
)

if(WIN32)
    list(APPEND SOURCES ${SRC_DIR}/AudioOutputWin.cpp)
endif()

add_executable(AWE32Emu ${SOURCES})
target_include_directories(AWE32Emu PRIVATE ${SRC_DIR})

if(WIN32)
    target_link_libraries(AWE32Emu PRIVATE winmm)
    target_compile_definitions(AWE32Emu PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(AWE32EMU_WITH_86BOX)
    target_sources(AWE32Emu PRIVATE ${SRC_DIR}/86box/snd_emu8k.c)
    target_include_directories(AWE32Emu PRIVATE ${SRC_DIR}/86box/include)
    target_compile_definitions(AWE32Emu PRIVATE AWE32EMU_WITH_86BOX)
endif()

install(TARGETS AWE32Emu RUNTIME DESTINATION .)

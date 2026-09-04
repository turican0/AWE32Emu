cmake_minimum_required(VERSION 3.16)
project(AWE32Emu LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Sestaveni bez Visual Studia - pro CI a pro Linux.
#
# Ve Windows zustava hlavni cestou `AWE32Emu.sln` (soubor .vcxproj je bohatsi:
# pripojuje `snd_emu8k.c` primo z datoveho adresare). Tenhle CMake je vedle nej
# a dela **tutez binarku**, jen bez zavislosti na veci, ktera v repozitari
# neni:
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# Jadro z 86Boxu (`--chip 86box`) potrebuje `snd_emu8k.c` z datoveho adresare
# `AWE32EmuData`, ktery je mimo repozitar. Zapina se rucne:
#
#   cmake -B build -DAWE32EMU_WITH_86BOX=ON -DAWE32EMU_DATA=../AWE32EmuData
#
# Bez nej se prelozi vsechno ostatni a `--chip 86box` jen ohlasi, ze v tehle
# binarce neni.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AWE32EMU_WITH_86BOX "Prilozit jadro cipu z 86Boxu (--chip 86box)" OFF)
set(AWE32EMU_DATA "${CMAKE_CURRENT_SOURCE_DIR}/../AWE32EmuData"
    CACHE PATH "Datovy adresar se zdrojaky 86Boxu")

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
    set(EMU8K_C "${AWE32EMU_DATA}/docs/86box-src/master-full/src/sound/snd_emu8k.c")
    if(NOT EXISTS "${EMU8K_C}")
        message(FATAL_ERROR
            "AWE32EMU_WITH_86BOX=ON, ale nenasel jsem ${EMU8K_C}. "
            "Nastav -DAWE32EMU_DATA=<cesta k AWE32EmuData>.")
    endif()
    target_sources(AWE32Emu PRIVATE "${EMU8K_C}")
    target_include_directories(AWE32Emu PRIVATE "${AWE32EMU_DATA}/ref86box/include")
    target_compile_definitions(AWE32Emu PRIVATE AWE32EMU_WITH_86BOX)
endif()

install(TARGETS AWE32Emu RUNTIME DESTINATION .)

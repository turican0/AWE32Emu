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
# Options:
#   AWE32EMU_WITH_86BOX    the chip from 86Box (`--chip 86box`,
#                          AWE32Emu/src/86box, GPL-2.0-or-later); without it
#                          `--chip 86box` reports that it is missing
#   AWE32EMU_WITH_RTAUDIO  live playback through RtAudio (`--audio rtaudio`);
#                          uses an installed RtAudio or downloads 6.0.1 from
#                          GitHub. On Linux it needs the ALSA and/or
#                          PulseAudio development packages.
#   AWE32EMU_NLS           translated messages through gettext (po/*.po);
#                          switched off by itself where libintl or msgfmt is
#                          missing (MSVC)
#
# The BASS output (`--audio bass`) needs nothing at build time - the library
# is loaded at run time. Targets `pot` and `update-po` refresh the message
# catalogue (xgettext, msgmerge).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AWE32EMU_WITH_86BOX "Include the chip core from 86Box (--chip 86box)" ON)
option(AWE32EMU_WITH_RTAUDIO "Live playback through RtAudio (--audio rtaudio)" ON)
option(AWE32EMU_NLS "Translate messages with gettext" ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/AWE32Emu/src)

set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/AudioOutput.cpp
    ${SRC_DIR}/AudioOutputBass.cpp
    ${SRC_DIR}/Emu8000.cpp
    ${SRC_DIR}/Emu8000Box.cpp
    ${SRC_DIR}/I18n.cpp
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

find_package(Threads REQUIRED)
target_link_libraries(AWE32Emu PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(UNIX)
    target_link_libraries(AWE32Emu PRIVATE m)
endif()

# --- RtAudio ---------------------------------------------------------------
if(AWE32EMU_WITH_RTAUDIO)
    find_package(RtAudio CONFIG QUIET)
    if(TARGET RtAudio::rtaudio)
        message(STATUS "RtAudio: installed package")
        set(AWE32EMU_RTAUDIO_TARGET RtAudio::rtaudio)
    else()
        message(STATUS "RtAudio: building 6.0.1 from GitHub")
        include(FetchContent)
        set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(RTAUDIO_API_ASIO OFF CACHE BOOL "" FORCE)
        set(RTAUDIO_STATIC_MSVCRT OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(rtaudio
            GIT_REPOSITORY https://github.com/thestk/rtaudio.git
            GIT_TAG 6.0.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(rtaudio)
        set(AWE32EMU_RTAUDIO_TARGET rtaudio)
    endif()
    target_sources(AWE32Emu PRIVATE ${SRC_DIR}/AudioOutputRt.cpp)
    target_link_libraries(AWE32Emu PRIVATE ${AWE32EMU_RTAUDIO_TARGET})
    target_compile_definitions(AWE32Emu PRIVATE AWE32EMU_WITH_RTAUDIO)
endif()

# --- gettext ---------------------------------------------------------------
set(PO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/po)
find_program(XGETTEXT_EXECUTABLE xgettext)
find_program(MSGMERGE_EXECUTABLE msgmerge)

if(XGETTEXT_EXECUTABLE)
    file(GLOB I18N_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
         ${SRC_DIR}/*.cpp ${SRC_DIR}/*.h)
    list(SORT I18N_SOURCES)
    add_custom_target(pot
        COMMAND ${XGETTEXT_EXECUTABLE} --c++ --from-code=UTF-8
                --keyword=_ --keyword=N_ --add-comments=TRANSLATORS:
                --package-name=AWE32Emu --msgid-bugs-address=https://github.com/turican0/AWE32Emu/issues
                --sort-by-file -o po/awe32emu.pot ${I18N_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Extracting messages into po/awe32emu.pot")
endif()

file(GLOB PO_FILES ${PO_DIR}/*.po)

if(MSGMERGE_EXECUTABLE)
    set(UPDATE_PO_COMMANDS)
    foreach(po ${PO_FILES})
        list(APPEND UPDATE_PO_COMMANDS
             COMMAND ${MSGMERGE_EXECUTABLE} --update --backup=none ${po} ${PO_DIR}/awe32emu.pot)
    endforeach()
    add_custom_target(update-po ${UPDATE_PO_COMMANDS}
        COMMENT "Merging po/awe32emu.pot into po/*.po")
endif()

if(AWE32EMU_NLS)
    find_package(Intl)
    find_package(Gettext)
    if(Intl_FOUND AND GETTEXT_FOUND)
        target_compile_definitions(AWE32Emu PRIVATE AWE32EMU_NLS)
        target_include_directories(AWE32Emu PRIVATE ${Intl_INCLUDE_DIRS})
        target_link_libraries(AWE32Emu PRIVATE ${Intl_LIBRARIES})

        # Catalogues go to locale/<lang>/LC_MESSAGES/awe32emu.mo next to the
        # executable - that is where I18n::Init looks.
        set(MO_FILES)
        foreach(po ${PO_FILES})
            get_filename_component(lang ${po} NAME_WE)
            set(mo ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES/awe32emu.mo)
            add_custom_command(OUTPUT ${mo}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES
                COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${mo} ${po}
                DEPENDS ${po})
            list(APPEND MO_FILES ${mo})
        endforeach()
        add_custom_target(awe32emu_mo ALL DEPENDS ${MO_FILES})
        if(CMAKE_CONFIGURATION_TYPES)
            add_custom_command(TARGET AWE32Emu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        ${CMAKE_BINARY_DIR}/locale $<TARGET_FILE_DIR:AWE32Emu>/locale)
            add_dependencies(AWE32Emu awe32emu_mo)
        endif()
        install(DIRECTORY ${CMAKE_BINARY_DIR}/locale DESTINATION . COMPONENT awe32emu)
        message(STATUS "gettext: messages translated (${PO_FILES})")
    else()
        message(STATUS "gettext: libintl or msgfmt not found - messages stay English")
    endif()
endif()

# Own component, so that `cmake --install --component awe32emu` skips the
# install rules of the fetched RtAudio (library, headers, pkg-config).
install(TARGETS AWE32Emu RUNTIME DESTINATION . COMPONENT awe32emu)

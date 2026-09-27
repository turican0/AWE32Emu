#!/usr/bin/env python
"""Removes the libslirp lookup from src/network/CMakeLists.txt in 86Box.

Called from ref86box/apply_noslirp.sh. The file has CRLF in the repository,
so it is compared on normalised text and the line ends are restored at the
end.
"""
import io
import sys

BLOCK = """find_package(PkgConfig REQUIRED)
pkg_check_modules(SLIRP REQUIRED IMPORTED_TARGET slirp)
target_link_libraries(86Box PkgConfig::SLIRP)

if(WIN32)
    target_link_libraries(PkgConfig::SLIRP INTERFACE wsock32 ws2_32 iphlpapi)
    if (NOT MSVC)
        target_link_libraries(PkgConfig::SLIRP INTERFACE iconv)
    endif()
    if(STATIC_BUILD)
        add_compile_definitions(LIBSLIRP_STATIC)
    endif()
endif()"""

REPLACEMENT = "# AWE32Emu: libslirp removed, see ref86box/apply_noslirp.sh"


def main():
    path = sys.argv[1]
    raw = io.open(path, encoding="utf-8", newline="").read()
    crlf = "\r\n" in raw
    text = raw.replace("\r\n", "\n")

    if BLOCK not in text:
        if REPLACEMENT in text:
            print("CMakeLists.txt is already patched")
        else:
            print("warning: the libslirp block in CMakeLists.txt not found")
        return 0

    text = text.replace(BLOCK, REPLACEMENT, 1)
    if crlf:
        text = text.replace("\n", "\r\n")
    io.open(path, "w", encoding="utf-8", newline="").write(text)
    print("CMakeLists.txt patched")
    return 0


if __name__ == "__main__":
    sys.exit(main())

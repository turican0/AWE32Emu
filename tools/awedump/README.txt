AWEDUMP - EMU8000 ROM dumper for DOS
=====================================

Files:
  AWEDUMP.COM  - the compiled DOS program (ready to run)
  awedump.asm  - full source (NASM syntax), for review/modification
  README.txt   - this file

Usage (from real DOS, on the machine with the physical AWE32/CT3900):

  AWEDUMP [hexbase]

  hexbase = EMU8000 base I/O port in hex (e.g. 620). If omitted, 0x620
  is used. Find your card's real base from the "Ex" field of the
  BLASTER environment variable (set by CTCM.EXE), or from
  DIAGNOSE.EXE / CTCU.EXE.

Output: AWE32ROM.BIN, exactly 1,048,576 bytes, written to the current
directory.

To rebuild from source (needs NASM: https://www.nasm.us):
  nasm -f bin awedump.asm -o AWEDUMP.COM

Notes:
  - Talks directly to hardware I/O ports - run it in real DOS (or a
    DOS environment with genuine ISA/EMU8000 port passthrough) on the
    machine that physically has the AWE32 installed.
  - Built-in bounded timeouts mean it will report an error and exit
    cleanly instead of hanging if the base port is wrong or no card
    responds.
  - Register-level logic is translated from the AWE32/EMU8000
    Programmer's Guide (Dave Rossum) and the GPL-licensed Linux ALSA
    driver (sound/isa/sb/emu8000.c, emu8000_reg.h). No proprietary
    sample/ROM data is included in these files.

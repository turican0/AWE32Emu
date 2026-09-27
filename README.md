# AWE32Emu

An emulation of the **Sound Blaster AWE32** music synthesis — the **EMU8000**
chip together with the logic of Creative's own drivers — that plays
`.mid` / `.xmi` music **the way the real card did**, not through a generic
softsynth.

Two things are emulated, and each is checked against its own reference:

| layer | what it does | matched against |
|---|---|---|
| **driver** | turns MIDI into EMU8000 register writes (voice allocation, SoundFont conversion, volume curves, pitch, filter, effect sends) | the original Creative drivers — `SBAWE.VXD` (Windows 95), `SBAWE32.MDI` (DOS games, Miles/AIL) and the AWE32 DOS SDK — run inside 86Box and traced register by register: **every register at every note-on matches** |
| **chip** | the EMU8000 itself: sample playback, interpolation, envelopes, LFOs, the resonant filter, reverb, chorus, EQ | **recordings of a real AWE32** made with a calibration program written for this project ([AWETEST](tools/awetest/README.md)) |

## Listen: Magic Carpet 2, real card vs AWE32Emu

Short excerpts, the real Sound Blaster AWE32 (line output, recorded by a
tester) next to AWE32Emu's render of the same music with the game's own
driver family (`--driver dos`) and bank:

| piece | real AWE32 | AWE32Emu |
|---|---|---|
| intro | [mc2_intro_card.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_intro_card.flac) | [mc2_intro_awe32emu.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_intro_awe32emu.flac) |
| menu | [mc2_menu_card.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_menu_card.flac) | [mc2_menu_awe32emu.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_menu_awe32emu.flac) |
| level 1 | [mc2_level1_card.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_level1_card.flac) | [mc2_level1_awe32emu.flac](https://github.com/turican0/AWE32Emu/raw/sources/demos/mc2_level1_awe32emu.flac) |

The excerpts are aligned and matched in loudness; the card recordings carry
their recording chain (the level 1 recording is noticeably darker than the
card — see [`demos/README.md`](https://github.com/turican0/AWE32Emu/blob/sources/demos/README.md)
in the `sources` branch). Magic Carpet 2 music © Bullfrog Productions / Electronic Arts;
the excerpts are included for comparison only.

## Features

- `.mid` (SMF format 0/1) and `.xmi` (Miles XMIDI) playback with a tempo-mapped
  sequencer
- SoundFont banks: `.SBK` (SoundFont 1.0), `.SF2`, and the GM presets
  compiled into the DOS driver `SBAWE32.MDI`; layered in load order, samples
  uploaded into the emulated sound DRAM
- the card's 1 MB wave ROM (`--rom`), including banks that only describe the
  ROM contents
- several banks in different MIDI bank slots (`--sf bank.sbk@1`), as the
  Creative control panel does
- three driver families (`--driver win95|dos|sdk`) — they really differ
  (init arrays, velocity table, attenuation formula, voice allocation,
  pitch computation, DRAM layout) and are not interchangeable
- the chip: 32 voices, cubic B-spline interpolation, six-stage volume and
  modulation envelopes (with the measured attack shapes), two LFOs, a
  Chamberlin resonant filter with the measured cutoff scale, bass/treble
  EQ, reverb (8 presets) and chorus (8 presets) fitted to the card's line
  output, the card's output headroom
- the initial MIDI state a game sends (`--conf`, e.g.
  [`conf/mc2.conf`](conf/mc2.conf)) and the AIL master volume
- SBK → SF2 conversion (`--export-sf2`) that converts units and semantics,
  ROM samples included
- `.wav` rendering (all platforms) and live playback (Windows)
- tools for verification: port-write traces (`--trace`), trace replay
  (`--replay`), per-note register dumps (`--dump-notes`)

## Quick start

You need the card's wave ROM `awe32.raw` — the dump used by PCem/86Box, e.g.
from [libretro-pcem](https://github.com/libretro/libretro-pcem/blob/master/awe32.raw)
(or dump your own card with [`tools/awedump`](tools/awedump/README.txt)) —
and a bank: `SYNTHGM.SBK` from the AWE32 driver disks, `SBAWE32.MDI` from a
DOS game, or any SoundFont.

```bash
# a General MIDI song with the Windows 95 driver family and the card's GM bank
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --wav out.wav

# a DOS game: GM presets from the game's driver plus the game's own bank
AWE32Emu song.xmi --rom awe32.raw --sf SBAWE32.MDI --sf GAME.SBK \
    --driver dos --wav out.wav

# Magic Carpet 2 with the start-up state the game sends
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf SBAWE32.MDI --sf BULLFROG.SBK \
    --driver dos --conf conf/mc2.conf --wav intro.wav

# convert an SBK bank (including its ROM samples) into a standalone SF2
AWE32Emu --rom awe32.raw --sf GAME.SBK --export-sf2 game.sf2
```

Without `--wav` the song plays live (Windows). Every option is explained in
[docs/USAGE.md](docs/USAGE.md).

## Building

Prebuilt Windows and Linux binaries are attached to the
[releases](https://github.com/turican0/AWE32Emu/releases); pushing a tag
starting with `v` builds them ([release.yml](.github/workflows/release.yml)).

**Windows, Visual Studio 2022:** open `AWE32Emu.sln`, build `Release|x64`;
the binary lands in `bin\x64\Release\`.

**CMake (Windows, Linux):**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

No external libraries. `-DAWE32EMU_WITH_86BOX=OFF` builds without the 86Box
chip core (then only the older own core, `--chip ours`, is available).

## How it is verified

- **Driver layer:** our own build of 86Box records every write a real
  Creative driver makes to the EMU8000 inside a Windows 95 or DOS virtual
  machine; an instruction tracer in it was used to reverse-engineer the
  note-on routines. The test matrix renders the same songs through our
  driver layer and compares every register at every note: all match for all
  three families (GM songs through Media Player, Magic Carpet 2 through its
  own Miles driver, DOSMid through the AWE32 SDK). The initialisation matches
  `AWEUTIL.COM` write for write (1611/1611).
- **Chip:** AWETEST plays about an hour of calibration tones on a real card —
  every tone changes one register. The same program runs in the VM with a
  trace, the trace is replayed through our chip, and card and render are
  compared block by block. Interpolation, filter, envelopes, EQ, reverb,
  chorus and the output headroom were fitted this way.
- **Emulator vs VM:** the emulator and the VM run the same `snd_emu8k.c`; a
  chip check over all AWETEST blocks and VM state dumps against replays
  confirm they compute identical output.

Details: [docs/TESTING.md](docs/TESTING.md). What was found and how:
[docs/re-notes/](docs/re-notes/).

## Repository

The `main` branch:

```
AWE32Emu.sln, CMakeLists.txt   build
AWE32Emu/src/
  main.cpp                     command line
  MidiFile, XmiFile            .mid / .xmi parsers
  Sequencer                    ticks -> time, dispatch into Synth
  Synth                        the driver layer: MIDI -> EMU8000 registers
  Awe32Driver.h                what differs between the driver families
  Awe32Curves.h                volume/velocity/expression tables and formulas
  Awe32InitArrays.h            the INIT1..INIT4 init arrays as the drivers send them
  SoundFont, SoundFontExport   .sbk/.sf2/.mdi loader, SF2 writer
  Emu8000Box                   adapter to the 86Box chip core
  86box/snd_emu8k.c            the EMU8000 core: 86Box + measured corrections (GPL)
  Emu8000, Emu8000Effects,
  Emu8000Regs                  the older own core (--chip ours) and the register map
  WavWriter, AudioOutputWin    output
conf/mc2.conf                  Magic Carpet 2 start-up state
tools/awetest/                 AWETEST — the calibration program for a real card (DOS)
tools/awedump/                 AWEDUMP — dumps the wave ROM of a real card (DOS)
docs/                          usage, testing, development notes, TODO, research notes
```

The **`sources` branch** holds the tester's recordings, the demo excerpts and
all research tools (trace comparison, the test matrix, driver
disassemblers, the 86Box instrumentation, the analysis scripts) — see
[docs/DATA.md](docs/DATA.md).

Not in the repository and never published: the wave ROM, Creative's drivers
and banks, the SDK, game music, VM images.

## Documentation

| | |
|---|---|
| [USAGE.md](docs/USAGE.md) | every option with examples |
| [TESTING.md](docs/TESTING.md) | how the emulation is verified |
| [DATA.md](docs/DATA.md) | the `sources` branch and what is where |
| [DEVELOPMENT.md](docs/DEVELOPMENT.md) | rules and traps |
| [TODO.md](docs/TODO.md) | roadmap and status |
| [re-notes/](docs/re-notes/) | register map, driver reverse engineering, chip tuning log |

## Open points

- XMIDI loops (`RBRN`, CC116/117) and SysEx are not interpreted
- live output is Windows-only (WinMM); a portable backend is wanted
- the project is a command-line program; a reusable library API is planned
- small chip details that the recordings cannot decide (see the end of
  [emu8000_tuning.md](docs/re-notes/emu8000_tuning.md))

## Credits and license

- The EMU8000 core `AWE32Emu/src/86box/snd_emu8k.c` comes from
  [86Box](https://github.com/86Box/86Box) and is licensed under the **GNU
  General Public License, version 2 or later**; our changes to it are marked
  `AWE32Emu:`. A build that includes it (the default) is a derivative work of
  86Box and is distributed under the GPL.
- Register map and behaviour: the AWE32/EMU8000 Programmer's Guide (Dave
  Rossum, E-mu/Creative), the Linux ALSA driver (used as a reference, not as
  code), and our own analysis of Creative's drivers. No driver code is
  copied; a few small data tables are included because the emulation must
  reproduce them exactly — the INIT1..INIT4 arrays the drivers send to the
  chip (`Awe32InitArrays.h`, also published in the ALSA driver) and the
  volume/velocity/expression curves (`Awe32Curves.h`). The envelope time
  tables are reproduced by formulas.
- Thanks to the tester who ran AWETEST on a real Sound Blaster AWE32 many
  times.
- The license of the rest of the project is to be decided.

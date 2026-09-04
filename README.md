# AWE32Emu

Register-level emulation of the **EMU8000** sound chip (Sound Blaster AWE32),
built by combining the published chip specification with reverse engineering of
Creative's own drivers (`SBAWE.VXD`, `SBAWE32.MDI`, `SBAWE32.DRV`, `AWEUTIL.COM`).
It plays `.mid` / `.xmi` music the way the card did, rather than through a
generic softsynth.

**A detailed usage guide with worked examples is in
[`docs/POUZITI.md`](docs/POUZITI.md)** (Czech).

## What works

- `.mid` (SMF format 0/1) and `.xmi` (Miles/AIL) parsing, tempo-mapped sequencer
- **SoundFont banks are fully wired**: `.SBK` (SoundFont 1.0) and `.SF2`,
  layered in load order, samples uploaded into the emulated sound DRAM, and
  generators translated into EMU8000 registers using conversions measured
  against the real drivers
- Wave ROM support (`--rom`), including banks that only *describe* ROM content
- Multiple banks in different MIDI bank slots (`--sbk bank.sbk@1`), as the
  Creative control panel does
- Both driver families (`--driver dos|win95`) — they differ in eight init-array
  values, the velocity table and the attenuation formula, and are not
  interchangeable
- 32-voice playback with six-stage volume and modulation envelopes, both LFOs,
  a resonant low-pass filter, panning, and the global reverb/chorus buses
- **The unmodified `snd_emu8k.c` from 86Box as an alternative core**
  (`--chip 86box`) — literally the same file that gets compiled into the
  emulator, so any difference is a difference in our code, not in transcription
- SBK → SF2 conversion (`--export-sf2`), converting units and semantics rather
  than just repackaging
- Offline rendering to `.wav`, per-note register dumps (`--dump-notes`) and
  port-write traces (`--trace`) for regression testing against real hardware
  recordings
- Live playback on Windows via `winmm`; on Linux the renderer works and live
  output does not

## Building

### Visual Studio (Windows)

Open `AWE32Emu.sln`, pick `Release|x64`, build. The result lands in
`bin\x64\Release\`. No external libraries. This build includes the 86Box core,
which it compiles straight out of the data directory (`../AWE32EmuData`, or
wherever `AWE32EMU_DATA` points).

### CMake (Windows and Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This build does **not** include the 86Box core — `snd_emu8k.c` lives in the
data directory, which is not part of this repository. Everything else is
identical. To include it:

```bash
cmake -B build -DAWE32EMU_WITH_86BOX=ON -DAWE32EMU_DATA=../AWE32EmuData
```

### Prebuilt binaries

Pushing a tag that starts with `v` builds Windows and Linux binaries and
attaches them to a GitHub release — see `.github/workflows/release.yml`.

## Quick examples

```bash
# render with the general MIDI bank out of the card's wave ROM
AWE32Emu song.mid --rom awe32.raw --sbk SYNTHGM.SBK --wav out.wav

# a game's own bank layered on top of GM, DOS driver family
AWE32Emu song.xmi --rom awe32.raw --sbk SYNTHGM.SBK --sbk GAME.SBK \
    --driver dos --wav out.wav

# convert an SBK bank (including its ROM samples) into a standalone SF2
AWE32Emu --rom awe32.raw --sbk GAME.SBK --export-sf2 game.sf2
```

See [`docs/POUZITI.md`](docs/POUZITI.md) for every option, what it means and
when to reach for it.

## Project structure

```
AWE32Emu.sln            Visual Studio solution
CMakeLists.txt          portable build (CI, Linux)
AWE32Emu/src/
  main.cpp                CLI entry point, argument parsing
  MidiTypes.h             shared MIDI event representation
  MidiFile.h/.cpp         .mid (SMF) parser
  XmiFile.h/.cpp          .xmi parser
  Sequencer.h/.cpp        ticks -> real time, dispatch into Synth
  Synth.h/.cpp            MIDI -> EMU8000 register translation
  Awe32Driver.h           differences between the two driver families
  Awe32Curves.h           volume/velocity/expression tables from the drivers
  Emu8000Regs.h           register map, port + sel encoding, unit constants
  Emu8000.h/.cpp          the chip core
  Emu8000Effects.h        reverb and chorus buses
  Emu8000Box.h/.cpp       wrapper around 86Box's snd_emu8k.c
  SoundFont.h/.cpp        .sbk / .sf2 loader and generator conversion
  SoundFontExport.h/.cpp  SF2 writer (--export-sf2)
  WavWriter.h             offline .wav output
  AudioOutputWin.h/.cpp   realtime output via WinMM (Windows only)
docs/
  POUZITI.md              usage guide
  re-notes/               what was found out about the chip and drivers, and how
```

## Open points

- The ~4.5 dB treble shelf above 12.8 kHz seen against hardware recordings is
  still unexplained; it is not interpolation, reverb, chorus or the filter
- SysEx handling (GS/GM/MT-32 reset), running status in XMI, SMF SMPTE division
- Everything is still tied into the CLI; splitting the core into a reusable
  library is the next step

## Note on data (licensing/legality)

This repository **deliberately contains no input data**:

- no `.mid`/`.xmi` files from specific games (copyright)
- no `.sbk`/`.sf2` banks (original Creative/E-mu banks are protected)
- no DOS driver binaries (AWEUTIL, CTVDSK.SYS, etc.) or their disassembly output
- no data tables lifted out of Creative drivers. The EMU8000 init arrays
  (INIT1..INIT4) found in AWEUTIL were deliberately **not** copied into the
  code: they configure the real chip's internal reverb/chorus DSP and mean
  nothing to a software emulation. See the note in
  `docs/re-notes/emu8000_register_map.md`

`.gitignore` excludes these file types. For local testing, download them
yourself (see the VOGONS Driver Library for DOS drivers) and keep them out of
the committed repository history.

## License

The code in this repository is an original implementation, not derived from
any DOS driver or existing SoundFont player. Project license: to be decided
(e.g. MIT) before publishing.

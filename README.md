# AWE32Emu

Foundation for a project aiming at a real emulation of the **EMU8000** sound
chip (Sound Blaster AWE32), built by combining the official chip specification
with reverse engineering of the DOS drivers (AWEUTIL, CTVDSK.SYS, etc.). The
end goal is a C++ plugin that can be embedded into a game and play `.mid`/`.xmi`
music through a register-accurate EMU8000 emulation instead of a generic
softsynth.

**This is a basic skeleton, not a finished emulation.** Current status and
what does (not) work yet is described below in [Project status](#project-status).

## What works right now

- Visual Studio 2022 console application (x64, C++17)
- `.mid` parser (Standard MIDI File, format 0/1)
- `.xmi` parser (Miles Sound System / AIL format - IFF container, `EVNT`
  chunk, conversion of XMI note-length encoding into explicit Note Off events)
- Basic sequencer that converts file ticks into real time using the tempo map
- Optional `.sbk` (SoundFont/E-mu bank) loading - informational only for now,
  it lists the RIFF chunks found; the data is not yet used during playback
- Playback via Windows `winmm`/`waveOut` (no external dependencies)
- **Register-level EMU8000 core** (`Emu8000.cpp`) built on the register map
  decoded from the AWEUTIL.COM disassembly - see
  `docs/re-notes/emu8000_register_map.md`. It implements the real
  Pointer/Data0..Data3 port scheme, the documented register file (CPF, PTRX,
  CVCF, VTFT, PSST, CSL, CCCA, ENVVOL/DCYSUSV/ENVVAL/DCYSUS, ATKHLDV/ATKHLD,
  LFO1VAL/LFO2VAL, IP, IFATN, PEFE, FMMOD, TREMFRQ, FM2FRQ2, HWCF1-7, SM*),
  the driver's power-on init sequence, 32-voice wavetable playback out of an
  emulated sound DRAM, six-stage volume and modulation envelopes, both LFOs,
  a resonant low-pass filter and panning
- **No sample data yet** - until a SoundFont/SBK bank is wired in, the synth
  uploads a generated sine table into the emulated DRAM and plays that. The
  whole chip path (pitch, loop points, envelopes, filter, pan) is real; only
  the contents of sound memory are a stand-in
- Offline rendering to `.wav` (`--wav`) for regression tests and A/B
  comparison against reference recordings

## Building

1. Open `AWE32Emu.sln` in Visual Studio 2022
2. Select the `Release` / `x64` configuration (or `Debug` / `x64`)
3. Build - the resulting `AWE32Emu.exe` will be in `bin\x64\Release\`

No external libraries or vcpkg packages are required - the project only uses
standard C++17 and the Windows SDK (`winmm.lib`, linked automatically).

## Usage

```
AWE32Emu.exe song.mid
AWE32Emu.exe song.xmi
AWE32Emu.exe song.mid --sbk bank.sbk
AWE32Emu.exe song.mid --wav out.wav
```

## Project structure

```
AWE32Emu.sln
AWE32Emu/
  AWE32Emu.vcxproj
  src/
    main.cpp               CLI entry point, argument parsing, main loop
    MidiTypes.h              Shared MIDI event representation (MidiEvent, ParsedSequence)
    MidiFile.h/.cpp           .mid (SMF) parser
    XmiFile.h/.cpp            .xmi parser
    Sequencer.h/.cpp          Converts ticks to real time, dispatches events to Synth
    Emu8000Regs.h             EMU8000 register map / port + sel encoding
    Synth.h/.cpp              MIDI -> EMU8000 register translation layer
    WavWriter.h               Offline .wav output (--wav)
    SoundFontSbk.h/.cpp        Basic RIFF loader for .sbk/.sf2
    AudioOutputWin.h/.cpp      Realtime output via WinMM waveOut
```

## Project status

A detailed, more granular task list (parsers, sequencer, EMU8000 core,
SoundFont layer, plugin API, testing, DOS driver reverse engineering in IDA)
lives outside this repository, in the project's TODO document. The most
important open points directly in this code:

- `SoundFontSbk.cpp` - wire up the loaded data (`shdr`, `phdr`, `sdta`/`smpl`)
  to actual sample playback: upload samples into the emulated DRAM and
  translate SoundFont generators into EMU8000 register values
- `Emu8000.cpp` - the envelope time constants, filter cutoff curve, LFO and
  pitch-modulation scalings are currently the values published for the
  EMU8000, marked `[DOC]` / `[?]` in the code. They still need to be checked
  against `SBAWE32.DRV` (the Windows AWE32 MIDI driver), which is where the
  SoundFont-generator to register conversion lives
- Chorus and reverb - the per-voice sends are already stored in the registers,
  the two global effect buses are not implemented yet
- `MidiFile.cpp`/`XmiFile.cpp` - SysEx messages (GS/GM/MT-32 reset), running
  status in XMI, SMF SMPTE division support
- `Sequencer.cpp` - currently renders one frame at a time (`RenderBlock(..., 1)`)
  for timing-accuracy simplicity; can be optimized for performance later
- Plugin API - everything is currently tied into the CLI `main.cpp`; splitting
  it into a reusable library/API is the next step

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

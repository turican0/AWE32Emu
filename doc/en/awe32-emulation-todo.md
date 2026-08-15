# AWE32 / EMU8000 emulation – game plugin (C++), input .mid / .xmi

Goal: C++ plugin that accepts MIDI (.mid) and XMI (.xmi) files, interprets them as a sequence of MIDI events and synthesizes sound using its own register-accurate emulation of the EMU8000 chip

(reverse-engineered with official documentation and DOS driver behavior).

**Architecture note:** The EMU8000 is a fixed-function ASIC without its own programmable
microcode (unlike the later EMU10K1/FX8010 from Sound Blaster Live!). So it is not about
"reversing the program from the chip", but about reconstructing its behavior based on the register
specification + verification/supplementation against the behavior of real hardware (see section 4). Disassembly
of DOS drivers (section 0.1) serves as **supplemental resource** – it shows how AWEUTIL/CTVDSK.SYS
actually writes to the chip registers (order, timing, values), filling in places where the
official documentation is incomplete.

---

## 0. Preparation and materials

- [ ] Get EMU8000 Technical Reference Manual + Programmer's Guide (register map, address scheme)
- [ ] Get/find XMI specification (Miles Sound System / AIL format, differences from SMF)
- [ ] Get reference recordings from real AWE32/AWE64 card (if hardware available) for A/B comparison
- [ ] Map available open-source references (DOSBox-X sources for SB16/MPU-401 layer, TiMidity++, FluidSynth) – purely as inspiration for architecture, not for copying code
- [ ] Decide on the target platform of the plugin (host game/engine format – VST-like API, or internal audio-engine hook?)

### 0.1 DOS driver sources

- [ ] **VOGONS Driver Library** (vogonsdrivers.com) – main community archive of drivers for
vintage hardware, contains complete "Sound Blaster AWE32 Install CD" and separate
"Driver Disk" packages (CTVDSK.SYS, AWEUTIL, MIXERSET, CTCM etc.)
- [ ] **VOGONS forum** (vogons.org) – threads with links to specific versions (e.g. AWEUTIL 1.00
vs. 1.20 – newer version tends to be more reliable, worth getting both for comparison)
- [ ] **Archive.org** – ISO images of installation CDs (e.g. SB32/AWE32 PnP CD), search under
"Sound Blaster AWE32 driver" or specific card model (CT3900, CT3980, CT4520...)
- [ ] Download multiple versions of the same file (drivers differed slightly between card revisions/versions) –
this will help distinguish general chip behavior from quirks of a specific driver version
- [ ] Verify integrity/checksum of downloaded files against multiple sources if possible
- [ ] For broader context: John Miles Audio Interface Library (AIL) – game driver layer
above AWE32, relevant mainly if the target games XMI played through AIL
- [ ] Clarify the licensing model of the output project (what can be included, what not – no original ROM/patch data from the card)

## 1. Parsing input files

### 1.1 Standard MIDI File (.mid)
- [ ] Header parser (MThd) – format 0/1/2, number of tracks, division (ticks/quarter or SMPTE)
- [ ] Stop parser (MTrk) – variable-length quantity decoder
- [ ] MIDI event decoding (Note On/Off, Control Change, Program Change, Pitch Bend, Aftertouch)
- [ ] Meta event decoding (Tempo, Time Signature, Track Name, End of Track)
- [ ] SysEx event decoding (min. GM/GS/XG reset messages, or Creative-specific SysEx for AWE)
- [ ] Merging multiple tracks into one timeline (event queue sorted by tick/delta-time)

### 1.2 XMI (.xmi)
- [ ] Parser of IFF/FORM container (CAT/FORM chunk structure)
- [ ] Parser of XMI-specific chunks (TIMB, EVNT, RBRN)
- [ ] Convert XMI delta-time (different from SMF – different encoding convention) to internal tick representation
- [ ] Processing of "Note On with embedded duration" (XMI specific – Note Off is derived, not explicit event)
- [ ] Support of RBRN (branch points) if the game uses it for looping music
- [ ] Optional: conversion of XMI → internal event stream shared with .mid branch (one common sequencer model)

## 2. Sequencer / timing engine

- [ ] Internal representation of event stream independent of input format
- [ ] Tempo map (support of tempo changes during playback)
- [ ] Precise scheduling of events relative to audio buffer (sample-accurate timing, not just "per callback")
- [ ] Looping support (for game music loops are not the exception, more the rule)
- [ ] API for pause/resume/seek/stop from host game

## 3. MIDI/MPU-401 interpretation layer (channel state machine)

- [ ] 16-channel MIDI state (program, pitch bend range, volume, pan, expression, sustain, RPN/NRPN)
- [ ] GS/XG/GM compatible Bank Select (MSB/LSB) processing – as AWEUTIL/driver did
- [ ] Voice allocation logic (how the real EMU8000 allocated 32 voices between channels when overloaded)
- [ ] Creative-specific SysEx processing (if the game/music uses it)

## 4. EMU8000 emulation core (register-level)

- [ ] Definition of the chip's register map (according to Tech Ref Manual) as an internal data structure
- [ ] Voice engine – 32 independent voices, each with its own state (pitch, sample pointer, loop points)
- [ ] Sample playback core – reading samples from patch/soundfont data, interpolation (EMU8000 used linear interpolation – verify)
- [ ] Envelope generators (volume envelope, modulation envelope) – precise time constants
- [ ] LFO1/LFO2 (tremolo, vibrato, filter modulation)
- [ ] Low-pass filter with resonance (cutoff/Q according to registers) – key point for tuning by ear/measurement
- [ ] Pan/volume mixing of voices to stereo output
- [ ] Chorus effect (algorithm + parameters – the worst documented part, expect empirical tuning)
- [ ] Reverb effect (same)
- [ ] Validation: comparison of output with reference recordings from point 0 (spectral/listening tests)

## 5. Patch/instrument data (SoundFont layer)

- [ ] Loader for SF2 (modern replacement for the original ROM/RAM bank of the card)
- [ ] Mapping of SF2 generators to EMU8000 registers (where it fits 1:1, where not)
- [ ] Support of own/user bank (for emulation of patch recording to RAM via AWEUTIL)
- [ ] Handling of missing/incomplete patches (fallback behavior)

## 6. Audio output layer

- [ ] Mixer – summing of 32 voices + effects to the final buffer
- [ ] Resampling to the target sample rate of the host game/engine
- [ ] Handling of clipping/limiter (optional, for "modern" use outside of DOS context)
- [ ] Multithreading/lock-free queue between sequencer and audio callback (real-time security)

## 7. Plugin API and game integration (C++)

- [ ] Design of public C++ API (init, loadMidi/loadXmi, play/stop/pause, setVolume, callback for audio buffer)
- [ ] Decision on thread model (audio thread vs. game thread, no allocations in audio callback)
- [ ] Build as static/dynamic library (according to the needs of the target game)
- [ ] Minimize dependencies (to easily integrate into a third-party engine)
- [ ] Configuration via file/API (path to SF2 bank, output sample rate, number of voices)
- [ ] Sample integration – simple demo host (e.g. SDL2 audio) for testing outside the target game

## 8. Testing and validation

- [ ] Unit tests of parsers (.mid/.xmi) on a set of test files
- [ ] Reference set of MIDI/XMI from specific DOS games for listening tests
- [ ] A/B comparison with real hardware (if available) or with DOSBox-X + FluidSynth as an indicative baseline
- [ ] Stress tests (32 voices at once, fast note on/off sequences, long loops)
- [ ] Performance tests (real-time CPU load on the target platform)

## 9. Documentation and legal issues

- [ ] Document the register map sources and verification methodology (what is from the manual, what is empirically measured)
- [ ] Ensure that there are no original Creative binaries in the repository (ROM patch banks, drivers)
- [ ] README with project architecture and instructions for plugin integration
- [ ] Project license (own code, not a derivative of a DOS driver)

## 10. Disassembly of DOS drivers in IDA (additional resource for section 4)

### 10.1 File preparation
- [ ] Find out the actual binary format – `.SYS`/`.COM` may not have a standard MZ header,
`.EXE`/TSR usually does. Check the first few bytes (`MZ` signature) manually in a hex editor,
before IDA even opens it
- [ ] For `.COM` files (no MZ header, always loaded at offset 0x100) set the segment type and load offset in IDA
manually – IDA will not guess it correctly for all DOS formats
- [ ] For TSR drivers (`.SYS`, device driver header) identify the driver header structure
(strategy routine, interrupt routine, device attributes) – without it IDA will not find the entry point

### 10.2 Setting up IDA for 16-bit real-mode
- [ ] Select the processor **Intel 80x86** and explicitly **16-bit** analysis (not 32-bit – real-mode
DOS code used 16bit segments even on 386/486)
- [ ] Set segmentation manually if IDA does not recognize it automatically (Segments window →
add/correct segments according to real `seg000`, `seg001`... and their base addresses)
- [ ] Enable DOS/BIOS interrupt call recognition (IDA has built-in signatures for `INT 21h`
functions – check that they are commented correctly in Disassembly view)
- [ ] If IDA offers FLIRT signatures for Borland C/Turbo Assembler runtime (common in
Creative drivers from the mid-90s) – apply it, it will save time on library functions

### 10.3 Mapping to EMU8000 I/O ports
- [ ] Find all occurrences of `OUT`/`IN` instructions in the code (IDA: Search → Text, or go through
cross-references to ports)
- [ ] Compare target I/O addresses with EMU8000 port range according to Tech Ref Manual (card has
basic port + several offsets for Data0/Data1/Pointer registers)
- [ ] For each entry, record: which function does it, in what order relative to the surrounding
entries, and what value/formula the code calculates for it (not just a constant – often it is
the result of some conversion from a MIDI parameter)
- [ ] Name the identified functions according to their identified purpose (e.g. `set_voice_pitch`,
`write_envelope_reg`) – this will make it easier to navigate during further browsing

### 10.4 Dynamic verification (supplement to static analysis)
- [ ] Run same driver in **DOSBox-X debugger** (built-in, `Alt+Pause` or
`debug` in console) and set breakpoints on identified functions
- [ ] Monitor actual values written to ports at runtime (I/O logging) and compare
with static analysis – TSR code often contains branches that static analysis itself
will not reveal (e.g. card revision detection, path fallback)
- [ ] Record timing between calls (especially in initialization sequence after driver startup)

### 10.5 Output to project
- [ ] Rewrite found write sequences into readable pseudo-C (commented, not 1:1 copy
of assembler) as reference notes – serves to verify/supplement register map
in section 4, not as plugin source code
- [ ] Mark in notes what is "verified by driver behavior" vs. "just according to the manual",
for future debugging of differences from real hardware

---

## Suggested order of work
1. .mid/.xmi parsers (section 1) + internal event model (section 2)
2. Basic MPU-401/channel layer (section 3)
3. Minimal EMU8000 core without effects (section 4, just voice+envelope+filter)
4. SF2 loader (section 5) – to be able to hear anything at all
5. Plugin API + demo host (section 7) – quick feedback by listening
6. Chorus/reverb tuning and fine filter details (section 4 completed)
7. Testing and validation (section 8), documentation (section 9)

# AWE32 / EMU8000 emulation — roadmap and status

Goal: a C++ player/library that takes MIDI (`.mid`) and XMI (`.xmi`) files,
interprets them as a stream of MIDI events and synthesises the sound with a
register-accurate emulation of the EMU8000 chip — reconstructed from the
official documentation, the behaviour of Creative's drivers and measurements
of a real card.

**A note on the architecture:** the EMU8000 is a fixed-function ASIC without
programmable microcode (unlike the later EMU10K1/FX8010 of the Sound Blaster
Live!). So this is not "reversing a program out of the chip" but
reconstructing its behaviour from the register specification, verified and
completed against real hardware. The disassembly of the drivers serves as a
**complementary source** — it shows how AWEUTIL, SBAWE32.MDI, SBAWE.VXD and
the SDK really write to the chip (order, timing, values), which fills the
places where the official documentation is incomplete.

Legend: `[x]` done, `[~]` partly done, `[ ]` open.

---

## 0. Sources

- [x] **AWE32/EMU8000 Programmer's Guide** (Rev. 1.00, Dave Rossum,
      E-mu/Creative 1994-1996) — the primary source for the register map and
      the meaning of the bit fields.
      <https://www.dosdays.co.uk/media/creative/emu8kpgm.pdf>
      Worked through in `docs/re-notes/emu8000_register_map.md`.
- [x] **The Linux ALSA driver** — `sound/isa/sb/emu8000.c` +
      `include/sound/emu8000_reg.h`. Independently confirms the register map
      and the init sequence. Taken as a reference of behaviour, not as a
      source of code.
- [x] **86Box** `snd_emu8k.c` — used as a second, independent chip model;
      now the default chip core, with our measured corrections.
- [x] Reference recordings from a real AWE32 card — the AWETEST series run
      by a tester on real hardware (see [DATA.md](DATA.md)).
- [x] Driver binaries of all three families (DOS MDI/DRV, Win95 VXD, DOS
      SDK) — disassembled and traced in a VM, see `docs/re-notes/`.
- [ ] XMI specification details for branch points (`RBRN`) and loops.

## 1. Input file parsing

### 1.1 Standard MIDI File (.mid)
- [x] Header (MThd), formats 0 and 1, ticks-per-quarter division
- [x] Tracks (MTrk), variable-length quantities, running status
- [x] MIDI events (Note On/Off, CC, Program, Pitch Bend, Aftertouch)
- [x] Meta events (Tempo, End of Track)
- [ ] SysEx events — currently skipped; GM/GS/XG and AWE-specific resets
      are not interpreted
- [ ] Format 2 and SMPTE division
- [x] Merging all tracks into one time line

### 1.2 XMI (.xmi)
- [x] IFF/FORM/CAT container, `EVNT` chunk
- [x] XMI delta times and Note On with embedded duration (derived Note Off)
- [ ] `TIMB` (timbre list) — skipped, not needed for playback
- [ ] `RBRN` branch points and XMIDI loop controllers (CC116/CC117) —
      without them looped game music plays straight through once

## 2. Sequencer / timing

- [x] Internal event stream independent of the input format
- [x] Tempo map
- [x] Event scheduling to the sample within a render block
- [ ] Looping (see 1.2)
- [ ] API for pause/resume/seek/stop from a host

## 3. MIDI interpretation layer (channel state machine)

- [x] 16-channel state (program, bend range via RPN, volume, pan,
      expression, sustain pedal, modulation wheel, channel pressure)
- [x] Bank Select and bank layering as the drivers do it
- [x] Voice allocation, exactly per driver family (`dos`, `win95`, `sdk`)
- [x] Attenuation, pitch, filter and effect-send formulas transcribed from
      the drivers; all registers match the real drivers on the measured songs
- [ ] Creative-specific SysEx/NRPN (AWE32 NRPN parameter editing)

## 4. EMU8000 core (register level)

- [x] Register map and port interface (pointer / data ports)
- [x] 32 voices, sample playback from ROM and DRAM, loops
- [x] Interpolation — measured: cubic B-spline (86Box core)
- [x] Volume and modulation envelopes, measured attack shape
- [x] LFO1/LFO2 (triangle), tremolo, vibrato, filter modulation
- [x] Resonant low-pass filter — measured: Chamberlin SVF, cutoff map
      101.81 Hz + 29.38 cents per step
- [x] Pan, sends, output mixing, output headroom
- [x] Chorus — fitted to line-out recordings (delay, LFO, feedback)
- [x] Reverb — fitted to line-out recordings (combs, early reflections,
      damping, echo presets)
- [x] Bass/treble equalizer — measured
- [x] Validation against recordings of the real card (see TESTING.md)
- [~] What remains open is listed in `docs/re-notes/emu8000_tuning.md`
      (e.g. the small overshoot after the attack; the finer shape of the
      reverb tail)

## 5. Instrument data (SoundFont layer)

- [x] SoundFont 1.0 (`.SBK`) loader, including ROM sample detection
- [x] SF2 loader
- [x] GM presets compiled into `SBAWE32.MDI` (`--sf SBAWE32.MDI`)
- [x] Generator → register conversion as measured on the drivers
- [x] User banks in MIDI bank slots (`--sf file@N`)
- [x] Export of loaded banks (ROM included) to one `.sf2`

## 6. Audio output layer

- [x] Mixing of 32 voices + effects
- [x] Resampling to the output rate
- [x] WAV writer
- [x] Live output through a common interface (`AudioOutput.h`): RtAudio
      (portable), WinMM, BASS (loaded at run time) and a silent real-time
      output; `--audio` selects it (issue #1)
- [~] Queue between the sequencer and an audio callback — a ring buffer
      with a mutex in the RtAudio backend; lock-free is not needed so far
- [ ] MIDI input (e.g. RtMidi), so the emulator can be played as a
      software synth

## 7. Library API and integration

- [ ] Public C++ API (init, loadMidi/loadXmi, play/stop/pause, setVolume,
      render callback) — today the project is a console program
- [ ] Build as a static/shared library
- [ ] Example host (e.g. SDL2 audio)

## 8. Testing and validation

- [x] Port-write traces and replay (`--trace`, `--replay`)
- [x] Comparison with 86Box (chip check over all test blocks; VM state
      dumps against replay)
- [x] Note-by-note comparison with the real drivers (`--dump-notes`)
- [x] AWETEST calibration program and recordings from a real card
- [ ] Automated tests in CI (parsers, a short render with a hash)

## 9. Documentation and legal

- [x] Sources of the register map and the verification method documented
      (`docs/re-notes/`, [TESTING.md](TESTING.md))
- [x] No original Creative binaries in the current tree (ROM, banks,
      drivers)
- [x] README with the architecture and usage
- [ ] Project licence — the 86Box chip file is GPL-2.0-or-later, so a
      binary containing it is GPL; the licence of the rest is still to be
      decided

## 10. Driver disassembly (complementary source for section 4)

- [x] AWEUTIL.COM — init sequence and register access
      (`docs/re-notes/aweutil_register_access.md`)
- [x] SBAWE32.MDI (DOS/AIL) — note-on, attenuation, pitch, voice
      allocation (`docs/re-notes/sbawe32_mdi.md`)
- [x] SBAWE.VXD (Win95) — note-on, voice allocation, curves
      (`docs/re-notes/driver_note_on.md`, `86box_comparison.md`)
- [x] AWE32 DOS SDK (`RAWE32L.LIB`, `midieng.c`) — the `sdk` family
- [x] Dynamic verification with instruction traces in 86Box

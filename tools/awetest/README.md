# AWETEST — calibration recording from a real AWE32

This program plays about **50 minutes** of precisely defined sounds. It is not
music: every tone changes one single thing and leaves everything else alone.
That is the only way the behaviour of the chip can be measured back out of a
recording.

## What you need

- A Sound Blaster AWE32 (or AWE64) under DOS, with the `BLASTER` environment
  variable set.
- Copy `AWETSTnn.EXE` (nn = version; the DOS extender is bound in, nothing
  else is needed) into the Magic Carpet 2 directory — the one that holds
  `BULLFROG.SBK`. Without that file only the last block is
  skipped; everything else still works.
- Something to record the card's line output.

## How to record

**Stereo** — block 3 (pan) is meaningless without it. 44.1 or 48 kHz, 16 bit.

**No equalisation, no normalisation, no "enhancement"** — record quietly
rather than loudly. The whole point is comparing levels between tones, and any
automatic processing destroys that.

Start recording, then run the program and let it finish. It waits five seconds
after launch so there is time to switch over. It writes `AWETEST.LOG` listing
what played when — please send that along with the recording.

It also helps to note the **card model** (the CT???? number on the board) and
how much sample RAM it has; the program prints that at startup.

## Markers

- Each block starts with **its number as ticks** at a high pitch. Block 7 is
  seven ticks. The recording can be split up from that alone, without the log.
- **Every minute** there are two ticks at a low pitch, for lining up time in
  case the recording drifts.
- A three-second reference tone plays at the very start and the very end. If
  the level changed during recording, it shows up there.

## Contents

| # | block | length |
|---|---|---|
| 1 | reference tone | 5 s |
| 2 | attenuation IFATN 0–126 | 39 s |
| 3 | pan PSST 0–255 | 20 s |
| 4 | pitch, sine, −36…+23 semitones | 38 s |
| 5 | pitch, noise (measures interpolation) | 38 s |
| 6 | filter cutoff, 64 values | 46 s |
| 7 | resonance, 6 cutoffs × Q 0–15 | 68 s |
| 8 | envelope: attack | 69 s |
| 9 | envelope: hold | 33 s |
| 10 | envelope: decay | 82 s |
| 11 | envelope: sustain | 34 s |
| 12 | envelope: release | 85 s |
| 13 | envelope: delay | 34 s |
| 14–15 | modulation envelope → pitch, → filter | 74 s |
| 16–19 | LFO1: rate, volume, pitch, filter | 150 s |
| 20 | LFO2: rate and depth | 73 s |
| 21 | LFO delay | 29 s |
| 22 | reverb: 8 presets × 5 send levels | 75 s |
| 23 | chorus: 8 presets × 5 send levels | 75 s |
| 24 | loop: long sustained tones | 27 s |
| 25 | voice summing 1–16 | 19 s |
| 26 | **as a game does**: ROM GM presets 0–15 | 115 s |
| 27 | **as a game does**: BULLFROG.SBK | 108 s |
| 28 | filter measured with a sine, PEFE and FMMOD depth | 3 min |
| 29 | GM drum kit (probes, full set, v25: again with effects off) | 4 min |
| 30 | modulation envelope timing | 42 s |
| 31 | note off during attack / decay / sustain | 18 s |
| 32 | pitch changed while the note sounds | 8 s |
| 33 | filter changed while the note sounds | 8 s |
| 34 | PTRX pitch target | 27 s |
| 35 | **v25** EMU8000 equalizer: treble and bass settings, noise | 100 s |
| 36 | **v25** filter map vs Q, top of the map, modulation clamps | 150 s |
| 37 | **v25** capture diagnostics: silence, isolated notes | 45 s |
| 38 | **v25** quiet material again at +12 and +24 dB capture level | 5 min |
| 39 | reference tone and clock probes (was block 35 before v25) | 18 s |

v25 widened the gaps between notes where tails ran into the next note
(blocks 2–7, 12, 22, 23, 26–29) and sizes envelope notes from the expected
envelope time, so the lengths above blocks 28 are those of v24 plus that.

`AWETEST /FROM:n /TO:n` plays only part of that, so a single block can be
repeated if it went wrong.

## What is checked and logged before the first sound (v25)

Lines starting with `#` in `AWETEST.LOG`:

- `# CARD` — BLASTER, DSP version and copyright string, sample RAM, EMU8000
  configuration words, equalizer and mixer registers as found, and a guess
  at the model from the DSP version. Please still write down the CT number
  printed on the board.
- `# CHIP` — the chip's sample clock measured silently from its play position
  against the PC timer, and whether the play position really wraps inside
  the sine / noise loops.
- `# LEVEL`, `# LADDER` — whether the internal capture is linear at full level
  (a tone and the same tone 12 dB down must differ by 12 dB); if not, input
  gain, mixer level and finally the chip attenuation are lowered.
- `# STEREO` — each capture channel on its own with a hard-left and a
  hard-right tone; if one is dead, which input still works, and block 3 is
  then played a second time with the other chip channel.

Every event line carries a stamp taken at the moment the note starts:
`<tab>@rt <BIOS tick>:<PIT phase> cap <file from ms>:<frame>` — the frame in
the capture file is exact, so the analysis no longer has to guess the grid.
`QUALITY` lines add the number of frames and the real start/stop time of each
capture file, which gives the true sample rate and any lost audio.

## Optional capture on the card itself

`AWETEST /REC:OUT.WAV` also records the card's own output through the Sound
Blaster 16 ADC — 44.1 kHz, 16 bit, stereo, about **240 MB** for the full run,
so check there is disk space.

This is a bonus, not the main path. Whether the wavetable output can be
selected as a recording source differs between cards; the program sets the
mixer to record from MIDI, Line and CD at once to give it the best chance,
and **tells you at the end whether anything but silence arrived**. If it says
the capture is silent, nothing is wrong with the run — the sounds played
correctly and the external recording is the one that counts.

Verified in an emulator: the DMA transfer runs at exactly the right rate with
no dropouts (48.77 s recorded for a 48.7 s run), but the emulated card feeds
the ADC silence, so the routing question can only be answered on real
hardware.

## Two paths at once

Blocks 1–25 write **straight into the EMU8000 registers** through the I/O
ports. Going through MIDI would not allow setting one value and leaving the
rest alone — the driver computes the registers itself from the bank, velocity
and controllers — so there would be no way to measure "what does decay rate
0x40 actually do". Direct writes give one variable per tone.

Blocks 26 and 27 go the other way, through the **Creative AWE32 API**
(`awe32NoteOn`, `awe32ProgramChange`), i.e. the same path a game uses. That
verifies the whole chain including the bank-to-register conversion.

## Test signals

Taken from the card's wave ROM, so they are identical on every AWE32 and known
byte for byte in advance:

| sample | address | used for |
|---|---|---|
| `sinewave` | 430271 | level, pan, pitch, envelopes |
| `whitenoisewave` | 458995 | filter and interpolation (full spectrum) |
| `sinetick` | 491098 | markers |

## Building

The program needs the AWE32 DOS SDK (headers and `SBKLIB`).

Open Watcom (protected mode, the path the SDK officially supports):

```
BUILD.CMD
```

Set `WATCOM` and `AWESDK` first, or edit the defaults at the top of the file.
The SDK must sit on a path **without spaces** — Watcom cannot cope with a
space in `-i=`.

Borland C++ 4.x/5.x (real mode, LARGE model):

```
make -f MAKEFILE.BC
```

`BUILD32.CMD` binds DOS/32A into the executable and names it after
`AWETEST_VER`, so there is a single file to ship.

## Timing

Delays are busy-waits on timer channel 2 — the one that otherwise drives the
PC speaker. The system clock and the interrupts are left alone, so nothing
else on the machine breaks and the tone lengths are accurate to a fraction of
a millisecond.

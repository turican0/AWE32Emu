# Measuring program for a real AWE32 — the original design

This is the first design of what became AWETEST
(`tools/awetest/AWETEST.C`). The program grew to 46 blocks over versions
v13–v28; the current list of blocks is in the program itself and the
findings in `emu8000_tuning.md`.

Goal: one recording from a real card from which every property of the chip
that we only model can be measured **separately**. Not music — a calibration
signal.

## The key decision: bypass the driver

The program **does not play MIDI**. It writes directly to the EMU8000
registers through the ports, after the documented initialisation sequence
(taken from `AWEUTIL.COM`, see `emu8000_register_map.md`).

Why: through MIDI you cannot set one value and leave the others alone — the
driver computes the registers itself from the bank, velocity and control
changes. So one could not measure "what decay rate 0x40 does", because it
cannot be reached on its own. Direct writes give **one variable per tone**,
and that is the only thing a calibration can be built from.

It belongs in the MC2 directory because of `BULLFROG.SBK` — one block loads
it into DRAM the same way the game does.

## The test signals come from the wave ROM

Nothing has to be uploaded to the card's memory. The ROM holds exactly what
is needed, it is the same on every AWE32 — and we have it byte for byte in
`awe32.raw`, so we know what the signal looks like **before** the chip:

| sample | address | loop | used for |
|---|---|---|---|
| `whitenoisewave` | 0x0700F3–0x072158 | 8281 words | filter, interpolation (full spectrum) |
| `sinewave` | 0x0690BF–0x06914A | 65 words | volume, pan, pitch, envelopes |
| `sinetick` | 0x077E5A–0x077EA2 | 60 words | synchronisation marks |

## Synchronisation marks

Three ticks (40 ms, 60 ms gap) and then 700 ms of silence — about 1 s in
total. At the start of the recording and then **every 60 s**. The recording
can be aligned by them even if it drifts in tempo. (Later replaced by block
marks and per-note timestamps in the log, see `tools/awetest/awelog.py`.)

At the very start and end a reference tone (3 s, sine, fixed volume) shows
whether the level changed during the recording.

## Contents (first version)

Everything on one voice, except blocks 11 and 12, which are about the sum of
several voices. There is always silence between tones so they can be
measured in isolation.

| # | block | what changes | items | length |
|---|---|---|---|---|
| 1 | **Attenuation** | IFATN low byte 0–127 in steps of 4 | 32 | 13 s |
| 2 | **Pan** | PSST pan 0–255 in steps of 16 | 17 | 7 s |
| 3 | **Pitch, sine** | IP over 6 octaves in semitones | 36 | 14 s |
| 4 | **Pitch, noise** | the same with noise = **interpolation** | 36 | 14 s |
| 5 | **Filter cutoff** | noise, Q=0, cutoff 0–255 in steps of 8 | 32 | 19 s |
| 6 | **Resonance** | noise, Q 0–15 at 4 cutoffs | 64 | 38 s |
| 7 | **Volume envelope** | attack 16, hold 8, decay 16, sustain 8, release 16, delay 8 | 72 | 134 s |
| 8 | **Modulation envelope** | PEFE hi (pitch), PEFE lo (filter), its own times | 24 | 48 s |
| 9 | **LFO1** | speed, → volume, → pitch, → filter | 32 | 64 s |
| 10 | **LFO2 and delays** | speed, depth, delay of both LFOs | 22 | 44 s |
| 11 | **Reverb** | 8 presets × 4 send levels | 32 | 48 s |
| 12 | **Chorus** | 8 presets × 4 send levels | 32 | 48 s |
| 13 | **Loops** | long hold, 3 pitches, both samples | 6 | 18 s |
| 14 | **Voice summing** | the same tone on 1, 2, 4, 8, 16, 32 voices | 6 | 9 s |
| 15 | **BULLFROG.SBK** | every sample of the bank at 3 pitches + one through the filter | 52 | 28 s |

About **9 minutes 15 s** in total, with marks and reserve **10–11 minutes**.
(The final v28 runs about 59 minutes.)

## What each item decides

- **1, 2** — the attenuation-to-dB conversion (0.375 dB per step) and the pan
  law. Both were taken from documentation, not from hardware.
- **3, 4** — interpolation. Noise at 36 pitches gives the transfer function
  of the interpolator directly; before, it was guessed from scores.
- **5** — the register-to-cutoff conversion **and the slope** at once.
  Measuring the slope from music failed (`filter_slope.py`), because the
  groups also differed in note pitch. On noise with one variable it works.
- **6** — the awe32faq resonance table claims the resonance falls with the
  cutoff (at Q=8 from 17 to 7 dB). Confirmed or refuted here.
- **7** — **the most important block.** A breakdown of the residual deviation
  showed that almost half of it is note volume error, largest 50–150 ms after
  the onset. The registers match 100 %, so the error is in how fast the chip
  reacts to them.
- **8, 9, 10** — the modulation depths come from the Programmer's Guide
  (`±1 octave`, `±6 octaves`, `±12 dB`), never verified.
- **11, 12** — effects. Our own implementation; the sound of the period
  presets was unknown.
- **13** — behaviour at the loop seam, where the interpolator reads past the
  end.
- **14** — summing and possible clipping with many voices.
- **15** — a check of the whole path on the bank the game uses.

## What we need from the person running it

- Record in **stereo** (block 2 is meaningless without it), 44.1 or 48 kHz,
  16 bit.
- **No** equalisation, normalisation or "enhancement" — rather quieter.
- Ideally from the line output; digital capture is even better.
- Note the card model (CT????), its DRAM size and the driver version.
- If block 15 makes trouble, run once with empty DRAM and again with
  `BULLFROG.SBK` loaded.

## If it had to be shorter

Priority order if something had to go: keep 7, 5, 6, 4, 3, 1. Blocks 9–12
(LFO and effects) are the least urgent — they can get a second recording
later.

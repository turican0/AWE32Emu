# The first goal and the early measurements

This is the log of the project's first phase, before the driver layer was
matched against the real drivers and before recordings from a real card
existed. Many of its open questions were answered later (the references at
the end of each section say where).

## The task

Take a file from `midi/` (`.xmi`, or `.mid`), play it through the EMU8000
emulation with the original sound data and get a `.wav` that is **audibly
and measurably similar** to the corresponding recording in `ogg/` (Magic
Carpet 2 music).

Sound data:
- `awe32.raw` — the card's 1 MB wave ROM (the basic GM set, "1MGM")
- `BULLFROG.SBK` — the game's user bank (SoundFont 1.0, `irom=1MGM`, i.e.
  **layered over the ROM**, not replacing it)

## Definition of done

Not "bit-exact" — `.ogg` is a lossy recording of a real card. The match is
wanted in what can be verified:

1. **Length and timing** — the same song length, events at the same places
2. **The right instruments** — every MIDI channel plays its patch
3. **Envelopes in time** — attack/decay at the same times
4. **Spectrum** — a similar spectrum over time (spectrogram correlation)
5. **Listening** — "it is the same piece with the same sound"

## Generality

The bank loader **must not be tailored to `BULLFROG.SBK`**. It must handle
any `.SBK` and `.SF2`:

- version detection by `INFO/ifil` (1.x = SF1, 2.x = SF2) and accordingly a
  different `shdr` size (16 vs 46 B), names in `snam` vs in `shdr`, and
  different generator units (register/ms vs timecents/centibels)
- any preset -> zone -> instrument -> zone hierarchy
- global zones (a zone without `sampleID` / `instrument` = defaults for the
  rest)
- `keyRange` / `velRange`, overlapping zones, several layers per note
- samples from ROM and from the bank's `smpl`, both at once
- a bank without ROM and ROM without a bank

## Steps

- [x] **Pairing** — `_w` is the AWE32 variant: its `TIMB` chunk lists patches
      3, 4, 5 (presets of `BULLFROG.SBK`) and 52 (from ROM). `_f` = FM/AdLib,
      `_g` = General MIDI, `_r` = Roland.
- [x] **XMI timing fix** — two different encodings: a delta time is the sum
      of bytes < 0x80, but **a note duration is a standard SMF VLQ**. The
      parser used the same for both and scattered the stream (293 events
      instead of 9948). And **the tempo meta must be ignored** — XMI runs at
      a fixed 120 Hz. Result: lengths 0.995 / 0.987 / 1.000 / 1.000 against
      the reference.
- [x] **Decode `.ogg`** into PCM (`soundfile`)
- [x] **Load the ROM** — mapped from address 0, the sample pool starts at
      word 495
- [x] **SoundFont parser** — `SoundFont.h/.cpp`, generic for SF1.0 and SF2
- [x] **Generator -> EMU8000 register conversion**
- [x] **Bank layering** — `--rombank` (a bank describing the ROM) and `--sf`
      (a user bank into DRAM), both can be given several times
- [x] **Measuring tools** — `compare.py`, `bands.py`, `rom_pitch.py`,
      `query_preset.py`, `dump_sbk.py`

## State of the measurement (002_C2GAME3)

What already matched:

- length 241.5 s vs 240.0 s
- all notes find a real sample, nothing falls back to the substitute sine
- **the melody matches** — in the 6-14 s window the 400-800 Hz band has a
  ratio of 0.96 against the reference
- the pitch of ROM samples verified: `kpianob1` has 639 Hz = note 75, exactly
  the `overridingRootKey 75` in `1mgm.sf2`. ROM samples are stored tuned up
  and the chip pitches them down.

What did not match:

- **channel 7 (Choir Aahs, prog 52)** — two notes lasting the whole 240 s, a
  sustained pad. Ours was **~16 dB louder** than the reference and at a
  different pitch (we: notes 36 and 37; the reference: peaks at 98.2 and
  99.6 Hz, +6 to +7 semitones). Not explained at the time. (Later the GM
  presets of the game were found to come from the driver itself,
  `--sf SBAWE32.MDI`; the MC2 recordings in `ogg/` also turned out to be
  partly mislabelled — see `mc2_game.md` and `emu8000_tuning.md`.)

## A better reference set: the AWE32 demo CD

Creative's AWE32 demo songs — **lossless FLAC** plus the matching `.MID`, a
much better calibration than `.ogg`:

| MIDI | length with the tempo map | FLAC | ratio |
|---|---|---|---|
| `RELAX_BK.MID` | 218.4 s | 218.8 s | **0.998** |
| `CRAZY_BK.MID` | 175.2 s | 171.5 s | 1.021 |
| `JUMP_BK.MID` | 169.1 s | 180.9 s | 0.935 |
| `MARS_BK.MID` | 150.0 s | 202.8 s | 0.740 |
| `GEORG_BK.MID` | 282.4 s | 153.1 s | 1.844 |

`RELAX` matches within 0.2 %, which **confirms that the SMF tempo map
handling is right**. For `GEORG_BK` the difference is a long silence after
the last note (the last tempo, 62 BPM, applies to tick 103360, but the notes
end much earlier).

`RELAX` also has its own bank `RELAX.SBK` (SF1.0, 32 presets, `irom=1MGM`),
so it is the best calibration pair.

## The authentic GM bank: `SYNTHGM.SBK`

The installation CD (`/WIN95/DRIVERS/SYNTHGM.SBK`) holds **the bank the
drivers themselves use** — SoundFont 1.0, "General MIDI", E-mu Systems 1993,
`irom=1MGM`, 153 samples, 128 presets in bank 0 and one in bank 128.

It has no `smpl` chunk at all: it describes only the wave ROM contents. The
addresses in `shdr` are already chip addresses — `kpianob1` starts at
**494**, exactly the offset derived from `BULLFROG.SBK` (the same sample has
index 0 in `1mgm.sf2`). The loader recognises it: an SF1 bank without
`smpl` = all samples in ROM.

It is closer to the original than `1mgm.sf2` (a later SF2 conversion),
because the generators are in the driver's native units. Measured:

| band | with `1mgm.sf2` | with `SYNTHGM.SBK` |
|---|---|---|
| 200-400 Hz | 1.55x | **1.18x** |
| 800-1600 Hz | 2.70x | **1.59x** |
| 1600-3200 Hz | 1.38x | **1.03x** |
| 3200-6400 Hz | 0.47x | **1.80x** |
| 6400-12800 Hz | 0.37x | **1.66x** |

The mean absolute log deviation over all bands fell from 5.40 to 4.13.

## Driver versions

The conversion tables are **byte for byte identical across three
generations**:

| file | origin |
|---|---|
| `SBAWE32.DRV` | Windows 3.x, driver disk |
| `SBAWE.VXD` | Windows 95, installation CD |
| `SBAWE32.MDI` | DOS AIL/Miles, from the game |

The expression and channel volume curves and the attack time table (11878,
5939, 3959, …) match. (Later: the velocity table differs in index 0 between
them — see `86box_comparison.md` 8.1.)

## Spectral balance during the early fixes

Ratio ours/reference on RELAX (target 1.0):

| band | initial | then |
|---|---|---|
| 0-100 Hz | 0.40x | 0.68x |
| 100-200 Hz | 0.63x | 0.71x |
| 200-400 Hz | 1.91x | 1.55x |
| 400-800 Hz | 4.56x | 3.22x |
| 800-1600 Hz | 3.88x | 2.70x |
| 1600-3200 Hz | 1.90x | 1.38x |
| 3200-6400 Hz | 0.32x | 0.47x |
| 6400-12800 Hz | 0.10x | 0.37x |

The steps:

1. **filter fix** — our first Chamberlin SVF was unstable above 4 kHz at
   Q=0.707 (`f + 1/Q < 2` did not hold) and was replaced by a TPT topology.
   The manual also says that at Q=0 and cutoff 0xFF the signal is unchanged
   — we cut at 5 kHz. (Later the card showed a Chamberlin SVF after all, with
   the frequency coefficient limited — see `emu8000_tuning.md`.)
2. **velocity -> cutoff, key -> envelope, one-shot loop** — three behaviours
   transcribed from `SBAWE32.DRV`, see `driver_note_on.md`
3. **chorus and reverb** — sends register-exact (PTRX 15..8, CSL 31..24,
   before the pan), the algorithm a substitute then
4. **drum selection fix** — the GM bank has one preset "Standard" in bank 128,
   but the song sends program 16 on channel 9. The fallback fell to bank 0,
   so one sample of the user bank replaced the whole drum kit. Now (128, 0) is
   tried for drums too.
5. **reverb gain** — a comb filter with feedback `f` has a DC gain of
   `1/(1-f)`, at `f = 0.854` 6.85x. Without input scaling `(1-f)` the reverb
   amplified several times and the output clipped.

## Regression test

`tests/regress.py` checks what is already measured:

    [OK] note 50 in tune within +-5 cents  146.69 Hz = -1.6 cents
    [OK] piano is not silent               amplitude 106.5
    [OK] length within 2 % of reference    219.9 s vs 218.8 s (ratio 1.005)
    [OK] no voice on the substitute        0 voices on the substitute
    [OK] output does not clip              peak 0.925

It deliberately does not measure "the strongest peak" — that can jump to a
harmonic when the timbre changes, and once already falsely reported an octave
shift.

## Which bank the Georgia recording of the demo CD uses

The hypothesis: `5 - Georgia On My Mind.flac` sounds as if recorded with other
soundfonts than the basic GM in ROM. **Not confirmed.** The only period
option is **the card's wave ROM described by `SYNTHGM.SBK`**.

**Ruled out by date.** The demo CD is from 10 May 1994. Only SoundFont 1.0
existed then; everything in SF2 is later:

| file | version | INAM | tool |
|---|---|---|---|
| `SYNTHGM.SBK` | **SF1.0** | General MIDI | - |
| `RELAX.SBK` | **SF1.0** | Special Effects | - |
| `1mgm.sf2` | SF2.0 | General MIDI | later conversion of the same ROM |
| `CT8MGM.SF2` | SF2.0 | 8MBGSFX E-mu Rev B | E-mu Systems SoundFont |
| `8MBGMSFX.sf2` | SF2.0 | 8MBGSFX E-mu Rev B | `:SFEDT v1.00` |
| `synergi-8mb.sf2` | SF2.1 | sYnerGi v1 | `:SFEDT v1.10`, a modern fan bank |

`1mgm.sf2` is not even a separate candidate: its `smpl` chunk is byte for
byte `awe32.raw`, only shifted by 495 words (990 B) — verified, 0 of
1 047 586 bytes differ.

**What is on the disc.** The data track of the demo CD holds **only seven
files**: five `*_BK.MID`, `RELAX_VX.MID` and `RELAX.SBK`. No other bank.

**The measurement agrees.** The mean absolute deviation of the third-octave
spectrum against the FLAC (aligned, equal RMS): `SYNTHGM.SBK` 3.07 / 4.10 /
3.23 / 3.23 / 2.91 / 3.03 dB in six windows; the 8 MB SF2 banks are 1 to 5 dB
worse in the same windows, most on the isolated piano chord of the intro.
A capture from the VM with the real driver (certainly the ROM bank) has 3.75 /
3.73 / 3.04 dB against the FLAC — so three dB is the floor given by 86Box vs
the real hardware, not by the choice of bank.

## Notes on the reference

The `.ogg` files are not an exact reference — lossy recordings. The
`*_danger.ogg` tracks have **the same length** as the basic versions, so it
is a layer within the same XMI (the battle layer), not another file. (Later
explained: the game mutes channels 7-9 through the XMIDI CC119 trigger until
a fight raises them — `trigger_mute` in `conf/mc2.conf`.)

## Open questions at the time

- How is the "danger" layer switched? (answered above)
- Did the game use chorus/reverb when the `.ogg` was recorded? (yes — the
  drivers always set them; the MC2 start-up state is in `conf/mc2.conf`)
- The meaning of SF1.0 generator 55 (always 6000 in `BULLFROG.SBK`) —
  answered in `soundfont1_sbk.md`
- `initialFilterQ` 0..127 -> CCCA Q 0..15: `v>>3` fits three points
  (12, 50, 79), but so does `lround(v*15/127)` — a note with Q 6, 14 or 22
  would decide

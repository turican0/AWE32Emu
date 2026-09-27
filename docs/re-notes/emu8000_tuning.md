# Tuning the EMU8000 against recordings from real hardware

## The rule: what belongs where

The project has two levels and each has a different standard:

| level | standard | state |
|---|---|---|
| **driver** (Synth, banks, conversions) | the original Creative drivers running in 86Box | 1:1 (10/10 cases) |
| **chip** (EMU8000) | **real recordings**, not 86Box | tuned by measurement |

Two things follow that are easy to forget:

1. **A chip fix must end up in both copies of the chip.** The chip that
   plays by default is `snd_emu8k.c` (with our measured corrections); the
   same file drives the 86Box VM used for the driver measurements. Every
   change goes into both copies (see [TESTING.md](../TESTING.md)). Our older
   own core (`Emu8000.cpp`, `--chip ours`) got the early fixes too.
2. **86Box is not the standard for the chip.** It is the standard for the
   **driver** — the original binaries run in it. Its stock `snd_emu8k.c` is
   an approximation and differs from the hardware in places (filter cutoff,
   interpolation, envelopes, effects). When our core differs from it, that
   alone is not an error — measurement against recordings decides.

The register level is done (10/10 cases against the real Creative drivers,
see `tests.md`). This document is one level below: whether single notes
**sound** the same as on a real card.

The first part is the log of the period before the tester's card was
available (commercial recordings, YouTube, demo CDs). The last part,
[Measurements on the tester's card](#measurements-on-the-testers-card-awetest-v05v28),
summarises what the AWETEST recordings from a real AWE32 decided — that is
what the current chip is built on.

## Why the whole mix is not measured

Recordings from hardware (a 486 with a Sound Blaster AWE32 CT2760 under
Windows 3.11, uploaded to YouTube) went through YouTube, another preamp and
another equalisation. They **always** differ from our render — that alone
says nothing. It becomes interesting when **one note differs differently
from the rest**: then it is not the transfer path, but the chip computing it
differently.

So it is measured like this:

1. `align2.py` — alignment by a **curve**, not by a single offset.
2. `note_probe.py` — the spectral difference per note, **minus the
   distortion of the whole**.
3. `tune.py` — the same over all pairs, because one song is not enough.

### The alignment must be a curve

The recordings come from a real machine whose clock does not run at the
same speed as ours. In DANCE the first third matches at 0 ms and then it
drifts by 267 ms, in STYLES1 even by **1950 ms**. A single transform (offset
+ scale) would turn the per-note measurement into nonsense.

The alignment is on the **onset** (spectral flux), not on the volume — the
onset of a tone is sharp in both recordings even with a completely
different colour. Outlying anchors are thrown out: the offset changes slowly
and smoothly, so an anchor running away from its neighbours is mis-snapped
and would bend the whole section around it.

### The method is calibrated

**A null test (the same render against itself) gives exactly 0.0000** — in
all modes including stereo. What the probe shows is signal, not noise of the
method. It also reacts to known changes: `--filter-poles 4` worsens the score
from 4.84 to 5.31.

## Verified pairs

Seventeen pairs, **sixteen** aligned. An anchor match below ~0.4 would mean
it is not the same song.

### 486 recording from YouTube (lossy)

| | anchors | match |
|---|---|---|
| dance / pop / starman / styles1 | 38 / 16 / 20 / 46 | 0.62 / 0.51 / 0.53 / 0.58 |
| styles2 / symphony / canon | 61 / 20 / 27 | 0.56 / 0.51 / 0.56 |
| nutcrack / violin / mozart | 48 / 10 / 13 | 0.60 / 0.48 / 0.50 |

**Correction to `pairs.txt`:** `02_GMAMOZAR` belongs to
`CLASSIC/GMMOZART.MID`, not to `DEMO/MOZART.MID`. Confirmed by
`tracklist.txt` and by the anchor match.

The least reliable pair is **violin** (only 10 anchors, match 0.480) — a
conclusion resting mainly on it must be taken with reserve.

### AWE32 demo CD — **lossless FLAC** (the best commercial material)

Recordings named directly after the songs. No codec touched them, so the
part of the difference due to YouTube is absent. The same MIDI files already
match **32/32 at the register level**.

| | anchors | match | drift |
|---|---|---|---|
| georgia | 71 | 0.596 | 46 ms |
| jump | 72 | **0.666** | 17 ms |
| relax | 51 | 0.617 | 493 ms |
| crazy | 78 | 0.642 | 23 ms |
| mars | 6 | 0.429 | 6 ms |

**mars** is weak: the recording ("2 - Vocal intro-Mars") has a vocal intro
before the song, so only a short section aligns.

### Magic Carpet 2

| | anchors | match |
|---|---|---|
| mc2-menu | 146 | **0.844** — the best of all |
| mc2-intro | - | **does not align**, see below |

### The alignment had to find a part within a whole

The original rough estimate correlated the whole recording against the whole
and failed on both MC2 cases and on Mars: one recording often covers only
**part** of the other (the Mars recording has a vocal intro, our XMI loops
and is 359 s against 140 s of recording). Now a piece of the **shorter**
recording is taken and searched for in the longer one.

## Magic Carpet 2, intro: no reference for it

The intro sounds wrong in our engine — an observation by the user, and the
measurement agrees. Our render has, against the recording:

- **12 dB less** energy around 400 Hz,
- **7 to 12 dB more** above 3 kHz.

A per-channel breakdown found the culprit: **channel 1** (504 of 670 notes,
program 5) has its energy centroid at **12 kHz**, while the recording has it
below 100 Hz.

What is measured and **rules out our core**:

1. **Our chip and 86Box play that channel the same** (difference within
   1 dB in all bands). So it is not an error of our chip.
2. **The registers match 24/24 against the trace from the game**, CCCA
   included, so we pick the same sample as the game's real driver.
3. The address `0x0498ED` = 301 293 is the end of the sample `filtersnap`
   and 46 words before the start of `belltree` (301 339); our `loopEnd` lies
   on belltree. So we play a glockenspiel — which **should** be bright,
   which explains those highs.
4. The XMI has exactly the channels we play (504+80+80+1+4 = 669 notes
   against our 670), so nothing is left out.

### `_f`/`_g`/`_r` vs `_w`: not different sound cards, different arrangements

The difference is not in what plays it — it is a **different piece** for a
different device. Compared directly from the XMI events (`xmi_events.py`):

| variant | channels (program: notes) | drums (channel 9) |
|---|---|---|
| f | ch3:47(195), ch4:36(2), ch9:0(**1090**) | 1090 notes |
| g | ch3:47(195), ch4:52(2), ch9:0(**1092**) | 1092 notes |
| r | ch3:112(195), ch4:34(2), ch5:121(210), ch9:0(**882**) | 882 notes |
| **w** | ch1:5(504), ch2:3(80), ch3:4(80), ch5:52(1), ch6:4(4) | **none** |

`_f`/`_g`/`_r` have a drum channel 9 with ~900-1100 drum notes. `_w` has
**no drums at all** — only a glockenspiel (program 5, 504 notes) and three
piano channels (programs 3/4/4). So it is not the same music for another
sound card, it is a **different, sparser arrangement without a drum
section**.

`_w` is provably the right variant for the AWE32 (registers 24/24 against the
trace from the game, a match of 0.76-0.79 against a capture of the running
game in 86Box). When it sounds sparser or different from what one
remembers, the more likely explanation is a comparison against the GM/MT-32
version with drums, not an error in the emulation.

**The recording `004_C2INTRO.ogg` does not align even rhythmically** (0.046
with the calibration "noise reaches 0.25"). Tried against the menu render
and against recordings 003 and 005 — no combination fits. The menu matches
at 0.844, so the method works on this material.

Conclusion: for the intro **we have no valid reference**. Either the
recording is from another device or it is another arrangement. Until that
is decided, tuning against it makes no sense.

## Hi-Octane: three new verified pairs and an own bank

The recordings `HO_TR1_2` / `HO_TR3_4` / `HO_TR5_6` were **unproven** — no
MIDI for them. Everything needed can be extracted from the game's ISO:

    python tests/iso_list.py HIOCTANE.ISO --extract .../files
    python tests/split_musicdat.py .../SOUND/MUSIC.DAT -o midi/hioctane

`MUSIC.DAT` is simply **20 XMI in a row** (each starts with `FORM....XDIR`);
`split_musicdat.py` splits them. The game also has **its own 450 kB bank**
(`SOUND/BULLFROG.SBK`, 17 samples) — the first big user bank with its own
data in this set.

`ho_pairs.py` found the pairs:

| song | recording | anchors | match | drift |
|---|---|---|---|---|
| ho_12 | HO_TR5_6 | 79 | **0.813** | 12 ms |
| ho_11 | HO_TR3_4 | 96 | **0.725** | 17 ms |
| ho_10 | HO_TR1_2 | 90 | **0.703** | 12 ms |

A drift of 12-17 ms is an order better than the YouTube recordings
(267-1950 ms) — these come from a much cleaner source. Each holds **two**
songs in a row, so only part of it aligns.

Scores against them: **4.47 / 4.83 / 3.26** — `ho-tr5` is the best pair we
have (for comparison `dance` 4.56).

### Open at the time: we have fewer highs than the hardware

Over the whole length:

| bands [%] | <100 | 400 | 1600 | 6400 | 22k |
|---|---|---|---|---|---|
| ours ho-tr5 | 33.6 | 57.5 | 2.8 | 1.9 | 4.2 |
| **recording** | 25.5 | 52.2 | 3.2 | **4.2** | **14.9** |

Above 6.4 kHz we had 6.1 % against 19.1 % on the hardware. (Later explained:
the card's filter is a Chamberlin SVF that is more open at high cutoffs, and
the card's EQ lifts the treble by +7.9 dB — see the last part.)

## The error is no longer in the MIDI -> register translation, but in the chip

After all changes the register check against the **real driver** (86Box
traces running `SBAWE.VXD`) was repeated:

| song | notes | register match |
|---|---|---|
| MINUET | 242 | **32 of 32 registers, 100 %** |
| Georgia | 3331 | **all registers, 100 %** (both reference traces) |

No regression. That means **everything the driver writes to the chip, we
write bit for bit the same**. The remaining deviation against the
recordings cannot be in the bank conversion nor in the MIDI handling — it is
in what the chip's sound path does with those registers.

### Trap: a group's deviation falls with the number of notes

For a while it looked like the error was in the filter. Splitting by
resonance gave the same picture across songs — notes with `Q = 0` around
0.1-0.3 dB, notes with `Q > 0` 0.8-2.3 dB — and it sounded convincing.

**It was an artefact.** A group's deviation is computed as the **median**
over its notes, and a median of few notes is itself noisy. A group of 19
notes therefore comes out "worse" than a group of 665 even with the same
error. It was visible in the data already: in every table the "deviation"
column anti-correlated with the note count.

`tests/group_signif.py` solves it by drawing, for each group, 400 random
groups of the same size and computing what would come out by pure chance.
Only the ratio of the real deviation to the random one says something:

| split (dance-bp) | notes | deviation | chance | ratio |
|---|---|---|---|---|
| Q 8 | 19 | 1.74 dB | 0.81 dB | **2.15** |
| Q 0 | 665 | 0.05 dB | 0.02 dB | **2.13** |

Indistinguishable. The same for channels (all 1.6-2.2) and samples: after
normalisation the most significant are **large** groups with small
deviations (`hatopenms` 2.86, `organwave` 2.51, `triangle` 2.22), not small
ones with large deviations. Groups of 11-13 notes have a ratio of 1.03, pure
noise.

That also killed the earlier conclusions "the kalimba is the worst
instrument", "the drums have 0.2 dB" and "channel 0 is bad" — all were only
differences in group size.

### What remains: the deviation is real, but even

Control experiment: the same ratio between **two recordings of the same
card** (`group_signif.py --ours ... --warp-ours ...`):

| group | our render vs hardware | hardware vs hardware |
|---|---|---|
| Q 8 (19 notes) | 2.15 | 1.58 |
| Q 0 (665 notes) | **2.13** | **0.96** |

For two recordings of the same card the Q=0 group has no structure (0.96 =
chance), for us 2.13. So our deviation **is** systematic per instrument —
only spread evenly over all of them instead of sitting in one subsystem.
That explains why every parametric sweep ended nowhere: there was nothing
local to fix.

### Resonance depends on the filter cutoff

`snd_emu8k.c` has, at the table `filter_atten`, a copy of the **measured
awe32faq table**:

| Q | resonance at a low cutoff | resonance at a high cutoff | DC attenuation |
|---|---|---|---|
| 0 | 5 dB | flat | -0.0 dB |
| 8 | 17 dB | 7 dB | -6.0 dB |
| 15 | 28 dB | 18 dB | -11.0 dB |

So the resonance **falls with a rising cutoff** — at Q = 8 from 17 dB to
7 dB. We used a single number `Q * 24/15 = 12.8 dB` regardless of the
cutoff. The option `--resonance-curve faq` interpolates between the two
columns by log2 of the cutoff; verified by computing the transfer:

| model | cutoff 7717 Hz | cutoff 781 Hz |
|---|---|---|
| our TPT (then) | peak +12.9 dB at 7634 Hz | +12.9 dB at 771 Hz |
| TPT with the awe32faq curve | +7.2 dB at 7393 Hz | +12.3 dB at 770 Hz |
| Moog from 86Box | +8.4 dB at **18406 Hz** | +2.6 dB at **2448 Hz** |

Notably, **the Moog variant of 86Box has its cutoff somewhere else entirely
than it asks for** — at register 120 (881 Hz) it peaks at 2448 Hz and at 255
(7717 Hz) at 18 kHz. As a model of the real chip it does not fit. (Confirmed
later on the card: 86Box's cutoff is 1.8-2.5 octaves too high.)

### Dead end: measuring the filter slope from recordings

The idea was to take the same sample once with an open and once with a
closed filter and read the transfer from the spectral ratio.
`tests/filter_slope.py` can do it, but **it does not work**: the two groups
also differ in note pitch and velocity, so the ratio is not a pure transfer
function. The results are unphysical (a positive slope for a low-pass, -3 to
+4.7 dB/octave instead of -12 or -24). The material is not enough — it needs
the same tone at two cutoffs. (That is what AWETEST later provided.)

### Measured filter properties (negative results, but solid)

Three sweeps over the ten cleanest pairs. Lower is better; **no variant
beat the default**, but several things were confirmed:

| variant | mean | colour |
|---|---|---|
| **default (2 poles, TPT, 8 kHz)** | **4.9688** | 2.5955 |
| `--loop-wrap off` | 4.9687 | - |
| `--resonance-curve faq` (Q>0 only) | 4.9694 | 2.6017 |
| `--filter-atten 0` | 5.1166 | - |
| `--filter-top 12000` | 5.1448 | - |
| `--filter-poles 4` | 5.3877 | 2.6856 |
| `--filter-poles 1` | 5.4460 | **2.4447** |
| `--filter-mode 86box` | 5.5483 | 2.5976 |

What follows:

- **The filter slope is 12 dB per octave.** Four poles and one pole are
  clearly worse. No documentation says it — it is measured.
- **The input attenuation by Q is right** (`--filter-atten 0` is 0.15
  worse), so the `filter_atten` table matches the card's behaviour.
- **The top cutoff of 8 kHz is right** (12 kHz breaks ho-tr5 from 2.99 to
  4.74).
- **Loop wrapping does not matter** — a difference of 0.0001 over ten pairs.
- **The Moog variant of 86Box is the worst of all**, exactly as the
  computation of its transfer predicted.
- One pole gives the best **colour** (2.44) and at the same time almost the
  worst score. A softer filter improves the overall tonal balance but breaks
  the per-note match — not the right fix, only a sign that energy around
  2 kHz is missing.

## Splitting the residue into loudness and colour

The score is one number and mixed two quite different defects: a note that
plays the right colour but louder, and a note with the right loudness but
another colour. `tests/resid_split.py` separates them — for every note it
computes a **flat offset** (weighted mean over the bands) and what remains
after subtracting it:

| component | our render vs hardware | hardware vs hardware (floor) | **left for us** |
|---|---|---|---|
| total | 4.1223 | 3.2278 | **2.56** |
| note loudness | 2.4022 | 1.6921 | **1.71** |
| note colour | 3.3500 | 2.7487 | **1.91** |

(Subtracted in quadrature, because that is how they add up.)

**Almost half of the fixable error is the loudness of single notes**, not
their colour. That had not been measured at all — all sweeps went after the
filter and interpolation, the other half. The attenuation registers match
the driver 100 %, so it is not the values but what the sound path does with
them. (This pointed at the envelopes — later the measured attack shape.)

## Two more dead ends

### The attack rate does not explain it

The spread of the flat offset by attack rate looked promising:

| attack | spread for us | hardware vs hardware |
|---|---|---|
| 0x60-0x6F | **3.42 dB** | 1.58 dB |
| 0x70-0x7F | 1.83 dB | 1.40 dB |

But all 450 notes with attack 0x60-0x6F are **one sample** — `hatopenms`.
So it is not a property of the attack rate, only another way of writing the
finding that the open hi-hat stands out most.

### Exclusive classes (`exclusiveClass`) — the Win95 driver does not cut notes

It looked like a strong lead: `SYNTHGM.SBK` has 16 zones in 7 exclusive
classes and two of the three most significant samples in `DANCE.MID` are in
them — `hatopenms` in class 1 and `triangle` in class 5.

The trace of the real driver did **not confirm** it at the time. On CRAZY
(7112 notes, registers 100 % the same) the driver issues 7239 releases
against our 7112 — 127 more, which first looked exactly like cutting. But
the distribution of **note lengths** is practically identical:

| | ours | driver |
|---|---|---|
| median note length | 54.7 ms | 53.5 ms |
| notes shorter than 30 ms | 3301 | 3301 |
| notes shorter than 50 ms | 3477 | 3489 |

(Later the win95 voice selection was transcribed from `SBAWE.VXD`, and the
exclusive class handling found there is now part of the `win95` family —
see `86box_comparison.md`.)

### Interpolation kernel length: eight points is the optimum

The residue breakdown showed an error depending on the sample and even across
instruments, which fits the interpolation. So a sinc kernel of 4 to 24
points was tested (`--sinc-taps`):

| kernel | mean |
|---|---|
| 4 points | 4.9776 |
| **8 points (default)** | **4.9688** |
| 12 points | 4.9726 |
| 16 points | 4.9815 |
| 24 points | 4.9992 |

Eight points is a local optimum and a longer kernel **monotonically
worsens** it. The sharpness of the interpolation is not the lever. (The card
later showed why: its interpolation is a smoothing cubic B-spline — see the
last part.)

That closed the fourth series of sweeps. In total **14 variants in four
runs, all worse than the default**: filter (cutoff, poles, resonance,
attenuation, topology), loop, interpolation. The default was a local optimum
in every direction explored — which is why the next step was no longer
guessing parameters but **measuring on real hardware** (see
`hardware_tests.md`).

## How far one can get: the noise floor of the metric

Three recordings of the same card (CT3980) playing the same `DANCE.MID`
gave, for the first time, a chance to measure something missing so far —
**how much of our score is not our error at all**. It is enough to run the
metric between two recordings instead of between render and recording
(`note_probe.py --warp-ours`):

| comparison | score |
|---|---|
| `bpx` vs `bp` — two digital recordings, differing only in driver version | **2.41** |
| `bp` vs `hw` — digital against analogue recording of the same card | **3.20** |
| our render vs `bp` | 4.12 |
| our render vs `hw` | 4.22 |

The lower bound is not zero. Even two recordings of the same card differ by
2.4 on this metric; that is the recording path, mp3 and the alignment
inaccuracy. Our 4.12 is thus about 2.4-3.3 above the reachable floor
(in quadrature), not 4.12.

**Practical conclusion: the goal is not "0" but getting close to 2.4-3.2.**
Improvements of 0.01, formerly taken as results, are noise against this
spread — only the mean over the whole set decides.

## Colour: what the score deliberately does not measure

The score subtracts the overall curve (the equalisation of the recording
path), so **a difference in colour does not show in it at all**. With a
hardware-against-hardware comparison its size can be stated:

| band | hardware vs hardware | us vs hardware |
|---|---|---|
| 60 Hz | +0.5 | -3.3 |
| 120 Hz | +0.7 | -2.7 |
| 240 Hz | +0.5 | -3.4 |
| 480 Hz | +0.7 | -2.6 |
| 960 Hz | +0.7 | -2.5 |
| **1920 Hz** | +0.6 | **-0.7** |
| 3840 Hz | -0.5 | -2.2 |
| **7680 Hz** | -1.3 | **-5.0** |
| weighted spread | **0.7 dB** | **2.4 dB** |

Two recordings of the same card are nearly identical in colour (spread
0.7 dB). Ours was flat at -2.9 dB (only loudness) **except in two places**:
at 1920 Hz 2.2 dB less and at 7680 Hz 2.1 dB more than it should be.

Both places had a common suspect — **the filter resonance**. In `DANCE.MID`
259 notes have `Q = 8` and `cutoff = 0xFF`, i.e. a resonance peak right
around 7.7 kHz, and they also get the input attenuation (`kFilterAtten`,
-6.0 dB at Q = 8), which takes away from the rest of the band.

## What was verified and is **not** wrong

So it is not examined again:

**The register -> cutoff conversion.** Creative released `SYNTHGM` as both
`.SBK` and `.SF2`, so for each zone the raw SF1 value and the absolute cents
per SF2 can be put side by side (`tests/cutoff_pairs.py`). Over 60 different
values the line is **4366 + 29.5 cents per register step** — exactly our
`kCutoffBaseCents` 4366 and `kCutoffCentsStep` 29.3843. The linear mapping
of Vu's guide (100 Hz + 31.25*reg) is off by a factor of five against it: at
register 120 it gives 3850 Hz instead of the 787 Hz Creative has there.
`--cutoff-map lin` is a dead end.

A special case at the end of the table: **SF1 127 -> 14400 cents (33 kHz)**,
i.e. "filter wide open", in 7 zones. It is not a point on the line. The
driver writes `127*2 = 254` to the register (measured on the preset
`piano2`), not 255.

**Volume and velocity curves.** Split by `--by vel` and `--by atten`: the
deviation **does not grow** with velocity or attenuation (a group of 381
notes has 0.2 dB, the outliers are only in bins of 15-20 notes).

**Layered notes.** 706 notes in `DANCE.MID` start two voices at once. It
should be a suspicious place (balance between layers), but it is not:
`--layers 2+` gives 4.17 against 4.23 for single-layer ones.

**Drums.** Channel 9 has 346 notes — half of the usable material — and a
deviation of **0.2 dB**.

## Two fixes of the metric

1. **The band weights were global.** `imp = W.mean(axis=0)` — a mean over the
   whole song. For an instrument with nothing in some band that band was
   counted with the weight of the whole song and the noise floor was
   measured. The kalimba (channel 0) came out as the worst group with
   "60 Hz +8.9 dB" — after switching to the weight of **that note** it became
   "762 Hz +1.1 dB" and the deviation fell from 2.8 to 1.6 dB. The default is
   now `--weight note`; `--weight global` brings the old one back. Numbers
   before this change are not comparable with those after it.

2. **`--layers`** separates single- and multi-layer notes, `--warp-ours`
   compares two recordings with each other (the noise floor table above).

## SBK -> SF2 export: three bugs found by the round trip

The conversion of a bank to SF2 can be verified strictly: load it back and
compare what comes out **per note**. A render of the Magic Carpet 2 intro
(671 notes) from the original `BULLFROG.SBK` and from the exported `.sf2`;
`--dump-notes` gives twenty registers for each note.

The first time nothing matched and the sound was 2.6 dB off. Three different
causes:

**1. Addresses 1/2/3 words off.** SF1 stores chip addresses directly
(already with the interpolator correction), SF2 stores indices — and
Creative describes the same sample 1/2/3 words differently in its own banks.
Our reader accounts for it (`- 1`, `- 2`, `- 3` in the SF2 branch), so the
writer must compensate the other way. Without it the loop came out **two
words shorter**: `loopEnd - ccca` was 0x1D50 instead of 0x1D52 on 668 of
671 notes.

**2. The sustain did not follow the driver's series.** It had `0x7F - v`, but
the driver does `v * 4 / 3` (clipped to 0x7F) — measured on `SYNTHGM`,
which we have in both formats. For sustain 99 the old series would give 28
steps of drop instead of zero.

**3. The asterisk in the sample name.** This was the main one. ROM samples
have a name with an asterisk at Creative (`*BellTree`) and our loader uses it
to reach into the ROM. When the sample is baked into the file it is no
longer in ROM — but the asterisk stayed, so the player looked for it in ROM
again, at address 0x14D1F, where completely different data lie. Channel 1
of the intro (glockenspiel) played **2.2x louder**.

Notably, **all twenty registers matched meanwhile**. The error was only in
where the samples are read from — which no register dump shows. It was found
by a per-channel breakdown: ch1 had +7.7 dB, the others below -20 dB.

After the fixes: levels 0.09067 against 0.09068, the channel with the baked
ROM sample -129 dB (i.e. exact), the rest -22 to -36 dB. That rest is no
conversion error — they are one-shot samples for which the driver puts the
loop **after** the sample (`end+4`..`end+8`); in the original bank its own
padding was there, in the export our 46 zeros as SF2 prescribes.

## Two banks at once

The SoundFont CD has `DEMO/SBK.TXT`, which says for each demo song which bank
to load into which slot — and two of them have **two**:

> 3. Dancesbk.mid - Load 9ftgrand.sbk on Bank 1, load Gmdrum.sbk on Bank 2.

`--sf file@N` moves the bank's presets to MIDI bank N and each bank gets its
own piece of DRAM after the previous one. Verified on `DANCESBK.MID`:
against a render with GM only, 99 % of the samples differ, so both banks
really are used.

## Material: three recordings of the same DANCE.MID

Three recordings from one card (CT3980) differing only in the recording
method and driver version. `pair_finder.py` assigned all three to
`DEMO/DANCE.MID` with an anchor match of 0.95-0.97 and a **margin of
2.4-2.5x** over the second candidate; the time conversion came out
1.000000 * audio + 0.35 s, i.e. without tempo drift. Alignment: 44-45
anchors, match 0.78-0.80, drift 6-12 ms.

Two of them are digital captures ("bit perfect"), so the equalisation of the
recording path is absent. The difference between `dance-hw` and `dance-bp`
measures what is still path noise and what is a chip property.

The other recordings of that batch were **not proven**: for Doom and Duke
Nukem 3D we do not have the right MIDI (the best candidate had a margin of
1.0-1.1x, which is noise), and `DOOMAWE.WAV` / `AWEWARCT.WAV` /
`HERETAWE.WAV` from the CD are game montages with sound effects, not clean
MIDI renders.

## The largest deviations are the recording's equalisation — the sign tells

The largest differences came out in the lowest bands — and they change sign
**by the source of the recording**, not by the song:

| source | difference at 60 Hz (recording minus us) |
|---|---|
| **FLAC from the demo CD** (georgia, relax, crazy, jump, mars) | +2.4 to **+10.1 dB** |
| YouTube recording (dance, pop, starman, styles1/2, symphony) | -2.1 to **-7.1 dB** |
| Hi-Octane mp3 (ho-tr1/3/5) | -7.9 / -8.0 / -7.9 |

Lossy sources have cut basses, the lossless CD does not. If it were our
error, the sign would be the same everywhere. That is exactly why
`note_probe.py` subtracts the overall curve.

After subtracting, the reliable pairs come out at 3.1-7.3. Higher are only
those with a known defect of the reference: `violin` 12.8 (only 10 anchors),
`mc2-game2` 10.9 (the game mutes channels), `concer` 8.0 (11 anchors).

**Conclusion: after the pan and metric fixes no gross defect of single
instruments remained on reliable material.** The median deviation per group
is 2.1 dB and Hi-Octane — the cleanest pair — has not a single group above
4 dB.

### False alarm: an offset of -46 words

In the ranking of the worst samples **all** had `ccca` exactly 46 words
before the start of the next sample in the bank. It looked like an off-by-
one-sample error. But the well-matching samples showed the same offset —
it is just the padding SoundFont prescribes between samples.

## Two traps in measuring

**1. A short window.** Twice a conclusion was drawn from a window that did
not cover what was to be measured:

- the loop of `belltree` has 1197 samples, 4096 were measured — so 71 % of
  the data came from **after the end of the sample**, and the result was
  "the loop is bassy" (it is not, its centroid is 17.4 kHz);
- the spectrum of the Hi-Octane render was taken from the first 6 s, a high
  prelude, and the result was "we lack bass completely" (we do not; over the
  whole length 43.0 against 41.1 %).

**2. The score grew with the note count.** The weighted score summed over all
notes and bands but divided only by the sum of weights — so it grew with the
square root of the note count (667 notes = a factor of 25.8). Within one pair
the factor is constant, so comparisons of variants stayed valid.

## The largest deviations (`worst.py`)

    python tests/worst.py --top 22

Groups by **sample and channel** and adds up over all pairs: when the same
sample is off in three songs, it is a property of the sample, not an
accident of the mix.

The median deviation is **2.4 dB**; above ~7 dB it is no longer a nuance.
The worst findings are in `mars` (10 groups above 4 dB) and `symphony` (7),
while `dance`, `starman`, `georgia`, `crazy` and `mc2-menu` have none.
`mars` has only 6 anchors, so part of it may be bad alignment.

## Findings from an external review (`analyze/EMU8000-next.md`)

### C — filter reset: bug confirmed, inaudible

At note start only `filtIc1/filtIc2` were cleared, not `filtIc3/filtIc4`
(the second cascade stage) and `filtLp1` (one-pole mode). Fixed. **No effect
on the score** (4.8388 before and after) — those states are unused with the
default two poles.

### D — Q base: the finding holds, the fix does not help

The code had `max(0.7071, pow(10, res/20))`, but `pow` is always `>= 1` for
`res >= 0`, so 0.7071 **never** applied. For Q = 0 to mean Butterworth, the
base must **multiply**. `--q-base` was added and measured: the difference
goes both ways and averages 0.015 dB. **Not decisive.** The default stays
1.0.

### J — interpolation: slightly in favour of linear

| variant | mean over 10 pairs |
|---|---|
| Catmull-Rom (default then) | 6.105 |
| **linear** | **6.014** |
| 86Box | 6.081 |

Half of that improvement came from `violin` alone, the least reliable pair.

## Trap: one song lies

Twice in a row a conclusion from one song did not hold over the whole set.

**`--filter-top`.** On DANCE the score fell monotonically (8000 -> 4.839,
20000 -> 4.789). Over all ten pairs it goes **up** (6.105 -> 6.116 ->
6.145).

**Our core against 86Box.** On DANCE 86Box is clearly closer to the hardware
(4.33 vs 4.84). Over the whole set they are **even** (6.081 vs 6.105).

Conclusion: **nothing is decided below five pairs.**

## Finding: pan — fixed

A capture of the sound straight from 86Box showed on the MC2 intro a flat
difference of 6.1 dB between our core and `snd_emu8k.c` with **identical
register writes** (24/24 on all 262 notes). `snd_emu8k.c` has:

```c
emu_voice->vol_l = emu_voice->psst_pan;        // 0..255
emu_voice->vol_r = 255 - (emu_voice->psst_pan);
(*buf++) += (dat * emu_voice->vol_l) >> 8;
```

The chip's pan is a **plain multiplication**, not a constant-power
`sin/cos` as we had. In the centre the multiplication gives -6 dB per
channel, sin/cos only -3 dB — the difference of 3.01 dB matches the measured
3.37-3.41 dB on isolated notes. 86Box already had it right; only our core was
fixed. Linear pan is the default since 2026-09-02 (verified against 20
pairs: linear 6.026, constant power 6.089). The tester's card confirmed it on
the line output (linear up to 192).

## Finding: "3 Point sample interpolation" from the documentation

Vu's Un-official AWE32 Programming Guide (1995) says "3 Point sample
interpolation" for the chip. A quadratic (Lagrange) interpolation over three
points was implemented (`Point3`, `Point3c`) — against 20 pairs Point3 5.937
against 6.025 for Catmull-Rom — and carried over into `snd_emu8k.c`
(`EMU8K_READ_INTERP_POINT3`). (Superseded: the card's line output shows a
cubic B-spline; POINT3 had rested on the non-linear internal capture — see
below.)

### The filter bypass condition

With identical pan and interpolation the curve spread between our core and
`snd_emu8k.c` on the MC2 intro fell from 6.1 dB to 3.4 dB. The rest was flat
(+3 to +3.7 dB from ~500 Hz up) — the filter: the driver writes the cutoff
as `cutoff << 8` = `0xFF00`, not `0xFFFF`, so the condition in `snd_emu8k.c`
for bypassing the filter (`filterq_idx == 0 && cvcf_curr_filt_ctoff ==
0xFFFF`) never holds — the filter runs even at "fully open". We had
followed the Programmer's Guide and switched it off.

A correction of an earlier mistake: the first attempt to port this took the
wrong branch — `FILTER_INITIAL`, which is disabled with `#if 0` in
`snd_emu8k.c`. The active one is **`FILTER_MOOG`**.

## Where our chip differed from 86Box

Both variants were driven by **the same layer with the same register
writes**, so the difference was purely in the chip. On isolated notes
(`make_probe_mid.py` + `env_cmp.py`, 20 notes, 4 instruments, 5 octaves):

| | difference (86Box minus us) |
|---|---|
| peak | **-3.41 dB** |
| energy | **-3.37 dB** |
| rise time | 0.0 ms |
| drop by 20 dB | -7.3 ms |

— which was the pan law above.

## Notes start better than they continue

The probe can measure with a shifted window (`--delay`) and the pan
separately (`--stereo`). On DANCE against the hardware:

| window | score |
|---|---|
| onset | 4.84 |
| pan | 4.50 |
| decay +0.25 s | **5.15** |
| decay +0.6 s | **5.10** |

The onset fits better than the decay. That points at **the envelope and the
effect chain**, not at the samples or the filter at the onset.

---

## Measurements on the tester's card (AWETEST v05–v28)

From September 2026 a tester ran AWETEST (`tools/awetest/`) on a real
Sound Blaster AWE32 (DSP 4.13, 8 MB DRAM) and sent the recordings — both the
card's own internal capture through its ADC and, from v25 on, the line
output recorded externally. The recordings and logs are in the `sources`
branch ([DATA.md](../DATA.md)). The analysis method: the same program runs in
our DOS VM under 86Box with a port-write trace, the trace is replayed through
our chip (`--replay`), and card and render are compared block by block,
aligned by the per-note timestamps of the log.

### Which recording path to trust

- **The internal capture is not linear.** It compresses loud signals and
  has a tilt of about **+2.6 dB/octave** across 56 Hz - 14 kHz (+1.9 dB/oct
  against the line output). For spectral comparisons each candidate's own
  line in dB against log f is subtracted; level comparisons use the line
  output where available.
- **The tester's card has a dead dry right channel**: the right line output
  carries only the effect returns (the dry signal is left only). Stereo pan
  was measured on the left channel; effects are compared on the right.
- **The earlier external recordings (AWETST26/27) were clipped by the
  recorder.** Conclusions about saturation and headroom from them were
  withdrawn; the recorder level was lowered to about -24 dBFS for AWETST28.
- **The card occasionally loses a note-off** (a DCYSUSV 0x0080 write that
  does not take effect). Those notes are excluded; it is not a chip
  property.
- The very first recordings (`ver3`, `test4`) were partly invalid: AWETEST
  then played its test tone on voice 31, which the driver reserves for the
  DRAM refresh, and IP writes do not take effect there.

### What was confirmed (no change needed)

| quantity | card | model |
|---|---|---|
| IFATN attenuation | 0.3735-0.3769 dB/step | 0.375 |
| sustain | 0.7477 dB/unit | 0.75 |
| pan | linear in amplitude | linear |
| LFO1 rate | 0.9955x [PG] | `0.01 + v*0.042` Hz |
| envelope delay (ENVVOL) | 725.8 us/unit | 725 us |
| vibrato LFO1/LFO2 | symmetric triangle from zero, ±1 oct at full depth | same |
| PEFE -> pitch | 0.97-0.99x of ±1 octave | same |
| pitch (block 4) | median -0.26 cent, max 0.82 cent | same |
| LFO1 -> filter, PEFE -> filter | within ±1.7 dB | ±3 / ±6 octaves |
| mod. envelope decay | matches | same |
| volume slide at note-off | 6.0 dB/ms to -40 dB | `cur += (target-cur)/64` per sample |
| attack 0x7F onset | within 1 dB from 1.5 ms | same |
| pitch change while playing | switches at once | same |
| filter cutoff map | register 0 = 101.81 Hz, 29.38 cents/step (0.5-0.8 %) | same |

### What was corrected on the evidence of the card

- **Hold:** 93.1 ms per step = 4096 samples, not the 92 ms of the
  Programmer's Guide (`kHoldSecPerStepChip`). The bank conversion keeps the
  driver's `idiv -92`.
- **Tremolo is one-sided:** attenuation only, 0 .. ~12 dB at full depth, in
  the half period where `lfo * depth < 0`; the other half period stays at
  0 dB.
- **Equalizer:** the card has bass/treble shelving EQ at the output, set
  from INIT3/INIT4. All drivers (Win95, DOS MDI, SDK) write treble 9 = a
  shelf of +7.9 dB at f0 2457 Hz (+1.2 dB at 1 kHz, +5.7 at 4 kHz, +7.3 at
  8 kHz). All 24 positions were fitted with RBJ shelves (S = 0.5, residual
  ≤ 0.07 dB rms). Neither 86Box nor our core had it. It acts on the effect
  returns too.
- **Filter = Chamberlin state-variable filter at 44.1 kHz.** A global fit
  over 157 notes (blocks 6/7): Chamberlin 0.80 dB rms with our cutoff map,
  analog 2-pole 1.48, Chamberlin at 2x rate 1.20, bilinear TPT 2.78. Q0 is
  0.931, 1.175 dB per Q step (Q15 = 17.6 dB), the input attenuation by Q
  equals `kFilterAtten` within 0.3 dB. The deep stopband (block 41) matches
  within ~0.5 dB. Still open: at cutoff 32-64 the card has a gentler slope
  1-3 octaves above the cutoff (+2..+7 dB).
- **Attack shape:** the card's volume attack is not linear: ~0 up to 0.1 T,
  then nearly linear, faster between 0.85 and 0.9 T, reaching 1.0 at 1.0 T
  (the same in the internal and the external recording). Implemented as a
  21-point table. The small overshoot after the attack (+3..6 % up to
  ~1.2 T) is not modelled. The **modulation envelope attack is strongly
  convex** (`1 - (1-x)^13.5`).
- **Interpolation = cubic B-spline** (4 points, approximating). AWETST28
  block 44 (noise played -3 .. +2 octaves off its pitch): card minus render
  per third-octave band within 0.1 dB up to 4 kHz at every pitch. rms over
  the block: B-spline 2.4 dB, quadratic B-spline 3.4, Catmull-Rom 5.5, sinc
  6.4, 3-point Lagrange 7.4, linear 7.6. The earlier choice of POINT3 rested
  on the non-linear internal capture; the "analog high cut at unity pitch"
  seen before was mostly the interpolation.
- **Reverb** (presets 0-5 fitted on the line output, the right channel
  carrying the reverb alone): room size, output gain and 17-tap early
  reflections at 1-51 ms (deconvolved from a 10 ms noise click, NNLS), with
  damping 0.03-0.10 (the card's reverb is flat from 200 Hz to 4 kHz and loses
  only the top in the loop). Time windows 0-25 .. 600-1200 ms after a tick
  within 0.3-1.3 dB; the tail keeps a ±1.5 dB curvature. **Echo presets 6
  (Delay) and 7 (Panning Delay):** echoes every ~117 ms, the first echo at
  full level (+5.4 dB against the dry tick), then a geometric series.
- **Chorus:** the feedback matches the register (-24/-12/-6/-2.5 dB per pass
  for 0x10..0xC0); the loop is **linear** — no saturation (send 32->255 =
  18.1 dB, constant third harmonic). The flanger (preset 5) matches in the
  left channel (modulation 32.8 vs 32.6 dB, correlation 0.96). The "slow tail
  after a tick" seen at first was a DC step through the analog coupling of
  the line output, not the chip.
- **Output headroom:** the chip's output is scaled by 57737/65536
  (-1.1 dB) after the effects and EQ — the card's ceiling with 3/4/6/8 voices
  in phase (+8.6/+9.3/+9.8/+10.0 dB) against the render
  (+8.5/+9.2/+9.7/+9.9 dB), independent of the mixer level, so it is the
  chip.
- **A core bug found through the replay:** a CCCA write did not set the
  voice address at once (it was taken only at note-on) — now as in 86Box.
- **The register side effects of the voice follow 86Box 1:1** (a note starts
  only on the DCYSUSV off -> on transition, envelope restarts by the trigger
  bits, release bits).

### What is still open

Everything left would need guessing the internal structure rather than
reading it off a recording: the filter slope at cutoff 32-64, a small bump of
our reverb network around 1 kHz, the ±1.5 dB curvature of the reverb tail,
and the overshoot after the attack.

## Tools

    python tests/tune.py --warp          # once: align
    python tests/tune.py --try "--interp linear"
    python tests/tune.py --only dance --detail --by sample

    python tests/note_probe.py --warp w.json --trace n.trace \
        --notes n.csv --ours ours.wav --ref ref.wav [--stereo|--delay 0.25]
        [--by sample|ch|note|atten|vel]

    python tests/make_probe_mid.py probe.mid --program 0
    python tests/env_cmp.py a.wav b.wav --trace probe.trace

One variant over all ten pairs takes **~3.5 minutes**. The AWETEST analysis
scripts are in the `sources` branch, `analysis/`.

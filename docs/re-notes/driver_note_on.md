# What the driver does on top at note-on

Found in `SBAWE32.DRV` (the Windows AWE32 MIDI driver), where it builds its
own patch structure from the SoundFont. These are things that are **neither
in the SoundFont nor in the Programmer's Guide** — they follow only from the
code.

The structure is addressed through `si`; the important fields:

| offset | meaning |
|---|---|
| `[si+0x18]` | filter cutoff (high byte of IFATN) |
| `[si+0x3A]` | ENVVAL — modulation envelope delay |
| `[si+0x3C]` | modulation envelope attack |
| `[si+0x4A]` | ENVVOL — volume envelope delay |
| `[si+0x4C]` | volume envelope attack |
| `[si+0x4E]` | volume envelope hold (in ms, then overwritten with the register value) |
| `[si+0x50]` | volume envelope decay |
| `[si+0x56]` | keynumToVolEnvHold |
| `[si+0x58]` | keynumToVolEnvDecay |
| `[si+0x64]` | note number |
| `[si+0x66]` | velocity |
| `[si+0x68]` | patch attenuation |
| `[si+0x74]` | sampleModes |

## 1. Velocity affects the filter cutoff  (`0x021E`)

```
0200  cmp  [bp+4], 9         ; channel 9 (drums) has its own branch
0204  jne  0x21E
021E  cmp  [si+0x4c], 0x7D   ; only when the attack rate < 0x7D
0222  jge  0x246
0224  mov  ax, [si+0x66]     ; velocity
022A  cmp  ax, 0x46          ; lower limit 70
022F  mov  [bp+8], 0x46
0237  imul word [si+0x18]    ; cutoff * velocity
023A  add  ax, 0x40          ; rounding
0241  idiv cx                ; / 0x7F
0243  mov  [si+0x18], ax
```

So:

    if (channel != 9 && attackRate < 0x7D)
        cutoff = (cutoff * max(velocity, 0x46) + 0x40) / 0x7F;

Softly played notes are darker. Drums are not adjusted this way. (The Win95
VXD has `(cutoff * v + 0xA0) >> 7` instead — see `mc2_game.md`.)

## 2. Envelope dependence on the note number  (`0x0278`)

```
0278  ax = 0x3C - [si+0x64]     ; 60 - note
027E  imul [si+0x56]            ; * keynumToVolEnvHold
0281  add  [si+0x4e], ax        ; hold +=
0284  jns  0x28B
0286  [si+0x4e] = 0             ; non-negative

028B  ax = [si+0x64] - 0x3C     ; note - 60
0291  imul [si+0x58]            ; * keynumToVolEnvDecay
0295  sub  [bp-6], ax           ; decay -=
029B  if (< 0) decay = 0
```

Relative to **note 60**. Higher notes have a shorter decay, lower notes a
longer hold.

## 3. Hold to register  (`0x02A9`) — confirmation

```
02A9  ax = [si+0x4e]     ; hold in ms
02AC  cx = 0xFFA4        ; -92
02B0  idiv cx
02B2  add  ax, 0x7F
02B5  [si+0x4e] = ax
```

So `holdReg = 127 - holdMs/92`, exactly as the Programmer's Guide says
("hold time in 92 msec increments, 0x7f = no hold time").

## 4. Non-looped sample  (`0x02C7`)

```
02C7  test byte [si+0x74], 1    ; sampleModes bit 0 = loop?
02CB  je   0x2E4
      ; looped: loopStart = [si+8], loopEnd = [si+0xC] + 1
02E4  ; non-looped:
02EA  ax = [si+0xC] + 4         ; loopStart = end + 4
02FC  ax = [si+0xC] + 8         ; loopEnd   = end + 8
```

The EMU8000 has no "one-shot" mode, so the driver puts the loop **into the
silence after the sample** — the format appends 46 zero samples after every
sample, so the offsets +4 and +8 safely fall into them. The voice then goes
silent after playing and the envelope attenuates it.

## 5. The delay register at an instant attack  (`0x0206`)

```
0206  cmp [si+0x3c], 0x7F       ; mod envelope attack == max?
020C  mov [si+0x3a], 0xB7FF     ;   -> ENVVAL = 0xB7FF
0211  cmp [si+0x4c], 0x7F       ; volume envelope attack == max?
0217  mov [si+0x4a], 0xB7FF     ;   -> ENVVOL = 0xB7FF
```

(The listing labelled this the drum branch with 0xB7FF; measured on the
Win95 driver it is **0xBFFF** and applies to all channels — see "Attack and
delay registers" below.)

---

# The whole write sequence of a note — measured, not read

The above is what could be read from the code. This is what the driver
**really** wrote: a Georgia trace from `SBAWE.VXD` under Windows 95 in 86Box,
extracted with

```bash
python tests/voice_seq.py tests/out/georg_win95.trace --note 2
```

The order is left to right, top to bottom; `^` is the upper half of a 32-bit
register.

| step | driver | ours (then) |
|---|---|---|
| end of the previous note | `DCYSUSV 8029` **and `DCYSUS 8027`** | only `DCYSUSV 8029` |
| silencing before the new note | `DCYSUSV 00FF` | `DCYSUSV 0080` |
| targets to silence | `VTFT FFFF` + `CVCF FFFF`, **VTFT twice** | once |
| parameter block | `ATKHLDV, LFO1VAL, ATKHLD, DCYSUS, LFO2VAL, IP, IFATN, PEFE, FMMOD, TREMFRQ, FM2FRQ2, ENVVAL, ENVVOL` | the same contents, but **after** the addresses |
| addresses — clearing | `PTRX 0000`, `CPF 0000` | missing |
| addresses | `PSST, CSL, CCCA` (with `CCCA^ = 0000`) | `PSST, CSL, CCCA` directly with Q |
| **`Z1 = Z1^ = Z2 = Z2^ = 0`** | yes, for every note | **missing entirely** |
| `CCCA` a second time, now with Q | `CCCA^ 6000` | - |
| filter targets | `VTFT FE00`, `CVCF FE00` | `FF00` |
| start | `PTRX 523D/1ED7`, `CPF 0000/1ED7` | `ENVVOL`, `ATKHLDV` |

That exactly explains the register census of `trace_diff.py`: `CCCA`, `CPF`,
`PTRX` and `DCYSUS` have **twice** the writes at the driver, `Z1`/`Z2` 3363
(once per note) against our 32 (initialisation only).

## Which values did not match (then)

`notes_diff.py` on Georgia, 3331 paired notes:

| register | match | typical difference |
|---|---|---|
| `FMMOD` | 85.6 % | high byte +3 on one note, low +8 on 478 notes |
| `FM2FRQ2` | 82.1 % | low byte (LFO2 frequency) **times 2** on 595 notes |
| `VTFT` / `CVCF` | 79.9 % | high byte -1 on 669 notes |
| `CCCA^` | 79.9 % | Q **+1** on the same 669 notes |
| `ATKHLD` | 79.9 % | low byte -1 on the same 669 notes |
| `TREMFRQ` | 75.3 % | frequency **times 2** on 824 notes, tremolo +35 on 478 |
| `ENVVOL` | 74.7 % | `8000` vs `BFFF` on 844 notes |
| `IFATN` | 69.6 % | high byte (cutoff) -1 on 669 notes |
| `ATKHLDV` | 60.3 % | low byte +2 on 844, -1 on 477 |
| `PEFE` | 57.7 % | low byte +1 on 669, +63 on 629 |

**The key finding:** the cutoff, `Q` and the mod envelope attack diverge on
**exactly the same 669 notes** — the group overlap is 1.000, not 0.99. Three
independent generators diverge at once. (Resolved below: it was one preset
and its SF1 conversions.)

`ENVVOL 8000` vs `BFFF` makes no difference to the sound — bit 15 means "no
delay" and the lower 15 bits are then ignored (`ENVVOL_TO_EMU_SAMPLES`). But
it matters for matching the trace.

## After aligning the sequence

`Synth::NoteOn` now has exactly the sequence above for the `win95` family;
`dos` has its own branch. Census after the fix:

| register | before | after | driver |
|---|---|---|---|
| `DCYSUS` | 3363 | **6691** | 6691 |
| `Z1`, `Z1^`, `Z2`, `Z2^` | 32 | **3363** | 3363 |
| `CCCA`, `CCCA^` | 3365 | **6696** | 6754 |
| `CPF`, `CPF^` | 3365 | **6696** | 6724 |
| `VTFT`, `VTFT^` | 6696 | **10027** | 10055 |
| `PTRX`, `PTRX^` | 4421 | **6697** | 6754 |
| `IP` | 4418 | 4418 | 4418 |

In total 159 220 writes against 160 480 at the driver, i.e. within 0.8 %.

### Side finding: PTRX is not written on a pitch bend

`IP` had 4418 writes both for us and the driver, but `PTRX` 4421 for us
instead of 3365 — about a thousand extra writes. `Synth::RefreshChannel`
wrote `pitch << 16` into the upper half of PTRX on a pitch bend. But **the
upper half of PTRX is a linear increment, not the logarithmic IP** — so it
overwrote the right value the chip computed itself from the IP write. The
real driver does not touch PTRX on a pitch bend at all. Fixed.

### No audible change yet

Against a capture of the real driver on the same chip (`--chip 86box`) the
envelope correlation stays **0.9471** before and after. It makes sense: most
of those writes end in the same register state within one frame — 86Box only
stores `Z1`/`Z2`, the second `CCCA` write carries the final value, `DCYSUSV
00FF` and `0080` both have the "engine off" bit and the start write
overwrites the lower bits.

**So the rest of the difference is in the values, not in the sequence.**

---

# SF1 -> register conversion, calibrated on Georgia

The 669 notes where the cutoff, `Q` and the mod envelope attack diverged at
once **were neither another zone nor velocity**. Sorting the notes by sample
address makes it clear:

| | ours -> driver |
|---|---|
| `Q = 0` (preset "Piano 1") | cutoff 255->255, atkMod 125->125 |
| `Q != 0` (preset "Piano 2") | cutoff 255->**254**, Q 5->**6**, atkMod 127->**126** |

So it is one specific preset and its generators. `SYNTHGM.SBK`, instrument
`piano2`, global zone: `initialFilterFc 127`, `initialFilterQ 50`,
`attackModEnv 6`.

## `initialFilterFc`: plain doubling

Calibration from four Georgia presets:

| SF1 | driver | |
|---|---|---|
| 52 (`fretlessbs`) | 104 | 52x2 |
| 97 (`jazzgtr`) | 194 | 97x2 |
| 127 (`piano2`) | **254** | 127x2 |
| missing (`organ3`, `tuba`) | 255 | default from the driver table |

Formerly `v * 255 / 127` was computed so that 127 gave 255. **That fix was
grafted onto a wrong measurement** — preset 52 `Choir Aahs` in Magic Carpet 2,
where the driver wrote cutoff 255, but `choiraahs` has no `initialFilterFc`
at all, so it was the **default value**, not the conversion of 127. A real
127 appeared only here and gave 254.

MINUET does not suffer from it: it has cutoffs 220 and 178, i.e. 110x2 and
89x2.

## `initialFilterQ`: a shift by three bits

| SF1 | driver | `v*15/127` (before) | `v>>3` |
|---|---|---|---|
| 12 | 1 | 1 | 1 |
| 50 | **6** | 5 | 6 |
| 79 | 9 | 9 | 9 |

`lround(v * 15 / 127.0)` fits the same three points too — a note with
`initialFilterQ` **6, 14 or 22** would decide. None of our traces has one.
`>>3` was chosen because it is one instruction and a 16-bit driver of 1994
would most likely do it that way.

## Result

| register | before | after |
|---|---|---|
| `CCCA^` (Q) | 79.9 % | **100 %** |
| `VTFT` | 79.9 % | **100 %** |
| `CVCF` | 79.9 % | **100 %** |
| `DCYSUSV` | 100 % | 100 % |
| `IFATN` | 69.6 % | **89.6 %** |

The sound did not move (envelope correlation 0.9471 -> 0.9468): cutoff 255
instead of 254 is imperceptible with the filter wide open, and `Q` 5 vs 6 is
one resonance step. But the registers are now right, and further fixes no
longer stand on a wrong base.

---

# Attack and delay registers, calibrated on Georgia

Another group, **844 notes**: `ATKHLDV`, `ENVVOL`, `VTFT^` and `CVCF^`
differed on the same notes (`ENVVOL` x `VTFT^` Jaccard 1.000). Splitting by
sample and `Q` showed three behaviours of the driver:

| | attack | `ENVVOL` | `VTFT^` |
|---|---|---|---|
| A — generator `attackVolEnv` **missing** | 0x7D | 0x8000 | 0 |
| B — `attackVolEnv = 0` | **0x7F** | **0xBFFF** | **target volume** |
| C — `attackVolEnv = 6` | **0x7E** | 0x8000 | 0 |

The key was the preset **Honky-Tonk (prog 3), which has two layers**:
`honkytonk` with `attackVolEnv 6` and `shonkytonk` with `attackVolEnv 0`.
That is why the notes with that sample split exactly in half (114 and 114) —
not two zones by velocity, but two voices per note. Group B also contains
drums (`snare24`, `bd15`, `paisteping`, `rideping`, `floortombrite`).

## What was wrong

1. **`AttackRateFromMs` returned `r` instead of `r-1`.** The driver picks —
   as for decay — the entry whose time is *longer than or equal to* the given
   one. The table has 5.99 ms at 0x7F and 6.19 ms at 0x7E, so 6 ms belongs to
   0x7E, not 0x7F.
2. **A missing generator merged with a zero time.** `timeMs(..., 0.0)`
   returns zero in both cases, and the function returned 0x7D for zero. Right
   is: missing -> 0x7D (driver table default), `= 0` -> 0x7F.
3. **The delay register at an instant attack.** When the attack comes out at
   0x7F, the driver writes **0xBFFF** to `ENVVOL` (or `ENVVAL`) instead of
   0x8000 — the branch at `SBAWE32.DRV` 0x0206, measured as 0xBFFF and valid
   outside channel 9 too. No effect on the sound, but it is in the trace.

## Result

| register | before | after |
|---|---|---|
| `ATKHLDV` | 60.3 % | **100 %** |
| `ATKHLD` | 79.9 % | **100 %** |
| `ENVVOL` | 74.7 % | **100 %** |
| `ENVVAL` | 100 % | 100 % |

## Then open: the target volume `VTFT^` / `CVCF^`

In group B the driver also writes a **target volume** into the upper half of
`VTFT` and `CVCF` (both the same value, 844 of 844), so the voice starts loud
right away instead of sliding there. It is not `attentable[atten]` from
86Box — the driver's value is always smaller:

| atten | driver | `attentable` |
|---|---|---|
| 114 | 0x01AE | 0x01DD |
| 104 | 0x0297 | 0x02DF |
| 80 | 0x0756 | 0x0818 |

A deterministic function of `atten` (59 distinct values, no contradiction);
a fit gives `60252 * 0.957567^atten`, a step of **0.3766 dB**. (Resolved
later: a 16-entry mantissa table in `SBAWE.VXD` at file offset 0x8DB0, see
below and `Awe32Curves.h`.)

---

# Fourth round: modulation, drum attenuation, loops, delay

## 1. Modulation depths and LFO frequencies are **doubled** in SF1

Measured on 3331 notes of Georgia, **without a single exception** (every
mismatch was exactly double):

| generator | register | ours -> driver | notes |
|---|---|---|---|
| `modEnvToFilterFc` | `PEFE` low | 3F -> 7E, 01 -> 02 | 1410 |
| `modLfoToFilterFc` | `FMMOD` low | 08 -> 10 | 478 |
| `modLfoToVolume` | `TREMFRQ` high | 23 -> 46 | 712 |
| `freqModLFO` | `TREMFRQ` low | 12 -> 24 | 824 |
| `freqVibLFO` | `FM2FRQ2` low | 2C -> 58 | 595 |

Pitches, on the other hand, are **not doubled**: `vibLfoToPitch` matches 03
and FF on 595 notes and `modLfoToPitch` the value 01 on 111 notes. The
dividing line is pitch against filter/volume, not SF1 against SF2.

For `freqModLFO` the doubling applies only when the generator exists; when it
is missing, 128 goes to the register directly (not 64x2).

## 2. Drum attenuation — the sum must be made in register units

The remaining 345 mismatching `IFATN` were **all on drums**. The preset
"Standard" (bank 128) has attenuation on both levels: the preset zone 127 and
each key zone of the instrument its own (121 for `snare24` on key 38, 112 on
40, …).

`AddFrom` added the **raw SF1 values** (121 + 127 = 248) and `127 - 248` then
dropped to zero. Correctly each level contributes `127 - v` register units
and those are added. The measurement fits exactly: the driver always had
`127 - zone attenuation` more than we did (zone 121 -> +6, 112 -> +15, …).

Melodic presets never showed it, because their instrument zones have no
attenuation. So the region keeps `sf1AttenUnits` separately from the combined
`GenSet`.

## 3. Loop offsets were not applied for SF1

`PSST` and `CSL` did not match on 232 notes, all on the sample `organwave`
(preset Organ 3), whose zone has `startloopAddrsOffset -1` and
`endloopAddrsOffset -1`. The SF1 branch ignored them.

## 4. The delay register step

`LFO1VAL` was one step off on two presets:

| generator | driver | ours (before) |
|---|---|---|
| `delayModLFO 120` (`jazzgtr`) | 165 | 166 |
| `delayModLFO 260` (`tuba`) | 358 | 359 |

(Resolved later: the step is **725 us**, as the SDK header `SFTYPE.H` says —
see "Correction: the delay is linear" below.)

## Now it is audible

The first three rounds did not move the sound. This one did — mainly thanks
to the drum attenuation, which made up to 16 units, i.e. 6 dB extra on the
cymbals.

Against a capture of the real driver on the same chip (`--chip 86box`):

| band Hz | before (round 3) | after |
|---|---|---|
| 1600-3200 | -0.6 | **-0.1** |
| 3200-6400 | -0.7 | **-0.2** |
| 6400-12800 | +1.9 | **-0.3** |
| 12800-22050 | +3.0 | **+0.6** |

Up to 12.8 kHz it is now **within 0.3 dB** across the whole spectrum.

---

# Pitch: `sub_192E` and where the ±1 comes from

Traced through the 86Box CPU trace and the disassembly, not through the
registers.

The driver builds `IP` in two steps. First it adds everything **in cents**
(`SBAWE.VXD` 0x1DBC..0x1DEB) and runs it through the conversion `sub_192E`
(0x192E):

```
esi = cents + 0x41A0        ; 16800, so everything is positive
edi = esi / 0x4B0           ; 1200 -> octave, clipped to 15
edx = esi % 0x4B0           ; remainder in cents
IP  = (edi << 12) | (edx*3 + (edx*31)/75)
```

`3 + 31/75` is exactly `4096/1200`, so the formula has no distortion of its
own. Transcribed 1:1 (`PitchFromCents` in SoundFont.cpp) instead of the
former `kPitchUnity + log2(...) * 4096` in doubles.

The values the driver wrote on the remaining 67 notes (`DC82`, `D72D`) **are
not in the image of `sub_192E`** at all — no integer input in cents gives
them; the function jumps by 3 to 4. So the difference arises **after** it,
in the second step (0x1E9C), where two channel components are added directly
in IP units.

## The ±1 in IP: pitch bend, not the pitch conversion

The channels with an `IP` difference were 1, 3 and 7 — exactly **the only
three** channels of Georgia with pitch bend. At a full bend down and a range
of 2 semitones that is **-682.667** IP units. The driver **truncates towards
zero** -> -682; we rounded -> -683.

Result: `IP` 98.0 % -> **99.1 %**.

### Dead end on the way

CC1 (modulation wheel) also runs only on ch1 and ch3, so it looked like the
explanation. It is not: **all 37 notes with +1 have CC1 zero**. A correlation
of channels is not a cause.

## Is 86Box reliable for measuring?

The worry that the emulator puts nonsense into the trace when it cannot keep
up is justified, but did not come true for these measurements. Two
independent runs of Georgia on different days:

| | |
|---|---|
| note count | 3331 and 3331 |
| registers differing between runs | **none** |
| offset of absolute time | 61892 frames (another boot moment) |
| divergence of relative spacing | max **135 frames in 150 s**, i.e. 3 ms |

The guest runs deterministically — 86Box computes in emulated time, and when
the host cannot keep up it only slows down in real time.

## The remaining 30 voices on ch7: timing

It first looked like the driver used a bend range of 22 semitones instead of
12 (a constant ratio of 1.833 between "the offset the driver must have used"
and ours). **That was wrong**: the offset was derived from the IP difference
against a base computed from **our** note, and the ch7 preset has two layers
of different pitch. A hook on the channel structures (`AWE32_TRACE_CH_OFF/LEN`)
decided it. The channel table is at **`EDI + 0x44F`, step 0x24**:

```
+0x00  byte   pitch bend range        (ch0..ch6 = 2, ch7 = 12)
+0x01  word   channel tuning          (0 everywhere)
+0x07  dword  computed bend offset
```

Measured on 4418 IP writes:

| range | tuning | offset | count |
|---|---|---|---|
| 12 | 0 | 0 | 2169 |
| 12 | 0 | -1706 | 966 |
| 12 | 0 | 320 | 22 |
| 12 | 0 | 5 | 42 |

`offset = bend * range / 24`, so for bend 640 it is 320 — **exactly what we
compute**. The remaining 30 voices of ch7 differ in **which bend was in
effect at the note-on moment** — our sequencer hits the note at another point
of the bend ramp than the MPU-401 in the guest.

Lesson: do not derive a quantity from a difference of results when it can be
measured directly. Two conclusions were built on it and both were wrong.

### RPN and pitch bend handling in SBAWE.VXD

The MIDI dispatcher is at `0x694` (`and eax,0xf0`, then branches for
0x80..0xE0). Control change (0xB0) calls `0x38F5`, pitch bend (0xE0) calls
`0x3D3B`. The channel structures have a step of **0x24** and lie at
`edi + ch*0x24`.

**Data entry MSB (`0x35DD`)** — for RPN 0 it stores the value directly as a
byte:

```
[esi+0x45e] == 0x100 ? RPN : NRPN
[esi+0x460] == 0  -> [esi+0x44f] = value      ; bend range in semitones
[esi+0x460] == 2  -> [esi+0x454] = clamp(v-0x40,-24,24) * 100   ; coarse tuning
[esi+0x460] == 1  -> [esi+0x452] = ((v<<7|lsb) - 0x2000)*100 >> 13  ; fine
```

**Pitch bend (`0x3D3B`)**:

```
ecx = ((MSB - 0x40) << 7) + LSB        ; bend -8192..8191
eax = byte [ebx+0x44f]                 ; range; when 0, 2 is used
eax = (eax * ecx) / 24                 ; idiv, i.e. truncation
[ebx+0x456] = eax                      ; offset in IP units
...
IP = clamp([ebx+0x450] + [esi+0x0e] + offset, 0, 0xFFFF)
```

---

# JUMP: the second song that verified it

`JUMP_BK.MID` has 15 channels, 3923 notes and 5077 voices (Georgia 8 / 2366 /
3331), so it goes through many more presets. The seven fixes derived from
Georgia hold without change — **28 registers stayed at 100 %**. It revealed
one more thing.

## The attack time table is in the driver at 0x09118

`ATKHLD` matched only 80.1 % (1008 voices), with only three values:

| ours | driver | notes |
|---|---|---|
| 9 | **10** | 504 |
| 98 | **100** | 504 |
| 125 | 125 | 4069 |

These are the presets `polysynth` (`attackModEnv 20`) and `spolysynth`
(`1270`), a layered pair. The time table is in `SBAWE.VXD` at offset
**0x09118** — 128 entries of 16 bits in ms, found as the only place in the
binary that fits three known points:

```
idx  1..15:  11878 5939 3959 2970 2376 1980 1697 1485 1320 1188 1080 990 914 848 792
idx 95..105: 24 23 22 21 20 19 18 17 16 15 15
idx 120..127: 8 7 7 7 7 6 6 6
```

Our `11878 / RateDivisor(r-1)` reproduces it after rounding **on all 127
entries**, so it need not be copied. The error was in the **selection**:

| | before | right |
|---|---|---|
| compared with | the exact time | **the rounded one** |
| returned | `r-1` | **`r`** |
| zero time | 0x7D | **0x7F** |
| falling through the loop | 0x7F | **0x7E** |

It fits four points from two songs: 0 ms -> 0x7F, 6 ms -> 0x7E, 20 ms -> 100,
1270 ms -> 10. Georgia did not show it, because its presets never reach the
middle of the table. **That is exactly why one calibrates on more than one
song.**

## Guest clock: measured, but not imitated

Comparing note times against the driver (3331 notes of Georgia) gave a linear
drift of **-1.527e-4 · t**, i.e. the guest plays 0.015271 % faster. It fits
the PIT divisor: Windows programs the millisecond timer with 1193 instead of
1193.182, so one "millisecond" tick lasts 0.99984747 ms — predicted
0.015253 %. Imitating it changed nothing in the register stream (pairing is
by order), so it was reverted. The residual spread after removing the drift
is **0.89 ms** (max 3.44 ms) — the dispatch jitter behind the remaining
voices.

---

# RELAX: the third song

6523 voices, 15 channels, bank select `CC0 = 1` and `8` (banks that do not
exist in `SYNTHGM.SBK` — fallback to bank 0). Careful: **`RELAX.SBK` is not
loaded in the guest** — the trace has only 3 `SMLD` writes, while a 6.4 MB
bank would need millions. So our render must not have it either.

## Modulation wheel (CC1)

The high byte of `FMMOD` had 01, 02 and 04 at the driver where we had zero.
The CC1 handler is at `0x34A4`:

```
mov ecx, 0x1E / div ecx    ; CC1 / 30 -> 0..4
add ebp, edx               ; + depth from the patch + channel component
cmp ebp, 0x7F / shl ebp, 8 ; clip and into the high byte of FMMOD
```

Added. It also raised **Georgia from 28 to 29** — its two remaining `FMMOD`
were for the same reason.

## The LFO frequency overflows the byte

`freqVibLFO 132` -> 264 -> the driver writes **0x08**; we clipped to 0xFF.
Fix: `(v * 2) & 0xFF` instead of a clamp.

---

# The generator conversion routine: pulled from the guest's memory

The static disassembler was not enough for it. The call at `+0x2885` has
`rel32 = 0x000012C7` in the file, but **0x001A06CB in memory** — a fixup
sends it elsewhere entirely, outside object 1. Solution: dump the code **from
the guest's memory**, where it is already loaded and linked. `awe32_trace.c`
can do it through `AWE32_TRACE_CODE_LEN` / `_BACK` / `_MIN`, triggered at the
DCYSUSV write (note start).

## What the routine contains

A jump table by generator number:

    mov  eax, [esp+4]                        ; generator number
    sub  eax, 0x15                           ; 21
    cmp  eax, 0x25                           ; range 21..58
    ja   C119C10D                            ; outside -> return unchanged
    movzx ecx, byte ptr [eax + 0xC119C362]   ; branch index
    jmp  dword ptr [ecx*4 + 0xC119C2FA]      ; branch address

| branch | generators |
|---|---|
| `C119C116` | delayModLFO, delayVibLFO, delayModEnv, delayVolEnv |
| `C119C180` | freqModLFO, freqVibLFO |
| `C119C1D9` | attackModEnv, attackVolEnv |
| `C119C257` | holdModEnv, holdVolEnv |
| `C119C2A6` | decayModEnv, releaseModEnv, decayVolEnv, releaseVolEnv |
| `C119C10D` | the rest — returns the value unchanged |

Each branch computes in **timecents** in 16.16 fixed point:

```
cmp eax, 0xFFFFD120   ; <= -12000 -> 0x8000 (no delay)
cmp eax, 0x156C       ; >= 5484   -> 0
add eax, 0x30E4       ; + 12516
mov ecx, 0x4B0        ; 1200
shl eax, 0x10 / idiv ecx
... (1 + frac) << intpart ...
sub esi, edi          ; 0x8000 - result
```

The `2^x` there is **not an exponential** but a linear substitute within the
octave, which overestimates by up to 6 % in the middle. Transcribed as
`SoundFont::DelayFromTimecents`; SF2 banks now go through it directly.

## Where the conversion does **not** happen (for SF1)

An instruction trace over the whole driver object (`lo=C1196C74
hi=C119DC74`) during boot and playback recorded **3 171 895 instructions,
none of them in the converter range `C119C1xx`**. The line limit was not
reached (4 427 473 of 8 000 000). So the routine is not called on the SF1
path at all.

### The instruction tracer

The conversion runs **before** the note's port writes, so it cannot be caught
by an address range. The tracer can do `AWE32_TRACE_INSN_AFTER_NOTE=1`: the
instruction record is armed at the first note-on and catches the processing
of the next note.

    AWE32_BUILD=build86box_int      # needed, the dynarec hook misses it
    AWE32_TRACE_INSN=1
    AWE32_TRACE_INSN_AFTER_NOTE=1

Register order on an `I` line: `EIP opcode EAX EBX ECX EDX ESI EDI EBP ESP`.

**The driver loads at the same address on every boot**, so a code dump
(`SBAWE.VXD.obj1.noteon.mem`, base 0xC0FF7BE0) can be reused across runs.

### Note-on map

| address | what it does |
|---|---|
| `C0FFA2AA` | note-on handler, `eax` = note number, `ebx` = velocity |
| `C0FF9C68` / `C0FFA0FF` | voice allocation, returns its number |
| `C0FFAADA` | pitch computation, returns `IP` |
| `C0FF9BB1` | register write: `eax` = pointer, value in `ecx` |
| `C0FF9C1B` | write of a 32-bit pair to 0x620/0x622 |
| `C0FFB2B9` | write of `ENVVAL` |
| `C0FFB3AF` | write of `ENVVOL` |

### The 0xBFFF branch — confirmed from the code

    C0FFB348  cmp word ptr [ebx + 0x44], 0x7f    ; volAttack
    C0FFB34D  jne C0FFB3A0
    C0FFB34F  cmp word ptr [ebx + 0x42], 0x8000  ; envvolDelay, **unsigned**
    C0FFB355  jb  C0FFB3A0
    C0FFB357  push 0xbfff                        ; ENVVOL = 0xBFFF

The modulation envelope has the same pair at `C0FFB245` with the fields
`0x34` and `0x32`.

### Target volume — the table confirmed

    C0FFB36B  movsx eax, word ptr [ebx + 0x26]   ; atten
              cdq / xor / sub                    ; |atten|
              and eax, 0xf
              xor / sub                          ; sign back
    C0FFB382  mov si, word ptr [edx*2 - 0x3efffe44]   ; table at 0xC10001BC
    C0FFB38A  cdq / and edx, 0xf / add / sar eax, 4   ; division by 16 towards zero
    C0FFB395  shr si, cl

That is exactly `Awe32Curves::VolumeTarget`. Both divisions truncate
**towards zero**, so for a negative attenuation the index is negative and
the driver reads **before** the table — the end of another table whose last
three words are zeros, so attenuation -1 to -3 gives silence. Added as
`kAttenBeforeTable`. The table itself is in the file at **0x8DB0**,
statically at 0x409010 and at run time at 0xC10001BC. Measured: on Georgia,
JUMP and RELAX (14 931 notes) the attenuation is always 16..255, so the
negative branch does not occur today.

## Ring-3 part: `SBAWE32.DRV` — and with it the whole SDK

The search for the ring-3 part that reads `.SBK` ended at **`SBAWE32.DRV`**
(44 176 B, `WIN95/DRIVERS/`), a **16-bit NE**:

| place | what is there |
|---|---|
| 0x03B4C | extension table `.SBK` / `.SF2` |
| 0x03DCE | an embedded minimal bank "WaveFx" (`RIFF..sfbk LIST INFO ... pdta phdr ...`) |
| 0x057E0 | `cmp dword ptr es:[bx+8], 'sfbk'` — RIFF type check |
| 0x05AF5 | the same again (upper and lower case) |

It has all SoundFont chunk names, but **none of the conversion constants**
(12516, 5484, -12000).

### The more important find: the AWE32 SDK

The Creative AWE32 SDK contains `WINDOWS/INCLUDE/SFTYPE.H`. **The structure
`_SFTYPE` is exactly the voice parameter block we had been cracking by
offsets** — 59 fields of type `short`, 0x76 bytes. All eleven offsets read
from the instruction trace match the header:

| offset | SDK | our former label |
|---|---|---|
| 0x0E | `env1ToPitch` | f0E |
| 0x12 | `initialFilterQ` | Q |
| 0x20 | `reverbEffectsSend` | reverb |
| 0x24 | `auxEffectsSend` | panAux |
| 0x26 | `sampleVolume` | atten |
| 0x32 | `delayEnv1` | envvalDelay |
| 0x34 | `attackEnv1` | modAttack |
| 0x42 | `delayEnv2` | envvolDelay |
| 0x44 | `attackEnv2` | volAttack |
| 0x48 | `decayEnv2` | volHoldLo |
| 0x4A | `sustainEnv2` | volHoldHi |

Env1 is the modulation envelope, env2 the volume one. `tests/patch_struct.py`
now uses the header's names.

The SDK has **two separate libraries**: `DOS/LIB/SBKLIB/` and
`DOS/LIB/SF2LIB/`. The timecent conversion constants (12516, -12000) are
**only in SF2LIB**. That is the dividing line.

## Correction: the delay is linear, step 725 us

`SFTYPE.H` says directly for all four delay fields:

    short delayLfo1;   /* delay 0x8000-n*(725us) */
    short delayEnv1;   /* delay 0x8000 - n(725us) */

So for SF1, which has times in milliseconds, simply `n = ms / 0.725`. It fits
all three measured values (20 ms -> 27, 140 ms -> 193, 440 ms -> 606).

**Where the earlier reasoning went wrong.** From the driver containing an
exponential conversion through timecents it was concluded that a linear step
could not be. But that routine is for **SF2** — for SF1 it is not called at
all (measured, see above). Both paths are physically the same and differ
only in rounding:

    1000/725                = 1.37931 steps per ms
    2^(12516/1200)/1000     = 1.37957

State of the code: `DelayFromMs` = the 725 us step (SF1),
`DelayFromTimecents` = the 1:1 transcription of branch `C119C116` (SF2).

## Pitch bend: the families have a different constant per semitone

Both families add the bend **to the finished IP** (not in cents), both
divide as integers only at the end and truncate towards zero. But the
constant differs:

    win95  bend * range * 4096 / (8192*12)     ; 4096/12 exactly
    dos    bend * range * 341 / 8192           ; 341, i.e. truncated

For win95 it fits **eleven** measured points of Georgia and RELAX (bend,
range -> exact -> driver):

| bend | range | exact | driver | | bend | range | exact | driver |
|---|---|---|---|---|---|---|---|---|
| 8064 | 2 | 672.000 | 672 | | -768 | 12 | -384.000 | -384 |
| -4729 | 12 | -2364.50 | -2364 | | -682 | 12 | -341.000 | -341 |
| 1280 | 12 | 640.000 | 640 | | -512 | 12 | -256.000 | -256 |
| -1280 | 12 | -640.000 | -640 | | 176 | 12 | 88.000 | 88 |
| -1312 | 12 | -656.000 | -656 | | 8191 | 2 | 682.583 | 682 |
| -6720 | 2 | -560.000 | -560 | | | | | |

For dos the evidence is a full bend down with range 12 in the Magic Carpet 2
intro: the driver wrote -4092, while 4096/12 would give exactly -4096.

**The bend range comes from RPN 0,0** (CC101/CC100 select the RPN, CC6 sets
the value). `Synth` could not do it and held the default two semitones,
although the MIDI sends 12 — that was the old mystery "the driver acts like
22 semitones on ch7".

### Dead end

In `SBAWE.VXD` (C0FFAF8A) two words of the channel structure (`[eax+0x10]`
and `[eax+0x12]`) are added to the note's cents before `sub_192E`. It looked
like the path of the bend — adding it in cents before the conversion made
RELAX **worse** from 32/32 to 29/32. So those are other channel tunings
(most likely RPN 1 and 2), not the bend.

## scaleTuning is not in percent

`SBAWE.VXD`, C0FFAF54..C0FFAF87:

    ecx = keynum - rootKey + coarseTune
    ecx = (ecx + 60) * 100 - samplePitch + fineTune
    cmp word [esi+0x70], 1        ; scaleTuning
    jne next
        eax = ecx; cdq; sub eax,edx; sar eax,1    ; division by 2 towards zero

So the driver **does not test percent** but equality with one, and then
**halves** the whole result. The field names are from `SFTYPE.H` (0x6E
`samplePitch`, 0x70 `scaleTuning`, 0x74 `rootKey`).

Measured: preset 122 SeaShore in `SYNTHGM.SBK` has `scaleTuning 1`, and those
were the last four mismatching notes of RELAX. For note 69 with
`samplePitch` 8781 it gives `(69-60+60)*100 - 8781 = -1881`, half is -940 —
exactly what the driver wrote.

## State: everything 1:1

| song | family | registers |
|---|---|---|
| Georgia | win95 | **32/32** |
| JUMP | win95 | **32/32** |
| MINUET | win95 | **32/32** |
| RELAX | win95 | **32/32** |
| Magic Carpet 2 (intro) | dos | **24/24** |
| chip (Georgia) | - | 0 differences in 6 927 532 frames |

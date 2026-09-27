# Magic Carpet 2 straight from the game

The game is measured running in 86Box with **its own** driver
`SOUND\SBAWE32.MDI` (Miles/AIL V3.02 of 18 Jan 1995), to have something to
build our `dos` family against.

## How to run it

```bash
powershell -File ref86box/run_trace.ps1 \
  -Trace tests/out/mc2_full.trace \
  -CpuTrace tests/out/mc2_full_cpu.trace \
  -Mode mc2 -Seconds 420 -NoEsc
```

(`ref86box/` and `tests/` are in the `sources` branch; the VM image itself is
not published.)

Pitfalls we hit:

| what | how it is |
|---|---|
| **Let it run to the end** | Loading the bank into DRAM takes the first ~20 s, only then the music starts. A run cut after two minutes has 5046 "notes", but they are all voices from the bank upload, not music. |
| **"Starting Windows 95"** | `MSDOS.SYS` has `BootGUI=0`, the GUI is not started. The message comes from `IO.SYS` and shows even in plain DOS mode — not an error. |
| **`AUTOEXEC.BAT`** | The image holds two backups: `AUTOEXEC.MC2` (starts the game) and `AUTOEXEC.MID` (DOSMid). Switched with `tests/fat16.py put`. |
| **Driver selection** | Not in `CONFIG.DAT` but in `NETHERW/SOUND/MDI.INI` (and `DIG.INI`). The image already has `Creative Labs AWE-32(TM) General MIDI` / `SBAWE32.MDI`. **Careful:** the copy in remc2 (`x64/Debug/NETHERW/SOUND/MDI.INI`) has `SBPRO2.MDI`, i.e. FM — misleading. |
| **`fat16.py`** | Handles subdirectories too (`ls NETHERW/SOUND`, `get NETHERW/SOUND/MDI.INI`). |

`SETSOUND` picks `sbawe32.mdi` automatically when `AUTOEXEC.BAT` contains
the string `AWEUTIL` (see `.A AWEUTIL` in `AILDRVR.LST`). The MC2 autoexec
does not contain it, so the driver choice **cannot be relied on** — it must
be in `MDI.INI`.

## What the game sends at the start

The driver gets a sequence of messages on **all sixteen channels** at
start-up (decompiled in remc2, `engine/Sound.cpp`, the loop
`for (i = 0; i < 16; i++)`):

    CC114 0, program 0, bend centre, CC112 0, CC1 0,
    CC7 <preference 13>, CC10 64, CC11 127, CC64 0,
    CC91 40, CC93 0, CC100 0, CC101 0, CC38 0, CC6 <preference 16>

AIL preferences: index 13 = `0x7F` (volume), index 16 = `0x02` (bend range).
Against our defaults, **CC7 (127 vs 100)** and **CC91 (40 vs 0)** differ —
that is the "volume calibration".

It is in [`conf/mc2.conf`](../../conf/mc2.conf) and loaded with `--conf`:

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf SBAWE32.MDI --sf BULLFROG.SBK \
  --driver dos --conf conf/mc2.conf --wav out.wav
```

A note on the source: the values come from the decompilation, not from a
static analysis of `NETHERW.EXE`. Statically it does not work — it is a
DOS/4GW **LE** executable and references into the data segment are not
relocated (`0x181DAC` does not occur as an absolute address in the code),
just as with `SBAWE.VXD`. The array itself is also in BSS. It can be verified
either by a memory dump of the guest at run time or — better — by our render
matching the trace from the game (it does, see below; two values turned out
different: CC91 and the bend range).

## The game's bank

`SOUND\BULLFROG.SBK` (172 196 B) is **only on the CD**, not in the disk
image. It has 15 presets and **not a single drum kit**:

    0:3 LOOP2   0:0 REV2   0:4 LOOP3   0:5 TBellD4Wave
    0:117 AgogoLoTone   0:118 SquareWave   0:119-127 SynthBassLoop

Only loops and effects; it refers to the ROM `1MGM`. **So the game takes the
drums from the wave ROM**, not from its own bank.

Our render loads **84 852** samples into DRAM, the game writes **84 851**
`SMLD` — matches.

## Structure of the trace from the game

From a run of 195 s (`dos_mdi.trace`):

| register | count | note |
|---|---|---|
| `SMLD` | 84 851 | bank upload, frames 1 731 427..1 754 803 |
| `DCYSUSV` | 16 703 | |
| `PSST` / `VTFT` | 5 455 | |
| `CCCA` | 10 469 | |
| `IP` | 979 | |
| `IFATN` | 739 | |
| `ATKHLDV` / `ENVVAL` | 439 | **that many real notes** |

The bank upload ends at frame 1 754 803, `ATKHLDV` goes on to the end — that
shows where the preparation ends and the music starts.

## Open question at the time: broken drums

Listening showed that **the drums sound wrong even in 86Box with the real
driver**, just as in our project. The menu music is fine, the intro is not.

What follows: our driver emulation **is not to blame**. Our chip was byte
identical with 86Box then (`--chip 86box`, 0 differences over 6 927 532
frames), so the common cause is one of three:

1. 86Box's `snd_emu8k.c` (an error in the chip emulation),
2. the wave ROM dump `awe32.raw`,
3. something shared higher up — e.g. the handling of channel 10.

The drums come from ROM (see the bank above), so point 2 is in play. Against
it: Georgia from ROM sounds right.

**How to decide it:** recordings from real hardware of the ROM bank that
contain drums, compared with our render of the same MIDI, separate a
chip/ROM error from anything specific to MC2. (Later: AWETEST block 26
plays the drums from ROM directly on the tester's card, and the ROM dump of
the tester's card is identical to `awe32.raw` apart from the one leading
word — see `rom_vs_sf2.md`.)

Note: the intro starts again in the menu after a while of inactivity, so a
long enough capture catches it.

## Matching the game: **all 24 registers at 100 %**

Our render of the intro is compared against the trace from the game
(`dos_mdi.trace`), only up to **note 260** — there the intro ends in the game
(a twelve-second gap) and after that the trace holds the menu music.

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf SYNTHGM.SBK --sf BULLFROG.SBK \
  --driver dos --conf conf/mc2.conf --wav out.wav --trace mc2_final.trace
```

(Later the GM presets were found compiled into the driver itself; today
`--sf SBAWE32.MDI` is used instead of `SYNTHGM.SBK`, see
`86box_comparison.md` and [USAGE.md](../USAGE.md).)

| register | match |
|---|---|
| **all 24** | **261/261** |

### What unlocked it

**1. The GM bank from the wave ROM was missing.** `BULLFROG.SBK` describes
only the game's 15 own presets; everything else the game takes from the GM
bank in ROM. Without it we substituted nonsense for presets missing from the
bank (`CCCA` came out `FFD2`) and played one layer where the game plays two.
So **both** must be loaded. The note timing then went from a divergence at
note 22 to a match **within a few milliseconds up to note 260**.

**2. The XMIDI master volume is 100, not 127.** The driver scales every
channel's CC7 with it (`cc7 * master / 127`). Sending it as CC7 at the start
is not enough — the XMI overwrites its own CC7 at once, so it has to be a
**continuous** scaling (`Synth::SetMasterVolume`, `master_volume 100` in the
configuration).

How it was found: `IFATN` was **0/261** with a constant difference `FF00` ->
`FF0A`, i.e. 10 units more attenuation. In the formula
`atten = (8*db + patch) / 3` that means `db = 4`, and `kChannelVolumeDb` has a
four exactly for CC7 99..103. With the master volume at 100, `IFATN` is
**259/261** and the attenuation difference exactly zero.

**3. `Synth::PitchBend` wants a deviation centred at zero**, not the raw
0..16383. The first version of `--conf` sent `bend 8192` directly, so every
channel had a permanent bend of +2 semitones — visible as a shift of all
pitches by 0x2AA.

**4. Reverb: the initial CC91 is 0, not 40.** The remc2 decompilation has
`CC91 40` in the initialisation, but it never reaches the device — AIL
overwrites the channel state from its own shadow copy when the sequence
starts. Measured: on the channels that do not set CC91 in the XMI (2, 5, 6)
the channel share is zero. With zero, `PTRX` matches **261/261** (233
before).

**5. The pitch bend range is 12 semitones and is set through RPN 0,0.**
`Synth` could not do RPN at all — `pitchBendRangeSemitones` was only read and
stayed at the default two. Added (CC101/CC100 select the RPN, CC6 sets the
range).

**6. The driver uses the integer constant 341 per semitone** (= 4096/12
truncated), not a fraction, and divides only at the end. The evidence is two
independently measured points:

    range 2,  full bend down:  -8192*2*341/8192  = -682   (67 notes of Georgia)
    range 12, full bend down:  -8192*12*341/8192 = -4092  (4 notes of ch6 in MC2)

A formula with the fraction would give -4096 for range 12, 4 units off.
(Later: this holds for the `dos` family; `win95` uses exactly 4096/12 — see
the comment in `Synth.cpp`.)

**Side effect of points 5 and 6:** Georgia and JUMP improved from **29/32 to
31/32**. The old mystery "ch7 sets the MIDI range to 12, but the driver acts
like 22" was exactly this — we held two semitones and the difference looked
like something else.

### Dead end: XMIDI loops

The difference of 669 against 407 notes looked like unimplemented XMIDI
loops (`XmiFile.cpp` skips `RBRN` blocks and CC116/117 are not handled).
**It is not that.** The intro has no loops — the file has no `RBRN` block
and no CC116/117, only CC119 (a callback, no effect on playback), and `EVNT`
has **669 notes**, exactly as many as our render makes. The difference was
that the trace from the game holds the intro only up to note 260 and then
plays the menu. Analysis: `tests/xmi_raw.py`.

**7. The filter cutoff differs between the families**, and we had both the
same (per the VXD):

    SBAWE32.DRV 0x021E (dos):    (cutoff * v + 0x40) / 0x7F
    SBAWE.VXD   0x1CF6 (win95):  (cutoff * v + 0xA0) >> 7

The difference shows only at the top: for cutoff 255 and velocity 127 DOS
gives 255 (32449/127 = 255.5), the VXD 254 (32545/128 = 254.3). Those were
the last two mismatching notes.

### What remains

Nothing. The Magic Carpet 2 intro matches the real Miles driver in all 24
registers on all 261 notes.

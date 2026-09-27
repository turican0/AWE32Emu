# Test log: our render against the real drivers

The goal is 1:1 on the registers at note-on. Everything is measured,
nothing is estimated. (The tools referred to here are in the `sources`
branch, `tests/`.)

## Tools

| tool | used for |
|---|---|
| `tests/matrix.py` | the test matrix — our render against driver traces, results to `matrix_results.json` |
| `tests/status.py` | overview across all levels (registers, intermediate values, chip) against `status_baseline.json` |
| `tests/trace_split.py` | splits a trace holding several songs into sections and prints `--dframes` windows |
| `tests/bank_sanity.py` | loads every SF1 bank and tries to play with it — looks for crashes and empty banks |
| `tests/notes_diff.py` | the core comparison, register by register |
| `tests/xmi_raw.py` | what is really in an XMI (IFF blocks, XMIDI control messages) |
| `tests/note_timing.py` | does the note timing match? Decides whether the match percentages mean anything |

Adding a case to the matrix = one line in `CASES`.

## State of the matrix

    python tests/matrix.py

| case | family | player | registers | notes |
|---|---|---|---|---|
| georgia | win95 | Media Player | **32/32** | 3331 |
| jump | win95 | Media Player | **32/32** | 5077 |
| relax | win95 | Media Player | **32/32** | 6523 |
| minuet | win95 | Media Player | **32/32** | 242 |
| mc2-intro | dos | Magic Carpet 2 | **24/24** | 261 |
| mc2-menu | dos | Magic Carpet 2 | **24/24** | 112 |

Chip: `--chip 86box` was byte identical against `emu8k_ref.exe` (Georgia,
6 927 532 frames, 0 differences).

## Banks

`bank_sanity.py`: **34 SF1 banks, all load and the render finishes, no
crash.** Including the whole `SFONT1/` set (ACSTGTRM, BASTIMPS, CHAPSTKS,
DULCIMRS, ELPERC_M, ELSITARM, FUNKBASM, FUZZGTRS, HARMONIS, JAZZGTRS,
JAZZKITM, LATDRUMM, LATHANDS, MANDOLNS, MTLDRUMS, NYLNGTRL, ORCHHRPM,
PIZZBASM, POPDRUMS, POPGTRSS, RATDRUMS, ROKBASSM, SYNFX01M, SYNFX02M,
SYNTH01S..SYNTH05S, TWELVSTM) and `RELAX.SBK`.

Careful: this is **not** a test of matching the driver, only that the
parser does not crash. Matching the driver for other banks can be verified
only by a VM run with that bank.

## What to watch when measuring

**Verify the timing first, only then read the percentages.** When the note
sequences diverge, `notes_diff.py` pairs by order and the differences then
come out in whole semitones — it looks like a pitch error but it is a bad
pairing. `note_timing.py` is for that.

**A trace from a VM run often holds several songs.** In Magic Carpet 2 the
intro ends at note 260 and after a twelve-second gap the menu music starts;
after another ~65 s of inactivity **the intro starts again** (recognised by
the signature `F400 F400 F400 B959`). The windows are found with
`trace_split.py`.

**The master volume is not fixed.** Magic Carpet 2 plays the intro at 100
and the menu at 127 — with each value the corresponding part matches 24/24.
The configuration has 100; for another part pass `--master-volume`.

**`AUTOEXEC.BAT` must have CRLF line ends.** With Unix ones the batch is
not executed and only the prompt stays in the guest.

**DOSMid is in the root** (`C:\DOSMID.EXE`). The directory `C:\DOSMID\`
does not exist, although the backup `AUTOEXEC.MID` in the image points to
it — see below.

## DOSMid

### The path through the MPU-401 cannot work in this VM

Not because of configuration, but in principle. Found by redirecting
AWEUTIL's output to a file in the guest and reading it with `fat16.py get`:

- `/EM:GM` **is in the menu** (the documentation says unsupported options
  are not shown), and the TSR even installs — it only reports `ERR014` and
  does not find its data.
- `AWEUTIL.TXT`: MIDI emulation needs the **MFBEN jumper** on the card, a
  loopback through which the card sees its own MPU traffic.
- In 86Box there is **no such loopback**: `snd_mpu401.c` sends the output to
  `midi_raw_out_byte()`, i.e. to host MIDI. The word MFBEN occurs nowhere in
  the source.

Note: `AWEUTIL.COM` is packed; the disassembly holds only the unpacking stub
— the path cannot be read from it statically.

### The way out: DOSMid /awe

DOSMid can drive the EMU8000 **directly**, without the MPU and without
AWEUTIL:

    C:\DOSMID.EXE /awe C:\TEST.MID

This tests a **fourth independent driver implementation** next to
`SBAWE.VXD`, `SBAWE32.MDI` and AWEUTIL. (Later it turned out DOSMid uses the
Creative AWE32 DOS SDK — that became our third driver family, `--driver
sdk`.)

It works: TEST.MID (Georgia) gave **3331 notes, exactly as many as our
render**. Timing and counts match. **As a 1:1 reference for the Win95/DOS
families, however, it is unsuitable** — it interprets the bank its own way:

| | DOSMid | ours | difference |
|---|---|---|---|
| `CCCA` note 0 | 0001E9 | 0001C0 | +41 |
| `CCCA` note 3 | 004566 | 00453D | +41 |
| `IFATN` | FE50 | FE42 | attenuation 14 units off |

The sample address differs by **a constant 41 samples** and the attenuation
by a fixed amount — so it is the same bank, only with DOSMid's (SDK's) own
choices (a different start offset and a different volume curve). In the
matrix the case `dosmid-georgia` was kept as **informative**, not as a
criterion.

The trace holds five songs in a row; `trace_split.py` finds the boundaries.

### Conclusion: not our defect, not to be matched (for win95/dos)

The differences are measured and clearly DOSMid's:

- **Sample address: +41 on all 3331 notes.** A single constant over the
  whole song — a different DRAM layout, not a computation error.
- **Attenuation: the difference varies** (8 to 16 units), i.e. a different
  volume curve.

For the `win95` and `dos` families our authorities are `SBAWE.VXD` and
`SBAWE32.MDI`, where we match 100 %. Matching DOSMid there would **break**
that. (The SDK behaviour is modelled separately in the `sdk` family.)

### Adding MFBEN to 86Box

An optional loopback was added to `src/sound/snd_mpu401.c`. In UART mode
only `midi_raw_out_byte(val)` was done before; now the byte can also be
returned to the input through `MPU401_RecQueueBuffer`, which is exactly what
a real card with the MFBEN jumper does and what the resident AWEUTIL needs.

It is switched on with `AWE32_MPU_LOOPBACK=1`; **the default is off**.

**Result: it was not enough.** With the loopback on there were still no
notes, neither with the old AWEUTIL (14 kB, v1.01 in the guest) nor with a
newer one from the distribution (28 kB, 95dosapp) plus `CTMIX.CFG`. AWEUTIL
/EM:GM also reports `ERR014` — it does not find its data. MIDI emulation on
the AWE32 evidently needs more from the card than a loopback, and 86Box
does not model it.

### Measured: DOSMid does not write to the MPU at all

A record `awe32_trace_note` was added to the loopback in `snd_mpu401.c`,
writing every byte that passes into the trace. Result of a run with the
loopback on: **zero records**.

So DOSMid did **not write a single byte** to the MPU-401 data port. The chain
breaks already there, not at the missing loopback — its `/mpu` mode does not
start at all in this environment. The added drivers (CTMIDI.DRV, CTGS.DRV,
CTMT32.DRV) changed nothing.

**Lesson for the method:** several cycles were spent removing supposed
obstacles behind the point where the chain really broke. Measuring at the
start whether anything flows at all would have saved them. Measure the chain
from its start, not from the supposed break.

The loopback **stays** in the code — the missing MFBEN is real missing
behaviour of 86Box — but this path cannot be unblocked by it.

That is not caution for its own sake: 86Box is our **measuring standard**.
If behaviour we derived ourselves is added to it and left on during normal
measurements, we risk "matching" our own assumption instead of the real
hardware. So switch it on only for the tests it concerns, and do not grade
their results like the measurements against the Creative drivers.

### More pitfalls

- `AUTOEXEC.BAT` must have **CRLF**; with LF the batch is not executed.
- **The `AUTOEXEC.BAT` in the image is shared between experiments and
  nobody cleans it up.** A capture of the game's sound came out as 411 s of
  silence, because the batch from the DOSMid experiment was still in the
  image. The trace told it at once: it ends at frame 1 255 506 (28.5 s) with
  writes that silence the voices — the end of AWEUTIL's initialisation and
  nothing more. The image holds the backups `AUTOEXEC.MC2` (the game) and
  `AUTOEXEC.DSM` (DOSMid); the right one is to be copied before every run:

        python tests/fat16.py <image> get AUTOEXEC.MC2 ae.txt
        python tests/fat16.py <image> put ae.txt AUTOEXEC.BAT
- DOSMid is in the **root** (`C:\DOSMID.EXE`), the directory `C:\DOSMID\`
  does not exist, although the backup `AUTOEXEC.MID` points to it.
- `fat16.py` must be called with **Windows** paths; with paths in the form
  `/c/prenos/...` Python does not find the file.

## Render stress test

    python tests/render_sweep.py

Runs a collection of MIDI files through the render and watches for crashes,
empty outputs and nonsensical voice counts.

A pass takes about an hour, hence two aids:

- `--resume` skips files that already passed cleanly (the list is in
  `sweep_done.json`). Damaged inputs are **not** stored there — for them it
  should be verified every time that the render rejects them.
- The state is written to `sweep_status.json` after every file and can be
  read at any time:

        python tests/sweep_status.py
        python tests/sweep_status.py --watch

  It shows a progress indicator, the pace, an estimate of the rest and the
  problems found. When the state does not change for a long time it says so
  — a hanging run shows.

Note: do **not** pipe a background run (`| tail`); the pipe holds the output
until the end and nothing is visible for an hour. That is what the status
file is for.

### Result: the whole collection passed

**232 files, 0 crashes, 0 empty outputs, 0 nonsensical voice counts.** The
only rejected input is `title2.mid` from WarCraft 2, and rightly: the track
header says 11004 B, but only 8170 remain in the file (it is truncated).

The run took 42 minutes on 160 files; the rest had passed earlier. It is not
a test of matching the driver — that is `matrix.py` — but a stress test of
the parser and synthesis: the render does not crash on any MIDI of the
collection and always plays something.

## Finding: preset generators are **not added**

Only the fifth song (CRAZY) found it, when the four previous ones looked
done.

Channel 4 plays program 97 "Soundtrack", which has `coarseTune` on both
levels — 1 on the preset and 3 on the instrument. We added them per the SF2
specification (4); the driver uses 3 and plays a semitone lower; all 58
notes of that channel were off.

It showed in a detail: two layers of the same tone had offsets of **-341
and -342**, i.e. unequal. That is the signature of an offset in **cents
before the conversion**, not of a constant in IP units — and that killed the
idea that it was a pitch bend.

Fix: the preset zone now only **fills in** what the instrument zone lacks.
The attenuation remains an exception; the driver adds it only in register
units. After the change CRAZY matches 32/32 and nothing else got worse.

## Testing another bank through the real driver

A user bank cannot be loaded in Win95 other than through the graphical
control panel, but it can be bypassed: the driver loads
`WINDOWS\SYSTEM\SYNTHGM.SBK` at start-up, so it is enough to **replace**
that file.

Used `SYNTH02S.SBK` (542 kB, 38 presets, its own samples — this also tests
the DRAM upload) and played MINUET. The driver loaded it and played 242
notes, exactly as many as our render.

Do not forget to restore `SYNTHGM.SBK` afterwards, otherwise further Win95
measurements run with a foreign bank.

### Finding 1: +16 units of attenuation apply only to samples in ROM

The code had the condition "the bank refers to ROM 1MGM -> add 16 units of
attenuation" with a note that the driver also conditions it on a byte flag
we had not decoded. **That flag is "does the sample lie in ROM?".**

SYNTH02S.SBK also refers to 1MGM, but has its own samples in DRAM — and the
driver did not add the 16 units there. It showed in `IFATN` (16 higher for
us) and in `VTFT^`/`CVCF^`, which came out exactly double — 16 units is
6 dB, i.e. a factor of 2. One cause, three registers.

Physically it fits: the samples in the wave ROM are 6 dB louder than what
the driver itself loads into DRAM.

### Finding 2: the user DRAM starts elsewhere for each family

After fixing finding 1, three address registers (`CCCA`, `PSST`, `CSL`)
were left, all exactly **34 samples** off. That is the offset of the start
of the user DRAM:

    SBAWE32.MDI (dos)    first sample at 0x200032   reserve 50
    SBAWE.VXD   (win95)  first sample at 0x200010   reserve 16

The fifty was measured against the MDI and was used for both families. The
reserve now depends on the family (`Synth::kDramReserveDos` /
`kDramReserveWin95`) and is computed when the first bank is loaded — which
is why `main.cpp` must set the driver variant **before** loading the banks.

After both fixes the replaced bank matches **32/32** and nothing else got
worse.

## A big own bank: RELAX.SBK (6.7 MB of vocals)

The same procedure as with SYNTH02S — replace
`WINDOWS\SYSTEM\SYNTHGM.SBK` — but the card's memory must also be
**increased**. The default 512 kB is not enough:

    [Sound Blaster AWE32 PnP]
    onboard_ram = 8192

**Careful:** device options belong in the section named after the device,
not in `[Sound]`. 86Box reads them through `device_get_config_int`, which
asks the section with the device's name; in `[Sound]` it is silently
ignored. The first attempt therefore ended with the bank not being loaded (3
`SMLD` writes and nothing more).

With the right setting **3 372 947** `SMLD` writes were loaded and 5229
notes played — exactly as many as our render makes.

Another pitfall: the matrix must have **only that one bank**, not GM plus it
— in the guest SYNTHGM.SBK is overwritten, so the driver has only it too.

### Finding 1: missing program -> the first preset of the bank

RELAX_VX uses GM programs up to 122, but the bank has only presets 0..31.
For the missing ones we played the **substitute sine** from `kDramOffset`
(visible in CCCA as 0x1FFFFC), while the driver plays **the first preset of
the bank** (0x20000C). It shows nicely per channel: most play 0x20000C, but
the channels whose program really is in the bank (16, 21, 22, 30, 31) play
their own samples.

Our substitute remains only for a bank that has not even preset 0.

### Finding 2: the drum channel does not reach into bank 0

After the first fix 1849 notes remained — exactly the number of notes on
channel 9. Our search chain had "bank 0 with the same program" as the last
step; channel 9 has program 16 in this song and the bank has preset 16, so
we took it. **On the drum channel the driver does not reach into bank 0** —
when it does not find the kit, it goes straight to preset 0.

On songs with the GM bank this cannot show: SYNTHGM.SBK has the drum bank
128, so the chain never gets to the third step.

## Operational lessons

**Do not start a long task a second time while it runs.** Two matrices at
once write to the same files in `out/matrix/` and overwrite each other's
results — one of them then crashes and the other cannot be trusted.

**Do not wait in a loop for the output of another task.** When that task
stops, the wait hangs forever (once it ran 5.5 hours like that).

**`matrix.py --save` no longer measures again** — every run is written to
`matrix_last.json` and `--save` only promotes it to the baseline. Formerly
it meant going through the whole matrix once more, ~13 minutes wasted.

## Chip: our render and 86Box compile the same file

At first this was not so, although a comment in the project claimed it.
There were **two copies**:

- `ref86box/upstream/snd_emu8k.c` — untouched upstream, compiled by our render
- `docs/86box-src/.../snd_emu8k.c` — the one 86Box for the VM is built from

In sound they were the same (the difference was only tracing and the ID
register), so it did not show in measurements. But once the EMU8000 is
tuned, a change in one copy does not reach the other and the match would
**silently fall apart**.

Then the project compiled the file from the 86Box tree directly; the
`emu8k_trace_*` calls are empty on our side (`snd_emu8k_trace.h`,
`awe32_trace.h`). Verified: the WAV before and after the switch had **the
same SHA-256** (GEORG_BK.MID, 50 MB) — the switch changed not a single bit.

**Since 2026-09-27** AWE32Emu carries its own copy again
(`AWE32Emu/src/86box/snd_emu8k.c`), so the repository builds on its own. The
two copies are kept byte identical; every chip change goes into both, and
`chipcheck.py` (all AWETEST blocks, AWE32Emu vs `emu8k_ref`) plus VM state
dumps against replay watch the agreement — see [TESTING.md](../TESTING.md).

The upstream copy **stays** and is not compiled: it is the proof of origin,
and `verify_upstream.py` checks it against GitHub.

### What is our change and what is still stock

    python tests/chip_diff.py
    python tests/chip_diff.py --full

Compares our chip against the untouched upstream and sorts the deviations
into known and **new**. At the time of writing there were two known ones,
neither of which changed the sound:

- the tracing hooks — they only write, they do not touch the chip state;
- the ID register reads `0x0C` instead of `0x1C` — read only, used for card
  detection (without it AWEUTIL.COM reports ERR012).

Since then the chip carries the measured corrections (interpolation,
filter, envelopes, effects, EQ); the list is in `emu8000_tuning.md` and the
comments marked `AWE32Emu:` in `snd_emu8k.c`.

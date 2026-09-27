# AWE32Emu – measurements and research tools (`sources` branch)

This is an orphan branch of [AWE32Emu](https://github.com/turican0/AWE32Emu).
The emulator itself, its build and its documentation are on `main`; this
branch holds what the emulator was measured and tuned against:

| path | contents |
|---|---|
| [`recordings/`](#recordings) | recordings from a real Sound Blaster AWE32, with the AWETEST logs |
| [`demos/`](demos/README.md) | short Magic Carpet 2 excerpts: the real card against AWE32Emu |
| [`tests/`](#tests) | Python measuring and comparison tools |
| [`ref86box/`](ref86box/README.md) | the reference renderer around 86Box's EMU8000, the 86Box instrumentation patch, build and VM scripts |
| [`analysis/`](analysis/README.md) | the analysis scripts of the AWETEST rounds |

```bash
git clone --branch sources --single-branch https://github.com/turican0/AWE32Emu.git AWE32Emu-sources
```

The documents that explain the measurements live on `main`:
[docs/DATA.md](https://github.com/turican0/AWE32Emu/blob/main/docs/DATA.md),
[docs/TESTING.md](https://github.com/turican0/AWE32Emu/blob/main/docs/TESTING.md),
[docs/re-notes/](https://github.com/turican0/AWE32Emu/tree/main/docs/re-notes).

## Recordings

All recordings come from the tester's card: Sound Blaster AWE32, DSP 4.13,
8 MB DRAM. The test program is AWETEST (source in `tools/awetest` on `main`);
it plays numbered blocks of notes and writes the time stamps of the notes into
`AWETEST.LOG`. The WAV files are converted to FLAC (lossless, 44.1 kHz);
files over 100 MB are split into `*.partN.flac` – concatenate the parts to get
the original.

Two kinds of recording:

- **internal** (numbered files: `test7…`, `test8…`, `reca…`, `recb…`, `recc…`,
  `39-41001…`, `22234001…`, `rec28…`) – the card records its own output
  through the SB16 ADC (AWETEST `/REC:<file>`).
  Exact timing, but the level path is not linear at loud signals.
- **line** (`awetst25_line`, `39-41.flac`, `222342.flac`, `28.flac`) – the
  line output recorded by a second card. Linear, but the v26 and v27
  recordings clip at the loudest places.

The dry right channel of this card is dead; the right output carries only
the effect (reverb/chorus) returns.

| folder | program | what |
|---|---|---|
| `2026-09-06/`, `2026-09-07/` | early AWETEST | first recordings – **partly invalid**: the test tone used voice 31, which the driver reserves for the DRAM refresh, and `test4` is overdriven |
| `2026-09-08/` | AWETEST v05 (run5) | internal capture, blocks 1-15 (`part1`) and 22-35 (`part2`) |
| `2026-09-12/` | AWETEST v25 | internal capture (`reca` blocks 1-11, `recb` 11-13, `recc` 11-39) with logs, and the line recording `awetst25_line` |
| `2026-09-23/39-41/` | AWETEST v26 | blocks 39-41 (filter below the limit, chorus), line + internal |
| `2026-09-26/22-23-42/` | AWETEST v27 | blocks 22, 23, 42 (reverb, chorus, chorus loop), line + internal |
| `2026-09-27/28/` | AWETEST v28 | blocks 43-46 (headroom, interpolation, reverb and chorus with noise), line + internal |

`awetest.log` is the tester's log (time stamps of the blocks and notes),
`awetrace.log` AWETEST's diagnostic output (hardware detection, bank
setup). Some rounds came without a log; for those the log of the same program
run in the DOS VM gives the note times (it is reproducible from
`tools/awetest`).

What the recordings showed is summarised in
[docs/re-notes/emu8000_tuning.md](https://github.com/turican0/AWE32Emu/blob/main/docs/re-notes/emu8000_tuning.md)
on `main`. Read it before drawing conclusions from a single file.

## Tests

`tests/` are the tools used during development: trace diffs against 86Box
(`trace_diff.py`, `chip_diff.py`, `cmp86box.py`), the test matrix and
regression (`matrix.py`, `regress.py`, `sweep*.py`, `status.py`),
alignment and spectral comparison with recordings (`align*.py`, `bands*.py`,
`compare.py`, `chroma_dtw.py`), disassemblers for the Creative drivers
(`ne_disasm.py`, `le_disasm.py`, `mdi_disasm.py`, `drv_*.py`), bank dumpers
(`sbk_dump.py`, `dump_sbk.py`), MIDI/XMI readers and a FAT16 image tool
(`fat16.py`) for putting files into the VM disks.

They need Python 3.10+ with `numpy` and `soundfile`; the disassemblers need
`capstone`. They assume the development layout: the repository in
`C:\prenos\AWE32Emu`, the data in `C:\prenos\AWE32EmuData` (ROM, banks,
MIDI files, the VM). Most scripts take paths from their own location or from
the environment variables `AWE32EMU_PROJECT` and `AWE32EMU_TESTER25`; a few
have absolute paths that have to be edited.

## What is not here

The wave ROM `awe32.raw`, Creative's drivers, SoundFont banks, the AWE32 SDK,
the demo CDs, game music (apart from the excerpts in `demos/`), the VM images
and the full 86Box tree are not published. See
[docs/USAGE.md](https://github.com/turican0/AWE32Emu/blob/main/docs/USAGE.md)
for where to get the ROM.

## Licence

The licence of the recordings, demos and our scripts follows the main
repository (not decided yet, see its README). `ref86box/upstream/`,
`ref86box/include/`, `ref86box/noslirp/` and `ref86box/86box-patch/` are
86Box code or patches to it and are under 86Box's licence
(GPL-2.0-or-later).

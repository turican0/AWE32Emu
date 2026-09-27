# Data: what is where

The `main` branch holds only the emulator source, the tools that run on the
real card (`tools/`) and the documentation. Everything measured and every
research tool is in the **`sources` branch**:

```bash
git clone --branch sources --single-branch https://github.com/turican0/AWE32Emu.git AWE32Emu-sources
```

It is an orphan branch (no shared history with `main`), about 0.8 GB, mostly
FLAC recordings.

## Layout of the `sources` branch

| path | contents |
|---|---|
| `recordings/` | the tester's recordings from a real Sound Blaster AWE32, with the AWETEST logs |
| `demos/` | short Magic Carpet 2 excerpts: the real card against our render |
| `tests/` | our Python measuring and comparison tools (trace diffs, the test matrix, disassemblers for the NE/LE/MDI drivers, FAT16 image tool, …) |
| `ref86box/` | the reference renderer `emu8k_ref` (harness + stub headers), the instrumentation patch for 86Box, build and VM control scripts |
| `analysis/` | the analysis scripts of the AWETEST rounds (alignment, per-block analysis, fits of reverb/chorus/filter/interpolation, chip checks) |

### `recordings/`

All recordings are from the tester's card: Sound Blaster AWE32, DSP 4.13,
8 MB DRAM. WAV files are converted to FLAC (lossless); files over 100 MB are
split into parts. The dry right channel of this card is dead — the right line
output carries only the effect returns.

| folder | program | what |
|---|---|---|
| `2026-09-06/`, `2026-09-07/` | early AWETEST | the first recordings (`ver3`; `test4`, `end4` with the log) — **partly invalid**: the test tone played on voice 31, which the driver reserves for the DRAM refresh, and `test4` is overdriven (see `hardware_tests.md`) |
| `2026-09-08/` | AWETEST v05 (run5) | internal capture, blocks 1-15 and 22-35 |
| `2026-09-12/` | AWETEST v25 | internal capture (`reca` blocks 1-11, `recb` 11-13, `recc` 11-39) with logs, and the external line recording `awetst25` |
| `2026-09-23/39-41/` | AWETEST v26 | blocks 39-41, line + internal |
| `2026-09-26/22-23-42/` | AWETEST v27 | blocks 22, 23, 42 (reverb, chorus, chorus loop), line + internal |
| `2026-09-27/28/` | AWETEST v28 | blocks 43-46 (headroom, interpolation, reverb and chorus with noise), line + internal |

Recorder clipping: the line recordings of v26 and v27 are clipped at the
loudest places; v28 was recorded at a lower level and is clean. Levels of the
internal capture are not linear. See `docs/re-notes/emu8000_tuning.md`
before drawing conclusions.

Some rounds arrived without the tester's `AWETEST.LOG`; for those the log of
the same program run in our DOS VM gives the note times (the VM trace and log
are reproducible from `tools/awetest`).

## What is not published anywhere

- the wave ROM `awe32.raw` — see [USAGE.md](USAGE.md) for where to get it
- Creative's drivers, SoundFont banks (`SYNTHGM.SBK`, game banks), the AWE32
  SDK, the demo CDs
- game music (MIDI/XMI, and the full Magic Carpet 2 recordings from the
  card) apart from the short excerpts in `demos/`
- the virtual machine images (they contain Windows 95, DOS and games)
- the 86Box source tree (it is upstream 86Box plus the patch in
  `ref86box/86box-patch/`)

Older commits of `main` contained some of these files; they were removed from
the history in September 2026.

## Local layout used during development

The tools assume the layout they were written in: the repository at
`C:\prenos\AWE32Emu` and a data directory `C:\prenos\AWE32EmuData` next to it
(`rom/`, `sbk/`, `midi/`, `tests/`, `ref86box/`, `tester/`, the VM images,
`docs/86box-src/master-full` with the patched 86Box). Scripts derive most
paths from their own location or from `AWE32EMU_PROJECT` /
`AWE32EMU_TESTER25`; some analysis scripts have absolute paths and need
editing.

# Testing and verification

AWE32Emu is checked on two levels, each against its own standard:

| level | what it covers | standard |
|---|---|---|
| **driver layer** | MIDI -> register translation (`Synth.cpp`, `SoundFont.cpp`, `Awe32Curves.h`) | the **original Creative drivers** running in a virtual machine (86Box) — their port writes are recorded and compared register by register |
| **chip** | what the EMU8000 does with the registers (interpolation, envelopes, filter, effects) | **recordings of a real Sound Blaster AWE32** made with the AWETEST calibration program |

86Box is the standard for the driver layer only — the original binaries run
in it. For the sound it is not: its stock `snd_emu8k.c` differs from the real
card (filter cutoff, interpolation, envelopes, effects). Our chip is
86Box's `snd_emu8k.c` **plus** the corrections measured on the card, and the
VM runs the very same code, so the driver comparison is not disturbed by
chip differences.

The tools named below are in the [`sources` branch](DATA.md) (`tests/`,
`ref86box/`, `analysis/`). They are research tools with hard-coded local
paths in places; they are published so the method can be followed and
repeated, not as a polished test suite.

---

## 1. Port-write traces

Everything revolves around one text format — one write per line, `#` is a
comment:

```
<frame> <port hex> <value hex> [b]
```

`<frame>` is the absolute sample index at 44100 Hz (non-decreasing), the
ports are real ISA addresses (`620`/`622`/`A20`/`A22`/`E20`/`E22`), a
trailing `b` marks a byte write. Reads may be recorded as `R <frame> <port>
<value>`.

- `AWE32Emu --trace file.trace` writes the trace of our driver layer (and a
  DRAM image `file.trace.dram.raw` next to it, because our banks are loaded
  into DRAM directly, not through `SMLD`).
- Our instrumented 86Box writes the same format from inside a VM
  (`EMU8K_TRACE=<file>`), i.e. what the **real** driver wrote.
- `AWE32Emu --replay file.trace --rom awe32.raw --wav out.wav` plays any trace
  through our chip, including sample uploads through `SMALW`/`SMLD`.
- `ref86box/emu8k_ref.exe` plays a trace through the 86Box tree's
  `snd_emu8k.c` (the harness `ref86box/harness.c` with stub headers):

```bash
emu8k_ref.exe --rom awe32.raw --trace song.trace --dram song.trace.dram.raw \
    --ram 8192 --wav song_86box.wav
```

`--ram` is in kilobytes and must hold the whole DRAM image. Further
switches: `--frames N`, `--addr 0x620`, `--gain F`, `--dram-offset N`, `-v`,
and a per-sample voice state dump (`--dump voices.csv --dump-from F
--dump-to F`).

## 2. The driver layer against the real drivers

### The virtual machines

Our own build of 86Box (MinGW; the patch and build scripts are in
`ref86box/86box-patch/` and `ref86box/build_86box.sh`) adds:

1. **a port-write and read trace** of the EMU8000 (`snd_emu8k_trace.c`);
2. **an instruction tracer** (`awe32_trace.c`): at every EMU8000 port access
   it dumps the CPU state and memory windows around `EBX`/`EBP` (the driver's
   voice parameter block), optionally every instruction in an address range
   (`AWE32_TRACE_INSN=1`, needs a build without the dynarec), and code dumps
   from guest memory — this is how the note-on routines of `SBAWE.VXD` and
   `SBAWE32.MDI` were reverse engineered;
3. **a WAV hook** (`AWE32_WAV=<file>`) — the chip's output before the card
   mixer;
4. **a layered state dump** (`EMU8K_STATE_DUMP=<file>`) — registers,
   envelopes, oscillators, filter and effects every N frames;
5. **the fix of the chip ID register** (`0x0C`; upstream returns `0x1C` and no
   original DOS driver detects the card).

Two guests were used (the images are not published — they contain
copyrighted software):

| guest | driver family | what plays |
|---|---|---|
| Windows 95 | `win95` — `SBAWE.VXD` (+ `SBAWE32.DRV`) | Media Player with GM songs |
| DOS 7 | `dos` — `SBAWE32.MDI` (Miles/AIL); `AWEUTIL.COM` for the init | Magic Carpet 2, AWETEST |
| DOS 7 | `sdk` — Creative AWE32 DOS SDK | DOSMid, AWETEST |

`ref86box/run_trace.ps1` starts a VM with the trace switched on, lets it run
for a given time and closes it cleanly (a hard kill corrupts the CMOS image
and the BIOS then waits for F1/ESC). `tests/fat16.py` reads and writes files
in the FAT16 disk image from the host (the VM must be closed). Pitfalls that
cost hours are listed in the re-notes (`tests.md`, `mc2_game.md`):
`AUTOEXEC.BAT` needs CRLF, `BootGUI` in `MSDOS.SYS`, conventional memory,
keyboard input does not reach the guest, the bank upload takes the first
~20 s of a game run.

### The comparisons

| tool | compares |
|---|---|
| `tests/trace_diff.py A B [--notes/--seq]` | two traces: register census, per-note register sets, first differences |
| `tests/notes_diff.py ours driver [--pair] [--dframes a:b]` | the register values at every note-on, register by register |
| `tests/patch_cmp.py cpu.trace ours.csv` | the driver's voice parameter block (from the CPU trace) against `AWE32Emu --dump-notes` |
| `tests/matrix.py` | the test matrix: every case renders a song with our driver layer and compares it with a trace of the real driver; results in `matrix_results.json` |

State of the matrix (all registers written at note-on):

| case | family | player | registers |
|---|---|---|---|
| Georgia, JUMP, RELAX, MINUET, CRAZY, MARS, RELAX_VX, SYNTH02S bank | win95 | Media Player | **all match** |
| Magic Carpet 2 intro and menu | dos | the game | **24/24** |
| Georgia via DOSMid | sdk | DOSMid | **32/32** |
| initialisation vs `AWEUTIL.COM /S` | dos | — | **1611/1611 writes** |

The register matrix compares values at note-on, not voice numbers or sound.
So after changes to the driver layer one more check is run: our MIDI render
against a `--replay` of the real driver's trace **through the same chip** —
any difference is then the driver layer (level, bands, envelope
correlation). It caught a bug the matrix could not see.

Details of every finding: `docs/re-notes/86box_comparison.md`,
`driver_note_on.md`, `mc2_game.md`, `tests.md`.

## 3. The chip: AWE32Emu against 86Box

AWE32Emu carries its own copy of `snd_emu8k.c` (`AWE32Emu/src/86box/`); the
86Box tree used for the VM has the other. They are kept **byte identical**,
and two checks confirm that the emulator and the VM compute the same thing:

- **`analysis/chipcheck.py`** — builds short traces from the AWETEST VM trace
  (initialisation + sample upload + the chosen block), renders them with
  AWE32Emu (`--replay`) and with `emu8k_ref`, and compares the layered state
  dumps and the audio. Run over all AWETEST blocks after every chip change;
  all blocks come out identical.
- **`analysis/statecmp.py`** — compares a state dump taken inside the running
  VM with the dump of a replay of that VM's trace, layer by layer (registers
  first, because a register difference explains the rest). 0 differences in
  every layer.

After a chip change all three are rebuilt: the emulator, `emu8k_ref` and the
VM's 86Box.

## 4. The chip against the real card: AWETEST

[`tools/awetest`](../tools/awetest/README.md) is a DOS program that plays
about an hour of precisely defined test tones on a real AWE32 — not music;
every tone changes one register and leaves everything else alone. It writes
the chip registers directly (through the Creative SDK for the MIDI blocks),
marks every block audibly, and logs every note with a timestamp (BIOS tick +
PIT phase) and its position in its own capture. `tools/awetest/awelog.py`
reads the log.

The procedure for each version:

1. The tester runs `AWETSTnn.EXE` on the real card and records the line
   output (and optionally the card's internal capture through its ADC,
   `/REC:`).
2. The same program runs in our DOS VM with a port trace; the trace is
   replayed through our chip (`--replay`).
3. Card and render are aligned block by block (block marks + timestamps) and
   compared: levels, envelope shapes, spectra relative to a reference note of
   the same path, pitch by Hilbert transform, echo/reverb windows, etc.

What has to be kept in mind (see `docs/re-notes/emu8000_tuning.md`):

- the internal capture of the card is non-linear (compresses loud signals,
  tilt ~+2.6 dB/oct) — levels are taken from the line output;
- the tester's card has a dead dry right channel — the right line output
  carries only the effect returns;
- the card occasionally loses a note-off — such notes are excluded;
- check recordings for clipping before drawing conclusions about headroom.

The analysis scripts of each round (`align28.py`, `ana28.py`, `erdeconv.py`,
`interp28.py`, …) are in `analysis/` of the `sources` branch, the recordings
in `recordings/`.

## 5. Other checks

| tool | what |
|---|---|
| `tests/regress.py` | quick regression: pitch of a GM piano note, level, song length, no voice on the substitute sine, no clipping |
| `tests/render_sweep.py` | stress test: renders a collection of 232 MIDI files, watches for crashes, empty outputs, nonsense voice counts |
| `tests/bank_sanity.py` | loads every SF1 bank and plays with it |
| `--export-sf2` round trip | render from `.SBK` and from the exported `.sf2`, compared note by note with `--dump-notes` |
| `tests/tune.py`, `tests/note_probe.py` | per-note comparison against commercial recordings (demo CD, game recordings) — the early method, see `emu8000_tuning.md` |

## 6. The four-way blame decomposition

With the same song in four forms, each neighbouring pair differs in exactly
one thing:

| form | driver | chip |
|---|---|---|
| our render | ours | ours |
| our trace through `emu8k_ref` | ours | 86Box |
| VM capture (`AWE32_WAV`) | **real** | 86Box |
| recording of real hardware | real | **real** |

This is how it was established that the remaining difference to the hardware
was in the chip (86Box vs hardware: envelope correlation 0.66 on Georgia)
and not in the driver layer — and why the chip was then tuned on AWETEST
recordings instead of against 86Box.

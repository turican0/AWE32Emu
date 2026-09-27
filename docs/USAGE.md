# AWE32Emu — usage

`AWE32Emu` is a console program. It takes a song (`.mid` or `.xmi`), a sound
bank and, optionally, the card's wave ROM, and either plays it live or writes
it to a `.wav` file.

```
AWE32Emu <song.mid|song.xmi> [options]
AWE32Emu --help
```

Live playback goes through `winmm`, so it is Windows only. Rendering to a file
(`--wav`) works everywhere — on Linux it is the only mode.

---

## Quick start

Without any bank the program plays a generated sine — you can tell the
sequencer runs, but it does not sound like an AWE32:

```bash
AWE32Emu song.mid --wav out.wav
```

The real sound starts with the ROM and a bank:

```bash
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --wav out.wav
```

### Where to get the files

| file | where from |
|---|---|
| `awe32.raw` (wave ROM, 1 MB) | the dump used by PCem/86Box, e.g. [libretro-pcem/awe32.raw](https://github.com/libretro/libretro-pcem/blob/master/awe32.raw), or your own card dumped with [`tools/awedump`](../tools/awedump/README.txt) |
| `SYNTHGM.SBK`, `SBAWE32.MDI` | the Creative AWE32 driver disks / the drivers installed with the card |
| game banks (`BULLFROG.SBK`, …) | the game's own directory |

None of these files are part of this repository — they are copyrighted by
Creative Technology / E-mu Systems or the game publishers.

The widespread `awe32.raw` carries one extra word (`0x1234`) in front of the
ROM; the loader detects and skips it, exactly as 86Box does.

---

## ROM, bank and "a bank that describes the ROM"

This is the one place where it is easy to get lost, so here is an example.

| switch | what it is |
|---|---|
| `--rom` | **data**. The raw 1 MB dump of the card's wave ROM (`awe32.raw`), 16-bit LE. On its own it plays nothing — it is only sound memory. |
| `--rombank` | a **description** of the ROM contents: which presets lie where in the ROM (`1mgm.sf2`, `SYNTHGM.SBK`). It holds no samples of its own. |
| `--sf` | a **user bank**. `.SBK` (SoundFont 1.0), `.SF2`, or the DOS driver `SBAWE32.MDI` (its built-in GM presets). Samples come with it and are loaded into the card's emulated DRAM. |

A typical full set looks like this:

```bash
AWE32Emu song.mid \
    --rom     rom/awe32.raw \
    --rombank rom/1mgm.sf2 \
    --sf      sbk/BULLFROG.SBK \
    --wav out.wav
```

Banks can be given several times and **are layered — later ones override
earlier ones**. That is exactly what the driver does when a game loads its own
bank next to the general GM one.

### Two banks at once (selection through CC0)

Some songs expect the user to load **several banks into different slots** and
switch between them with Bank Select (CC0). The slot is appended to the path
with `@`:

```bash
AWE32Emu DANCESBK.MID \
    --rom rom/awe32.raw \
    --sf SYNTHGM.SBK \
    --sf SBK/9FTGRAND.SBK@1 \
    --sf SBK/GMDRUM.SBK@2 \
    --wav out.wav
```

This mirrors what `DEMO/SBK.TXT` on the SoundFont CD says about the demo:

> 3. Dancesbk.mid — Load 9ftgrand.sbk on Bank 1, load Gmdrum.sbk on Bank 2.

User banks have bank number 0 in their `phdr`; `@N` moves their presets to
MIDI bank N. Each bank gets its own piece of DRAM after the previous one, as
on a real card.

---

## Output

```bash
# play in real time (Windows only)
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK

# write a .wav (44 100 Hz, 16 bit, stereo)
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --wav out.wav

# only selected MIDI channels - the fastest way to find which instrument is wrong
# channels 1..16; positive numbers = only these, negative = all but these
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --tracks 1 --wav ch1.wav
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --tracks -8,-9 --wav no8and9.wav
```

---

## Which driver family

Creative shipped several driver families and **they are not interchangeable**
— they differ in eight values of the initialisation arrays, the velocity
table, the attenuation formula, the voice allocation and more:

| `--driver` | driver | where you meet it |
|---|---|---|
| `win95` (default) | `SBAWE.VXD` | Windows 95, Creative MIDI |
| `dos` | `SBAWE32.MDI` / `SBAWE32.DRV` | DOS games (Miles/AIL), period recordings |
| `sdk` | Creative AWE32 DOS SDK (`midieng.c`) | programs built on the SDK, e.g. DOSMid |

DOS games using AIL (e.g. Magic Carpet 2) load only their own bank; the GM
presets for the ROM are compiled into the `SBAWE32.MDI` driver. So instead of
`SYNTHGM.SBK` the driver itself is given (`--sf SBAWE32.MDI`); the registers
then match the game 24/24. Only the Win95 driver loads `SYNTHGM.SBK` from a
file.

The recordings from real hardware used for tuning are older than Win95, so
they are rendered with `--driver dos`.

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf SBAWE32.MDI --sf BULLFROG.SBK \
    --driver dos --wav out.wav
```

The details of the differences are in `AWE32Emu/src/Awe32Driver.h`.

---

## Which chip core

```bash
--chip 86box   # snd_emu8k.c from 86Box with our measured corrections (default with --rom)
--chip ours    # our older own core - float, tunable filter (default without --rom)
```

`--chip 86box` compiles **literally the same file** as our 86Box tree (a copy
lives in `AWE32Emu/src/86box/`), so the emulator and the virtual machine used
for the measurements run identical chip code. It needs `--rom`. The file
carries the corrections measured on real hardware: cubic B-spline
interpolation, a Chamberlin filter, the equalizer, the measured attack shape,
reverb/chorus fitted to line-out recordings, and more — see
[TESTING.md](TESTING.md) and `AWE32Emu/src/86box/README.md`.

`--ram <KB>` sets the DRAM size of that chip (86Box `onboard_ram`, default
8192).

`--chip ours` is the original float core. It is kept for comparison and for
the tuning options below.

---

## Debug output

```bash
# the first 40 started voices with the registers written to them
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --debug-voices 40 --wav out.wav

# CSV with all note-ons: the columns follow the parameter block in SBAWE.VXD,
# so it can be put next to a dump from the real driver
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --dump-notes notes.csv --wav out.wav

# a trace of all port writes (comparison with 86Box / the real driver)
AWE32Emu song.mid --rom awe32.raw --sf SYNTHGM.SBK --trace song.trace --wav out.wav

# play a port-write trace (ours or one captured in a VM) through the chip
AWE32Emu --replay song.trace --rom awe32.raw --wav replay.wav
```

`--dump-notes` is the main tool when two builds differ: comparing two CSVs
immediately shows **which** register diverged, instead of guessing from the
sound.

---

## Initial channel state (`--conf`)

A game typically sends a set of control changes (volume, pan, reverb, …)
before the first note. When only a bare XMI is rendered, that state is
missing and the render starts somewhere else than the recording. `--conf`
supplies it:

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf SBAWE32.MDI --sf BULLFROG.SBK \
    --driver dos --conf conf/mc2.conf --wav out.wav
```

The file is a list of messages (`cc <number> <value>`, `program <n>`,
`bend <0..16383>`) that are sent in file order on all sixteen channels, plus
`master_volume <0..127>` and `trigger_mute <0|1>`; everything after `#` is a
comment. [`conf/mc2.conf`](../conf/mc2.conf) is the measured start-up state of
Magic Carpet 2. `--master-volume N` (0–127) overrides the master volume of the
AIL sequencer.

---

## Converting SBK → SF2

A SoundFont 1.0 bank is of little use today: hardly any player reads `.SBK`,
and the samples often lie in the card's wave ROM, which nobody has.
`--export-sf2` turns it into one self-contained file.

```bash
# a single bank (the ROM samples are baked into the file)
AWE32Emu --rom rom/awe32.raw --sf sbk/BULLFROG.SBK --export-sf2 bullfrog.sf2

# several banks into one (later ones override earlier ones, as in playback)
AWE32Emu --rom rom/awe32.raw --sf SBAWE32.MDI --sf sbk/BULLFROG.SBK \
    --export-sf2 all.sf2
```

No song is needed — nothing is played during the export.

**It is not a repackaging.** SF1 stores values in different units than SF2
(times in milliseconds, cutoff 0–127, attenuation "127 = none", sustain
through `v * 4 / 3`), and if they were just copied into an SF2 container the
bank would sound wrong. The meaning is converted, with formulas measured
against the real Creative driver.

Check that the conversion holds: a render from the original `.SBK` and a
render from the exported `.sf2` are compared note by note. On the Magic
Carpet 2 intro (671 notes) all twenty tracked registers match and the levels
agree to five decimal places.

---

## Tuning options of the own core

These are not used for normal playback — they exist to measure how an option
affects the result against recordings from real hardware. They apply to
`--chip ours` only.

| option | default | what it does |
|---|---|---|
| `--interp linear\|cubic\|3point\|3pointc\|sinc` | `sinc` | sample interpolation |
| `--filter-mode cham\|tpt\|86box` | `cham` | filter structure (`cham` = Chamberlin, as measured on the card) |
| `--filter-top <Hz>` | 8000 | cutoff at register 0xFF |
| `--cutoff-base <Hz>` | 101.81 | cutoff at register 0 |
| `--filter-poles 1\|2\|4` | 2 | filter slope 6/12/24 dB per octave |
| `--q-base <x>` | 1.0 | resonance base (0.7071 = Butterworth) |
| `--q-cutoff-shift <oct>` | 0 | cutoff drop at Q = 15 |
| `--filter-atten <x>` | 1 | strength of the filter input attenuation |
| `--resonance-db <x>`, `--resonance-curve faq\|flat` | 24, `flat` | resonance amount and its dependence on the cutoff |
| `--cutoff-map exp\|lin` | `exp` | register to cutoff mapping |
| `--pan linear\|power` | `linear` | pan law (`linear` = a multiplier, as in the chip) |
| `--loop-wrap on\|off` | `on` | wrap the interpolation samples into the loop |
| `--eq on\|off` | `on` | the card's bass/treble equalizer |
| `--hold-scale`, `--decay-scale`, `--attack-scale` | 1 | envelope time scales |
| `--sinc-taps <n>` | 8 | sinc kernel length |
| `--reverb 0..7`, `--chorus 0..7` | driver | effect preset |
| `--rev-room`, `--rev-damp`, `--rev-return`, `--cho-return` | — | effect tuning |

---

## Building

### Windows, Visual Studio

Open `AWE32Emu.sln` and build `Release|x64`.

### CMake (Windows and Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The 86Box chip core is in the repository and is built in by default;
`-DAWE32EMU_WITH_86BOX=OFF` leaves it out (`--chip 86box` then reports that
it is missing).

### Prebuilt binaries

Every tag starting with `v` (e.g. `v0.3`) starts a GitHub Action that builds
the Windows and Linux binaries and attaches them to a release — see
`.github/workflows/release.yml`.

---

## Where next

- [TESTING.md](TESTING.md) — how the emulation is verified against the real
  card and against 86Box
- [DATA.md](DATA.md) — the `sources` branch: tester recordings, measuring tools
- `docs/re-notes/` — what was found out about the chip and the drivers, and
  how (register map, unit conversions, comparison with 86Box, the tuning log)

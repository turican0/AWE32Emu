# Development notes

How the project is worked on, the rules that keep the measurements honest,
and the traps that already cost time. For the test methods themselves see
[TESTING.md](TESTING.md); for the findings, `docs/re-notes/`.

## Build

```bash
# Windows, Visual Studio 2022
MSBuild AWE32Emu.sln -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo

# anywhere
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

After a change of the chip (`AWE32Emu/src/86box/snd_emu8k.c`) three things are
rebuilt, because they must run identical chip code: the emulator, the
reference renderer `emu8k_ref` and the 86Box used for the virtual machines
(see `ref86box/` in the `sources` branch). Then `chipcheck.py` over all
AWETEST blocks.

## Translations

User-facing messages are English and wrapped in `_()` (`I18n.h`); a message
composed of parts is one format string (`StrFormat`), so the translator sees
the whole sentence. Traces, CSV dumps and other machine-read output are not
translated, and `I18n::Init` keeps `LC_NUMERIC` at `C`.

```bash
cmake --build build --target pot        # po/awe32emu.pot from the sources
cmake --build build --target update-po  # merge it into po/*.po
msginit -i po/awe32emu.pot -l de_DE.UTF-8 -o po/de.po   # a new language
```

Every `po/<lang>.po` is compiled into `locale/<lang>/LC_MESSAGES/awe32emu.mo`
at build time. On Windows `xgettext`, `msgmerge` and `msgfmt` come with
MSYS2 (`C:\msys64\usr\bin`).

## Rules

1. **Two levels, two standards.** The driver layer is matched 1:1 against the
   real Creative drivers running in 86Box (register traces). The chip is
   matched against recordings of a real card. 86Box is not the standard for
   the sound.
2. **A chip change goes into both copies** of `snd_emu8k.c` (AWE32Emu and the
   86Box tree) and is marked with a comment `AWE32Emu:`.
3. **Change the driver layer only with evidence** from the driver code or its
   trace; every change keeps all families at a full register match
   (`tests/matrix.py`) and passes the replay comparison (sound through the
   same chip).
4. **Change the chip only where a recording says so unambiguously** and the
   effect is not an artefact of the recording path (clipping, the non-linear
   internal capture, the dead right channel of the tester's card, lost
   note-offs).
5. **Do not decide below five pairs** of songs when tuning against music;
   a single song lies.
6. **Measure directly instead of deriving from a difference of results.**
   Two conclusions built on derived quantities were wrong (the "22-semitone
   bend range", the "exponential delay").

## Traps in measuring

- **Do not measure "the strongest peak"** — when the timbre changes it jumps
  to a harmonic and reports an octave shift. Measure the amplitude at the
  expected frequency.
- **Do not compare band energy ratios** — an excess in the mids lowers the
  share of the bass and looks like missing bass. Use absolute band energies
  with the overall level subtracted.
- **Do not put into the pairing key what is to be compared** — pairing notes
  by `(IP, PSST, CSL)` guaranteed a match in `PSST` and `CSL` and hid the drum
  notes.
- **Check the timing before reading match percentages** — when the note
  sequences diverge, order pairing produces nonsense differences.
- **A VM trace often holds several songs** (Magic Carpet 2: intro, menu, the
  intro again) — cut the window first (`trace_split.py`, `--dframes`).
- **Short windows mislead** — a 4096-sample window over a 1197-sample loop
  reads mostly data after the loop.
- **Medians of small groups are noisy** — compare a group's deviation with
  random groups of the same size (`group_signif.py`).
- **Check line recordings for clipping** (±32767) before any conclusion about
  saturation or headroom.
- **XMI has two different encodings**: delta times are sums of bytes < 0x80,
  note durations are standard SMF VLQs; XMI ignores tempo meta events and
  runs at a fixed 120 Hz.

## Traps in the virtual machines

- A hard kill of 86Box corrupts the CMOS image; the BIOS then waits for
  F1/ESC. Close the window cleanly.
- `AUTOEXEC.BAT` must have CRLF line endings (tools like `sed` strip the CR).
- `BootGUI` in `MSDOS.SYS` decides between DOS and Windows 95.
- The Windows 95 guest needs `HIMEM`/`EMM386`/`DOS=HIGH,UMB`, otherwise
  Media Player reports "Insufficient memory" on larger songs.
- `run=` in `WIN.INI` must point to a batch file, not to `MPLAYER.EXE`.
- A mounted CD with `AUTORUN.INF` starts its installer and steals the focus.
- Keyboard input does not reach the guest — everything is driven from
  `AUTOEXEC.BAT`.
- Device options belong in the section named after the device in the 86Box
  config (e.g. `onboard_ram` under `[Sound Blaster AWE32 PnP]`), not in
  `[Sound]`.
- Games upload their bank first (Magic Carpet 2: ~20 s) — let the run go long
  enough.

## Where the knowledge is

| file | about |
|---|---|
| `re-notes/emu8000_register_map.md` | registers, ports, init sequence |
| `re-notes/aweutil_register_access.md` | AWEUTIL's register access |
| `re-notes/86box_comparison.md` | the driver layer, section by section, against the real drivers |
| `re-notes/driver_note_on.md` | what the drivers compute at note-on, with disassembly |
| `re-notes/sbawe32_mdi.md`, `mc2_game.md` | the DOS family and Magic Carpet 2 |
| `re-notes/soundfont1_sbk.md`, `rom_vs_sf2.md` | SoundFont 1.0 and the wave ROM |
| `re-notes/tests.md` | the test matrix, other banks, DOSMid |
| `re-notes/hardware_tests.md` | the original design of AWETEST |
| `re-notes/emu8000_tuning.md` | the chip tuning log and the AWETEST results |
| `re-notes/early_measurements.md` | the first goal and measurements (commercial recordings) |

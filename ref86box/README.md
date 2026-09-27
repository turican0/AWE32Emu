# ref86box – 86Box as the reference

Two things live here:

1. **`emu8k_ref`** – a standalone renderer around 86Box's EMU8000
   (`snd_emu8k.c`). It plays a text trace of port writes
   (`<frame> <port> <value>` at 44.1 kHz) and writes a WAV, so AWE32Emu's
   chip can be compared with 86Box's sample by sample.
2. **The instrumented 86Box** – a patch for 86Box that records the EMU8000
   port writes of a running guest (DOS, Windows 95, games), the CPU state at
   those writes, the chip's output as a WAV, and layered chip state dumps.
   This is how the traces of the real drivers were obtained.

How the traces are used is described in
[docs/TESTING.md](https://github.com/turican0/AWE32Emu/blob/main/docs/TESTING.md)
on `main`.

## Files

| file | what |
|---|---|
| `harness.c` | the renderer: 86Box globals and services that `snd_emu8k.c` needs, trace reader, WAV writer |
| `include/86box/` | stub headers for the harness |
| `upstream/` | untouched copies of 86Box's `snd_emu8k.c` / `snd_emu8k.h` (for diffs) |
| `verify_upstream.py` | checks the copies against stored SHA-256 or, with `--online`, against GitHub |
| `build.bat` | builds `emu8k_ref.exe` with MSVC from the patched 86Box tree |
| `86box-patch/` | `awe32emu.patch` against the 86Box commit in `86box-commit.txt`, plus the new files under `src/` |
| `build_86box.sh` | builds the patched 86Box in MSYS2 MINGW64 |
| `noslirp/`, `apply_noslirp.sh` | fallback without libslirp (networking is not needed) |
| `run_trace.ps1` | starts a VM with the trace (and optionally the CPU trace and WAV) switched on and closes it after a set time |
| `run_vm.bat` | the same, simpler, for the Windows 95 image |
| `screenshot.ps1` | a screenshot of the 86Box window |

## Building

```bash
git clone https://github.com/86Box/86Box.git master-full
cd master-full
git checkout 998ee1eb3e0fb6c5fb2c29cd54c1b438f05a6531
git apply ../ref86box/86box-patch/awe32emu.patch
cp -r ../ref86box/86box-patch/src/* src/
```

The scripts expect this tree in `docs/86box-src/master-full` of the data
directory (`C:\prenos\AWE32EmuData`); adjust the paths at the top of
`build.bat` and `build_86box.sh` for another layout. The chip in the patched
tree must stay byte-identical with `AWE32Emu/src/86box/snd_emu8k.c` on
`main`.

## The VM – pitfalls

The VM images (Windows 95, DOS 6.22 with games) are not published.

- **Never kill 86Box hard.** A hard kill leaves a broken CMOS
  (`nvr/thor.nvr`) and the BIOS then waits on *Press F1 for Setup, ESC to
  Boot*. `run_trace.ps1` closes the window cleanly, keeps a known good CMOS
  copy (`thor.nvr.good`) and restores it when the checksum is bad.
- 86Box needs `-R <roms>` pointing to its ROM set.
- Booting to the music can take several minutes; set `-Seconds`
  accordingly.
- The dynarec skips the instruction hook. For the CPU tracer build a second
  86Box without it (`AWE32_DYNAREC=OFF AWE32_BUILDDIR=... build_86box.sh`)
  and select that build in `run_trace.ps1` with `AWE32_BUILD`.

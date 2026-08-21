# Kde je co

V repozitari je **jen zdrojak emulatoru a dokumentace**. Vsechno ostatni -
vzorky, banky, ROM, referencni nahravky, obrazy virtualnich stroju, zdrojak
86Boxu, ale i nase testovaci a merici nastroje - lezi vedle:

```
C:\prenos\AWE32Emu\        projekt (to, co se commituje)
C:\prenos\AWE32EmuData\    vsechno ostatni (mimo git)
```

Prikazy se pisou **z korene projektu**, takze maji prefix `../AWE32EmuData/`.

> **Pozor:** nastroje v `../AWE32EmuData/tests/` a `../AWE32EmuData/ref86box/`
> uz **nejsou pod verzi**. Je to nas vlastni kod - 23 python nastroju,
> `harness.c`, merici skripty - takze jeho zaloha je na tobe.

---

## Co je v projektu

| cesta | co |
|---|---|
| `AWE32Emu/src/` | zdrojak emulace EMU8000 |
| `AWE32Emu.sln` | reseni pro Visual Studio 2022 |
| `docs/` | dokumentace a nalezy z reverzovani (`re-notes/`) |
| `README.md`, `awe32-emulace-todo.md`, `aweutil_register_access_notes.md` | |
| `bin/`, `obj/`, `.vs/` | vystupy prekladu, v `.gitignore` |

## Co je v datovem adresari

| cesta | MB | co to je |
|---|---|---|
| `tests/` | 3258 | **23 nasich python nastroju** + `out/` s vygenerovanymi WAVy a stopami |
| `ref86box/` | 1183 | **nas harness a merici skripty** + obrazy VM, buildy 86Boxu, prevzate zdrojaky |
| `cdrom/` | 460 | rozbalene instalacni CD Creative (`SYNTHGM.SBK`, ovladace) |
| `SAMPLES2/` | 134 | demo CD Creative - bezztratove FLAC + odpovidajici MID |
| `ogg/` | 80 | referencni nahravky k `midi/` (ztratove) |
| `docs/86box-src/` | 63 | klon 86Boxu vcetne nasi instrumentace |
| `SAMPLES/` | 35 | dalsi vzorky a 121 MIDI od Creative |
| `SoundBlaster AWE32/` | 7 | ovladace Creative vcetne `traced-drivers/` |
| `docs/next docs/` | 5 | SDK, Programmer's Guide, vytazene texty |
| `rom/` | 2 | `awe32.raw` (wave ROM karty), `1mgm.sf2` |
| `midi/` | 1 | skladby Magic Carpet 2 ve ctyrech variantach |
| `sbk/` | 1 | `BULLFROG.SBK`, `SBAWE32.MDI` |
| `analyze/` | 0,1 | analyza die shotu EMU8000, patch proti 86Boxu, `envelope_sim.py` |

Podrobny popis obsahu ma `../AWE32EmuData/README.md`.

### Uvnitr `../AWE32EmuData/ref86box/`

| cesta | puvod |
|---|---|
| `harness.c`, `include/86box/*.h` (osm stubu) | **nas kod** |
| `run_trace.ps1`, `run_vm.bat`, `build.bat`, `build_86box.sh`, `screenshot.ps1` | **nase skripty** |
| `verify_upstream.py` | **nas** - kontrola otisku prevzatych zdrojaku |
| `86box-patch/` | **nase** instrumentace 86Boxu: patch, ctyri nove zdrojaky, hash commitu |
| `noslirp/` | **nas** stub za `net_slirp.c`, aby build nepotreboval glib |
| `upstream/snd_emu8k.c`, `upstream/snd_emu8k.h` | 86Box, **bajt po bajtu** |
| `include/86box/snd_emu8k.h` | tataz hlavicka jeste jednou, aby ji nasel `#include <86box/...>` |
| `vm/`, `vmdos/` | virtualni stroje (Windows 95 / DOS + Magic Carpet 2) |
| `build86box/`, `build86box_int/`, `build/` | prelozene binarky |

Pozor na jednu vec, ktera se lehko prehledne: `include/86box/snd_emu8k.h`
**neni stub**, ale bajt po bajtu skutecna hlavicka 86Boxu (799 radku, totozna
s `upstream/snd_emu8k.h`) - jen se jmenuje jako osm stubu vedle ni. Ty maji
4 az 22 radku a napsali jsme si je sami.

---

## Jak si skripty hledaji cesty

Vsechny merici a testovaci skripty uz **lezi v datovem adresari**, takze si
cesty odvozuji od sve vlastni polohy - nic se jim predavat nemusi:

| skript | co si odvodi |
|---|---|
| `tests/regress.py` | data = o adresar vys; prelozeny prehravac hleda v `../AWE32Emu` (prebiji promenna `AWE32EMU_PROJECT`) |
| `ref86box/verify_upstream.py` | prevzate zdrojaky vedle sebe |
| `ref86box/build.bat` | `include/`, `upstream/`, `harness.c` vedle sebe, vystup do `build/` |
| `ref86box/build_86box.sh` | zdrojak 86Boxu v `../docs/86box-src/master-full` |
| `ref86box/run_trace.ps1`, `run_vm.bat` | `vm/`, `vmdos/`, `build86box/` vedle sebe |

---

## Prikazy s aktualnimi cestami

Spousti se z korene projektu (`C:\prenos\AWE32Emu`).

Prelozeni:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" AWE32Emu.sln -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

Regresni sada:

```bash
python ../AWE32EmuData/tests/regress.py
```

Prehrani RELAXu vcetne zpevu (uzivatelska banka patri do MIDI banky 1):

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/SAMPLES2/RELAX_VX.MID --rom ../AWE32EmuData/rom/awe32.raw --sbk "../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK" --sbk "../AWE32EmuData/SAMPLES2/RELAX.SBK@1" --wav ../AWE32EmuData/tests/out/relax_vx.wav
```

Srovnani se skutecnou nahravkou:

```bash
python ../AWE32EmuData/tests/cmp_real.py ../AWE32EmuData/tests/out/relax_vx.wav "../AWE32EmuData/SAMPLES2/3 - Relax.flac"
```

Reprodukce dosoveho mereni (hra ma hlavni hlasitost AIL kolem 100/127):

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_w.xmi --rom ../AWE32EmuData/rom/awe32.raw --sbk ../AWE32EmuData/sbk/BULLFROG.SBK --wav ../AWE32EmuData/tests/out/mc_intro.wav --trace ../AWE32EmuData/tests/out/mc_intro.trace --driver dos --master-volume 100
```

Srovnani registru proti ovladacum:

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/mc_intro.trace ../AWE32EmuData/tests/out/dos_mdi.trace --pair --dframes 0:6000000
```

Referencni renderer 86Boxu a spektralni srovnani:

```bash
../AWE32EmuData/ref86box/build/emu8k_ref.exe --rom ../AWE32EmuData/rom/awe32.raw --trace ../AWE32EmuData/tests/out/ours_win95.trace --dram ../AWE32EmuData/tests/out/ours_win95.trace.dram.raw --ram 8192 --wav ../AWE32EmuData/tests/out/minuet_86box.wav
```

```bash
python ../AWE32EmuData/tests/cmp86box.py ../AWE32EmuData/tests/out/minuet_ours.wav ../AWE32EmuData/tests/out/minuet_86box.wav
```

Kontrola, ze prevzate zdrojaky 86Boxu jsou beze zmeny:

```bash
python ../AWE32EmuData/ref86box/verify_upstream.py --online
```

Disassemblery:

```bash
python ../AWE32EmuData/tests/mdi_disasm.py ../AWE32EmuData/sbk/SBAWE32.MDI --at 0x1e76 --count 60
```

```bash
python ../AWE32EmuData/tests/le_disasm.py "../AWE32EmuData/SoundBlaster AWE32/traced-drivers/SBAWE.VXD" --obj 1 --at 0x1ec0 --count 20
```

Prelozeni harnessu a instrumentovaneho 86Boxu:

```bash
cmd /c call C:\prenos\AWE32EmuData\ref86box\build.bat
```

```bash
C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=MINGW64 AWE32_DYNAREC=OFF AWE32_BUILDDIR=/c/prenos/AWE32EmuData/ref86box/build86box_int /c/prenos/AWE32EmuData/ref86box/build_86box.sh"
```

---

## Obnova 86Boxu s nasi instrumentaci

Kdyby se `../AWE32EmuData/docs/86box-src` ztratil:

```bash
git clone https://github.com/86Box/86Box ../AWE32EmuData/docs/86box-src/master-full
```

Pak v tom klonu prepnout na commit z
`../AWE32EmuData/ref86box/86box-patch/86box-commit.txt`, nakopirovat do stromu
`../AWE32EmuData/ref86box/86box-patch/src/` a aplikovat
`instrumentace.patch` (4 zmenene soubory, 35 radku).

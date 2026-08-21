# Prompt pro novou session

Zkopiruj celý tenhle soubor jako první zprávu nové konverzace.

> **Data nejsou v repozitari.** Vzorky, banky, ROM, nahravky, obrazy
> virtualnich stroju a zdrojak 86Boxu lezi v `C:/prenos/AWE32EmuData`.
> Prikazy nize proto maji prefix `../AWE32EmuData/`. Mapa presunu a
> nastaveni promenne `AWE32EMU_DATA` je v [docs/DATA.md](DATA.md).

---

## Co je projekt

`C:\prenos\NeuralFin2` je pracovní adresář, ale **projekt je
`C:\prenos\AWE32Emu`** — C++ emulace čipu **EMU8000** (Sound Blaster AWE32).
Cíl: přehrávat `.mid`/`.xmi` s originálními bankami a umět napojit reversed
DOS hru přes portovou úroveň.

Mluv česky. Uživatel je Tomáš, chce měření a důkazy, ne dojmy.

Build:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" AWE32Emu.sln -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

**Nejdřív si přečti `docs/POKRACOVANI.md` a `docs/re-notes/86box_srovnani.md`.**
Ten druhý má 16 sekcí a je hlavní zdroj pravdy o tom, co je ověřené a čím.

---

## Kde to stojí

Ověřuje se proti **skutečným ovladačům Creative** běžícím v našem buildu
86Boxu, ne proti dokumentaci.

| co | stav |
|---|---|
| inicializace vs `AWEUTIL.COM /S` | **1611 z 1611 zápisů** |
| note-on vs `SBAWE.VXD` (`--driver win95`) | **všech 24 registrů, 242/242 not** |
| note-on vs `SBAWE32.MDI` (`--driver dos`) | **všech 24 registrů, 255/255 not** |
| `../AWE32EmuData/tests/regress.py` | prochází 5/5, peak RELAXu 0,576 |

**Registrová vrstva je hotová u obou rodin ovladačů.** Creative má dvě rodiny,
které se v osmi bodech záměrně liší; přepínají se `--driver dos|win95`
(výchozí `win95`), rozcestník s tabulkou odchylek je `src/Awe32Driver.h`.
Neexistuje jedna správná varianta — nesnaž se jednu přepsat druhou.

Nejdůležitější nález poslední session: **blok parametrů vrstvy je v obou
ovladačích přímé pole generátorů SoundFontu indexované `generátor*2`**, a oba
mají v sobě tabulku výchozích hodnot generátorů (MDI `0x16AD`, VXD obj 1
`0x6D60`). Tím se dají všechny dosud záhadné offsety typu `[si+0x60]` přečíst
rovnou. Viz sekce 16.2 a 16.3.

---

## Co je další velký úkol: filtr a resampler

Registry při note-on už sedí, ale zvuk pořád ne. Proti 86Boxu na MINUETu
(86Box přehrává **naši** stopu, takže rozdíl je čistě v jádru):

| pásmo Hz | rozdíl dB |
|---|---|
| 0–100 až 3200–6400 | +2,8 až +4,1 (rovnoměrné = jen rozdíl úrovně) |
| 6400–12800 | −4,8 |
| 12800+ | **−30,7** |

Korelace obálky hlasitosti 0,998. My máme TPT state-variable filtr, 86Box
`FILTER_MOOG` (Moog ladder) a `RESAMPLER_CUBIC`. Měří se:

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/SAMPLES/MIDI/BACH/MINUET.MID --rom ../AWE32EmuData/rom/awe32.raw --sbk "../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK" --wav ../AWE32EmuData/tests/out/minuet_ours.wav --trace ../AWE32EmuData/tests/out/ours_win95.trace
```

```bash
../AWE32EmuData/ref86box/build/emu8k_ref.exe --rom ../AWE32EmuData/rom/awe32.raw --trace ../AWE32EmuData/tests/out/ours_win95.trace --dram ../AWE32EmuData/tests/out/ours_win95.trace.dram.raw --ram 8192 --wav ../AWE32EmuData/tests/out/minuet_86box.wav
```

```bash
python ../AWE32EmuData/tests/cmp86box.py ../AWE32EmuData/tests/out/minuet_ours.wav ../AWE32EmuData/tests/out/minuet_86box.wav
```

Vedle toho stojí za pohled, že u intra Magic Carpet 2 je korelace obálky
proti 86Boxu jen **0,84** (u MINUETu 0,998) a obě strany klipují na 1,0 —
tam je asi ještě něco k nalezení v jádru, ne v ovladači.

---

## Infrastruktura (všechno hotové, jen používat)

### Měření proti ovladačům

```bash
powershell -File ../AWE32EmuData/ref86box/run_trace.ps1 -Trace ../AWE32EmuData/tests/out/win95.trace -Mode win95 -Seconds 300
```

```bash
powershell -File ../AWE32EmuData/ref86box/run_trace.ps1 -Trace ../AWE32EmuData/tests/out/dos_mdi.trace -Mode mc2 -Seconds 240
```

Porovnání registrů:

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/mc_intro.trace ../AWE32EmuData/tests/out/dos_mdi.trace --pair --dframes 0:6000000
```

**`--dframes` je povinné, když hra během měření přehrála víc skladeb**
(Magic Carpet 2 hraje intro, pak jinou skladbu, pak intro znovu). Bez toho
se do srovnání dostanou noty, které v našem vstupu vůbec nejsou — přesně
tohle vyrobilo neexistující „rozdíl panu u 37 not".

Reprodukce dosového měření (hra má hlavní hlasitost AIL ≈ 100/127):

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_w.xmi --rom ../AWE32EmuData/rom/awe32.raw --sbk ../AWE32EmuData/sbk/BULLFROG.SBK --wav ../AWE32EmuData/tests/out/mc_intro.wav --trace ../AWE32EmuData/tests/out/mc_intro.trace --driver dos --master-volume 100
```

### Nástroje v `../AWE32EmuData/tests/`

| skript | k čemu |
|---|---|
| `regress.py` | **po každé změně** |
| `notes_diff.py` | registry při note-on proti ovladači (`--pair`, `--dframes`) |
| `trace_diff.py` | přehled zápisů, note-on rozpis |
| `cmp86box.py` | spektrum proti 86Boxu |
| `xmi_events.py` | MIDI události z XMI — programy, controllery, velocity |
| `sbk_dump.py` | generátory presetů a nástrojů z SBK/SF2 |
| `insn_view.py` | instrukční stopa z 86Boxu + disassembly VXD |
| `le_disasm.py` | 32bit VxD (objekty, stránky, xref) |
| `mdi_disasm.py` | 16bit AIL/Miles `.MDI` |
| `ne_disasm.py` | 16bit NE `.DRV` |
| `fat16.py` | čtení/zápis do obrazu disku guesta |

### Instrukční tracer v 86Boxu

Vypíše stav CPU u každého přístupu na porty EMU8000, volitelně i každou
instrukci v zadaném rozsahu.

```bash
C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=MINGW64 AWE32_DYNAREC=OFF AWE32_BUILDDIR=/c/prenos/AWE32EmuData/ref86box/build86box_int /c/prenos/AWE32EmuData/ref86box/build_86box.sh"
```

Proměnné: `AWE32_TRACE_FILE`, `AWE32_TRACE_INSN=1`, `AWE32_TRACE_LO/HI`,
`AWE32_TRACE_MEM`, `AWE32_TRACE_MAX`. Objekt 1 `SBAWE.VXD` byl při minulém
běhu natažený na `0xC0FF8B48` — **ověř znovu**, mezi běhy se to může lišit.

### Převod adres v `SBAWE.VXD`

Objekt 1 začíná na file offsetu `0x2000`, takže `VA = fileoff − 0x2000`.
`le_disasm.py --obj 1 --at <VA>`.

---

## Pasti, na které jsem narazil

1. **Heredoc v Bash nástroji rozbíjí zpětná lomítka a někdy se celý zblázní.**
   `\n` uvnitř heredocu skončí jako skutečný nový řádek; delší markdown
   heredoc mi dvakrát shodil bash na „unexpected EOF looking for matching `'`".
   Na obsah se escape sekvencemi používej Write, nebo si napiš `.py` soubor
   do scratchpadu a spusť ho. Uvnitř python skriptu se zpětné lomítko dá
   obejít přes `chr(92)`.
2. **Soubory mají CRLF.** Python patche musí normalizovat `\r\n` → `\n`,
   porovnat, a na konci vrátit zpátky.
3. **Klávesnice se do guesta nedostane.** `SendKeys` 86Box přes SDL ignoruje.
   Všechno musí jít z `AUTOEXEC.BAT` nebo zápisem do obrazu disku přes
   `fat16.py`, a **86Box musí být zavřený**, než se do obrazu zapisuje.
4. **86Box se pod MSVC nepřeloží** — používá GCC rozšíření. Jde to jen přes
   MSYS2/MinGW, závislosti jsou hotové balíčky.
5. **Zdroj pravdy pro disassembly je `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/`** —
   ty binárky skutečně běžely při měření. Existuje pět různých verzí
   `SBAWE32.DRV` a ta měřená note-on logiku vůbec neobsahuje (je v `SBAWE.VXD`).
6. **Nedávej do párovacího klíče to, co chceš porovnávat.** Původní párování
   podle `(IP, PSST, CSL)` zaručovalo shodu v `PSST` a `CSL` a schovalo celý
   jeden nástroj (bicí). Teď je klíč `(IP, adresa vzorku ± 256 slov)`.
7. **Než něco označíš za rozdíl v ovladači, ověř, že měříš totéž.** Dva
   z původních pěti „rozdílů" byly artefakty měření a jeden (`IFATN` +10)
   nebyl vzorec, ale nastavení hlasitosti hudby ve hře.
8. **Porovnávej výsledný stav registrů, ne pořadí zápisů** — SBAWE32.DRV
   některé hodnoty dodatečně přepisuje.

---

## Co nedělat

- Neměň `../AWE32EmuData/ref86box/upstream/` — je to doslovná kopie 86Boxu, kontrola
  `python ../AWE32EmuData/ref86box/verify_upstream.py --online`.
- Neměň `C:\prenos\86BoxWipeout*` — pracujeme s kopiemi v `../AWE32EmuData/ref86box/vm`
  a `../AWE32EmuData/ref86box/vmdos`.
- Tabulky decay/release **nebrat z 86Boxu** — má je z linuxového ovladače,
  my máme ty z Creative, bajt po bajtu shodné ve třech generacích.

---

## Úkol pro tuhle session

Registrová vrstva je hotová, takže na řadě je **jádro čipu**: filtr
(TPT vs Moog ladder), resampler a to, proč nad 12,8 kHz chybí 30 dB.
Referenční implementace je `../AWE32EmuData/ref86box/upstream/snd_emu8k.c`.

Po každé změně:

```bash
python ../AWE32EmuData/tests/regress.py
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/mc_intro.trace ../AWE32EmuData/tests/out/dos_mdi.trace --pair --dframes 0:6000000
```

Obě varianty musí zůstat na úplné shodě.

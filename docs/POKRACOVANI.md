# Predavaci dokument - stav projektu a jak pokracovat

> **Data nejsou v repozitari.** Vzorky, banky, ROM, nahravky, obrazy
> virtualnich stroju a zdrojak 86Boxu lezi v `C:/prenos/AWE32EmuData`.
> Prikazy nize proto maji prefix `../AWE32EmuData/`. Mapa presunu a
> nastaveni promenne `AWE32EMU_DATA` je v [docs/DATA.md](DATA.md).

> **Pro novou konverzaci pouzij `docs/NOVA_SESSION.md`** - je to hotovy
> prompt se stavem, prikazy a pastmi.

Napsano na konci relace, ve ktere se emulace dostala z "placeholder sinusovka"
na "hraje realne skladby z ROM a bank". Tenhle soubor ma stacit k tomu, aby
se dalo pokracovat bez znalosti predchozi konverzace.

---

## 1. Co projekt dela

C++ emulace cipu **EMU8000** (Sound Blaster AWE32). Dve zamyslena pouziti:

1. prehravani `.mid` / `.xmi` s originalnimi bankami z DOS her
2. primé napojeni reversed DOS hry (i pres AIL/Miles), proto ma jadro
   i portovou uroven (`PortOut16`/`PortIn16` na skutecnych I/O adresach)

Build: `AWE32Emu.sln`, Visual Studio 2022, x64 Release. Bez zavislosti.

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" AWE32Emu.sln -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

Doporucene spusteni (autenticka GM banka + banka skladby):

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/SAMPLES2/RELAX_BK.MID \
    --rom ../AWE32EmuData/rom/awe32.raw \
    --sbk "../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK" \
    --sbk ../AWE32EmuData/SAMPLES2/RELAX.SBK \
    --wav ../AWE32EmuData/tests/out/relax.wav
```

Prepinace: `--rom`, `--rombank`, `--sbk` (vicekrat, vrstvi se), `--wav`,
`--interp linear|cubic`, `--reverb 0..7`, `--chorus 0..7`, `--rev-room`,
`--rev-damp`, `--rev-return`, `--cho-return`, `--only-ch`, `--debug-voices`,
`--trace <soubor>` (zaznam portovych zapisu pro `ref86box`),
`--master-volume 0..127` (hlavni hlasitost sekvenceru AIL, viz sekce 16.5),
`--driver dos|win95` (varianta ovladace Creative, viz `src/Awe32Driver.h`
a `docs/re-notes/86box_srovnani.md` sekci 12; vychozi je `win95`).

---

## 2. DALSI KROK, na kterem jsme se domluvili

**Nejdriv dostat emulaci 1:1 na 86Box, teprve pak vylepsovat.**

> **Aktualizace:** infrastruktura na to uz existuje - viz `../AWE32EmuData/ref86box/README.md`
> a nalezy v `docs/re-notes/86box_srovnani.md`. Nas prehravac umi prepinacem
> `--trace` vypsat vsechny portove zapisy a `../AWE32EmuData/ref86box/build/emu8k_ref.exe` je
> prehraje pres **nezmeneny** `snd_emu8k.c` z 86Boxu. Referencni kopie nize je
> overene bajt po bajtu shodna s 86Box master
> (`python ../AWE32EmuData/ref86box/verify_upstream.py --online`).
>
> **Krome toho** mame vlastni build 86Boxu se zaznamem portovych zapisu, ve
> kterem bezi skutecne ovladace od Creative. Tim se overuje ovladacova vrstva
> nezavisle na jadru cipu. Inicializace uz sedi **1611 z 1611 zapisu** proti
> `AWEUTIL.COM /S`. Note-on sedi u **vsech registru u obou rodin ovladacu**:
> `SBAWE.VXD` 242/242 not a `SBAWE32.MDI` 255/255 not. Registrova vrstva je
> tim hotova; dalsi na rade je jadro cipu (filtr, resampler - sekce 11.5).
> Note-on rutina je zreverzovana pomoci instrukcniho traceru zabudovaneho
> do naseho 86Boxu (sekce 10) a rozvrzeni bloku parametru vrstvy je popsane
> v sekci 16.2.

Referencni implementace je
`../AWE32EmuData/docs/next docs/extracted/snd_emu8k-86box-referencni-kopie.c` (2389 radku C).
Je to funkcni emulace EMU8000 z 86Boxu a resi presne ty veci, ktere u nas
zustaly jako nahrada. Doporuceny postup:

1. Projit ji funkci po funkci a srovnat s nasim `Emu8000.cpp`.
2. Prevzit hlavne:
   - `emu8k_update()` (radek 1759) - hlavni smycka hlasu, poradi operaci,
     fixed-point aritmetika, zpracovani obalek po krocich
   - `emu8k_work_chorus()` (1555) a `emu8k_work_reverb()` (1684) -
     **86Box ma reverb i chorus skutecne dekodovane** z init poli, coz jsem
     puvodne povazoval za nemozne. Viz `emu8k_outw` kolem radku 985, kde se
     z hodnot jako `0x8474`, `(val & 0xF0) >> 4` cte `out_mix`,
     `allpass[].feedback`, `reflections[].output_gain`.
   - `emu8k_vol_slide()` (1739) - jak cip najizdi na cilovou hlasitost
3. Az bude vystup shodny s 86Boxem, teprve pak ladit proti nahravkam.

**Pozor:** 86Box neni ve vsem lepsi. Pro decay/release pouziva tabulku
z linuxoveho ovladace (`45120, 22614, 15990, 11307, ...`), zatimco my mame
tabulku primo z Creative ovladace (`47513, 23756, 15838, 11878, ...`), ktera
je bajt po bajtu identicka ve trech generacich ovladacu. **Nasi si nechat.**

---

## 3. Kde jsou data

| cesta | obsah |
|---|---|
| `../AWE32EmuData/rom/awe32.raw` | 1 MB wave ROM karty, mapuje se od adresy 0, zvukovy fond od slova **495** |
| `../AWE32EmuData/rom/1mgm.sf2` | tataz data jako SF2 (starsi nahrada, uz nepotreba) |
| `../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK` | **autenticka GM banka od E-mu**, SF1.0, bez `smpl` (vse v ROM) |
| `../AWE32EmuData/sbk/BULLFROG.SBK` | banka hry (Magic Carpet 2), SF1.0, `irom=1MGM` |
| `../AWE32EmuData/sbk/SBAWE32.MDI` | AIL/Miles driver ze hry |
| `../AWE32EmuData/SoundBlaster AWE32/SBAWE32/` | DOS ovladace + `AWEUTIL.COM.asm` (IDA disassembly) |
| `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/` | **ovladace, ktere skutecne bezely pri mereni** - jediny zdroj pravdy pro disassembly, viz README tam |
| `../AWE32EmuData/cdrom/2/WIN95/DRIVERS/` | Win95 ovladace vytazene z instalacniho CD |
| `midi/` | 6 skladeb Bullfrog ve 4 variantach (`_f` FM, `_g` GM, `_r` Roland, **`_w` AWE32**) |
| `ogg/` | referencni nahravky k `midi/` (ztratove) |
| `../AWE32EmuData/SAMPLES2/` | **demo CD Creative: bezztratove FLAC + odpovidajici MID** - nejlepsi reference |
| `../AWE32EmuData/SAMPLES/MIDI/` | 121 cistych GM souboru od Creative |
| `../AWE32EmuData/docs/next docs/` | 86Box zdrojak, oficialni SDK, Programmer's Guide jako text |

Rozbaleni ISO obrazu: `python ../AWE32EmuData/tests/iso_list.py <bin> --extract <dir>`.

---

## 4. Co je overene 1:1 (a odkud)

Tohle **nemenit bez noveho dukazu**. Vsechno je zdokumentovane
v `docs/re-notes/`.

| vec | zdroj potvrzeni |
|---|---|
| registrova mapa, kodovani `sel`, porty | AWEUTIL.COM + SBAWE32.DRV + SBAWE32.MDI + ALSA `emu8000_reg.h` + Programmer's Guide |
| `pointer = (reg << 5) \| voice`, porty 0x620/0xA20/0xE20, pointer 0xE22 | tri ovladace nezavisle |
| **cela inicializacni sekvence, 1611 zapisu** | **skutecny AWEUTIL.COM /S v 86Boxu, shoda 1611/1611** |
| init pole INIT1..INIT4 (4 x 128 hodnot) | tamtez; ALSA se v 16 hodnotach lisi, viz 86box_srovnani.md 5.3 |
| ID registr Data3/7 = `0x000C` (86Box ma `0x001C` spatne) | AWEUTIL `sub_12B40`: `cmp ax, 0Ch` bez maskovani |
| poradi zapisu pri note-on, `VTFT=0x0000FFFF`, DCYSUSV posledni | SBAWE32.DRV + SBAWE32.MDI, shodne |
| vypocet utlumu + 3 prevodni tabulky | **tri generace ovladacu, bajt po bajtu identicke** - viz `src/Awe32Curves.h` |
| tabulky obalek: `attack = 11878/k(r-1)`, `decay = 47513/k(r-1)` | vzorec reprodukuje obe tabulky s 0 odchylkami |
| `hold = 127 - ms/92` | SBAWE32.DRV `idiv -92; add 0x7F` |
| velocity -> cutoff filtru | SBAWE32.DRV 0x021E |
| keynum -> obalka (hold/decay k note 60) | SBAWE32.DRV 0x0278 |
| one-shot smycka na `konec+4 .. konec+8` | SBAWE32.DRV 0x02E4 |
| utlum filtru podle Q (`kFilterAtten`) | AWE32 FAQ NRPN tabulka, potvrzeno 86Boxem |
| LFO je **trojuhelnikovy**, frekvence `0.01 + v*0.042` Hz | 86Box `lfotable`, `lfofreqtospeed` |
| cutoff filtru: exponencialne 125 Hz .. 8 kHz, 42.5 kroku/oktavu | 86Box zvolil totez (42.66); linearni NRPN varianta je u nich zamitnuta |
| ~~velocity -> cutoff, skala cutoffu~~ | **ZPOCHYBNENO**: proti SBAWE32.DRV zapisujeme do IFATN presne polovicni cutoff u vsech 242 not, viz 86box_srovnani.md 6.3 |
| 8 reverb presetu (vychozi 4 = Hall 2), 8 chorus presetu (vychozi 2 = Chorus 3) | SBAWE32.DRV 0x60FA a 0x612D |
| parametry chorusu (feedback/delay/depth/lfo_freq) | SBAWE32.DRV ds:0x19A2, format overeny proti HWCF5=0x83 v AWEUTILu |

---

## 5. Co je NAHRADA, ne emulace

Tady se da nejvic zlepsit, a prave tady pomuze 86Box:

1. **Reverb** - nase je Freeverb-like (8 hrebenu + 4 allpass). 86Box ma
   skutecnou strukturu cipu dekodovanou z init poli.
2. **Chorus** - nase je modulovana zpozdovaci linka. Parametry uz mame
   z ovladace, ale topologii ne.
3. **Filtr** - pouzivame TPT state-variable. 86Box ma tri varianty
   (`FILTER_INITIAL` = Chamberlin s `coef0 = sin(2*pi*fc/44100)` a
   `coef2 = 1/(0.7071+q)`, `FILTER_MOOG`, `FILTER_CONSTANT`) a vychozi je
   **`FILTER_MOOG`** - zbyle dve jsou v kodu zabalene v `#if 0`. Stejne tak
   je vychozi resampler **`RESAMPLER_CUBIC`**, ne linearni.
4. **Interpolace** - mame linearni i kubickou (Catmull-Rom, prepsano
   z 86Boxu). Realny cip pouzival patentovanou 3bodovou interpolaci,
   takze ani jedno neni presne.
5. **Scitani 32 hlasu** - scitame ve float bez modelu fixed-point saturace.
6. **Kradeni hlasu** - vlastni heuristika, ne algoritmus ovladace.
7. **Globalni urovne `/R:` a `/C:`** z AWEUTILu neimplementovane.
   Pozn.: init pole INIT1..INIT4 uz se **posilaji** (`Awe32InitArrays.h`),
   protoze z nich 86Box dekoduje reverb i chorus. Nase jadro z nich zatim
   necte nic - to je prave bod 1 a 2 vyse.
8. Oznacene `[?]` v kodu: skala `initialFilterQ` (0..127 -> 0..15),
   vyznam SF1 generatoru 55, pan zakon (pouzivame constant-power).

---

## 6. Nastroje v `../AWE32EmuData/tests/`

| skript | k cemu |
|---|---|
| `regress.py` | **spustit po kazde zmene** - ladeni, delka, zadne nahradni hlasy, neklipuje |
| `bands_abs.py` | absolutni energie v pasmech + odchylka po korekci na celkovou uroven |
| `bands.py` | podily energie (pozor, zavadejici - prebytek stredu srazi podil basu) |
| `sweep_params.py` | vyrenderuje mnoho variant a seradi je podle skore |
| `sweep.py` | chromaticky prubeh jednim nastrojem - ladeni a uroven po celem rozsahu |
| `align.py` | casovy posun mezi renderem a referenci |
| `compare.py` | obalka hlasitosti, spektralni teziste |
| `dump_sbk.py`, `query_preset.py` | struktura banky, konkretni preset |
| `rom_pitch.py` | zmeri skutecnou vysku vzorku primo v ROM |
| `iso_list.py` | vypis/rozbaleni CD obrazu |
| `pair_check.py` | parovani midi/ogg podle delek |
| `cmp86box.py` | srovnani naseho renderu s renderem pres kod 86Boxu |
| `trace_diff.py` | srovnani dvou zaznamu portovych zapisu (registry, note-on) |
| `fat16.py` | cteni a zapis do obrazu disku guesta (FAT16) |
| `notes_diff.py` | **srovnani registru pri note-on proti skutecnemu ovladaci** |
| `ne_disasm.py` | rozebrani 16bit NE ovladace (segmenty, vstupy funkci, disassembly) |
| `le_disasm.py` | rozebrani 32bit VxD (objekty, mapa stranek, xref, disassembly) |
| `insn_view.py` | prohlizec instrukcni stopy z 86Boxu s disassembly VXD |

Potreba: `pip install numpy soundfile pypdf capstone`.

---

## 7. Aktualni stav mereni

`../AWE32EmuData/tests/regress.py` prochazi (5/5). Ladeni GM klaviru: **-1.6 centu**.
Delka RELAXu 219.9 s proti 218.8 s reference.

Odchylka spektra na RELAXu po korekci na celkovou uroven (cil je 0 dB):

| pasmo | linear | cubic |
|---|---|---|
| 0-100 Hz | -5.8 | -5.8 |
| 100-200 | 0.0 | 0.0 |
| 200-400 | -1.5 | -1.5 |
| 400-800 | +2.8 | +2.9 |
| 800-1600 | -0.5 | -0.2 |
| 1600-3200 | -2.8 | -2.2 |
| 3200-6400 | +0.8 | +2.3 |
| 6400-12800 | +1.0 | +2.6 |
| 12800+ | +7.7 | +6.8 |

Energeticky vazene skore (`sweep_params.py --quick`): **linear 3.017,
cubic 3.343**. Zatim tedy vyhrava linearni, ale rozdil je maly a rozhodnout
by se to melo az po srovnani s 86Boxem.

**Systematicky pres vsechny tri testovane skladby:** chybi nam bas
(-4 az -6 dB) a prebyva ultravysoke pasmo. Chromaticky prubeh ale ukazal,
ze uroven nasich nizkych not je plocha (rozdil 0.4 dB mezi nejnizsimi
notami a stredem), takze ten basovy deficit **neni chyba naseho
syntetizeru** - je to vlastnost referencnich nahravek.

---

## 8. Nevyresene

- **Bullfrog `002_C2GAME3`, kanal 7**: dve noty dlouhe celych 240 s (pad,
  Choir Aahs). U nas hraje noty 36 a 37 (65.4 a 69.3 Hz) presne podle
  souboru, ale reference ma misto toho spicky na 98.2 a 99.6 Hz a je tam
  o 19 dB hlasitejsi. Rozdil +6 az +7 pultonu. Vsechny ctyri varianty
  (`_f/_g/_r/_w`) maji v tom miste identicke noty, takze to neni vyberem
  varianty. Nevysvetleno.
- Pasmo 400-800 Hz je 3x nad referenci; zmereno po kanalech, dela to
  z drtive vetsiny **kanal 4** (Synth Brass1, vzorek `sawstackwavems`).
- Jak se prepina "danger" vrstva u Bullfrogu (stopy `*_danger.ogg` maji
  stejnou delku jako zakladni). V XMI je CC116/117/119, coz jsou
  Miles/AIL ridici controllery.

---

## 9. Na co si dat pozor (nastrahy, na ktere jsem narazil)

- **Heredoc v Bash nastroji rozbiji zpetna lomitka.** `\n` uvnitr
  python heredocu skonci jako skutecny novy radek v C++ retezci. Na upravy
  souboru s escape sekvencemi pouzivat Edit/Write, ne `python - <<'EOF'`.
- **Nemerit "nejsilnejsi spicku".** Kdyz se zmeni barva zvuku, skoci na
  harmonickou a hlasi posun o oktavu. Merit amplitudu na konkretni
  ocekavane frekvenci (viz `regress.py`).
- **Nemerit podily energie v pasmech.** Prebytek ve stredu srazi podil
  basu a vypada to jako chybejici bas. Pouzivat `bands_abs.py`.
- **XMI ma dve ruzna kodovani**: delta-time je soucet bajtu < 0x80, ale
  **delka noty je standardni SMF VLQ**. A tempo meta se v XMI ignoruje,
  bezi na pevnych 120 Hz.
- **`cwd` se v Bash nastroji resetuje** mezi volanimi - pouzivat absolutni
  cesty nebo `cd` v kazdem prikazu.
- Render 220 s hudby trva ~20 s. Pri sweepech s tim pocitat.

---

## 10. Souvisejici dokumentace

- `docs/CIL.md` - cil a prubeh mereni
- `../AWE32EmuData/ref86box/README.md` - referencni renderer nad nezmenenym kodem 86Boxu
- `docs/re-notes/86box_srovnani.md` - nalezy ze srovnani s 86Boxem
- `docs/re-notes/emu8000_register_map.md` - registrova mapa, co je z ceho
- `docs/re-notes/driver_note_on.md` - co ovladac dela navic pri note-on
- `docs/re-notes/sbawe32_mdi.md` - AIL driver
- `docs/re-notes/soundfont1_sbk.md` - SF1.0 a mapovani generatoru
- `docs/re-notes/rom_vs_sf2.md` - vztah `.raw` a `.sf2`
- `../AWE32EmuData/docs/next docs/` - 86Box zdrojak, SDK, Programmer's Guide

I 86box verze má své chyby, nejprve prověř, zda to není zbytkem kódu - zkus si to spustit přímo v emulátoru 86box, zda to hraje stejně.

pokud ano, nezbyde, než nasbírat co nejvíce vzorků midi(awe32)/wav(awe32) a zkoušet. Zkus něco postahovat z youtube a pak zkoušet klíčové kousky, dokud se nepodaří přijít na to jak to funguje.

Část s ovladačem můžeš vzít reverzním engeneerstvím, takže nám zbyde doladit implementaci emu8000.

To zkus ještě prověřit ze všech zdrojů, jestli ještě něco nenajdeš a pak to se to bude muset ladit systémem pokus-omyl.
Tady lze získat nahrávky skladeb pro porovnání:https://www.youtube.com/watch?v=OJVtAjJU4r8

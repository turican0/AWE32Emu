# Prompt pro novou session

Zkopíruj celý tenhle soubor jako první zprávu nové konverzace.

> **Data nejsou v repozitáři.** Vzorky, banky, ROM, nahrávky, obrazy
> virtuálních strojů, zdroják 86Boxu **i všechny naše nástroje a skripty**
> leží v `C:\prenos\AWE32EmuData`. Příkazy níže proto mají prefix
> `../AWE32EmuData/`. Mapa přesunu je v [docs/DATA.md](DATA.md).

---

## Co je projekt

`C:\prenos\NeuralFin2` je pracovní adresář, ale **projekt je
`C:\prenos\AWE32Emu`** — C++ emulace čipu **EMU8000** (Sound Blaster AWE32).

Mluv česky. Uživatel je Tomáš, chce měření a důkazy, ne dojmy.

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" AWE32Emu.sln -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

**Nejdřív si přečti `docs/POKRACOVANI.md`, `docs/re-notes/86box_srovnani.md`
(16 sekcí, hlavní zdroj pravdy) a `../AWE32EmuData/ref86box/README.md`
(6 částí, provoz měření).**

---

## Stav: registry hotové, zvuk ne

| co | stav |
|---|---|
| inicializace vs `AWEUTIL.COM /S` | 1611 z 1611 zápisů |
| note-on vs `SBAWE.VXD` (`--driver win95`) | všech 24 registrů, 242/242 not |
| note-on vs `SBAWE32.MDI` (`--driver dos`) | všech 24 registrů, 255/255 not |
| `regress.py` | prochází |

Registrová vrstva při note-onu je tedy hotová u obou rodin ovladačů. **Zvuk
ale pořád nesedí** a to je celé zadání.

---

## Nejdůležitější metoda: rozklad viny

Tohle je nejcennější věc z poslední session. Když má člověk tutéž skladbu ve
**čtyřech** podobách, jde rozdělit, čí je chyba — každé dvě sousední se liší
jen v jedné věci:

| podoba | ovladač | čip |
|---|---|---|
| náš render | náš | náš |
| náš render přes `emu8k_ref.exe` | náš | 86Box |
| záznam z VM (`-Wav`) | **skutečný** | 86Box |
| nahrávka z demo CD | skutečný | **skutečný hardware** |

Naměřeno na Georgii:

| srovnání | drží se pevně | korelace obálky | největší odchylka |
|---|---|---|---|
| náš čip vs 86Box | ovladač = náš | 0,975 | +4,3 dB (0–100 Hz) |
| náš ovladač vs skutečný | čip = 86Box | 0,876 | +3,8 dB (12,8 kHz+) |
| 86Box vs hardware | ovladač = skutečný | **0,657** | +5,1 dB (400–800 Hz) |

**Z toho plyne, že 86Box není pro zvuk dobrá reference** — proti skutečné
kartě má v pásmu 400–1600 Hz skoro +5 dB. Na registry ano, na zvuk ne.

---

## Tři konkrétní stopy, v pořadí

**1. Obálky a release.** Náš ovladač proti skutečnému na stejném čipu má
korelaci obálky 0,876, přestože spektrum do 6,4 kHz sedí na ±0,5 dB. Rozdíl
je tedy v tom, *co posíláme do registrů v čase*, ne v tom, co posíláme.
Note-off a release jsou oblast, kterou registrové srovnání nikdy nepokrylo —
stopy z ovladače obsahovaly jen note-ony.

Nově to jde změřit: `georgia_win95.trace` ze skutečného ovladače obsahuje
**i note-offy a průběžné zápisy**. Odtud se dá poprvé zjistit, jak skutečný
ovladač uvolňuje tón.

Souvisí s tím dvě staré pozorování: náš render intra Magic Carpet 2 měl
**197 sekund ocasu** po poslední notě, a v dvoukanálovém okně C2SETUPu byla
naše RMS přes 26 s konstantní, zatímco reference kolísala. Držíme drón.

**2. Pásmo 400–1600 Hz.** 86Box tam má +5 dB nad hardwarem a my +2,2 dB nad
86Boxem — obojí týmž směrem, takže nejspíš společná příčina. Podezřelý je
útlum filtru podle Q (`filt_att` v 86Boxu, u nás `kFilterAtten`).

**3. Chybějící výšky.** Nad 6,4 kHz jsme proti skutečné kartě −17 dB
(měřeno na sólovém klavíru). Mapování mezního kmitočtu už to vysvětlit
nemůže — je odvozené z primárního zdroje, viz níže.

---

## Převodní vrstva SF1 → registry

Klíčový nález: **`SYNTHGM.SBK` (SF1) a `SYNTHGM.SF2` z DOSového SDK popisují
tutéž banku v různých jednotkách.** Z té dvojice jde převod odečíst přímo
místo odhadovat. K tomu Programmer's Guide (`EMU8KPGM.PDF`, vytažený text
v `docs/next docs/extracted/`) dává jednotky registrů.

| generátor | vztah | stav |
|---|---|---|
| `sustainVolEnv` | SF1 je v celých dB, registr v 0,75 dB → **×4/3**, ořez na 127 | **opraveno** |
| `initialFilterFc` | **59 centů na krok SF1** od 101,81 Hz; `127` = 14400 centů = filtr dokořán | **opraveno** |
| `pan` | `(SF1 − 64) × 1000/128` | opraveno |
| `initialAttenuation` | 0,75 dB na jednotku, 127 = bez útlumu | **ověřit** |
| `chorusEffectsSend` | SF2 ‰ = SF1 × 1000/255 | ověřit |
| `attackVolEnv`, `holdVolEnv`, `decayVolEnv`, `releaseVolEnv` | nelineární | **neopraveno** |

Poslední řádek je zároveň stopa číslo 1. Nástroj, který zbývá napsat: spárovat
zóny obou SYNTHGM podle vzorku a rozsahu kláves (mají 743 vs 753 zón, podle
pořadí to nejde) a proložit z toho převod.

**Dva známé rozpory** mezi cestami SF1 a SF2 na téže bance:
- adresa vzorku se liší o **494 slov** (základ zvukového fondu v ROM) — SF1
  sedí proti ovladači, chybu má SF2
- útlum 48 vs 66, tedy faktor 2 na jednotkách

---

## Vzorky

**20 ověřených dvojic** nahrávka/MIDI. Nejlepší laboratoře:

| dvojice | proč |
|---|---|
| `SAMPLES/MIDI/PIANOIMP/BBDRAG.MID` × `samples3/tracks/11_GMARAG.wav` | **sólový klavír**, jen program 0, 2 kanály; naše hlasitost sedí na 0,36 dB |
| `SAMPLES2/GEORG_BK.MID` × `SAMPLES2/5 - Georgia On My Mind.flac` | 8 kanálů, existuje i záznam z 86Boxu → rozklad viny |
| `midi/003_C2SETUP_w.xmi` × `ogg/003_C2SETUP.ogg` | jediná MC2 nahrávka, kde `_w` vyhrává jednoznačně |

**Pozor na past:** `ogg/004_C2INTRO.ogg` a `ogg/005_C2CUTS.ogg` jsou
**špatně pojmenované** — `005_C2CUTS` je ve skutečnosti znovu `003_C2SETUP_w`
a `004_C2INTRO` nesedí na nic. Ověřuj dvojice, nespoléhej na jména.

Nástroje na to: `markers.py` (shodné úseky přes otisky, zvládne útržek
i opakování), `match_tracks.py` (korelace celku), `pair_finder.py` (obojí
dohromady + stahování). Kalibrace: správná dvojice 0,4–0,7, šum sahá do 0,25.

---

## Měření: nejužitečnější příkazy

Render s trasou a přehrání přes 86Boxí čip:

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/SAMPLES2/GEORG_BK.MID --rom ../AWE32EmuData/rom/awe32.raw --sbk "../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK" --wav ../AWE32EmuData/tests/out/g.wav --trace ../AWE32EmuData/tests/out/g.trace --driver win95
```

```bash
../AWE32EmuData/ref86box/build/emu8k_ref.exe --rom ../AWE32EmuData/rom/awe32.raw --trace ../AWE32EmuData/tests/out/g.trace --dram ../AWE32EmuData/tests/out/g.trace.dram.raw --ram 8192 --wav ../AWE32EmuData/tests/out/g_86chip.wav
```

Záznam ze skutečného ovladače ve VM, včetně zvuku:

```bash
powershell -File ../AWE32EmuData/ref86box/run_trace.ps1 -Trace ../AWE32EmuData/tests/out/x.trace -Wav ../AWE32EmuData/tests/out/x.wav -Mode win95 -Seconds 380
```

Srovnání:

```bash
python ../AWE32EmuData/tests/cmp_real.py nas.wav referencni.wav --segments 12
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

```bash
python ../AWE32EmuData/tests/regress.py
```

Ladicí přepínače přehrávače: `--driver dos|win95`, `--master-volume N`,
`--filter-top <Hz>`, `--filter-poles 1|2|4`, `--sbk soubor@N` (MIDI banka).

---

## Provoz virtuálních strojů — pět pastí

Všechny stály hodiny. Když guest „nic nedělá", jdi popořadě. Podrobně
v `ref86box/README.md` část 5.

1. **`BootGUI` v `MSDOS.SYS`** — `0` bootuje do DOSu a MPLAYER se nespustí.
2. **CMOS se rozbije tvrdým ukončením** — `nvr/thor.nvr` 256 B místo 128 B;
   funkční je v `C:\prenos\86BoxWipeout2\nvr\`.
3. **Namountované CD spouští svůj instalátor** (`AUTORUN.INF`) a sebere fokus.
4. **Konvenční paměť** — bez `HIMEM`/`EMM386`/`DOS=HIGH,UMB` hlásí MPLAYER
   „Insufficient memory"; poznáš to tak, že malá skladba projde a větší ne.
5. **`run=` musí ukazovat na dávku** — přímé volání MPLAYERu nefunguje.

---

## Co je slepá ulička

**DOSové přehrávání MIDI přes skutečný ovladač.** Creative žádný DOSový
přehrávač MIDI nedodával (prošly se čtyři obrazy CD a dva archivy).
`AWEUTIL /EM:GM` v 86Boxu MIDI z portu 330h nepřekládá — DOSMid v0.9.8 hraje
prokazatelně správně, ale do EMU8000 nejde nic než inicializace. Nejspíš
proto, že 86Box emuluje MPU-401 jako skutečný hardwarový port, kdežto AWEUTIL
si ho musí zabrat sám. Podrobně v `ref86box/README.md` část 6.

Zbývá nevyzkoušené: DOSMid v režimu `/awe` píše do EMU8000 přímo — dalo by to
zvuk z DOSu hned, ale měřila by se tím syntéza DOSMidu, ne ovladač Creative.
Na srovnávání čipů použitelné, na srovnávání ovladačů ne.

---

## Pasti v práci samotné

1. **Heredoc v Bash nástroji rozbíjí zpětná lomítka.** `\n` skončí jako
   skutečný nový řádek, `\E` v cestě `\EMM386` se sežere celé. Na obsah se
   escape sekvencemi používej Write, nebo `.py` soubor ve scratchpadu; uvnitř
   Pythonu obcházej přes `chr(92)`.
2. **Soubory mají CRLF** — patche musí normalizovat a na konci vrátit zpátky.
3. **Nedávej do párovacího klíče to, co chceš porovnávat.** Párování podle
   `(IP, PSST, CSL)` zaručovalo shodu v `PSST` a `CSL` a schovalo celý nástroj.
4. **Ověř, že měříš totéž.** Dva z pěti „rozdílů" u rodiny `dos` byly artefakty
   měření a jeden nebyl vzorec, ale nastavení hlasitosti hudby ve hře.
5. **Neopravuj podle jednoho měření.** Hypotéza s π u mezního kmitočtu vypadala
   přesvědčivě a byla špatně; zabil ji až primární zdroj.
6. **Zdroj pravdy pro disassembly** je `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/`
   — existuje pět verzí `SBAWE32.DRV` a jen ta měřená je ta pravá.

---

## Úkol

Stopa číslo 1: **obálky a release**. Z `georgia_win95.trace` změřit, jak
skutečný ovladač uvolňuje tón, a srovnat s tím, co děláme my. Souvisí s tím
dokončení převodů `attackVolEnv`/`holdVolEnv`/`decayVolEnv`/`releaseVolEnv`
z dvojice SYNTHGM.

Po každé změně:

```bash
python ../AWE32EmuData/tests/regress.py
```

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

Obě rodiny ovladačů musí zůstat na úplné shodě registrů.

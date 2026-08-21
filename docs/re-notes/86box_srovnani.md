# Srovnani s 86Boxem - stav a nalezy

> **Data nejsou v repozitari.** Vzorky, banky, ROM, nahravky, obrazy
> virtualnich stroju a zdrojak 86Boxu lezi v `C:/prenos/AWE32EmuData`.
> Prikazy nize proto maji prefix `../AWE32EmuData/`. Mapa presunu a
> nastaveni promenne `AWE32EMU_DATA` je v [docs/DATA.md](DATA.md).

> **Nejdulezitejsi vysledky** (obojí merene proti skutecnym ovladacum Creative
> bezicim pod nasim buildem 86Boxu; jak, viz `../AWE32EmuData/ref86box/README.md` cast 2):
>
> - **Inicializace: 1611 z 1611 zapisu shodne** s `AWEUTIL.COM /S`. Sekce 5.
> - **Note-on: VSECHNY registry sedi 242/242** ve variante `--driver win95`.
>   Sekce 11 a 12.
> - **Rodina `--driver dos` proti `SBAWE32.MDI`** (Magic Carpet 2 v oddelenem
>   DOSovem stroji): **VSECHNY registry sedi 255/255**. Sekce 14 az 16.
> - **Blok parametru vrstvy je prime pole generatoru SoundFontu** indexovane
>   `generator*2` - a oba ovladace maji v sobe tabulku vychozich hodnot
>   generatoru (MDI 0x16AD, VXD 0x6D60). Sekce 16.2 a 16.3.
> - **Instrukcni tracer** v nasem 86Boxu - stav CPU a pameti u kazdeho
>   pristupu na porty, volitelne i kazda instrukce. Sekce 10.
> - **Note-on rutina je zreverzovana** (v `SBAWE.VXD`, ne v `.DRV`) - mapa
>   registru, zapisovace i vypocty. Sekce 9.
> - **Vyreseno, proc nam chybely vysoke kmitocty:** `initialFilterFc` je
>   v SF1 sedmibitovy a ovladac ho zdvojnasobuje. Po oprave se rozptyl mezi
>   pasmy do 6,4 kHz zmensil z 13 dB na 0,8 dB. Sekce 9.6 a 9.8.
> - **Vyreseno, proc jsme byli hlasiti a klipovali:** kdyz se banka odkazuje
>   na ROM "1MGM", ovladac pricita k utlumu 16 jednotek = **6 dB**. Sekce 10.2.

Podklad: `../AWE32EmuData/ref86box/upstream/snd_emu8k.c`, overene jako **bajt po bajtu shodne
s 86Box master** (`python ../AWE32EmuData/ref86box/verify_upstream.py --online`).
Infrastruktura na srovnani je popsana v `../AWE32EmuData/ref86box/README.md`.

## 1. Opravy provedene na zaklade srovnani

### 1.1 32bitovy zapis do DCYSUSV nulovaly LFO1VAL

`Emu8000Core::Write()` povazoval **vsechny** registry na Data1 za 32bitove
a horni polovinu posilal na `A22h`. Jenze na `A22h` lezi u registru 2..7
uplne jiny registr, ne horni pulka:

| reg | `A20h` (Data1) | `A22h` (Data2) |
|---|---|---|
| 0 | CCCA | CCCA high |
| 1 | HWCF4..7, SMALR/SMARR/SMALW/SMLD | HWCF high, SMRD, WC |
| 2 | INIT1 | INIT2 |
| 3 | INIT3 | INIT4 |
| 4 | ENVVOL | ATKHLDV |
| 5 | DCYSUSV | LFO1VAL |
| 6 | ENVVAL | ATKHLD |
| 7 | DCYSUS | LFO2VAL |

Potvrzeno v 86Boxu (`snd_emu8k.c`, `case 0xA00` vs `case 0xA02`) i v ALSA
(`emu8000_reg.h`, `EMU8000_DATA1` vs `EMU8000_DATA2`).

Dusledek: `Synth::StartVoice()` zapisuje LFO1VAL a hned potom DCYSUSV, takze
32bitovy zapis DCYSUSV vzdy prepsal LFO1VAL nulou - **zpozdeni LFO1 tim bylo
u kazde noty vynulovane**. Opraveno funkci `IsReg32()`: 32bitove jsou jen
Data0 (vsech osm) a na Data1 registry 0 (CCCA) a 1 (HWCF).

### 1.2 Portova uroven neposilala pointer ani horni slovo

`WriteReg16()` nastavovalo `m_pointer` primo a `WriteReg32()` zapisovalo
horni slovo primo do registroveho pole. Vysledkem bylo, ze portova cesta
(kterou trida deklaruje jako "presne to, co dela ovladac") vynechavala dva
ze tri skutecnych zapisu. Ted jde vsechno pres `PortOut16()` v poradi
pointer -> low -> high, jak to dela `sub_10F46` v AWEUTILu.

Na zvuk to vliv nema (chovani registroveho pole je stejne), ale bez toho
neslo z portove urovne porizovat pouzitelnou stopu.

### 1.3 Init pole INIT1..INIT4 se neposilala

Doposud se INIT1..INIT4 vedome preskakovaly ("jsou to DSP koeficienty, do
softwarove emulace se neprenaseji"). 86Box z nich ale dekoduje **cely**
reverb a chorus, takze bez nich neslo srovnani udelat vubec.

Pole jsou ted v `AWE32Emu/src/Awe32InitArrays.h` (4 x 128 hodnot) a
`PowerOnInit()` je posila v poradi podle ALSA `init_arrays()`: init1, init2,
init3, HWCF4/5/6, init4. Nase jadro z nich stale nic necte - jen se ulozi do
registroveho pole - takze na nas vystup to nema vliv.

Puvodne byla prevzata z ALSA (`sound/isa/sb/emu8000.c`); v sekci 5.3 jsou
opravene proti skutecnemu AWEUTILu, protoze ALSA se v 16 hodnotach lisi.

## 2. Opravy v dokumentaci

`docs/POKRACOVANI.md` sekce 5 uvadela, ze vychozi filtr 86Boxu je
`FILTER_INITIAL` + `FILTER_CONSTANT`. Neni. V `snd_emu8k.c` jsou vychozi:

```c
#define FILTER_MOOG
#define RESAMPLER_CUBIC
```

`FILTER_INITIAL`, `FILTER_CONSTANT` a `RESAMPLER_LINEAR` jsou v kodu, ale
zabalene v `#if 0`. Pro srovnani plati, ze 86Box jede **Moog ladder filtr
a kubicky resampler**.

Tataz sekce oznacovala referencni kopii za "funkcni emulaci EMU8000
z 86Boxu"; overeno, ze je opravdu identicka s masterem, ale **neodpovida
zadnemu vydani** - v `v5.3` ani `v6.0` jeste nektere zmeny nejsou.

## 3. Aktualni odchylka

RELAX (`../AWE32EmuData/SAMPLES2/RELAX_BK.MID`, GM banka + banka skladby), 219,9 s,
oba renderu z tehoz zaznamu portovych zapisu:

| velicina | hodnota |
|---|---|
| RMS nas | -19,66 dB |
| RMS 86Box | -21,65 dB |
| RMS rozdilu | -20,74 dB |
| pomer chyby k signalu | -1,08 dB |
| korelace obalky hlasitosti | 0,955 |

Odchylka po pasmech (kladne = my mame vic):

| pasmo Hz | rozdil dB |
|---|---|
| 0-100 | +2,5 |
| 100-200 | +2,1 |
| 200-400 | +1,6 |
| 400-800 | +0,9 |
| 800-1600 | -0,3 |
| 1600-3200 | +2,8 |
| 3200-6400 | +2,8 |
| 6400-12800 | +1,1 |
| 12800+ | +1,4 |

Obe emulace tedy hraji tutez skladbu se stejnym prubehem, ale nejsme
zdaleka 1:1. Cast rozdilu je znama a ocekavana: 86Box na vystup neaplikuje
mixer SB16 a nase efektove navraty jsou nastavene empiricky.

### 3.1 MINUET (jen GM banka, vzorky vyhradne z ROM) - vyrazny nalez

`../AWE32EmuData/SAMPLES/MIDI/BACH/MINUET.MID`, 48,2 s, jen `SYNTHGM.SBK`, tedy zadna DRAM
a zadny vliv nahravani banky:

| velicina | hodnota |
|---|---|
| korelace obalky hlasitosti | **0,990** |
| RMS nas | -22,56 dB |
| RMS 86Box | -26,42 dB |

| pasmo Hz | rozdil dB (kladne = my vic) |
|---|---|
| 0-100 | +3,1 |
| 100-200 | +3,3 |
| 200-400 | +3,9 |
| 400-800 | +4,6 |
| 800-1600 | -1,1 |
| 1600-3200 | **-7,4** |
| 3200-6400 | **-9,2** |
| 6400-12800 | **-25,0** |
| 12800+ | **-38,4** |

Casovy prubeh sedi temer dokonale (0,99), ale barva zvuku ne. Nad 6 kHz je
nas vystup prakticky ticho, zatimco 86Box tam ma poradnou energii. Pod
800 Hz je to obracene.

To vypada na **filtr**, ne na obalky ani na ladeni.

> **VYRESENO, viz 9.6 a 9.8.** Nebyla to topologie filtru, ale mapovani:
> `initialFilterFc` je v SF1 sedmibitovy a ovladac ho pred zapisem do `IFATN`
> zdvojnasobuje. Po oprave sedi cutoff 242/242 a odchylka do 6,4 kHz je
> rovnomerna (+3,0 az +3,8 dB), coz uz je jen rozdil celkove urovne.

Zajimave je, ze proti *referencnim nahravkam* nam podle
`docs/POKRACOVANI.md` sekce 7 naopak ultravysoke pasmo **prebyvalo**
(+7,7 dB nad 12,8 kHz). Az se filtr srovna s 86Boxem, je treba tohle mereni
zopakovat - jedno z tech dvou tvrzeni se zmeni.

Tohle je nejlepsi jednotlivy zachytny bod pro dalsi praci.

## 4. Co zbyva projit

Poradi podle toho, jak moc to podle mereni ovlivnuje vysledek:

1. **`emu8k_update()`** (radek 1759) - hlavni smycka. Fixed-point aritmetika,
   poradi operaci, obalky po krocich. Nase je ve floatu s casem v sekundach.
2. **Filtr** - 86Box `FILTER_MOOG`; my mame TPT state-variable.
3. **Reverb a chorus** - 86Box je ma dekodovane z init poli, ktera ted
   posilame. Nase jsou porad nahrada (Freeverb-like + modulovana zpozdovaci
   linka). Tohle je nejvetsi jednotlivy zdroj rozdilu v urovni.
4. **`emu8k_vol_slide()`** (1739) - najezd na cilovou hlasitost.
5. **Scitani 32 hlasu** - 86Box ma fixed-point saturaci, my float.
6. **Kradeni hlasu** - u nas vlastni heuristika.

**Nechat nase, nebrat z 86Boxu:** tabulky decay/release. 86Box pouziva
tabulku z linuxoveho ovladace (`45120, 22614, 15990, 11307, ...`), my mame
tabulku z Creative ovladace (`47513, 23756, 15838, 11878, ...`), ktera je
bajt po bajtu shodna ve trech generacich ovladacu.

---

# 5. Overeni proti skutecnemu ovladaci (AWEUTIL.COM v DOSu)

Do teto chvile se vsechno porovnavalo tak, ze stopu registrovych zapisu
generoval **nas** kod. Spolecna chyba v ovladacove vrstve by se tim padem
neprojevila. Proto jsme si postavili vlastni 86Box se zaznamem zapisu
a pustili v nem pod DOSem `AWEUTIL.COM /S`, tedy ten binarni soubor, ze
ktereho je nase `PowerOnInit()` odvozene.

Postup je popsany v `../AWE32EmuData/ref86box/README.md`, cast 2.

## 5.1 Vysledek

Po opravach nize je shoda **uplna**:

```
nase: 1611 zapisu, AWEUTIL: 1611 zapisu, shodnych: 1611
```

Porovnava se posloupnost `(registr, hlas, hodnota)`, casy se ignoruji.

## 5.2 Chip ID: 86Box vraci spatnou hodnotu

AWEUTIL nejdriv detekuje kartu. `AWEUTIL.COM.asm`, `sub_12B40`:

```asm
mov     ax, 7C00h        ; registr 7, Data3, hlas 0
push    ax
call    sub_10EFA        ; cteni slova
cmp     ax, 0Ch          ; PLNE 16bitove porovnani, bez maskovani
jz      short loc_12B50
xor     ax, ax           ; jinak konec
retn
```

86Box na tomhle registru vraci `0x1c`:

```c
case 7: /*ID?*/
    return 0x1c | ((emu8k->id & 0x0002) ? 0xff02 : 0);
```

Takze detekce spadne a AWEUTIL vypise `ERR012: Echec de l'initialisation
de AWE32`. Ve stope je videt presne to - jediny zapis `E22 = 00E0`
(pointer na registr 7) a cteni `E20 -> 001C`.

**Nase hodnota `0x000C` je spravna**, potvrzeno primo kodem ovladace.
Linuxovy ovladac ma stejny test (`(U1_READ & 0x000f) != 0x000c`), jen ho ma
zakomentovany.

V nasem buildu 86Boxu je to opravene na `0x0c` - jinak by v nem zadny
originalni DOS ovladac nenabehl. Je to kandidat na hlaseni chyby do 86Boxu.

## 5.3 Init pole: tri zdroje, dve varianty

> **Opraveno pozdeji.** Puvodne tu stalo, ze "ALSA se plete a AWEUTIL ma
> pravdu". Neni to tak. Kdyz se pozdeji zmerila i inicializace windowsoveho
> `SBAWE32.DRV`, ukazalo se, ze **ALSA a SBAWE32.DRV se shoduji uplne**
> a od nich se lisi **AWEUTIL**. Neexistuje tedy jedna spravna sada -
> dva ovladace od Creative konfiguruji DSP jinak. Viz 5.3.1.

Pole `INIT1..INIT4` jsme prevzali z ALSA (`sound/isa/sb/emu8000.c`).
Srovnani s tim, co doopravdy posila AWEUTIL:

| pole | shoda |
|---|---|
| `kInit1` | 128/128 |
| `kInit2` | 128/128 |
| `kInit3` | 120/128 |
| `kInit4` | 120/128 |

Lisi se **stejne indexy** v obou polich - 83, 97, 103, 109, 113, 115, 121, 123:

| index | ALSA (kInit3) | AWEUTIL | ALSA (kInit4) | AWEUTIL |
|---|---|---|---|---|
| 83 | `D208` | `D280` | `D208` | `D280` |
| 97 | `C208` | `C280` | `C208` | `C280` |
| 103 | `D308` | `D380` | `D308` | `D380` |
| 109 | `D26E` | `D2E6` | `D26E` | `D2E6` |
| 113 | `C308` | `C380` | `C308` | `C380` |
| 115 | `B2FF` | `B27F` | `32FF` | `327F` |
| 121 | `D36E` | `D3E6` | `D36E` | `D3E6` |
| 123 | `B3FF` | `B37F` | `33FF` | `337F` |

V `Awe32InitArrays.h` jsou hodnoty AWEUTILu (protoze `PowerOnInit()` je prepis jeho sekvence); varianta SBAWE32 je tam vedle jako `kAltInit3Sbawe`/`kAltInit4Sbawe`. Jsou to koeficienty
interniho DSP, ktere v 86Boxu ridi reverb a chorus, takze na zvuk vliv maji.

Proc se ALSA lisi, nevime. Cast rozdilu vypada jako prohozene posledni dve
sestnactkove cislice (`D208`/`D280`, `D26E`/`D2E6`), ale `B2FF`/`B27F` do
toho vzoru nezapada, takze to nebude jedna systematicka transformace.
Komentar v ALSA u `send_array()` rika "Taken from the oss driver, not obvious
from the doc how this is meant to work".

## 5.4 Tri zapisy, ktere nam chybely

1. **SMARR podruhe.** AWEUTIL zapisuje `SMALR, SMARR, SMALW, SMARR` - tedy
   `SMARR` dvakrat a `SMARW` vubec. ALSA na tom miste dela
   `SMALR, SMARR, SMALW, SMARW`. Meli jsme jen prvni tri.

2. **`PTRX` hlasu 30 = `0x48280000`** a
3. **Data1 registr 1, hlas 28 = `0x0000`** (nezdokumentovany registr HWCF).

Body 2 a 3 uz byly popsane v `emu8000_register_map.md`
("pointer=003Eh ... Data0+2=4828h, pointer=003Ch, Data1=0"), ale nebylo
jasne, kam v sekvenci patri. Ted to vime: mezi blok hlasu 31 a zaverecne
`VTFT` hlasu 30 a 31.

## 5.5 Co to znamena

Overena je tim **cela inicializacni cast** ovladacove vrstvy, vcetne
registrove mapy, kodovani `sel`, portu, poradi zapisu a init poli.

Neoverene zatim zustava to hlavni: **prevod MIDI udalosti na registry**
(`Synth.cpp`) a **mapovani generatoru SoundFontu** (`SoundFont.cpp`).
Na to je potreba nechat v guestu skutecne hrat - plan je Windows 95
a `MPLAYER.EXE`, ktery jde pres `SBAWE32.DRV`. Nastroje na to uz existuji
(`../AWE32EmuData/tests/trace_diff.py --notes` vypise ke kazde note kompletni sadu
registru), chybi jen samotny beh.

---

# 6. Overeni note-on proti SBAWE32.DRV (Windows 95)

Druha, podstatnejsi cast: nechat v guestu skutecne hrat a porovnat, jake
registry ovladac nastavuje pri kazde note. Prehravalo se
`../AWE32EmuData/SAMPLES/MIDI/BACH/MINUET.MID` pres Media Player Windows 95, tedy pres
`SBAWE32.DRV` - ten ovladac, ze ktereho mame poradi zapisu pri note-on
i prevodni tabulky utlumu.

Opakuje se prikazem:

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_init.trace ../AWE32EmuData/tests/out/win95.trace
```

**Kterou binarku jsme merili** je zasadni: guest ma novejsi sadu ovladacu,
nez je na instalacnim CD. Vsechny soubory, ktere pri mereni skutecne bezely,
jsou vytazene do `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/` vcetne otisku - pri
disassembly se ma pouzivat **jen odtud**. Pokus nasadit starsi `SBAWE32.DRV`
z CD skoncil tim, ze guest prestal hrat (0 not misto 242).

Dulezite pro platnost srovnani: **`SYNTHGM.SBK` i `awe32.raw` jsou bajt po
bajtu shodne** s tim, co pouziva nas prehravac. Rozdily v registrech tedy
nejdou na vrub jinych dat.

Skutecne noty se poznaji tak, ze `IFATN != FF00` - touhle hodnotou ovladac
na zacatku zticha vsech 32 hlasu. **Nas prehravac i ovladac udelaly presne
242 note-on**, takze se daji parovat po sobe.

## 6.1 Co sedi (18 registru, 242/242)

`IP`, `PSST`, `PEFE`, `FMMOD`, `FM2FRQ2`, `LFO1VAL`, `LFO2VAL`, `ENVVOL`,
`ENVVAL`, `CVCF^`, `CSL^`, `CCCA^`, `VTFT^`, `Z1`, `Z1^`, `Z2`, `Z2^`, `CPF`.

Nejdulezitejsi z toho je **`IP` - vypocet vysky tonu je presny u vsech
242 not**. Stejne tak zacatek smycky (`PSST`) a cela modulacni cast
(`PEFE`, `FMMOD`, `FM2FRQ2`, oba `LFOxVAL`).

## 6.2 Co nesedi - 12 systematickych pravidel

Kazda odchylka je cisty, opakovatelny vztah, ne sum:

| registr | vztah | poznamka |
|---|---|---|
| `IFATN` cutoff | **ovladac = nase x 2,0** (242/242) | mezni kmitocet filtru |
| `IFATN` atten | ovladac = nase +13 (150x) nebo +14 (92x) | utlum |
| `VTFT` low | **= `IFATN` cutoff << 8** (242/242) | my zapisujeme `FFFF` |
| `CVCF` low | totez jako `VTFT` | my zapisujeme `FFFF` |
| `PTRX^` | **= `CPF^`** u ovladace (242/242) | my tam davame `IP` |
| `CPF^` | ovladac zapisuje linearni prirustek | my necháváme 0 |
| `PTRX` low | konstanta `1C81` | reverb send - vubec ho nenastavujeme |
| `TREMFRQ` | konstanta `0080` | my zapisujeme 0 |
| `PSST^` (pan) | ovladac `7F`, my `80` | stred je 0x7F, ne 0x80 |
| `DCYSUSV` sustain | ovladac vzdy `00`, my `7F` | sustain 0 = doznivani do ticha |
| `DCYSUSV` decay | ovladac = nase -1 | |
| `DCYSUS` | konstanta `007F` | modulacni obalka |
| `ATKHLDV` | konstanta `797D`; hold +1, attack -2 proti nam | |
| `ATKHLD` | konstanta `7F7D` | |
| `CSL` | ovladac = nase +1 | konec smycky |
| `CCCA` low | ovladac = nase -4 | pocatecni adresa |

## 6.3 Nejdulezitejsi nalez: cutoff filtru je 2x mimo

Mezni kmitocet filtru zapisujeme **presne polovicni** proti ovladaci, a to
u vsech 242 not bez vyjimky. To je velmi pravdepodobne vysvetleni toho, co
je v sekci 3.1 - ze nam proti 86Boxu nad 6 kHz chybi 25 az 38 dB. Pulka
cutoffu v logaritmicke skale filtru znamena o oktavu niz.

Zaroven ovladac zapisuje cilovy cutoff (`VTFT` a `CVCF` low) rovny
pocatecnimu, zatimco my tam davame `FFFF` (plne otevreno). To je druha cast
tehoz problemu.

## 6.4 Dalsi krok

Opravit `Synth.cpp` podle tabulky v 6.2, po kazde zmene spustit:

```bash
python ../AWE32EmuData/tests/regress.py
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_init.trace ../AWE32EmuData/tests/out/win95.trace
```

Cilem je dostat vsech 30 registru na 242/242. Az potom ma smysl vracet se
k mereni spektra proti 86Boxu a proti nahravkam - dokud je cutoff 2x mimo,
jsou vsechna spektralni mereni k nicemu.

Pozor pri opravach: `DCYSUSV` sustain `00` proti nasemu `7F` neni maly
detail, meni to charakter doznivani. A `PTRX` low je reverb send, ktery
zatim vubec neposilame, takze nase reverbova sbernice dostava jen to, co si
tam davame sami.

---

# 7. AWEUTIL.COM proti SBAWE32.DRV

Otazka: jsou DOSovy a windowsovy ovladac z hlediska zvuku identicke?
**Nejsou** - a nejsou to ani tytez programy. Zmereno na inicializaci, ktera
je u obou zaznamenana.

| | AWEUTIL.COM /S | SBAWE32.DRV (Windows) |
|---|---|---|
| zapisu do registru pri init | 1611 | 2400 (do prvni noty) |
| INIT1, INIT2 | 128 + 128 | 128 + 128, **hodnoty shodne** |
| INIT3, INIT4 | 128 + 128 | 131 + 137, **8 hodnot jinych** |
| `SMLD` (nahravani do DRAM) | 0 | 3 - nahrava banku |
| `SMALR^`, `SMALW^`, `SMARR^`, `SMARW^` | 0 | ano - pouziva i horni pulky |
| Data1 reg 1, hlasy 0..19 | jen hlas 28 | vsech 20 + horni pulky |
| cteni ID registru | ano | ano |

## 7.1 Osm hodnot v init polich

Ve **vyslednem stavu** registru (ne v poradi zapisu - SBAWE32 nektere
hodnoty dodatecne prepisuje) se lisi presne osm:

| registr | AWEUTIL | SBAWE32.DRV | ALSA (ADIP) |
|---|---|---|---|
| `INIT3` v19 | `D280` | `D208` | `D208` |
| `INIT4` v1 | `C280` | `C208` | `C208` |
| `INIT4` v7 | `D380` | `D308` | `D308` |
| `INIT4` v13 | `D2E6` | `D26E` | `D26E` |
| `INIT4` v17 | `C380` | `C308` | `C308` |
| `INIT4` v19 | `327F` | `32FF` | `32FF` |
| `INIT4` v25 | `D3E6` | `D36E` | `D36E` |
| `INIT4` v27 | `337F` | `33FF` | `33FF` |

**ALSA a SBAWE32.DRV se shoduji ve vsech osmi.** Rozdily nejdou popsat jednim
bitovym prevodem (`08`->`80` a `6E`->`E6` je zamena bitu 3 a 7, ale `FF`->`7F`
je jen shozeni bitu 7), takze to nebude preklep v jednom zdroji - jsou to
dve zamerne odlisne konfigurace DSP.

Prakticky dopad: jsou to koeficienty reverbu a chorusu. Az se bude resit
efektova cast, je treba vedet, kterou cestu emulujeme.

## 7.2 Co zatim neni zmereno

Note-on. Ten je porovnany **jen** proti `SBAWE32.DRV` (sekce 6). AWEUTIL ma
vlastni MIDI engine v rezimu `/EM:GM`, ktery jsme nerozbehali - potrebuje
EMM386 pro odchytavani portu ve V86 a nejakou DOSovou aplikaci, ktera posila
MIDI na MPU-401. Takze **nevime**, jestli oba ovladace pocitaji registry pri
note-on stejne.

Nepnimy argument pro shodu: prevodni tabulky utlumu jsou podle
`docs/POKRACOVANI.md` bajt po bajtu identicke ve trech generacich ovladacu.
To ale neni tvrzeni o shodnem vypoctu note-on, jen o shodnych tabulkach.

---

# 8. Kde je note-on logika: v .VXD, ne v .DRV

Pokus zreverzovat note-on v `SBAWE32.DRV` narazil hned na zacatku:

- V cele binarce (45008 B, 4 kodove segmenty, 84 funkci) **neni ani jedna
  instrukce `out`/`in` na porty**. Overeno disassembly po funkcich od prologu
  `55 8B EC`, ne linearnim sweepem (`python ../AWE32EmuData/tests/ne_disasm.py <drv> --funcs`).
- Prevodni tabulky utlumu v ni **take nejsou**.

Obojí je v `SBAWE.VXD` (86054 B). Ve windowsove architekture AWE32 tedy
`.DRV` resi jen rozhrani k MIDI mapperu a vlastni prace - vypocet registru
i pristup na porty - je ve VXD.

**Cil reverzovani je proto `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/SBAWE.VXD`.**
Je to 32bitovy VxD ve formatu LE (Linear Executable), takze `ne_disasm.py`
na nej nesedi a bude potreba jiny parser.

## 8.1 Co uz je z VXD potvrzene

Dve ze tri prevodnich tabulek jsou v nem **bajt po bajtu shodne** s nasim
`Awe32Curves.h`:

| tabulka | offset ve VXD | shoda |
|---|---|---|
| `kChannelVolumeDb` | `0x8F10` | 128/128 |
| `kExpressionDb` | `0x8F90` | 128/128 |
| `kVelocityDb` | `0x8E90` | **127/128** |

U `kVelocityDb` se lisi jediny bajt - index 0:

| zdroj | `kVelocityDb[0]` |
|---|---|
| `SBAWE32.MDI` (Miles) | 50 |
| `SBAWE32.DRV` 45632 B (WINDRV) | 50 |
| `SBAWE.VXD` 86054 B (merene) | **99** |

Nechavame 50 - dva zdroje ze tri, a velocity 0 je v MIDI note-off, takze se
hudebne nepouzije. Hodnota 99 zapada do vzoru ostatnich tabulek, kde 99
slouzi jako "prakticky ticho" pro nizke indexy.

## 8.2 Provenience starych poznamek

`docs/re-notes/driver_note_on.md` a `Awe32Curves.h` citovaly offsety typu
`SBAWE32.DRV ds:0592` a `0x021E`. Ty odkazuji na kopii
`../AWE32EmuData/SoundBlaster AWE32/SBAWE32/WINDRV/SBAWE32.DRV` (**45632 B**), coz je jeste
jina verze nez ta merena (45008 B) i nez ta na instalacnim CD (44176 B).
Pri dalsi praci s temi offsety je treba pouzit WINDRV kopii, nebo je znovu
najit v merene binarce.

Prehled verzi, na ktere jsme narazili:

| soubor | velikost | obsahuje tabulky utlumu | port I/O |
|---|---|---|---|
| `SBAWE32.DRV` (merena, guest) | 45008 | ne | ne |
| `SBAWE32.DRV` (instalacni CD) | 44176 | ne | ne |
| `SBAWE32.DRV` (WINDRV) | 45632 | **ano** | ? |
| `SBAWE32.DRV` (SDK, Win 3.1) | 38720 | ne | ? |
| `SBAWE.VXD` (merena, guest) | 86054 | **ano** | **ano** |
| `SBAWE32.MDI` (Miles/AIL) | 36880 | **ano** | ano |

---

# 9. Note-on rutina v SBAWE.VXD - co uz je precteno

Nastroj: `python ../AWE32EmuData/tests/le_disasm.py <vxd>` (parser LE, mapa stranek, xref,
disassembly 32bit). Soubor vzdy z `../AWE32EmuData/SoundBlaster AWE32/traced-drivers/`.

Struktura VxD: 5 objektu, stranka 4096, data od `0x2000`.

| objekt | virt_size | k cemu |
|---|---|---|
| 1 | `0x7b68` | **note-on a prevodni tabulky** |
| 2 | `0x37` | maly stub |
| 3 | `0x21c6` | 75 pristupu na porty |
| 4 | `0x5a7d` | inicializace hardwaru |
| 5 | `0xdd8` | data (rw) |

## 9.1 Zapisovac registru - objekt 1, `0x0A05`

```asm
0A05: push ebp; mov ebp,esp; push esi; pushfd; cli
0A0B: mov esi,[ebp+8]              ; arg1: struktura zarizeni
0A0E: mov eax,[ebp+0xC]            ; arg2: "sel"
0A11: movzx edx,word [esi+4]       ; bazovy port ze struktury
0A15: add edx,2
0A1A: out dx,al                    ; spodni bajt sel -> pointer port
0A1B: and ch,0xFE
0A1E: and eax,0x100
0A23: shr ecx,8
0A26: movzx edx,word [ecx+esi]     ; datovy port z tabulky ve strukture
0A2A: shr eax,7                    ; bit 8 sel -> +2
0A2D: or edx,eax
0A2F: mov eax,[ebp+0x10]           ; arg3: hodnota
0A32: out dx,ax
0A37: ret 0xC
```

Tedy `write_reg16(dev, sel, value)`, kde:

- `sel & 0xFF` je pointer, tj. `(reg << 5) | voice`
- `(sel >> 8) & 0xFE` je index do tabulky portu v te strukture
- bit 8 v `sel` prida k portu 2 (horni slovo / "Data2")

**Je to jine kodovani `sel` nez v AWEUTILu** (tam `(reg<<12)|(portSel<<9)|voice`).
Volani je stdcall, 114 mist v objektu 1.

## 9.2 Note-on blok - objekt 1, od `~0x1F40`

Vzor je porad stejny: hodnota se slozi jako `hi<<8 | lo` z per-voice
struktury v `ebx`, `sel` je `voice | konstanta`:

| `sel` OR | reg | +2 | registr | hodnota |
|---|---|---|---|---|
| `0x380` | 4 | ano | ATKHLDV | `[ebx+0x46]<<8 \| [ebx+0x44]` (hold, attack) |
| `0x3A0` | 5 | ano | LFO1VAL | `[ebx+0x2A]` |
| `0x3C0` | 6 | ano | ATKHLD | `[ebx+0x36]<<8 \| [ebx+0x34]` |
| `0x2E0` | 7 | ne | DCYSUS | `[ebx+0x3A]<<8 \| [ebx+0x38]` |
| `0x3E0` | 7 | ano | LFO2VAL | `[ebx+0x2E]` |
| `0x400` | 0 | ne | CPF | `[ebp-0x10]` |
| `0x420` | 1 | ne | PTRX low | `[ebx+0x10]<<8 \| [ebx+0x26]` |
| `0x440` | 2 | ne | CVCF low | `[ebx+0x0E]<<8 \| [ebx+0x16]` |
| `0x460` | 3 | ne | (VTFT/DCYSUSV) | `atten<<8 \| [ebx+0x14]` |
| `0x480` | 4 | ne | | `[ebx+0x1A]<<8 \| [ebx+0x2C]` |
| `0x4A0` | 5 | ne | | `[ebx+0x0C]<<8 \| [ebx+0x30]` |
| `0x2C0` | 6 | ne | | `[ebx+0x32]`, nebo `0xBFFF` ve zvlastni vetvi |

Prirazeni jmen registru u poslednich ctyr jeste **neni potvrzene** - chce to
dohledat, cim je inicializovana tabulka portu v te strukture (`[esi+2]`,
`[esi+4]`, ...), aby bylo jiste, ktery index je Data0, Data1 a Data3.

### Znama pole per-voice struktury (`ebx`)

| offset | vyznam (odvozeno z hodnot ve stope) |
|---|---|
| `+0x0A` | utlum patche |
| `+0x0C` | ? (do horniho bajtu s `[ebx+0x30]`) |
| `+0x0E` | **mezni kmitocet filtru** - ve stope `0xDC` |
| `+0x10` | reverb send - ve stope `0x1C` |
| `+0x14` | decay rate |
| `+0x16` | 0 (spodni bajt CVCF) |
| `+0x1A`, `+0x2C` | ? |
| `+0x26` | pan pro PTRX - ve stope `0x81` |
| `+0x2A`, `+0x2E` | zpozdeni LFO1, LFO2 |
| `+0x32` | ? |
| `+0x34`, `+0x36` | attack a hold modulacni obalky |
| `+0x38`, `+0x3A` | DCYSUS |
| `+0x44`, `+0x46` | attack a hold hlasitostni obalky |

## 9.3 Vypocet utlumu - `0x2029`

```asm
2029: mov edx,[ebp-0xC]            ; stav kanalu
202C: movzx ecx,byte [edx+0xA]     ; hlasitost kanalu v dB (tabulka)
2030: movzx eax,byte [edx+0xB]     ; velocity v dB (tabulka)
2034: add ecx,eax
2036: movsx eax,word [ebx+0xA]     ; utlum patche
203A: add eax,ecx
203C: cmp eax,0x7F
2041: mov eax,0x7F                 ; ostriha na 127
2046: movsx ecx,word [ebx+0x14]
204A: shl eax,8
204D: or ecx,eax                   ; hodnota = atten<<8 | [ebx+0x14]
```

Struktura odpovida tomu, co mame v `Awe32Curves.h` - soucet hlasitosti
kanalu, velocity a utlumu patche. **Chybi tu ale nasobeni 8/3**, ktere nas
komentar u `Awe32Curves.h` uvadi z `SBAWE32.MDI 0x2102`. Tady se scitaji
priamo tabulkove hodnoty a strihaji se na `0x7F`. To je bud jiny vypocet
v teto verzi, nebo je prevod na jednotky registru jinde.

## 9.4 Co zbyva

1. **Dohledat tabulku portu** v strukture zarizeni, aby bylo jiste prirazeni
   indexu 2 a 4 na Data0/Data1/Data3. Bez toho jsou ctyri radky v tabulce
   v 9.2 nejiste.
2. **Najit, kdo plni `[ebx+0x0E]`** (cutoff). Tam je odpoved na otazku, proc
   je nas cutoff presne polovicni - viz 6.3. To je nejdulezitejsi.
3. Zjistit, jestli `[ebx+0x10]` (reverb send `0x1C`) a `TREMFRQ = 0x0080`
   pochazi z presetu banky, nebo je to konstanta ovladace.
4. Overit vypocet utlumu proti 9.3 vcetne toho chybejiciho 8/3.

## 9.5 Tabulka portu dokoncena

Ze zapisovace `0x0A05` jde mapovani odvodit presne. Pointer port je
`[esi+4] + 2`, datovy port `[esi + ((sel>>8) & ~1)]`, a bit 8 v `sel` prida 2:

| index | port | poznamka |
|---|---|---|
| 0 | `0x620` / `0x622` | Data0, 32bitove registry (pres `0x0A6F`) |
| 2 | `0xA20` / `0xA22` | Data1 / Data2 |
| 4 | `0xE20` | Data3, jen 16bit; bit 8 se tu nikdy nepouziva |

`[esi+4] = 0xE20`, takze pointer vychazi na `0xE22`. Ctyri pomocne rutiny:

| adresa | funkce |
|---|---|
| `0x0A05` | `write_reg16(dev, sel, val)` |
| `0x0A3A` | `read_reg16(dev, sel)` |
| `0x0A6F` | `write_reg32(dev, sel, val)` - jedno `out dx, eax` |
| `0x0A95` | `read_reg32(dev, sel)` |

Tim se opravuje tabulka v 9.2 - spravne prirazeni je:

| `sel` OR | registr | hodnota |
|---|---|---|
| `0x2C0` | ENVVAL | `[ebx+0x32]` |
| `0x2E0` | DCYSUS | `[ebx+0x3A]<<8 \| [ebx+0x38]` |
| `0x380` | ATKHLDV | `[ebx+0x46]<<8 \| [ebx+0x44]` |
| `0x3A0` | LFO1VAL | `[ebx+0x2A]` |
| `0x3C0` | ATKHLD | `[ebx+0x36]<<8 \| [ebx+0x34]` |
| `0x3E0` | LFO2VAL | `[ebx+0x2E]` |
| `0x400` | IP | `[ebp-0x10]` |
| `0x420` | **IFATN** | `[ebx+0x10]<<8 \| [ebx+0x26]` |
| `0x440` | PEFE | `[ebx+0x0E]<<8 \| [ebx+0x16]` |
| `0x460` | FMMOD | `(modwheel+patch, max 0x7F)<<8 \| [ebx+0x14]` |
| `0x480` | TREMFRQ | `[ebx+0x1A]<<8 \| [ebx+0x2C]` |
| `0x4A0` | FM2FRQ2 | `[ebx+0x0C]<<8 \| [ebx+0x30]` |

**Dve opravy proti 9.2:** `[ebx+0x10]` je mezni kmitocet filtru (ne reverb
send) a `[ebx+0x26]` je utlum (ne pan). A vypocet na `0x2029`, ktery jsem
nejdriv cetl jako utlum, je ve skutecnosti **LFO1 -> vyska** pro FMMOD.

Take potvrzeno: na zacatku note-on se do `VTFT` i `CVCF` zapisuje
`0x0000FFFF` (`0x1F3B`, `0x1F4F`) a jednorazova smycka se stavi na
`konec+4 .. konec+8` (`0x1F1C`) - obojí presne jak uvadeji stare poznamky.

## 9.6 VYRESENO: cutoff je v SF1 sedmibitovy

Ovladac cte `initialFilterFc` z banky a **zdvojnasobuje** ho. Dolozeno
trojmo, nezavisle:

1. **Kod:** `SBAWE.VXD` orezava pole cutoffu na `0..0xFF`
   (`0x2547`..`0x255E`), takze registr je osmibitovy.
2. **Data:** v `SYNTHGM.SBK` neni ani jedna hodnota `initialFilterFc` vetsi
   nez 127 (nejvyssi je presne 127) - generator je tedy sedmibitovy.
   Piano 1 ma 110, zona klaves 58..66 ma 89.
3. **Stopa:** ovladac zapsal 220 a 178, tedy presne dvojnasobky, u vsech
   242 not.

Opraveno v `SoundFont.cpp`. Po oprave sedi cutoff **242/242**.

## 9.7 Velocity -> cutoff, presne znenie

`SBAWE.VXD` objekt 1, `0x1CF6`:

```asm
1CF6: cmp word [esi+0x44], 0x7D    ; attack
1CFB: jge preskocit
1CFD: movsx eax, word [esi+0x5E]   ; velocity
1D01: cmp eax, 0x46
1D06: mov eax, 0x46                ; max(velocity, 0x46)
1D0B: movsx ecx, word [esi+0x10]   ; cutoff
1D0F: imul ecx, eax
1D12: add ecx, 0xA0
1D18: shr ecx, 7
1D1B: mov word [esi+0x10], cx
```

tedy `cutoff = (cutoff * max(velocity, 0x46) + 0xA0) >> 7`.

Meli jsme `+0x40` a deleni `0x7F`. Opraveno v `Synth.cpp`. (Vyjimku pro
kanal 9 jsme si nechali - v tehle vetvi neni videt, ale muze byt driv.)

## 9.8 Dopad na spektrum

MINUET proti 86Boxu, pred opravou cutoffu a po ni:

| pasmo Hz | pred | po |
|---|---|---|
| 0-100 | +3,1 | +3,0 |
| 100-200 | +3,3 | +3,1 |
| 200-400 | +3,9 | +3,1 |
| 400-800 | +4,6 | +3,2 |
| 800-1600 | -1,1 | +3,5 |
| 1600-3200 | **-7,4** | **+3,8** |
| 3200-6400 | **-9,2** | **+3,4** |
| 6400-12800 | **-25,0** | **-4,8** |
| 12800+ | **-38,4** | **-31,7** |
| korelace obalky | 0,990 | **0,9991** |

Do 6,4 kHz je ted odchylka rovnomerna (+3,0 az +3,8 dB), coz uz je jen
rozdil celkove urovne - 86Box na vystup neaplikuje mixer SB16. Predtim byl
rozptyl mezi pasmy 13 dB, ted 0,8 dB.

Zbyva pasmo nad 12,8 kHz (-31,7 dB). To uz nebude mapovani cutoffu, ale
strmost a topologie filtru - my mame TPT state-variable, 86Box Moog ladder.

---

# 10. Instrukcni tracer v 86Boxu

Staticka disassembly byla pomala a nekolikrat me svedla (viz opravy v 9.5).
Proto ma nas build 86Boxu tracer, ktery umi:

1. **Pri kazdem pristupu na porty EMU8000** vypsat kompletni stav CPU
   a okna pameti kolem `EBX` a `EBP`. Tim je u kazdeho zapisu do registru
   videt, s cim ovladac pracoval.
2. **Vypsat kazdou instrukci** v zadanem rozsahu linearnich adres i s registry.

Zapina se promennymi prostredi, bez nich to nic nestoji:

```
AWE32_TRACE_FILE   cesta k vystupu
AWE32_TRACE_MEM    kolik bajtu kolem EBX/EBP (default 128)
AWE32_TRACE_INSN   1 = i jednotlive instrukce
AWE32_TRACE_LO/HI  rozsah linearnich adres
AWE32_TRACE_MAX    strop poctu radku
```

Instrukcni rezim potrebuje build **bez dynarecu**:

```bash
C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=MINGW64 AWE32_DYNAREC=OFF \
  AWE32_BUILDDIR=/c/prenos/AWE32EmuData/ref86box/build86box_int \
  /c/prenos/AWE32EmuData/ref86box/build_86box.sh"
```

Pozor: hook musi byt ve **dvou** smyckach - `exec386_dynarec_int()`
i `exec386()`. Pentium bez dynarecu jede pres tu druhou; kdyz jsem ji
zapomnel, stopa neobsahovala ani jednu instrukci.

Prohlizec stopy je `../AWE32EmuData/tests/insn_view.py` - k linearnim adresam doplni
disassembly z obrazu VXD.

## 10.1 Kam je VXD natazeny

Objekt 1 `SBAWE.VXD` lezi na linearni adrese **`0xC0FF8B48`**. Zjisteno tak,
ze navratove adresy volajicich (ctene z `[EBP+4]` v okne pameti) sedly na
12 volacich mist nalezenych staticky. Ta adresa se muze mezi behy lisit -
overit vzdy proti nekolika znamym offsetum.

## 10.2 VYRESENO: utlum, chybejicich 6 dB

Uplny vzorec z `SBAWE.VXD` objekt 1, `0x1C54`..`0x1CE7`:

```asm
1C54: mov  [ebp-0x10], 0x18            ; delitel 24
1C5B: movsx ecx, word [esi+0x5E]       ; velocity
1C62: movzx ecx, byte [ecx+0x408E90]   ; velDb[velocity]
1C69: movzx eax, byte [edx+0x408F10]   ; volDb[CC7]
1C70: add  ecx, eax
1C72: movsx eax, word [esi+0x60]       ; dalsi zdroj utlumu (neznamy)
1C76: add  eax, 0xC
1C7A: idiv dword [ebp-0x10]            ; (x + 12) / 24
1C7D: add  ecx, eax
1C81: lea  eax, [ecx*8]
1C8D: div  ecx=3                       ; * 8 / 3
1C8F: movzx ecx, word [edi+0x10]       ; utlum patche
1C93: add  ecx, eax
1C95: cmp  ecx, 0xFF                   ; oriznuti
1CA0: mov  al, byte [eax+8]            ; expression (CC11)
1CA3: cmp  al, 0x7F / jae preskocit
1CAA: movzx edx, byte [eax+0x408F90]   ; exprDb[expression]
1CB1: mov  eax, 0x100                  ; 256
1CB9: sub  eax, ecx
1CBB: imul eax, edx
1CBE: shr  eax, 7                      ; * exprDb / 128
1CC1: add  ecx, eax
1CCB: cmp  dword [edi+0x158E], 0x4D474D31   ; "1MGM"
1CD7: add  ecx, 0x10                    ; +16 jednotek = 6 dB
1CE7: mov  word [esi+0x26], cx
```

Tri odchylky proti nasemu `Awe32Curves.h`:

1. **`+16` kdyz se banka odkazuje na ROM "1MGM"** - to je 6 dB a mели jsme
   to uplne. Ve stope slo `ecx` z `0x18` na `0x28` presne tady.
   Opraveno v `Synth.cpp`. Rozdil v `IFATN` klesl ze 14 jednotek na 2.
2. **Utlum patche se pricita rovnou** (`patchAtten + 8*soucet/3`), zatimco
   my mame `(8*soucet + ((3*(127-patchAtten)) & ~7)) / 3`.
3. **Expression:** ovladac `(256 - atten) * exprDb / 128`, my
   `(255 - atten) * exprDb / 127`.

Body 2 a 3 spolu se zatim neznamym clenem `(word[esi+0x60] + 12) / 24`
vysvetluji zbylé 2 jednotky rozdilu. Nemenil jsem je - `ComputeAttenuation`
je psana podle `SBAWE32.MDI` a ta se od VXD muze legitimne lisit, stejne
jako se lisi init pole (sekce 7).

**Vedlejsi efekt:** tech 6 dB byla presne chybejici rezerva. RELAX pred
opravou klipoval (peak 1,000), po ni ma peak 0,592 a regrese zase prochazi.

## 10.3 Stav registru pri note-on

**25 z 33 registru sedi 242/242.** Zbyva:

| registr | naše → ovladac | poznamka |
|---|---|---|
| `IFATN` | `DC2A` → `DC28` | uz jen 2 jednotky, viz 10.2 |
| `TREMFRQ` | `0000` → `0080` | `[ebx+0x2C]` = frekvence LFO1 z presetu |
| `PTRX^`, `CPF^` | `CD5C` → `21A0` | linearni prirustek vysky, ne `IP` |
| `PTRX` low | `0000` → `1C81` | reverb send + druhy bajt |
| `DCYSUSV` | `7F05` → `0004` | sustain a decay |
| `DCYSUS` | `7F7F` → `007F` | sustain modulacni obalky |

---

# 11. Vstup do EMU8000: 31 z 33 registru sedi 242/242

Vsechno nize je precteno z `SBAWE.VXD` (staticky i instrukcni stopou), ne
odhadnuto z namerenych hodnot.

## 11.1 Pole struktury hlasu

Pri kazdem zapisu na porty tracer vypisuje okno pameti kolem `EBX`, takze se
da precist cela struktura hlasu naraz. Pro prvni notu MINUETu:

| offset | vyznam | hodnota |
|---|---|---|
| `+0x10` | mezni kmitocet filtru | `00DC` |
| `+0x12` | Q filtru | `0000` |
| `+0x1E` | chorus send | `0000` |
| `+0x20` | reverb send | `001C` |
| `+0x24` | doplnkova panorama | `0081` |
| `+0x26` | utlum | `0028` |
| `+0x2A`, `+0x2E` | zpozdeni LFO1, LFO2 | `8000` |
| `+0x2C` | frekvence LFO1 | `0080` |
| `+0x34`, `+0x36` | attack, hold modulacni obalky | `007D`, `007F` |
| `+0x38`, `+0x3A` | decay, sustain modulacni obalky | `007F`, `0000` |
| `+0x44`, `+0x46` | attack, hold hlasitostni obalky | `007D`, `0079` |
| `+0x48`, `+0x4A` | decay, sustain hlasitostni obalky | `0004`, `0000` |
| `+0x5C` | cislo noty | |
| `+0x5E` | velocity | `006D` |

## 11.2 Vychozi hodnoty ovladace

`SYNTHGM.SBK` u piana **nema** generatory `freqModLFO`, `sustainVolEnv`
ani `reverbEffectsSend` (ten nema v cele bance) a MINUET neposila CC91.
Presto ovladac zapisuje nenulove hodnoty - jsou to jeho vychozi hodnoty:

| generator chybi | ovladac pouzije | meli jsme |
|---|---|---|
| `freqModLFO` | **128** (TREMFRQ lo) | 0 |
| `sustainVolEnv`, `sustainModEnv` | **0** = doznivani do ticha | `0x7F` = drzeni |
| `reverbEffectsSend` | **28** | 0 |

Do spodniho bajtu `PTRX` jde **`256 - pan`** (pri pan `0x7F` tedy `0x81`).

## 11.3 Vyska: PTRX i CPF nesou linearni prirustek

Horni pulka `PTRX` a `CPF` neni logaritmicke `IP`, ale linearni prirustek.
`SBAWE.VXD` objekt 1, `0x212E`:

```asm
212E: mov  esi, 1
2133: mov  ecx, [ebp-0x10]        ; IP
2136: shr  ecx, 0xC
2139: shl  esi, cl                ; 2^(IP>>12)
213B: test byte [ebp-0xF], 8      ; bit 11
2148: imul eax, esi, 0x102E
214F: idiv 0x2710
2151: add  esi, eax               ; * (1 + 0,41420)
2153: test byte [ebp-0xF], 4      ; bit 10 -> 0x764/0x2710 = 0,18920
216B: test byte [ebp-0xF], 2      ; bit  9 -> 0x389/0x2710 = 0,09050
218C: sar  eax, 2
218F: add  esi, eax               ; * 1,25
2191: cmp  esi, 0xFFFF            ; oriznuti
```

Je to `2^(IP/4096)` v pevne radove carce: celociselna cast dava posun, tri
nejvyssi bity zlomku pridavaji `2^(1/2)`, `2^(1/4)` a `2^(1/8)` pres zlomky
se jmenovatelem 10000, nakonec nasobeni 1,25.

Konstanty odpovidaji presne: `0x102E/0x2710 = 0,41420` proti
`2^0.5-1 = 0,41421`, `0x764/0x2710 = 0,18920` proti `2^0.25-1 = 0,18921`,
`0x389/0x2710 = 0,09050` proti `2^0.125-1 = 0,09051`.

Overeno na trech notach na bit presne (`IP=CD5C -> 21A0`, `BE17 -> 1254`,
`D006 -> 2800`). Implementovano jako `PitchIncrement()` v `Synth.cpp`.

## 11.4 Zbyvaji dva registry

| registr | naše → ovladac | rozdil |
|---|---|---|
| `IFATN` | `DC2A` → `DC28` | 2 jednotky utlumu (0,75 dB) |
| `DCYSUSV` | `0005` → `0004` | jeden krok decay |

`IFATN`: zbyva dopsat vzorec podle 10.2 - utlum patche se pricita rovnou,
expression je `(256-a)*e/128` a je tam clen `(word[esi+0x60]+12)/24`, ktery
jsme neidentifikovali. Pozor, `ComputeAttenuation` je psana podle
`SBAWE32.MDI` a ta se od VXD muze legitimne lisit (jako init pole v sekci 7).

`DCYSUSV`: keynum skalovani ovladac dela jako
`decay += keynumToDecay * (60 - key)` (`SBAWE.VXD` `0x1D4E`), coz je stejna
formule jako nase; rozdil bude v prevodu ms na krok obalky, podobne jako
u `hold`, kde slo o utinani misto zaokrouhlovani.

## 11.5 Spektrum

MINUET proti 86Boxu po vsech opravach:

| pasmo Hz | rozdil dB |
|---|---|
| 0-100 | +2,8 |
| 100-200 | +3,1 |
| 200-400 | +3,3 |
| 400-800 | +3,3 |
| 800-1600 | +3,6 |
| 1600-3200 | +4,1 |
| 3200-6400 | +3,4 |
| 6400-12800 | -4,8 |
| 12800+ | -30,5 |

Korelace obalky hlasitosti **0,998**. Do 6,4 kHz je odchylka rovnomerna
(+2,8 az +4,1 dB) - to uz je jen rozdil celkove urovne, 86Box na vystup
neaplikuje mixer SB16.

Pasmo nad 12,8 kHz (-30,5 dB) uz **neni otazka registru** - ty sedi. Je to
rozdil filtru: my mame TPT state-variable, 86Box Moog ladder. To je dalsi
prace a patri do jadra, ne do ovladacove vrstvy.

---

# 12. Obe rodiny ovladacu v kodu, prepinac `--driver`

Creative ma dve rodiny ovladacu, ktere se v nekolika bodech **zamerne** lisi.
Neni tedy jedna spravna varianta a nemelo smysl jednu prepsat druhou, takze
jsou v kodu obe. Prepina se prepinacem `--driver dos|win95`, vychozi je
`win95`. Vychozi bod je `AWE32Emu/src/Awe32Driver.h`.

| co | `dos` | `win95` | kde v kodu |
|---|---|---|---|
| 8 hodnot v `INIT3`/`INIT4` | AWEUTIL | ALSA == VXD | `Awe32InitArrays.h`, `kAltInit*Sbawe` |
| `kVelocityDb[0]` | 50 | 99 | `Awe32Curves.h`, `VelocityDb()` |
| vzorec utlumu | MDI `0x2102` | VXD `0x1C54` | `Awe32Curves.h` |
| utlum `+16` pro ROM "1MGM" | ne | ano | `Synth.cpp` |

`dos` = `AWEUTIL.COM` (inicializace) + `SBAWE32.MDI` (Miles/AIL, note-on).
`win95` = `SBAWE.VXD`, tedy binarka, proti ktere je overena cela note-on
cesta.

## 12.1 Nejistoty, ktere je poctive priznat

**Rodina `dos` neni u note-on nicim overena.** MIDI engine AWEUTILu
(`/EM:GM`) jsme nikdy netrasovali - potreboval by EMM386 a DOSovou aplikaci
posilajici MIDI na MPU-401. Seskupeni AWEUTILu a MDI do jedne rodiny stoji
na tom, ze se v DOSu pouzivaji spolu a ze `SBAWE32.MDI` a starsi
`SBAWE32.DRV` maji prevodni tabulky bajt po bajtu shodne. Stejne tak
**nevime**, jestli `+16` pro "1MGM" dela i DOSova cesta; zmerene to je jen
ve VXD, proto je na `win95`.

**Vzorec utlumu z VXD je prepsany, ale zatim se nepouziva.**
`ComputeAttenuationVxd()` v `Awe32Curves.h` je hotovy podle disassembly, ale
zdroj utlumu patche v nem - pole `word[edi+0x10]` ve strukture kanalu -
jsme nezreverzovali a nase `patchAttenUnits` mu neodpovida. Vysledek je pak
**4 jednotky vedle**, zatimco MDI vzorec plus `+16` je vedle jen o **2**.
Proto obe rodiny zatim pocitaji podle MDI a prepnuti je oznacene v kodu jako
dalsi krok. Je to merene, ne odhadnute: staci porovnat

```bash
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
```

## 12.2 Stav obou variant

Proti zmerenemu `SBAWE.VXD` (MINUET, 242 not):

| varianta | sedi | `IFATN` | `DCYSUSV` |
|---|---|---|---|
| `--driver win95` | **30 z 32** | `DC2A` vs `DC28` | `0005` vs `0004` |
| `--driver dos` | 30 z 32 | `DC1A` vs `DC28` | `0005` vs `0004` |

Rozdil `IFATN` u `dos` je tech 14 jednotek, ktere delá chybejici `+16` pro
"1MGM" - u DOSove rodiny ho zamerne neaplikujeme, protoze pro ni dukaz
nemame.

---

# 13. Varianta `win95` sedi uplne

```
python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/ours_win95.trace ../AWE32EmuData/tests/out/win95.trace
  -> vsechny registry se shoduji
```

Vsech 32 sledovanych registru pri note-on sedi u vsech 242 not. Posledni dva
kusy skladacky, oba odectene z instrukcni stopy:

## 13.1 Utlum patche je v poli `[esi+0x60]` v 1/20 dB

Ve stope ma to pole hodnotu **150** pro preset s `initialAttenuation` 107.
A `150 * 0,05 dB = 7,5 dB = (127 - 107) * 0,375 dB`, takze je to tyz utlum,
jen v jinych jednotkach. Nase `patchAttenUnits` je v jednotkach registru
(0,375 dB), prevod je `* 15 / 2`.

Podstatne je, **kam** se v tom vzorci pricita: do souctu v dB jeste pred
prevodem na jednotky registru, a celociselnym delenim 24 se pritom orizne.
Varianta Dos ho pricita az za prevodem pres `(3*p) & ~7`. Odtud ten trvaly
rozdil dvou jednotek.

Kontrola na prvni note: `volDb[127] = 0`, `velDb[109] = 3`, `X = 150`.

```
soucet = 0 + 3 + (150 + 12) / 24 = 9
atten  = 9 * 8 / 3 = 24 = 0x18
+ 16 za ROM "1MGM"          = 0x28
```

Ovladac zapsal `0x28`. Take pole `[edi+0x10]` (jeste jeden, globalni utlum)
bylo ve vsech 242 notach 0, takze ho nemodelujeme.

## 13.2 Prevod ms na krok obalky zaokrouhluje na delsi cas

Ovladac vybira polozku tabulky, jejiz cas je **delsi nebo roven** zadanemu,
zatimco my jsme brali prvni kratsi. Ve stope:

```
+01D4E  edx = [esi+0x5C] = 0x45      ; nota 69
        eax = 60 - 69 = -9
+01D59  edx = [esi+0x50] = 0xB7      ; keynumToDecay 183
        eax = -9 * 183 = -1647
        ecx = 12600 - 1647 = 10953   ; ms
+01D9D  [esi+0x48] = 4               ; rate
```

Rate 4 ma v tabulce 11878 ms, rate 5 jen 9502 ms. Pro 10953 ms tedy ovladac
volí 4. Opraveno v `DecayRateFromMs()`.

Samotne keynum skalovani `decay_ms += keynumToDecay * (60 - key)` je shodne
s tim, co jsme meli.

## 13.3 Co to udelalo se zvukem

Regrese prochazi, RELAX ma peak 0,597. Proti 86Boxu (MINUET, oba renderu
z teze stopy):

| pasmo Hz | rozdil dB |
|---|---|
| 0-100 az 3200-6400 | +2,8 az +4,1 |
| 6400-12800 | -4,8 |
| 12800+ | -30,7 |

Korelace obalky 0,998. Spektrum se proti stavu pred temito dvema opravami
nezmenilo - byly to rozdily radu desetin dB. **Vstup do EMU8000 je tim
hotovy**; co zbyva, je jadro, konkretne filtr.

---

# 14. Rodina `dos`: co uz je zmerene ze `SBAWE32.MDI`

Merilo se tak, ze v oddelenem DOSovem virtualnim stroji (`../AWE32EmuData/ref86box/vmdos/`)
bezela **Magic Carpet 2**, ktera pouziva `SBAWE32.MDI`. Ten soubor je
v guestu bajt po bajtu shodny s nasim `../AWE32EmuData/sbk/SBAWE32.MDI`, takze jde o presne
tu binarku, ze ktere je odvozena rodina `dos`. Postup je v
`../AWE32EmuData/ref86box/README.md`, cast 3.

Zaznam: `../AWE32EmuData/tests/out/dos_mdi.trace`, 195 236 zapisu do registru, z toho
341 hudebnich note-on.

## 14.1 Co MDI pri note-on zapisuje

Vsech 24 registru u vsech 341 not, tedy stejnou sadu jako VXD **az na CVCF**,
ktery nezapisuje vubec.

Ukazka jedne noty:

```
ATKHLD=7F7D  ATKHLDV=267D  CCCA=98ED  CCCA^=0004  CSL=B63F  CSL^=FE04
DCYSUS=7F7F  DCYSUSV=7F07  ENVVAL=8000  ENVVOL=8000  FM2FRQ2=0000
FMMOD=0000   IFATN=FF0A    IP=F400     LFO1VAL=8000 LFO2VAL=8000
PEFE=0000    PSST=B191     PSST^=0F04  PTRX=7200   PTRX^=9837
TREMFRQ=0080 VTFT=FFFF     VTFT^=0000
```

## 14.2 Tri zmerene rozdily proti VXD

| vec | `SBAWE.VXD` | `SBAWE32.MDI` |
|---|---|---|
| `VTFT` na konci note-on | prepise na `cutoff << 8` | necha `0x0000FFFF` |
| `CVCF` | zapisuje | **nezapisuje vubec** |
| spodni bajt `PTRX` (pan aux) | `256 - pan` | **0** |

Ten treti je videt primo: ve stope ma `PSST^` pan `0x0F`, ale `PTRX` ma
spodni bajt `0x00` - kdyby platilo `256 - pan`, bylo by tam `0xF1`.

Prvni dva jsou v kodu podminene variantou (`Synth.cpp`), treti taky.

`TREMFRQ = 0x0080` plati u obou rodin, takze vychozi frekvence LFO1 = 128
je spolecna.

## 14.3 Vyska: MDI ji vubec nepocita, precte si ji z cipu

> **Oprava.** Puvodne tu stalo, ze MDI pouziva vlastni, presnejsi prevod.
> Neni to tak - po zreverzovani cele note-on rutiny se ukazalo, ze **zadny
> prevod nedela**.

`SBAWE32.MDI`, note-on rutina, `0x227C`:

```asm
227C: mov ax, di / or ah, 0x10   ; sel = PTRX (Data0 reg 1, 32bit)
2282: call 0x182C                ; read_reg32 -> dx:ax
2285: sub ah, ah                 ; nechat jen spodni bajt
2287: mov [bp-8], ax
228A: mov [bp-6], dx             ; horni slovo si odlozit
2290: mov al, [bx+4]             ; reverb kanalu
2293: add ax, [si+0x20]          ; + reverb patche, oriznuti na 255
22A9: mov ah, [bp-0xA]           ; reverb do horniho bajtu
22AC: sub al, al
22AE: or  ax, [bp-8]             ; zachovat puvodni spodni bajt
22B1: push dx                    ; zachovat puvodni horni slovo
22B3: call 0x17F0                ; write_reg32
```

Funkce na `0x182C` je **cteni 32bitoveho registru**, ne prevod vysky - posle
pointer na `[0x712]+2` a precte dve slova z datoveho portu.

MDI si tedy PTRX precte, nechá jeho horni slovo (cilovou vysku) i spodni
bajt beze zmeny a prepise **jen bajt s reverb sendem**. Cilovou vysku si
drzi sam cip.

Dolozeno tim, ze hodnoty `PTRX^` ve stope presne odpovidaji tomu, co si
dopocital 86Box (`ptrx_pit_target = freqtable[ip] >> 18`), ne necemu od
ovladace:

| `IP` | `PTRX^` ve stope | `freqtable[ip] >> 18` |
|---|---|---|
| `F400` | `9837` | `9837` |
| `F000` | `8000` | `8000` |
| `EEAA` | `78CD` | `78CD` |
| `EC00` | `6BA2` | `6BA2` |
| `B959` | `0BFE` | `0BFE` |

Vsech pet sedi presne. Vzorec s nasobenim 1,25 (sekce 11.3) je tedy vlastni
vypocet **windowsoveho** ovladace, ktery to, co dela cip, jen priblizne
napodobuje.

Implementovano: varianta `dos` dela u PTRX cteni-uprava-zapis, varianta
`win95` pocita `PitchIncrement()`.

## 14.4 Nahravani banky pres SMLD

Zasadni rozdil v provozu: **MDI nahrava banku hry do DRAM po vzorcich pres
registr `SMLD`** - 84 851 zapisu proti trem u VXD. My davame vzorky do DRAM
`memcpy` a harness si je nacita pres `--dram`, takze tahle cast v nasi stope
neni vubec. Na hodnoty registru pri note-on to vliv nema, ale pri srovnavani
celych stop je to potreba vedet.

## 14.5 Co jeste chybi

Porovnat note-on cislo po cisle jako u VXD. K tomu je potreba vyrenderovat
**tutez skladbu**: hudba hry je v `midi/` jako XMI, banka je
`../AWE32EmuData/sbk/BULLFROG.SBK`. Zatim jsou zmerene jen strukturalni rozdily z 14.2
a 14.3, ne uplna shoda.

## 14.6 Kodovani `sel` v MDI

Miles ovladac pouziva jine kodovani nez AWEUTIL i nez VXD. `sel` je
`voice | (NN << 8)`, kde `NN = (reg << 4) | portKod`:

| portKod | port |
|---|---|
| 0 | Data0 (`0x620`), 32bitove registry pres `0x17F0` |
| 4 | Data1 (`0xA20`) |
| 6 | Data2 (`0xA22`) |
| 8 | Data3 (`0xE20`) |

Pomocne rutiny: `0x177E` zapis 16 bitu, `0x17F0` zapis 32 bitu,
`0x182C` cteni 32 bitu. Tabulka portu je v datech na `0x70E`, pointer port
na `[0x712] + 2`.

Poradi zapisu v note-on (od `0x2175`):

```
DCYSUSV=0x80 (umlceni)  VTFT=0x0000FFFF  ENVVOL  ATKHLDV  ENVVAL  ATKHLD
DCYSUS  IP  IFATN  LFO1VAL  LFO2VAL  PEFE  FMMOD  TREMFRQ  FM2FRQ2
PTRX (cteni-uprava-zapis)  ...  a DCYSUSV nakonec
```

`CVCF` ani `CPF` mezi nimi nejsou.

## 14.7 Co uz je v kodu

| rozdil | `dos` | `win95` |
|---|---|---|
| `CVCF` pri note-on | nepise | pise |
| `CPF` pri note-on | nepise | pise prirustek |
| `VTFT` na konci | nechá `0x0000FFFF` | prepise na `cutoff << 8` |
| `PTRX` | cteni-uprava-zapis, jen reverb | spocita a zapise cele |
| spodni bajt `PTRX` | zachova (0) | `256 - pan` |
| vzorec utlumu | MDI `0x2102` | VXD `0x1C54` |
| `+16` pro ROM "1MGM" | ne | ano |
| 8 hodnot v init polich | AWEUTIL | ALSA == VXD |
| `kVelocityDb[0]` | 50 | 99 |

Overeno, ze varianta `win95` po vsech temhle zmenach **porad sedi
242/242 na vsech registrech** proti zmerenemu `SBAWE.VXD`.

---

# 15. Srovnani rodiny `dos` cislo po cisle

Skladbu se podarilo urcit z dat, ne odhadem: nase renderovana stopa
`004_C2INTRO_w.xmi` s bankou `../AWE32EmuData/sbk/BULLFROG.SBK` ma u prvni noty `IP=F400`,
`PSST=B191` a `CSL=B63F` presne jako zaznam z hry. Je to tedy tataz hudba.

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_w.xmi \
    --rom ../AWE32EmuData/rom/awe32.raw --sbk ../AWE32EmuData/sbk/BULLFROG.SBK \
    --wav out.wav --trace ../AWE32EmuData/tests/out/mc_intro.trace --driver dos

python ../AWE32EmuData/tests/notes_diff.py ../AWE32EmuData/tests/out/mc_intro.trace ../AWE32EmuData/tests/out/dos_mdi.trace --pair
```

## 15.1 Proc parovani a ne poradi

Zaznam z hry nezacina na zacatku skladby (hra uz chvili bezela) a nase
renderovana stopa ma 668 not proti 407 v zaznamu. Poradove parovani proto
nesedi - nejlepsi posun dava jen 62 % shody ve vyskach.

Rezim `--pair` proto kazdou notu ovladace spáruje s nasi notou, ktera ma
**tutez vysku a tyz vzorek** (`IP`, `PSST`, `CSL`). Tim se porovnava jen
prevod parametru, ne sekvencovani. Sparovat se podarilo 270 ze 407 not.

## 15.2 Vysledek: 19 z 24 registru sedi 270/270

Sedi: `ATKHLD`, `ATKHLDV`, `CCCA^`, `CSL`, `CSL^`, `DCYSUS`, `DCYSUSV`,
`ENVVAL`, `ENVVOL`, `FM2FRQ2`, `FMMOD`, `IP`, `LFO1VAL`, `LFO2VAL`, `PEFE`,
`PSST`, `TREMFRQ`, `VTFT`, `VTFT^`.

Stoji za zminku, ze **obalky sedi uplne** (`ATKHLDV`, `DCYSUSV`, `DCYSUS`,
`ENVVOL`, `ENVVAL`) a stejne tak cela modulacni cast - u jine hudby a jine
banky nez u ktere se ladila varianta `win95`.

## 15.3 Pet zbylych rozdilu

| registr | vztah | poznamka |
|---|---|---|
| `CCCA` | ovladac = nase **-42** (270/270) | pocatecni adresa; `PSST` i `CSL` pritom sedi, takze to neni posun baze |
| `PTRX` | ovladac = nase **-0x8C00** (270/270) | reverb send: nase `0xFE`, ovladac `0x72` |
| `PTRX^` | nase 0, ovladac ma cilovou vysku | nase jadro `ptrx_pit_target` nepocita, viz 14.3 |
| `IFATN` | +10 u 233 not, 0 u 37 | utlum; zavisi na stavu kanalu |
| `PSST^` | shoda u 233, u 37 se lisi pan (`0x0F` vs `0x7F`) | |

### Reverb send

MDI ho pocita jako soucet (`0x2290`):

```asm
2290: mov al, [bx+4]      ; reverb kanalu (CC91)
2293: add ax, [si+0x20]   ; + reverb patche
2296: oriznuti na 255
```

My bereme `max(patchReverb, CC91*2)`. `BULLFROG.SBK` ma
`reverbEffectsSend` 0, 16 nebo 58 - nase `0xFE` tedy nepochazi z banky
a je to chyba v nasem prevodu.

### Panorama

MDI ji pocita takto (`0x22B6`):

```asm
22B9: mov ax, 0x17F
22BC: mov cl, [bx+6]      ; pan kanalu (CC10)
22C1: add cx, [si+0x22]   ; + pan patche
22C4: add cx, cx          ; *2
22C6: sub ax, cx          ; 0x17F - 2*(chPan + patchPan)
      pak oriznuti: >= 0xFE -> 0xFF, zaporne -> 0
```

Nase `panFromCc`/`bankPan` je jiny prevod. U 233 z 270 not vyjde stejne,
u 37 ne.

## 15.4 Opravy provedene na zaklade tohohle srovnani

**`PTRX^` (cilova vyska) - vyreseno v jadru, ne v ovladaci.**
Nase `Emu8000Core` cilovou vysku vubec nepocitalo, takze cteni-uprava-zapis
u rodiny `dos` vracelo nulu. Ted se pri zapisu do `IP` dopocita stejne jako
v 86Boxu (`ptrx_pit_target = freqtable[ip] >> 18`). Dve veci, na kterych to
stalo:

- mezivysledek musi byt **64bitovy** - pro vysoka `IP` presahne 2^32
  a v 32 bitech se ustrihne na `0x3FFF`;
- `PTRX` se musi cist **az po zapisu `IP`**, jinak je v nem jeste stara
  hodnota. MDI ma to poradi presne tak (IP na 0x21DC, PTRX az na 0x227C).

Po oprave sedi `PTRX^` **270/270**.

**Reverb send** se u rodiny `dos` ted scita (`kanal + patch`) podle
`0x2290`, misto abychom brali vetsi z obou. Priblizilo to hodnotu z rozdilu
140 na 13, ale uplne nesedi - viz nize.

**Panorama zustala pri starem.** Vzorec z MDI (`0x17F - 2*(chPan + patchPan)`)
jsme zkusili a vysel **hur** - nase `bankPan` zjevne neodpovida jeho poli
`[si+0x22]`. Puvodni prevod sedi u 233 z 270 not, "presny" u zadne, takze
zustava puvodni a v kodu je u nej poznamka proc.

## 15.5 Stav po sekci 16: obe rodiny sedi uplne

| | `win95` proti `SBAWE.VXD` | `dos` proti `SBAWE32.MDI` |
|---|---|---|
| skladba | MINUET, `SYNTHGM.SBK` | C2INTRO, `BULLFROG.SBK` |
| noty | 242 | 255 sparovanych z 261 |
| shoda | **vsech 24 registru** | **vsech 24 registru** |

Pet rozdilu, ktere tu byly popsane driv, je vyresenych v sekci 16. Dva
z nich pritom nebyly rozdily v prevodu, ale chyby v tom, **jak jsme merili**
- viz 16.1.

---

# 16. Doreseni rodiny `dos` - a dve chyby v mereni

## 16.1 Nejdriv oprava metody

Puvodni tabulka "pet zbylych rozdilu" mela dve vady, kvuli kterym ukazovala
jinam, nez kde problem byl.

**1) Zaznam ovladace obsahoval vic nez jednu skladbu.** Magic Carpet 2 behem
mereni prehralo intro, pak jinou skladbu a pak intro znovu. Casova osa
zaznamu se rozpada na tri useky:

| snimky | co hraje |
|---|---|
| 2 810 017 - 5 986 589 | intro (`ccca=98ED`, `pan=0F`, `atten=10`) |
| 6 516 243 - 9 331 847 | **jina skladba** (`ccca=ECD1`/`E7DA`, `rev=8E`) |
| 9 388 955 - dal | intro znovu |

Prostredni usek pouziva tyz nastroj na kanalu s jinym `CC10`, takze se do
srovnani dostavaly noty, ktere v nasem vstupnim souboru vubec nejsou. Odtud
pochazel "rozdil panu u 37 not". `../AWE32EmuData/tests/notes_diff.py` proto ma nove
prepinace `--dframes` a `--oframes`:

```bash
python ../AWE32EmuData/tests/notes_diff.py nase.trace ovladac.trace --pair --dframes 0:6000000
```

**2) Parovaci klic obsahoval prave ty registry, ktere se meli porovnavat.**
Puvodne se parovalo podle `(IP, PSST, CSL)`. Nota ovladace se tedy sparovala
jen s takovou nasi notou, ktera uz mela **stejne** `PSST` a `CSL` - a rozdil
v nich se pak nemohl nikdy projevit. Prakticky to znamenalo, ze se
porovnavaly jen noty jednoho ROM vzorku a vsechny bici se tise ztratily.
Klic je nove `(IP, adresa vzorku s toleranci 256 slov)`; adresa se z klice
neodstranila uplne, protoze bez ni nejde poznat, ktery nastroj to je, ale
tolerance nechava rozdil v ni videt.

Po obou opravach vzroslo srovnani z 270 not jednoho vzorku na 255 not tri
vzorku a objevily se dva rozdily, o kterych jsme predtim nevedeli (smycka
u jednorazovych vzorku, posun DRAM).

## 16.2 Kde je v `SBAWE32.MDI` co

Cely note-on je jedna funkce od **0x1E76**. Rozvrzeni:

| adresa | co dela |
|---|---|
| 0x1E76 | vstup, argumenty `[bp+4]` nota, `[bp+6]` velocity, `[bp+8]` kanal |
| 0x1E84 | `si = 0xE46 + kanal*0x1C` - **zaznam kanalu**, uklada se do `[bp-0x1e]` |
| 0x1E97 | volani 0x1A8C - najde vrstvy presetu a naplni bloky parametru |
| 0x1EFD | `[bp-2] = 0xBC6 + hlas*0x14` - **zaznam hlasu** |
| 0x1ED1 | `[bp-0x1a] = 0x9D6` - **blok parametru vrstvy**, krok 0x86 |
| 0x1FF4 | adresy vzorku, vetveni smycka / jednorazovy vzorek |
| 0x2102 | utlum (tabulky + expression) |
| 0x2175 | zapisy do registru cipu |

Klicovy nalez je v 0x1CD0 uvnitr 0x1A8C:

```
1CCB:  mov si, 0x16ad
1CD0:  push cs ; pop ds
1CD2:  mov cx, 0x43
1CD5:  rep movsw                  ; 67 slov = vychozi hodnoty generatoru
...
1CDD:  [bx+0x5c] = nota           ; generator 46
1CE3:  [bx+0x5e] = velocity       ; generator 47
1CFE:  for kazdy generator z banky:  [blok + gen*2] = hodnota
```

Blok parametru vrstvy je tedy **prime pole generatoru SoundFontu, indexovane
cislem generatoru krat dva**. Odtud jdou vsechny dosud zahadne offsety:

| offset | generator | vyznam |
|---|---|---|
| `[si+0x10]` | 8 | initialFilterFc |
| `[si+0x1e]` | 15 | chorusEffectsSend |
| `[si+0x20]` | 16 | reverbEffectsSend |
| `[si+0x22]` | 17 | pan |
| `[si+0x5c]` | 46 | keynum (nota) |
| `[si+0x5e]` | 47 | velocity |
| `[si+0x60]` | 48 | initialAttenuation |
| `[si+0x6c]` | 54 | sampleModes |
| `[si+0x76]` | - | zacatek vzorku (dopocitany) |
| `[si+0x7a]` | - | konec vzorku |
| `[si+0x7e]` | - | zacatek smycky |
| `[si+0x82]` | - | konec smycky |

`SBAWE.VXD` ma **uplne stejne rozvrzeni** (jen 32bitove), takze tahle mapa
plati pro obe rodiny. Videt je to napriklad na 0x1ECF: `eax = [ebx+0x76]`
a `test byte ptr [ebx+0x6c], 1`.

## 16.3 Tabulka vychozich hodnot generatoru

Ta, co se kopiruje pres `rep movsw`, lezi v MDI na **0x16AD** a ve VXD
(objekt 1) na **0x6D60**. Jsou to tytez hodnoty az na jednu:

| generator | | MDI 0x16AD | VXD 0x6D60 |
|---|---|---|---|
| 8 | initialFilterFc | 255 | 255 |
| 15 | chorusEffectsSend | 0 | 0 |
| 16 | reverbEffectsSend | 28 | 28 |
| 17 | pan | **64** | **64** |
| 22 | freqModLFO | 128 | 128 |
| 26 | attackModEnv | 125 | 125 |
| 27 | holdModEnv | 127 | 127 |
| 28 | decayModEnv | 127 | 127 |
| 30 | releaseModEnv | 127 | 127 |
| 34 | attackVolEnv | 125 | 125 |
| 38 | releaseVolEnv | 127 | 127 |
| 43 | keyRange | 0..127 | 0..127 |
| 44 | velRange | 0..127 | 0..127 |
| 48 | initialAttenuation | **110** | **127** |
| 54 | sampleModes | **0** | **0** |
| 58 | overridingRootKey | 60 | 60 |

Dve veci z toho jsou pro nas dulezite:

- **`sampleModes` ma vychozi nulu**, tedy jednorazovy vzorek. Meli jsme
  u SF1 vychozi jednicku (smycka). Poznat to jde na bance Magic Carpet 2:
  presety `LOOP2` a `LOOP3` zadny `sampleModes` nemaji a ovladac jim opravdu
  pokládá smycku az za vzorek. Nazev presetu tedy klame.
- **`initialAttenuation` se mezi rodinami lisi** (110 vs 127). To je dalsi
  polozka do `Awe32Driver.h`.

## 16.4 Rozdil po rozdilu

### CCCA: `-46` u DOSu, `-4` u Windows

```
SBAWE32.MDI 0x1FF4:  ax:dx = [si+0x76];  sub ax, 0x2e   (46)
SBAWE.VXD   0x1ECF:  eax   = [ebx+0x76]; sub eax, 4
```

Neni to preklep ani chyba mereni - proti MDI to byl rozdil presne 42 slov
u vsech not a proti VXD sedi `CCCA` na 242 notach se ctyrkou. Prehravani
tedy DOSovy ovladac spousti 46 slov pred zacatkem vzorku.

### Reverb a chorus: kanalovy podil se skaluje na 90 %

Handlery controlleru jsou v MDI ctyri za sebou:

```
242A:  mov ax,0x5a; mul si; mov cx,0x64; div cx;  [kanal+4] = al   ; CC91 reverb
244C:  mov ax,0x5a; mul si; mov cx,0x64; div cx;  [kanal+5] = al   ; CC93 chorus
246E:  [kanal+6] = al                                              ; CC10 pan
2482:  [kanal+8] = al                                              ; CC7 hlasitost
```

Pan ani hlasitost se neskaluji, reverb a chorus ano. `127 * 90 / 100 = 114`
= `0x72`, presne to, co bylo ve stope. VXD to ma doslova stejne (objekt 1,
0x314D a 0x3174, do `[kanal+0x447]` a `[+0x448]`).

Skladani s hodnotou z banky je soucet s oriznutim na 255 (MDI 0x2290 pro
reverb, 0x230A pro chorus). Meli jsme misto toho `max(banka, CC*2)`.

### Pan: `0x17F - 2*(panBanky + CC10)`

```
MDI 0x22B6:  ax = 0x17F;  cx = [kanal+6] + [si+0x22];  cx += cx;  ax -= cx
             if (ax >= 0xFE) ax = 0xFF;   if (zaporne) ax = 0
VXD 0x4253:  tentyz vzorec, jen spodni mez je  if (ax <= 1) ax = 0
```

Drive jsme scitali uz hotovou registrovou hodnotu z banky s posunem od CC10.
Vyslo to stejne jen proto, ze v obou merenych bankach generator `pan` chybi
a vychozich 64 dava tentyz vysledek. Pri prvnim pokusu tenhle vzorec vysel
**hur** - protoze se do nej dosazovala nase registrova hodnota (vychozi 127)
misto surove hodnoty generatoru (vychozi 64).

### CSL: jednicka se pricita jen u smyckoveho vzorku

```
MDI 0x2019 (smycka):        loopEnd_reg = [si+0x82] + 1
MDI 0x208B (jednorazovy):   loopEnd_reg = konec + 8      ; bez jednicky
```

Meli jsme `+1` bezpodminecne, takze u 52 not bicich vychazelo `CSL` o jednu
vic.

### DRAM: prvni vzorek nezacina na zacatku

Ovladac necha pred prvnim vzorkem v DRAM **50 slov** - jeho prvni vzorek
zacina na `0x200032`, nas na `0x200000`. Dava to smysl: kdyz `CCCA` ukazuje
46 slov pred zacatek vzorku, bez rezervy by mirilo jeste do ROM karty.
Rozdil byl u vsech 52 not bicich presne 50.

## 16.5 IFATN: nebyl to vzorec, byla to hlasitost hudby ve hre

Utlum vychazel u vsech not o 10 jednotek nizsi nez ovladaci. Vzorec pritom
sedel; problem byl ve vstupu.

Utlum se pocita takhle (MDI 0x2102, prepsano do `Awe32Curves.h`):

```
if (CC7 <= 10) return 255;
atten = ( 8*(volDb[CC7] + velDb[velocity]) + ((3*(127 - patchAtten)) & ~7) ) / 3;
if (atten >= 255) return 255;
if (CC11 < 127) atten += exprDb[CC11] * (255 - atten) / 127;
```

XMI posila `CC7 = 127` a vsechny noty maji velocity 127; `volDb[127]` i
`velDb[127]` jsou nula, takze nam vyslo 0. Ovladac ale zapsal 10, coz
odpovida `volDb` = 4, tedy **efektivni CC7 kolem 100**.

Overeni na druhem kanalu: kanal 5 posila `CC7 = 90` a ovladac u nej zapsal
utlum 29, coz odpovida `volDb` = 11, tedy efektivnimu CC7 = 70. Jedina
hodnota hlavni hlasitosti, ktera sedi na oba pripady zaroven, je
**99 nebo 100 ze 127**:

| hlavni hlasitost | CC7 127 -> | utlum | CC7 90 -> | utlum |
|---|---|---|---|---|
| 99 | 99 | 10 | 70 | 29 |
| 100 | 100 | 10 | 70 | 29 |
| 101 | 101 | 10 | 71 | **26** |

Skalovani nedela ovladac - `0x2482` uklada `CC7` syrove - ale sekvencer AIL
(`AIL_set_XMIDI_master_volume`), tedy hra sama. Neni to tedy rozdil
v prevodu, ale nastaveni hlasitosti hudby v Magic Carpet 2.

Prehravac ma proto novy prepinac `--master-volume N` (0..127, vychozi 127),
kterym se mereni da reprodukovat:

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_w.xmi --rom ../AWE32EmuData/rom/awe32.raw --sbk ../AWE32EmuData/sbk/BULLFROG.SBK --wav ../AWE32EmuData/tests/out/mc_intro.wav --trace ../AWE32EmuData/tests/out/mc_intro.trace --driver dos --master-volume 100
```

## 16.6 Vysledek

```
sparovano 255 z 261 not ovladace
OK ATKHLD  ATKHLDV  CCCA  CCCA^  CSL  CSL^  DCYSUS  DCYSUSV  ENVVAL  ENVVOL
OK FM2FRQ2  FMMOD  IFATN  IP  LFO1VAL  LFO2VAL  PEFE  PSST  PSST^  PTRX
OK PTRX^  TREMFRQ  VTFT  VTFT^
vsechny registry se shoduji
```

Varianta `win95` zustala na 242/242 u vsech registru a `../AWE32EmuData/tests/regress.py`
prochazi (peak RELAXu 0,576). Spektrum proti 86Boxu se nezmenilo - rozdil
nad 12,8 kHz zustava -30,7 dB, coz uz neni vec ovladace, ale jadra
(sekce 11.5).

## 16.7 Nove nastroje

| skript | k cemu |
|---|---|
| `../AWE32EmuData/tests/xmi_events.py` | vypis MIDI udalosti z XMI - programy, controllery, velocity po kanalech |
| `../AWE32EmuData/tests/sbk_dump.py` | generatory presetu a nastroju z SBK/SF2 |

`../AWE32EmuData/tests/notes_diff.py` ma nove `--dframes` / `--oframes` a jine parovani
(viz 16.1).

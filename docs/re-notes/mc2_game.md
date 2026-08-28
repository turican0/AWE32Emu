# Magic Carpet 2 primo ze hry

Meri se hra bezici v 86Boxu s **jejim vlastnim** ovladacem
`SOUND\SBAWE32.MDI` (Miles/AIL V3.02 z 18. 1. 1995), aby bylo proti cemu
stavet nasi `dos` rodinu.

## Jak to spustit

```bash
powershell -File ../AWE32EmuData/ref86box/run_trace.ps1 \
  -Trace ../AWE32EmuData/tests/out/mc2_full.trace \
  -CpuTrace ../AWE32EmuData/tests/out/mc2_full_cpu.trace \
  -Mode mc2 -Seconds 420 -NoEsc
```

Pasti, na ktere jsme narazili:

| co | jak to je |
|---|---|
| **Nechat to dobehnout** | Nahravani banky do DRAM zabere prvnich ~20 s a teprve pak zacne hudba. Beh utnuty po dvou minutach ma 5046 "not", ale jsou to vsechno hlasy z nahravani banky, ne hudba. |
| **"Starting Windows 95"** | `MSDOS.SYS` ma `BootGUI=0`, do GUI se nejde. Ta hlaska je z `IO.SYS` a naskoci i v cistem DOS rezimu - neni to chyba. |
| **`AUTOEXEC.BAT`** | V obrazu jsou dve zalohy: `AUTOEXEC.MC2` (spousti hru) a `AUTOEXEC.MID` (DOSMID). Prepina se pres `tests/fat16.py put`. |
| **Vyber ovladace** | Neni v `CONFIG.DAT`, ale v `NETHERW/SOUND/MDI.INI` (a `DIG.INI`). V obrazu uz je spravne `Creative Labs AWE-32(TM) General MIDI` / `SBAWE32.MDI`. **Pozor:** kopie u remc2 (`x64/Debug/NETHERW/SOUND/MDI.INI`) ma `SBPRO2.MDI`, tedy FM - ta je matouci. |
| **`fat16.py`** | Umi i podadresare (`ls NETHERW/SOUND`, `get NETHERW/SOUND/MDI.INI`), jen to ma v hlavicce napsane zastarale. |

`SETSOUND` vybira `sbawe32.mdi` automaticky, kdyz `AUTOEXEC.BAT` obsahuje
retezec `AWEUTIL` (viz `.A AWEUTIL` v `AILDRVR.LST`). MC2 autoexec ho
neobsahuje, takze na vyber ovladace **spolehat nejde** - musi byt v `MDI.INI`.

## Co hra posila na zacatku

Ovladac dostane pri startu sled zprav na **vsech sestnact kanalu**
(dekompilovane v remc2, `engine/Sound.cpp`, smycka `for (i = 0; i < 16; i++)`):

    CC114 0, program 0, bend stred, CC112 0, CC1 0,
    CC7 <predvolba 13>, CC10 64, CC11 127, CC64 0,
    CC91 40, CC93 0, CC100 0, CC101 0, CC38 0, CC6 <predvolba 16>

Predvolby AIL: index 13 = `0x7F` (hlasitost), index 16 = `0x02` (rozsah
ohybu). Proti nasim vychozim hodnotam se lisi **CC7 (127 vs 100)** a
**CC91 (40 vs 0)** - to je ta "kalibrace hlasitosti".

Je to v `../AWE32EmuData/conf/mc2.conf` a nacita se prepinacem `--conf`:

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_g.xmi \
  --rom ../AWE32EmuData/rom/awe32.raw --sbk ../AWE32EmuData/sbk/BULLFROG.SBK \
  --driver dos --conf ../AWE32EmuData/conf/mc2.conf --wav out.wav
```

Poznamka ke zdroji: hodnoty jsou z dekompilace, ne ze statickeho rozboru
`NETHERW.EXE`. Staticky to nejde - je to DOS/4GW **LE** a odkazy do datoveho
segmentu jsou nerelokovane (`0x181DAC` se v kodu jako absolutni adresa
nevyskytuje), stejne jako u `SBAWE.VXD`. Samotne pole je navic v BSS.
Overit se to da bud vypisem pameti guesta za behu, nebo - lepe - tim, ze
nas render bude sedet na stopu ze hry.

## Banka hry

`SOUND\BULLFROG.SBK` (172 196 B) je **jen na CD**, ne v obrazu disku.
Ma 15 presetu a **ani jeden bicí**:

    0:3 LOOP2   0:0 REV2   0:4 LOOP3   0:5 TBellD4Wave
    0:117 AgogoLoTone   0:118 SquareWave   0:119-127 SynthBassLoop

Samé smycky a efekty; hlasi se k ROM `1MGM`. **Bicí tedy hra bere z wave
ROM**, ne z vlastni banky. To je dulezite pro otazku nize.

Nas render nahraje do DRAM **84 852** vzorku, hra zapise **84 851** `SMLD` -
sedi.

## Struktura stopy ze hry

Z behu na 195 s (`dos_mdi.trace`):

| registr | pocet | poznamka |
|---|---|---|
| `SMLD` | 84 851 | nahrani banky, snimky 1 731 427..1 754 803 |
| `DCYSUSV` | 16 703 | |
| `PSST` / `VTFT` | 5 455 | |
| `CCCA` | 10 469 | |
| `IP` | 979 | |
| `IFATN` | 739 | |
| `ATKHLDV` / `ENVVAL` | 439 | **tolik je skutecnych not** |

Nahravani banky konci uz ve snimku 1 754 803, `ATKHLDV` jde az do konce -
podle toho se pozna, kde konci priprava a zacina hudba.

## Otevrena otazka: rozbite bicí

Tomas pri poslechu zjistil, ze **bicí zni spatne i v 86Boxu se skutecnym
ovladacem**, stejne jako v nasem projektu. Hudba v menu je v poradku, intro
ne.

Co z toho plyne: nase emulace ovladace **neni na vine**. Nas cip je
s 86Boxem bajtove shodny (`--chip 86box`, 0 rozdilu na 6 927 532 snimcich),
takze spolecna pricina je jen jedna ze tri:

1. `snd_emu8k.c` z 86Boxu (chyba v emulaci cipu),
2. dump wave ROM `awe32.raw`,
3. neco spolecneho vys - napriklad zachazeni s kanalem 10.

Bicí jsou z ROM (viz banka vyse), takze bod 2 je ve hre. Proti tomu ale
stoji, ze Georgia z ROM zni spravne.

**Jak to rozseknout:** v `SAMPLES3/` jsou nahravky ze skutecneho zeleza
z ROM banky, ktere bicí obsahuji (`AWE32 (ROM) - Doom E1M1.mp3` a dalsi).
Porovnat je s nasim renderem tehoz MIDI oddeli chybu cipu/ROM od cehokoli,
co je specificke pro MC2. `MC2_Menu.mp3` je taky ze zeleza, ale je z casti,
ktera zni dobre - hodi se jako kontrola, ne jako test.

Pozn.: intro se v menu po chvili necinnosti spusti znovu, takze na jeho
zachyceni staci nechat bezet dost dlouhy zaznam.

## Sladeni s hrou: **vsech 24 registru na 100 %**

Porovnava se nas render intra proti stope ze hry (`dos_mdi.trace`), jen po
**notu 260** - tam intro ve hre konci (dvanactisekundova mezera) a dal uz
stopa obsahuje hudbu z menu.

```bash
./bin/x64/Release/AWE32Emu.exe ../AWE32EmuData/midi/004_C2INTRO_w.xmi \
  --rom ../AWE32EmuData/rom/awe32.raw \
  --sbk ../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK \
  --sbk ../AWE32EmuData/sbk/BULLFROG.SBK \
  --driver dos --conf ../AWE32EmuData/conf/mc2.conf \
  --wav out.wav --trace ../AWE32EmuData/tests/out/mc2_final.trace
```

| registr | shoda |
|---|---|
| **vsech 24** | **261/261** |

### Tri veci, ktere to odemkly

**1. Chybela GM banka z wave ROM.** `BULLFROG.SBK` popisuje jen 15 vlastnich
presetu hry; vsechno ostatni bere hra z GM banky v ROM. Bez ni jsme u presetu,
ktere v bance nejsou, dosazovali nesmysl (`CCCA` vychazelo `FFD2`) a hrali
jednu vrstvu tam, kde hra hraje dve. Nacist se proto musi **obe**:
`--sbk SYNTHGM.SBK --sbk BULLFROG.SBK`. Casovani not se tim srovnalo
z rozchodu u noty 22 na shodu **do par milisekund az po notu 260**.

**2. Hlavni hlasitost XMIDI je 100, ne 127.** Ovladac s ni skaluje CC7
kazdeho kanalu (`cc7 * master / 127`). Poslat ji jako CC7 na zacatku nestaci -
XMI si vlastni CC7 hned prepise, takze to musi byt **prubezne** skalovani
(`Synth::SetMasterVolume`, v konfiguraci `master_volume 100`).

Jak se to naslo: `IFATN` bylo **0/261** se stalym rozdilem `FF00` -> `FF0A`,
tedy o 10 jednotek utlumu vic. Ve vzorci `atten = (8*db + patch) / 3` to
znamena `db = 4`, a `kChannelVolumeDb` ma ctyrku prave pro CC7 99..103.
Po nastaveni hlavni hlasitosti na 100 je `IFATN` **259/261** a rozdil
v utlumu presne nula.

**3. `Synth::PitchBend` chce odchylku se stredem v nule**, ne surovych
0..16383. Prvni verze `--conf` posilala `bend 8192` primo, takze kazdy kanal
mel trvaly ohyb +2 pultony - bylo to videt jako posun vsech vysek o 0x2AA.

**4. Reverb: pocatecni CC91 je 0, ne 40.** Dekompilace remc2 ma v inicializaci
`CC91 40`, ale k zarizeni se to nedostane - AIL si pri startu sekvence stav
kanalu prepise z vlastni stinove kopie. Zmereno: na kanalech, ktere si CC91
v XMI nenastavuji (2, 5, 6), je prispevek kanalu nulovy. S nulou sedi `PTRX`
**261/261** (drive 233).

**5. Rozsah ohybu vysky je 12 pultonu a nastavuje se pres RPN 0,0.**
`Synth` RPN vubec neumel - `pitchBendRangeSemitones` se jen cetlo a zustavalo
na vychozich dvou. Doplneno (CC101/CC100 vybiraji RPN, CC6 nastavuje rozsah).

**6. Na pulton ma ovladac celociselnou konstantu 341** (= 4096/12 utnuto), ne
zlomek, a deli az nakonec. Doklad jsou dva nezavisle namerene body:

    rozsah 2,  plny ohyb dolu:  -8192*2*341/8192  = -682   (67 not Georgie)
    rozsah 12, plny ohyb dolu:  -8192*12*341/8192 = -4092  (4 noty ch6 v MC2)

Vzorec se zlomkem by u rozsahu 12 dal -4096, tedy o 4 jednotky vedle.

**Vedlejsi ucinek bodu 5 a 6:** Georgia a JUMP se zlepsily z **29/32 na
31/32**. Stara zahada "na ch7 nastavuje MIDI rozsah 12, ale ovladac se chova
jako 22" byla presne tohle - my jsme drzeli dva pultony a rozdil vypadal jako
neco jineho.

### Slepa ulicka: XMIDI smycky

Rozdil 669 proti 407 notam vypadal na neimplementovane XMIDI smycky
(`XmiFile.cpp` bloky `RBRN` preskakuje a CC116/117 nezpracovavame).
**Neni to tim.** Intro zadne smycky nema - v souboru neni blok `RBRN` ani
CC116/117, jen CC119 (callback, na prehravani bez vlivu), a `EVNT` ma
**669 not**, presne tolik, kolik jich nas render udela. Rozdil byl v tom, ze
stopa ze hry obsahuje intro jen do noty 260 a pak uz hraje menu.
Rozbor: `tests/xmi_raw.py`.

**7. Mezni kmitocet filtru se mezi rodinami lisi** a my jsme mely obe stejne
(podle VXD):

    SBAWE32.DRV 0x021E (dos):    (cutoff * v + 0x40) / 0x7F
    SBAWE.VXD   0x1CF6 (win95):  (cutoff * v + 0xA0) >> 7

Rozdil je videt jen nahore: pro cutoff 255 a velocity 127 da DOS 255
(32449/127 = 255,5), VXD 254 (32545/128 = 254,3). Byly to posledni dve
nesedici noty.

### Co zbyva

Nic. Intro Magic Carpet 2 sedi na skutecny Miles ovladac ve vsech 24
registrech u vsech 261 not.

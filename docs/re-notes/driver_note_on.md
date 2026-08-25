# Co ovladac dela navic pri note-on

Zjisteno z `SBAWE32.DRV` (Windows AWE32 MIDI driver) v miste, kde si
sestavuje vlastni patch strukturu ze SoundFontu. Jsou to veci, ktere
**nejsou ve SoundFontu ani v Programmer's Guide** - vyplynou az z kodu.

Struktura je adresovana pres `si`; dulezita pole:

| offset | vyznam |
|---|---|
| `[si+0x18]` | mezni kmitocet filtru (horni bajt IFATN) |
| `[si+0x3A]` | ENVVAL - delay modulacni obalky |
| `[si+0x3C]` | attack modulacni obalky |
| `[si+0x4A]` | ENVVOL - delay volume obalky |
| `[si+0x4C]` | attack volume obalky |
| `[si+0x4E]` | hold volume obalky (v ms, pak prepsano registrovou hodnotou) |
| `[si+0x50]` | decay volume obalky |
| `[si+0x56]` | keynumToVolEnvHold |
| `[si+0x58]` | keynumToVolEnvDecay |
| `[si+0x64]` | cislo noty |
| `[si+0x66]` | velocity |
| `[si+0x68]` | utlum patche |
| `[si+0x74]` | sampleModes |

## 1. Velocity ovlivnuje mezni kmitocet filtru  (`0x021E`)

```
0200  cmp  [bp+4], 9         ; kanal 9 (bicí) ma vlastni vetev
0204  jne  0x21E
021E  cmp  [si+0x4c], 0x7D   ; jen kdyz attack rate < 0x7D
0222  jge  0x246
0224  mov  ax, [si+0x66]     ; velocity
022A  cmp  ax, 0x46          ; spodni mez 70
022F  mov  [bp+8], 0x46
0237  imul word [si+0x18]    ; cutoff * velocity
023A  add  ax, 0x40          ; zaokrouhleni
0241  idiv cx                ; / 0x7F
0243  mov  [si+0x18], ax
```

Tedy:

    if (kanal != 9 && attackRate < 0x7D)
        cutoff = (cutoff * max(velocity, 0x46) + 0x40) / 0x7F;

Tise hrane noty jsou tmavsi. Bicí se takhle neupravuji.

## 2. Zavislost obalky na cisle noty  (`0x0278`)

```
0278  ax = 0x3C - [si+0x64]     ; 60 - nota
027E  imul [si+0x56]            ; * keynumToVolEnvHold
0281  add  [si+0x4e], ax        ; hold +=
0284  jns  0x28B
0286  [si+0x4e] = 0             ; nezaporne

028B  ax = [si+0x64] - 0x3C     ; nota - 60
0291  imul [si+0x58]            ; * keynumToVolEnvDecay
0295  sub  [bp-6], ax           ; decay -=
029B  if (< 0) decay = 0
```

Vztazne k **note 60**. Vyssi noty maji kratsi decay, nizsi delsi hold.

## 3. Prevod hold na registr  (`0x02A9`) - potvrzeni

```
02A9  ax = [si+0x4e]     ; hold v ms
02AC  cx = 0xFFA4        ; -92
02B0  idiv cx
02B2  add  ax, 0x7F
02B5  [si+0x4e] = ax
```

Tedy `holdReg = 127 - holdMs/92`, presne jak udava Programmer's Guide
("hold time in 92 msec increments, 0x7f = no hold time").

## 4. Nezasmyckovany vzorek  (`0x02C7`)

```
02C7  test byte [si+0x74], 1    ; sampleModes bit 0 = smycka?
02CB  je   0x2E4
      ; smyckovany: loopStart = [si+8], loopEnd = [si+0xC] + 1
02E4  ; nezasmyckovany:
02EA  ax = [si+0xC] + 4         ; loopStart = konec + 4
02FC  ax = [si+0xC] + 8         ; loopEnd   = konec + 8
```

EMU8000 nema "one-shot" rezim, takze ovladac polozi smycku **do ticha za
vzorek** - format za kazdy vzorek pripisuje 46 nulovych vzorku, offsety
+4 a +8 tedy bezpecne padnou do nich. Hlas pak po dohrani mlci a utlumi
ho obalka.

## 5. Bicí kanal  (`0x0206`)

```
0206  cmp [si+0x3c], 0x7F       ; attack modulacni obalky == max?
020C  mov [si+0x3a], 0xB7FF     ;   -> ENVVAL = 0xB7FF
0211  cmp [si+0x4c], 0x7F       ; attack volume obalky == max?
0217  mov [si+0x4a], 0xB7FF     ;   -> ENVVOL = 0xB7FF
```

**[?] Nezatim neimplementovano** - hodnota 0xB7FF lezi nad 0x8000, coz
u delay registru (kde 0x8000 = bez prodlevy) nedava zjevny smysl.
Nutno overit, co s takovou hodnotou dela cip.

---

# Cely sled zapisu na notu - merene, ne ctene z disassembly

Vyse je to, co se dalo vycist z kodu. Tohle je to, co ovladac **opravdu**
zapsal: `georg_win95.trace` (Georgia, `SBAWE.VXD` ve Windows 95 v 86Boxu),
vytazene nastrojem

```bash
python ../AWE32EmuData/tests/voice_seq.py ../AWE32EmuData/tests/out/georg_win95.trace --note 2
```

Poradi je zleva doprava shora dolu; `^` je horni pulka 32bitoveho registru.

| krok | ovladac | my |
|---|---|---|
| konec predchozi noty | `DCYSUSV 8029` **a `DCYSUS 8027`** | jen `DCYSUSV 8029` |
| ztiseni pred novou notou | `DCYSUSV 00FF` | `DCYSUSV 0080` |
| cile na ticho | `VTFT FFFF` + `CVCF FFFF`, **VTFT dvakrat** | jednou |
| blok parametru | `ATKHLDV, LFO1VAL, ATKHLD, DCYSUS, LFO2VAL, IP, IFATN, PEFE, FMMOD, TREMFRQ, FM2FRQ2, ENVVAL, ENVVOL` | tentyz obsah, ale **az po** adresach |
| adresy - nulovani | `PTRX 0000`, `CPF 0000` | chybi |
| adresy | `PSST, CSL, CCCA` (s `CCCA^ = 0000`) | `PSST, CSL, CCCA` rovnou s Q |
| **`Z1 = Z1^ = Z2 = Z2^ = 0`** | ano, u kazde noty | **chybi uplne** |
| `CCCA` podruhe, ted s Q | `CCCA^ 6000` | - |
| cile filtru | `VTFT FE00`, `CVCF FE00` | `FF00` |
| spusteni | `PTRX 523D/1ED7`, `CPF 0000/1ED7` | `ENVVOL`, `ATKHLDV` |

Odtud presne vychazi i census registru z `trace_diff.py`: `CCCA`, `CPF`,
`PTRX` a `DCYSUS` maji u ovladace **dvojnasobek** zapisu, `Z1`/`Z2` 3363
(jednou na notu) proti nasim 32 (jen inicializace).

## Co z hodnot nesedi

`notes_diff.py` na Georgii, 3331 sparovanych not:

| registr | shoda | typicky rozdil |
|---|---|---|
| `FMMOD` | 85,6 % | horni bajt +3 u jedne noty, dolni +8 u 478 not |
| `FM2FRQ2` | 82,1 % | dolni bajt (frekvence LFO2) **krat 2** u 595 not |
| `VTFT` / `CVCF` | 79,9 % | horni bajt -1 u 669 not |
| `CCCA^` | 79,9 % | Q **+1** u tychz 669 not |
| `ATKHLD` | 79,9 % | dolni bajt -1 u tychz 669 not |
| `TREMFRQ` | 75,3 % | frekvence **krat 2** u 824 not, tremolo +35 u 478 |
| `ENVVOL` | 74,7 % | `8000` vs `BFFF` u 844 not |
| `IFATN` | 69,6 % | horni bajt (cutoff) -1 u 669 not |
| `ATKHLDV` | 60,3 % | dolni bajt +2 u 844, -1 u 477 |
| `PEFE` | 57,7 % | dolni bajt +1 u 669, +63 u 629 |

**Nejdulezitejsi nalez:** mezni kmitocet, `Q` a attack modulacni obalky
nesedi na **presne tychz 669 notach** - prekryv skupin je 1,000, ne 0,99.
Tri nezavisle generatory se rozejdou naraz. Adresy vzorku jsou pritom u
rozjetych i shodnych not tytez (zadna adresa neni jen v jedne skupine), takze
to neni jinym nastrojem. Nejpravdepodobnejsi vysvetleni je **jina zona
SoundFontu** - lisi se nam hranice rozsahu velocity, a u not u kraje pak
sahneme do sousedni vrstvy. Pozor, neni to tedy pravidlo 1 vyse (velocity ->
cutoff): to by `Q` ani `ATKHLD` nezmenilo.

Overit se to da tak, ze se pro tech 669 not vyjmenuji zony instrumentu
v `SYNTHGM.SBK` a najde se ta, ze ktere ovladacova trojice
(cutoff, Q, attack) vychazi.

`ENVVOL 8000` vs `BFFF` je na zvuk jedno - bit 15 znamena "bez delay" a
spodnich 15 bitu se pak ignoruje (`ENVVOL_TO_EMU_SAMPLES`). Na shodu stopy
ale ne.

## Stav po srovnani sledu (bod 1 hotovy)

`Synth::NoteOn` ma pro rodinu `win95` ted presne ten sled vyse; `dos` zustal
beze zmeny (ma vlastni vetev). Census po oprave:

| registr | pred | po | ovladac |
|---|---|---|---|
| `DCYSUS` | 3363 | **6691** | 6691 |
| `Z1`, `Z1^`, `Z2`, `Z2^` | 32 | **3363** | 3363 |
| `CCCA`, `CCCA^` | 3365 | **6696** | 6754 |
| `CPF`, `CPF^` | 3365 | **6696** | 6724 |
| `VTFT`, `VTFT^` | 6696 | **10027** | 10055 |
| `PTRX`, `PTRX^` | 4421 | **6697** | 6754 |
| `IP` | 4418 | 4418 | 4418 |

Celkem 159 220 zapisu proti 160 480 u ovladace, tedy do 0,8 %.

### Vedlejsi nalez: pri pitch bendu se PTRX nepise

`IP` melo 4418 zapisu u nas i u ovladace, ale `PTRX` u nas 4421 misto 3365 -
tedy zhruba tisic zapisu navic. Ukazalo se, ze `Synth::RefreshChannel` pri
pitch bendu psal do horni pulky PTRX `pitch << 16`. Jenze **horni pulka PTRX
je linearni prirustek, ne logaritmicke IP** - prepisovalo to tedy spravnou
hodnotu, kterou si cip sam dopocital ze zapisu do IP. Skutecny ovladac na
PTRX pri pitch bendu nesahá vubec. Opraveno.

### Na zvuk to zatim nehnulo

Proti zaznamu skutecneho ovladace na tomtez cipu (`--chip 86box`) zustava
korelace obalky **0,9471** pred i po. Dava to smysl: vetsina tech zapisu
konci ve stejnem stavu registru v ramci jednoho snimku - `Z1`/`Z2` si 86Box
jen uklada, druhy zapis `CCCA` nese finalni hodnotu, `DCYSUSV 00FF` i `0080`
maji oba bit "engine off" a spodni bity stejne prepise spousteci zapis.

Slyset by mel byt jedine `DCYSUS` pri note-offu (uvolneni modulacni obalky),
a ten se neprojevil - v tehle bance je modulacni obalka slaba (`PEFE` byva
0001/0002) a release casto 0x7F, tedy okamzity.

**Zbytek rozdilu je tedy v hodnotach, ne ve sledu.** Dalsi na rade jsou zony
podle velocity (669 not), frekvence LFO krat 2, a pak `PEFE`/`ATKHLDV`/`ENVVOL`.

---

# Prevod SF1 -> registr, kalibrovano na Georgii

Tech 669 not, kde se naraz rozesel mezni kmitocet, `Q` i attack modulacni
obalky, **nebylo jinou zonou ani velocity**. Kdyz se noty roztridi podle
adresy vzorku, vyjde to jednoznacne:

| | nase -> ovladac |
|---|---|
| `Q = 0` (preset "Piano 1") | cutoff 255->255, atkMod 125->125 |
| `Q != 0` (preset "Piano 2") | cutoff 255->**254**, Q 5->**6**, atkMod 127->**126** |

Je to tedy jeden konkretni preset a jeho generatory, ne rozsah kláves ani
sila uderu. `SYNTHGM.SBK`, instrument `piano2`, globalni zona:
`initialFilterFc 127`, `initialFilterQ 50`, `attackModEnv 6`.

## `initialFilterFc`: prosty dvojnasobek

Kalibrace ze ctyr presetu Georgie:

| SF1 | ovladac | |
|---|---|---|
| 52 (`fretlessbs`) | 104 | 52x2 |
| 97 (`jazzgtr`) | 194 | 97x2 |
| 127 (`piano2`) | **254** | 127x2 |
| chybi (`organ3`, `tuba`) | 255 | vychozi z tabulky ovladace |

Drive se pocitalo `v * 255 / 127`, aby 127 davalo 255. **Ta uprava byla
naroubovana na spatne mereni.** Vychazela z presetu 52 `Choir Aahs` v Magic
Carpet 2, kde ovladac zapsal cutoff 255 - jenze `choiraahs` zadny
`initialFilterFc` nema, takze slo o **vychozi hodnotu**, ne o prevod cisla
127. Skutecna 127 se objevila az tady a dala 254.

MINUET tim nijak netrpi: ma cutoff 220 a 178, coz je 110x2 a 89x2.

## `initialFilterQ`: posun o tri bity

| SF1 | ovladac | `v*15/127` (drive) | `v>>3` |
|---|---|---|---|
| 12 | 1 | 1 | 1 |
| 50 | **6** | 5 | 6 |
| 79 | 9 | 9 | 9 |

`lround(v * 15 / 127.0)` sedi na tytez tri body taky - rozliseni by prinesla
nota s `initialFilterQ` **6, 14 nebo 22**, u tech se obe varianty lisi.
V zadne nasi stope zatim takova neni. Zvoleno `>>3`, protoze je to jedina
instrukce a 16bitovy ovladac z roku 1994 by to nejspis udelal tak.

## Vysledek

| registr | pred | po |
|---|---|---|
| `CCCA^` (Q) | 79,9 % | **100 %** |
| `VTFT` | 79,9 % | **100 %** |
| `CVCF` | 79,9 % | **100 %** |
| `DCYSUSV` | 100 % | 100 % |
| `IFATN` | 69,6 % | **89,6 %** |

`IFATN` uz nema chybu v hornim bajtu; zbylych 10 % je **utlum** ve spodnim
(napr. `FF38` proti `FF48`), coz je jina vec a patri k `ATKHLDV`/`ENVVOL`.

Na zvuk to nehnulo - korelace obalky proti zaznamu skutecneho ovladace na
tomtez cipu je 0,9471 pred a 0,9468 po. Dava to smysl: cutoff 255 misto 254
je u filtru dokoran nepostrehnutelny rozdil a `Q` 5 vs 6 je jeden krok
rezonance. Registrove je to ale ted spravne a dalsi opravy uz nestoji na
spatnem zakladu.

## Co zbyva

| registr | shoda | co s tim |
|---|---|---|
| `ATKHLDV` | 60,3 % | ovladac dava 125/126/127, my jen 125/127 - krivka attack neni jen o jednu useknuta, plete se v obou smerech |
| `PEFE` | 57,7 % | +1 u 669 not, +63 u 629 |
| `VTFT^`, `CVCF^`, `ENVVOL` | 74,7 % | tytez **844 not**; `ENVVOL 8000` vs `BFFF` a nenulovy cilovy objem |
| `TREMFRQ`, `FM2FRQ2` | 75-82 % | frekvence LFO krat 2 |
| `FMMOD` | 85,6 % | dolni bajt +8 u 478 not |
| `IP` | 98,0 % | +-1 u 67 not |

`ATKHLDV`, `ENVVOL` a `VTFT^` se lisi na tychz 844 notach, takze to nejspis
bude jedna pricina - podobne jako tady u `piano2`.

---

# Attack a delay registry, kalibrovano na Georgii

Dalsi skupina, tentokrat **844 not**: `ATKHLDV`, `ENVVOL`, `VTFT^` a `CVCF^`
se lisily na tychz notach (`ENVVOL` x `VTFT^` Jaccard 1,000). Rozklad podle
vzorku a `Q` ukazal tri chovani ovladace:

| | attack | `ENVVOL` | `VTFT^` |
|---|---|---|---|
| A - generator `attackVolEnv` **chybi** | 0x7D | 0x8000 | 0 |
| B - `attackVolEnv = 0` | **0x7F** | **0xBFFF** | **cilovy objem** |
| C - `attackVolEnv = 6` | **0x7E** | 0x8000 | 0 |

Klic k tomu byl preset **Honky-Tonk (prog 3), ktery ma dve vrstvy**:
`honkytonk` s `attackVolEnv 6` a `shonkytonk` s `attackVolEnv 0`. Proto se
noty s tymz vzorkem delily presne na pul (114 a 114) - nejsou to dve zony
podle velocity, jsou to dva hlasy na jednu notu. Do skupiny B patri jeste
bicí (`snare24`, `bd15`, `paisteping`, `rideping`, `floortombrite`).

## Co bylo spatne

1. **`AttackRateFromMs` vracelo `r` misto `r-1`.** Ovladac vybira - stejne
   jako u decay - polozku, jejiz cas je *delsi nebo rovny* zadanemu. Tabulka
   ma u 0x7F cas 5,99 ms a u 0x7E 6,19 ms, takze 6 ms patri 0x7E, ne 0x7F.
2. **Chybejici generator splyval s nulovym casem.** `timeMs(..., 0.0)` vrati
   nulu v obou pripadech, a funkce na nulu vracela 0x7D. Spravne je: chybi
   -> 0x7D (vychozi z tabulky ovladace), `= 0` -> 0x7F.
3. **Delay registr pri okamzitem attacku.** Kdyz attack vyjde 0x7F, ovladac
   zapise do `ENVVOL` (resp. `ENVVAL`) **0xBFFF** misto 0x8000. Odpovida to
   vetvi na `SBAWE32.DRV` 0x0206, ktera je v disassembly vedena jako "bicí
   kanal" a s hodnotou 0xB7FF - merenim vychazi **0xBFFF** a plati i mimo
   kanal 9. Na zvuk to nema vliv (bit 15 = bez prodlevy, spodnich 15 bitu se
   ignoruje), ale ve stope to je.

## Vysledek

| registr | pred | po |
|---|---|---|
| `ATKHLDV` | 60,3 % | **100 %** |
| `ATKHLD` | 79,9 % | **100 %** |
| `ENVVOL` | 74,7 % | **100 %** |
| `ENVVAL` | 100 % | 100 % |

Zvuk se nepohnul (0,9468). Vsechny tri opravy jsou registrove, ne zvukove:
attack 0x7D vs 0x7F je rozdil 5,99 az 6,19 ms proti okamziku a `ENVVOL`
spodni bity cip ignoruje.

## Otevrene: cilovy objem `VTFT^` / `CVCF^`

Ve skupine B ovladac jeste zapise do horni pulky `VTFT` i `CVCF` **cilovy
objem** (obe stejnou hodnotu, 844 z 844), takze hlas zacne rovnou nahlas
misto aby se k tomu doklouzal pres `emu8k_vol_slide`. Neni to
`attentable[atten]` z 86Boxu - hodnota ovladace je vzdy mensi:

| atten | ovladac | `attentable` |
|---|---|---|
| 114 | 0x01AE | 0x01DD |
| 104 | 0x0297 | 0x02DF |
| 80 | 0x0756 | 0x0818 |

Je to deterministicka funkce `atten` (59 ruznych hodnot, zadny rozpor) a
proklad da `60252 * 0,957567^atten`, tedy krok **0,3766 dB** - prakticky
tychz 0,375 dB jako `attentable`, jen zacatek je jinde (60252 misto 65535).
Zadny jednoduchy tvar ale nesedi presne na vsech 59 bodech:
`attentable[a+1]`, `attentable[a+2]`, `65535*10^(-0,375a/20)` ani rekurentni
deleni od 60252 - u nizkych utlumu se lisi o 0,1 %.

Nejpravdepodobnejsi vysvetleni: ovladac si utlum drzi v jemnejsich
jednotkach, nez je 0,375 dB krok registru `IFATN`, a amplitudu pocita
z toho. Pak to funkce zaokrouhleneho bajtu byt nemuze a dohledat se to musi
v tabulce v `SBAWE.VXD`. Do te doby to zustava neimplementovane.

---

# Ctvrte kolo: modulace, utlum bicich, smycky, delay

## 1. Hloubky modulace a frekvence LFO se v SF1 **zdvojuji**

Zmereno na 3331 notach Georgie, **bez jedine vyjimky** (kazdy nesouhlas byl
presne dvojnasobek, zadny "jiny"):

| generator | registr | nase -> ovladac | not |
|---|---|---|---|
| `modEnvToFilterFc` | `PEFE` dolni | 3F -> 7E, 01 -> 02 | 1410 |
| `modLfoToFilterFc` | `FMMOD` dolni | 08 -> 10 | 478 |
| `modLfoToVolume` | `TREMFRQ` horni | 23 -> 46 | 712 |
| `freqModLFO` | `TREMFRQ` dolni | 12 -> 24 | 824 |
| `freqVibLFO` | `FM2FRQ2` dolni | 2C -> 58 | 595 |

Vysky se naopak **nezdvojuji**: u `vibLfoToPitch` sedi 03 a FF na 595 notach
a u `modLfoToPitch` hodnota 01 na 111 notach - nasobeni by tam shodu rozbilo.
Delici cara je tedy vyska proti filtru/hlasitosti, ne SF1 proti SF2.

U `freqModLFO` plati zdvojeni jen kdyz generator existuje; kdyz chybi, jde do
registru rovnou 128 (ne 64x2).

## 2. Utlum bicich - soucet se musi delat az v jednotkach registru

Zbylych 345 nesedicich `IFATN` bylo **cele na bicich**. Preset "Standard"
(banka 128) ma utlum na obou urovnich: preset zona 127 a kazda klavesova zona
instrumentu svuj (121 u `snare24` na klavese 38, 112 na 40, ...).

`AddFrom` scitalo **surove SF1 hodnoty** (121 + 127 = 248) a `127 - 248` pak
spadlo na nulu. Spravne prispiva kazda uroven `127 - v` jednotkami registru
a ty se scitaji. Merenim to sedi presne: ovladac mel vzdy o `127 - utlum zony`
vic nez my (zona 121 -> +6, 112 -> +15, ...).

Melodicke presety to nikdy neukazaly, protoze jejich zony instrumentu utlum
nemaji. Region proto vede `sf1AttenUnits` zvlast od slozeneho `GenSet`.

## 3. Offsety smycky se v SF1 neaplikovaly

`PSST` a `CSL` nesedily na 232 notach, vsechny na vzorku `organwave`
(preset Organ 3), jehoz zona ma `startloopAddrsOffset -1` a
`endloopAddrsOffset -1`. Vetev SF1 je ignorovala - pocitala jen se surovymi
adresami z `shdr`. Druhy organovy vzorek `organwavea3` ty generatory nema,
proto se to projevilo jen u jednoho.

## 4. Krok delay registru je 32 vzorku, ne 0,725 ms

`LFO1VAL` nesedelo o jeden krok u dvou presetu:

| generator | ovladac | my (drive) |
|---|---|---|
| `delayModLFO 120` (`jazzgtr`) | 165 | 166 |
| `delayModLFO 260` (`tuba`) | 358 | 359 |

Krok je `(0x8000 - v) << 5`, tedy **32 vzorku na 44100 Hz = 0,72562 ms**,
ne zaokrouhlenych 0,725. S presnym krokem sedi obe hodnoty. Utinani je tam
proto, ze ovladac jinde deli celociselne pres `idiv` (viz `HoldFromMs`);
rozlisit utinani od zaokrouhleni tyhle dve hodnoty neumozni.

## Stav

**26 registru na 100 %** z 3331 sparovanych not Georgie. Zbyva:

| registr | shoda | co to je |
|---|---|---|
| `FMMOD` | 99,9 % | dve noty |
| `PTRX^`, `CPF^` | 99,5 % | odvozene z `IP` |
| `IP` | 98,0 % | vyska +-1 u 67 not |
| `VTFT^`, `CVCF^` | 74,7 % | cilovy objem, krivka nedohledana (viz vyse) |

## A ted uz to je slyset

Prvni tri kola zvukem nehnula. Tohle ano - hlavne diky utlumu bicich, ktery
delal az 16 jednotek, tedy 6 dB navic na cinelech.

Proti zaznamu skutecneho ovladace na tomtez cipu (`--chip 86box`):

| pasmo Hz | pred (kolo 3) | po |
|---|---|---|
| 1600-3200 | -0,6 | **-0,1** |
| 3200-6400 | -0,7 | **-0,2** |
| 6400-12800 | +1,9 | **-0,3** |
| 12800-22050 | +3,0 | **+0,6** |

Do 12,8 kHz je to ted **do 0,3 dB** pres cele spektrum. Korelace obalky
0,9471 -> 0,9497.

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

---

# Vyska tonu: `sub_192E` a kde vznika rozdil +-1

Dohledano pres CPU stopu 86Boxu a disassembly, ne pres registry.

Ovladac stavi `IP` ve dvou krocich. Nejdriv secte vsechno **v centech**
(`SBAWE.VXD` 0x1DBC..0x1DEB) a prozene to prevodem `sub_192E` (0x192E):

```
esi = centy + 0x41A0        ; 16800, aby bylo vse kladne
edi = esi / 0x4B0           ; 1200 -> oktava, orez na 15
edx = esi % 0x4B0           ; zbytek v centech
IP  = (edi << 12) | (edx*3 + (edx*31)/75)
```

`3 + 31/75` je presne `4096/1200`, takze vzorec sam zkresleni nema. Prepsali
jsme ho 1:1 (`PitchFromCents` v SoundFont.cpp) misto drivejsiho
`kPitchUnity + log2(...) * 4096` v doublech.

**Na tech 67 notach to ale nepomohlo - a to je ten nalez.** Hodnoty, ktere
ovladac zapsal (`DC82`, `D72D`), totiz **v obrazu `sub_192E` vubec nejsou** -
zadny celociselny vstup v centech je nedava, funkce skace po 3 az 4. Nase
`DC81` a `D72C` v obrazu jsou. Rozdil tedy nevznika v prevodu, ale az **po**
nem, v druhem kroku (0x1E9C):

```
ecx = movsx [esi+0x0e]     ; esi = struktura KANALU ([ebp-0xc])
eax = movzx [edx+0x0e]     ; vysledek sub_192E ulozeny ve slotu hlasu
ecx += eax
ecx += [esi+0x14]
IP = clamp(ecx, 0, 0xFFFF)
```

Ke spocitane vysce se tedy jeste pricitaji **dve kanalove slozky primo
v jednotkach IP**. Ty nam chybi a delaji tech +1 (tuba 30 not, baskytara 7,
zbytek jsou noty kytary s ohybem).

> Pozor na zamenu struktur: `esi` je tady **kanal** (`[ebp-0xc]`), kdezto
> `ebx` v druhe polovine rutiny je blok parametru hlasu. Nejdriv jsem cetl
> `[0x0e]` a `[0x14]` z bloku u `ebx`, vyslo to nula a vypadalo to, ze
> kanalove slozky zadne nejsou. Byla to spatna struktura.

Dalsi krok: pridat do `awe32_trace.c` okno i na tuhle strukturu, jinak se
obe slozky dohledat nedaji.

## Rozdil +-1 v IP: pitch bend, ne prevod vysky

Prevod `sub_192E` v tom byl nevinne. Rozhodl az test, jestli jsou hodnoty,
ktere ovladac zapsal, v jeho obrazu vubec dosazitelne:

```
DC81 v obrazu sub_192E: True     <- nase
DC82 v obrazu sub_192E: False    <- ovladacova
```

Funkce skace po 3 az 4 jednotkach, takze `DC82` z ni **zadny celociselny**
**vstup v centech nedava**. Rozdil tedy vznikal az pri scitani za ni.

Kanaly s rozdilem v `IP` byly 1, 3 a 7 - a to jsou presne **jedine tri**
kanaly Georgie s pitch bendem (177, 231 a 437 udalosti). U ch3 je nejcastejsi
pripad ohyb -8192 s rozdilem +1, 25x.

Pri plnem ohybu dolu a rozsahu 2 pultony to je **-682,667** jednotek IP.
Ovladac **utina k nule** -> -682, my jsme zaokrouhlovali -> -683. Utinani je
to same jako vsude jinde, kde ovladac deli pres `idiv`.

Vysledek: `IP` 98,0 % -> **99,1 %**. Zbylych 30 not je **vsech na ch7**,
jedinem kanalu s RPN a nejrychlejsim ohybem; rozdily jsou velke a rozhazene
(+267, +120, -1970, ...) a chodi po dvojicich, tedy dva hlasy na notu. To uz
neni chyba prevodu, ale to, ze nas sekvencer trefi notu do jineho mista
ohyboveho nabehu nez MPU-401 v guestovi.

### Slepa ulicka po ceste

CC1 (modulacni kolecko) jede taky jen na ch1 a ch3, takze to vypadalo jako
vysvetleni. Neni: **vsech 37 not s +1 ma CC1 nulove** a jedina nota
s CC1 > 0 sedi. Korelace kanalu jeste neni pricina.

## Je 86Box pri mereni spolehlivy?

Obava, ze emulator pri nestihani pousti do stopy nesmysly, je namiste, ale
pro tahle mereni se nepotvrdila. Dva nezavisle behy Georgie z ruznych dnu:

| | |
|---|---|
| pocet not | 3331 a 3331 |
| registry, ktere se mezi behy lisi | **zadny** |
| posun absolutniho casu | 61892 snimku (jiny okamzik bootu) |
| rozjezd relativnich rozestupu | max **135 snimku za 150 s**, tedy 3 ms |

Guest tedy bezi deterministicky - 86Box pocita v emulovanem case, a kdyz
host nestiha, jen se to zpomali v realnem case. Kdyby stopa vznikala
poskozena, projevilo by se to jako nahodne rozdily, ne jako systematicke.

## Zbylych 30 hlasu na ch7: rozsah ohybu, ne casovani

Vypadalo to na casovani (nota trefena do jineho mista ohyboveho nabehu), ale
neni to tak. Podil mezi posunem, ktery musel ovladac pouzit, a tim nasim je
porad stejny:

```
587/320 = 1,834    264/144 = 1,833    1174/640 = 1,834
532/290 = 1,834    147/80  = 1,838    -4334/-2364 = 1,833
```

1,8333 = 22/12. Kdyz se pro kazdou notu dopocita, jaky rozsah by presne
sedel, vyjde **22 pultonu** (u 10 z 15 not presne, u zbytku nejednoznacne,
protoze se ohyb prave menil).

MIDI pritom rozsah nastavuje jasne - `RPN 0/0`, `DataEntry MSB = 12`, pak
`RPN 127/127` (odvoleni). My tedy pouzivame 12 spravne, ovladac se chova jako
22, tedy **o 10 pultonu vic**.

Lisi se presne tech 15 not (30 hlasu, dve vrstvy na notu) - jsou to jedine
noty ch7 s **nenulovym ohybem v okamziku note-onu**; kde je ohyb nula, je
rozsah jedno.

**Neopravovat nasilim.** Pricitat natvrdo 10 by bylo presne to fitovani na
jedno mereni, na ktere uz jsme dvakrat doplatili. Rozdil 22 = 12 + 10 vypada
jako by ovladac k rozsahu **pricital** misto aby ho nastavoval, nebo mel
vychozi 10. Dohledat to jde v jeho obsluze RPN / data entry.

Poznamka: `ch2` a `ch6` posilaji `DataEntry MSB` (2 resp. 12) **bez toho, aby**
**predtim vybraly RPN**. Zadny ohyb na nich neni, takze se to neprojevi, ale
pri hledani obsluhy RPN je to dobre mit na pameti.

### Obsluha RPN a pitch bendu v SBAWE.VXD

Cesta k ni: dispatcher MIDI je na `0x694` (`and eax,0xf0`, pak vetve pro
0x80..0xE0). Control change (0xB0) vola `0x38F5`, pitch bend (0xE0) vola
`0x3D3B`. Kanalove struktury maji krok **0x24** a lezi na `edi + ch*0x24`.

**Data entry MSB (`0x35DD`)** - pri RPN 0 ulozi hodnotu rovnou jako bajt:

```
[esi+0x45e] == 0x100 ? RPN : NRPN
[esi+0x460] == 0  -> [esi+0x44f] = hodnota      ; rozsah ohybu v pultonech
[esi+0x460] == 2  -> [esi+0x454] = clamp(v-0x40,-24,24) * 100   ; hrube ladeni
[esi+0x460] == 1  -> [esi+0x452] = ((v<<7|lsb) - 0x2000)*100 >> 13  ; jemne
```

**Pitch bend (`0x3D3B`)**:

```
ecx = ((MSB - 0x40) << 7) + LSB        ; ohyb -8192..8191
eax = byte [ebx+0x44f]                 ; rozsah; kdyz 0, pouzije se 2
eax = (eax * ecx) / 24                 ; idiv, tedy utinani
[ebx+0x456] = eax                      ; posun v jednotkach IP
...
IP = clamp([ebx+0x450] + [esi+0x0e] + posun, 0, 0xFFFF)
```

`(ohyb * rozsah) / 24` je **presne to, co pocitame my** - nas
`(bend/8192) * rozsah * 4096/12` je totez a od minule uz taky utina.
Vzorec tedy sedi a rozdil musi byt v **hodnote** rozsahu (`[ebx+0x44f]`),
nebo v ohybu platnem v ten okamzik.

Rozliseni ze stopy nejde: `ohyb 640, rozsah 22` da 586, ale ovladac ukazuje
587 - a `ohyb 641, rozsah 22` uz 587 da taky. Rozsah a okamzik ohybu jsou
z portove stopy nerozlisitelne.

**Dalsi krok je hacek na kanalovou strukturu.** Staci do `awe32_trace.c`
pridat okno na `EDI + 0x440` delky 0x1A0 (pokryje `+0x44f`, `+0x450` a
`+0x456` pro vsech 16 kanalu) a vypisovat ho jen u zapisu do IP, aby stopa
nenarostla. Pak je videt primo, jaky rozsah ovladac drzi.

### Oprava: rozsah je 12, rozdil je casovani

Vyse uvedeny zaver, ze se ovladac chova jako rozsah **22 pultonu**, je
**spatne**. Vysel z pomeru 1,833 mezi "posunem, ktery musel ovladac pouzit"
a nasim - jenze ten posun jsem dopocitaval z rozdilu IP proti zakladu, ktery
jsem odvodil z **nasi** noty. U ch7 ma preset dve vrstvy s ruznou vyskou,
takze staci prohodit vrstvy a vyjde konzistentni, ale nesmyslny pomer.

Rozhodl az hacek na kanalove struktury (`AWE32_TRACE_CH_OFF/LEN`). Tabulka
kanalu je na **`EDI + 0x44F`, krok 0x24**; pole v ni:

```
+0x00  bajt   rozsah pitch bendu       (ch0..ch6 = 2, ch7 = 12)
+0x01  word   ladeni kanalu            (vsude 0)
+0x07  dword  spocteny posun ohybu
```

Namereno na 4418 zapisech do IP:

| rozsah | ladeni | posun | pocet |
|---|---|---|---|
| 12 | 0 | 0 | 2169 |
| 12 | 0 | -1706 | 966 |
| 12 | 0 | 320 | 22 |
| 12 | 0 | 5 | 42 |

`posun = ohyb * rozsah / 24`, tedy pro ohyb 640 vychazi 320 - **presne to,**
**co pocitame my**. Rozsah, ladeni i vzorec se shoduji.

Zbylych 30 hlasu ch7 se tedy lisi tim, **jaky ohyb platil v okamziku**
**note-onu** - nas sekvencer trefi notu do jineho mista nabehu nez MPU-401
v guestovi. Neni to chyba prevodu a bez presneho napodobeni casovani
dispatche MIDI to spravit nejde.

Poucení: nedopocitavat velicinu z rozdilu vysledku, kdyz jde primo zmerit.
Postavil jsem na tom dva zavery a oba byly spatne.

---

# JUMP: druha skladba, ktera to overila

`JUMP_BK.MID` ma 15 kanalu, 3923 not a 5077 hlasu (Georgia 8 / 2366 / 3331),
takze prochazi mnohem vic presetu. Sedm oprav odvozenych z Georgie na nem
plati beze zmeny - **28 registru zustalo na 100 %**. Odhalil ale jednu vec
navic.

## Tabulka casu attacku je v ovladaci na 0x09118

`ATKHLD` sedelo jen na 80,1 % (1008 hlasu). Mezivysledek `modAttack` ukazal,
ze je to primo v prevodu, a rozlozeni melo jen tri hodnoty:

| nase | ovladac | not |
|---|---|---|
| 9 | **10** | 504 |
| 98 | **100** | 504 |
| 125 | 125 | 4069 |

Jsou to presety `polysynth` (`attackModEnv 20`) a `spolysynth` (`1270`),
vrstvena dvojice. Tabulka casu je v `SBAWE.VXD` na offsetu **0x09118** -
128 polozek po 16 bitech v ms, nalezena jako jedine misto v binarce, ktere
vyhovuje trem znamym bodum:

```
idx  1..15:  11878 5939 3959 2970 2376 1980 1697 1485 1320 1188 1080 990 914 848 792
idx 95..105: 24 23 22 21 20 19 18 17 16 15 15
idx 120..127: 8 7 7 7 7 6 6 6
```

Nase `11878 / RateDivisor(r-1)` ji po zaokrouhleni reprodukuje **na vsech**
**127 polozkach**, takze ji netreba opisovat. Chyba byla ve **vyberu**:

| | drive | spravne |
|---|---|---|
| porovnava se s | presnym casem | **zaokrouhlenym** |
| vraci se | `r-1` | **`r`** |
| nulovy cas | 0x7D | **0x7F** |
| propadnuti cyklem | 0x7F | **0x7E** |

Sedi na ctyri body ze dvou skladeb: 0 ms -> 0x7F, 6 ms -> 0x7E, 20 ms -> 100,
1270 ms -> 10. Na Georgii se to neprojevilo, protoze jeji presety doprostred
tabulky vubec nesahnou - hlasitostni obalka tam nabyva jen 125, 126 a 127.
**To je presne ten duvod, proc kalibrovat na vic nez jedne skladbe.**

## Stav

| | Georgia | JUMP |
|---|---|---|
| registru na 100 % | 28 | **29** |
| mezivysledku | 8/8 | 8/8 |
| nas cip vs `emu8k_ref.exe` | 0 rozdilu | **0 rozdilu z 7 524 396** |

Zbyvaji `IP`, `PTRX^` a `CPF^` (99,6 %, 21 hlasu) - vsechny tri jsou odvozene
z jedne veliciny a je to **jitter dispatche**, ne prevod.

## Hodiny guesta: zmereno, ale nenapodobujeme

Porovnani casu not proti ovladaci (3331 not Georgie) dalo linearni drift
**-1,527e-4 · t**, tedy guest hraje o 0,015271 % rychleji. Sedi to na PIT
delicku: Windows programuji milisekundovy timer hodnotou 1193 misto 1193,182,
takze jeden "milisekundovy" tik trva 0,99984747 ms - predpoved 0,015253 %.
Neni to tedy fitovana konstanta, ale hardware.

Zkusili jsme to do sekvenceru zavest a **na registrovem proudu to nezmenilo**
**nic** - parovani je podle poradi a rovnomerna zmena rychlosti preskaluje
noty i ohyby stejne. Vraceno: prehravac by kvuli tomu hral rychleji, nez MIDI
predepisuje, a nic by to nevyneslo.

Zbytkovy rozptyl po odecteni driftu je **0,89 ms** (max 3,44 ms) - to je ten
jitter, ktery zbylych 21 hlasu zpusobuje. Deterministicky se reprodukovat
neda.

---

# RELAX: treti skladba

6523 hlasu, 15 kanalu, bank select `CC0 = 1` a `8` (banky, ktere v
`SYNTHGM.SBK` neexistuji - fallback na banku 0). Pozor: **`RELAX.SBK` v
guestovi nahrana neni** - ve stope jsou jen 3 zapisy do `SMLD`, u banky
6,4 MB by jich byly miliony. Nas render ji proto taky nesmi mit, jinak by se
porovnavaly dve ruzne konfigurace.

## Modulacni kolecko (CC1)

`FMMOD` horni bajt mel u ovladace 01, 02 a 04 tam, kde jsme meli nulu.
Obsluha CC1 je na `0x34A4`:

```
mov ecx, 0x1E / div ecx    ; CC1 / 30 -> 0..4
add ebp, edx               ; + hloubka z patche + kanalova slozka
cmp ebp, 0x7F / shl ebp, 8 ; orez a do horniho bajtu FMMOD
```

Doplneno. Zvedlo to i **Georgii z 28 na 29** - jeji dve zbyle `FMMOD` byly
z tehoz duvodu.

## Frekvence LFO preteka bajtem

`freqVibLFO 132` -> 264 -> ovladac zapise **0x08**, my jsme oriznuli na 0xFF.
Oprava: `(v * 2) & 0xFF` misto `clamp`.

## Otevrene: konstanta prevodu delay

`ENVVAL` nesedi u 18 hlasu (`7F40` proti `7F3F`). Neni to o rezimu
zaokrouhleni: zaokrouhlovani ho spravi, ale rozbije `LFO1VAL` a `ENVVOL`
(383 hlasu). Z namerenych bodu vychazi, ze pocet kroku na milisekundu musi
lezet v **<1,378571; 1,380769)**, kdezto nase fyzikalne odvozena
`44100/32000 = 1,378125` je **tesne pod** tim intervalem. Kandidat je
`1379/1000`.

Ovladac ma na to **jednu spolecnou rutinu** volanou s cislem generatoru:

```
push 0x15 (21 delayModLFO)  -> [edi+0x2a]  LFO1VAL
push 0x17 (23 delayVibLFO)  -> [edi+0x2e]  LFO2VAL
push 0x19 (25 delayModEnv)  -> [edi+0x32]  ENVVAL
push 0x21 (33 delayVolEnv)  -> [edi+0x42]  ENVVOL
push 0x1a (26 attackModEnv) -> [edi+0x34]
push 0x22 (34 attackVolEnv) -> [edi+0x44]
call 0x3b51
```

**Past:** cil `0x3B51` lezi uvnitr funkce zacinajici na `0x3AFE`, takze
lineárni disassembly ho nerozplete - `le_disasm.py` neaplikuje fixupy LE
souboru. Az se to spravi, bude v te rutine cela prevodni tabulka pro vsechny
generatory naraz, tedy i ta konstanta.

## Stav po trech skladbach

| | Georgia | JUMP | RELAX |
|---|---|---|---|
| hlasu | 3331 | 5077 | 6523 |
| registru na 100 % | **29** | **29** | **28** |

---

# Prevodni rutina generatoru: vytazena z pameti guesta

Staticky disassembler na ni nestacil. Volani na `+0x2885` ma v souboru
`rel32 = 0x000012C7`, ale **v pameti 0x001A06CB** - fixup ho posila uplne
jinam, mimo objekt 1. Proto cil `0x3B51` vychazel uvnitr jine funkce.

Reseni: vypsat kod **z pameti guesta**, kde uz je zavedeny a slinkovany.
`awe32_trace.c` to umi pres `AWE32_TRACE_CODE_LEN` / `_BACK` / `_MIN`
a spusti se **az u zapisu do DCYSUSV** (spusteni noty) - prvni pristup na
porty dela jiny modul a VxD se pri kazdem bootu nahraje jinam.

Ulozeno v `SoundBlaster AWE32/runtime-dumps/` i s `.json` (zaklad objektu,
EIP, delka).

## Co v te rutine je

Skokova tabulka podle cisla generatoru, kazda vetev pocita v **timecents**
v pevne radove carce 16.16:

```
cmp eax, 0xFFFFD120   ; <= -12000 -> 0x8000 (bez delay)
cmp eax, 0x156C       ; >= 5484   -> 0
add eax, 0x30E4       ; + 12516
mov ecx, 0x4B0        ; 1200
shl eax, 0x10 / idiv ecx
... (1 + frac) << intpart ...
sub esi, edi          ; 0x8000 - vysledek
```

Slozenim `ms -> timecents -> 2^x` vypadne linearni cinitel
**2^(12516/1200)/1000 = 1,379567**. Nezavisle odvozeny interval z mereni byl
<1,378571; 1,380769) - konstanta z kodu do nej padne, nas drivejsi odhad
`44100/32000 = 1,378125` ne.

## Linearni nahrada nestaci

| | ovladac | my (1,378125) | smer |
|---|---|---|---|
| RELAX `ENVVAL` | 193 kroku | 192 | potrebuje **vetsi** cinitel |
| JUMP `ENVVAL` | 606 kroku | 607 | potrebuje **mensi** |

Dva body tahnou opacne, takze zadny linearni cinitel oba netrefi. To je
dukaz, ze prevod je opravdu exponencialni. Konstanta z kodu je v repu
(je doloziltelna), ale sama o sobe jen presouva chybu z RELAXu na JUMP:
RELAX 28 -> 29, JUMP 29 -> 28.

Dotahnout to znamena prepsat celou tu rutinu vcetne kroku
`ms -> timecents`, ktery zatim nemame nalezeny - rutina uz timecents dostava
na vstupu.

## Vyreseno: krok `ms -> timecents` se zaokrouhluje **dolu**

Chybejici krok se nasel a prodleva obalky uz sedi na obou skladbach naraz.
Nehadalo se - zmerilo se to.

### Instrukcni stopa

Prevod probiha **pred** portovymi zapisy noty, takze rozsahem adres se
chytit neda. Tracer proto umi `AWE32_TRACE_INSN_AFTER_NOTE=1`: instrukcni
zaznam se odjisti u prvniho note-onu a zachyti zpracovani noty dalsi.

    AWE32_BUILD=build86box_int      # nutne, dynarec hook mine
    AWE32_TRACE_INSN=1
    AWE32_TRACE_INSN_AFTER_NOTE=1

Poradi registru na radku `I`: `EIP opcode EAX EBX ECX EDX ESI EDI EBP ESP`
(overeno na `POP ESI`, `POP EBP` a posunu `ESP`).

**Ovladac se pri kazdem bootu nahraje na stejnou adresu.** Vypis kodu
`SBAWE.VXD.obj1.noteon.mem` (zaklad 0xC0FF7BE0) ma `eip_pri_vypisu`
0xC0FF9BE0 - presne tu adresu, kde se stopa odjistila v uplne jinem behu.
Vypis je tedy pouzitelny opakovane a cile volani v nem sedi
(`call 0xc0ff9bb1` souhlasi se stopou).

### Mapa note-onu

| adresa | co dela |
|---|---|
| `C0FFA2AA` | obsluha note-onu, `eax` = cislo noty, `ebx` = velocity |
| `C0FF9C68` / `C0FFA0FF` | prideleni hlasu, vraci jeho cislo |
| `C0FFAADA` | vypocet vysky, vraci `IP` |
| `C0FF9BB1` | zapis registru: `eax` = ukazatel, hodnota v `ecx` |
| `C0FF9C1B` | zapis 32bitove dvojice na 0x620/0x622 |
| `C0FFB2B9` | zapis `ENVVAL` |
| `C0FFB3AF` | zapis `ENVVOL` |

### Vetev 0xBFFF - potvrzena z kodu

    C0FFB348  cmp word ptr [ebx + 0x44], 0x7f    ; volAttack
    C0FFB34D  jne C0FFB3A0
    C0FFB34F  cmp word ptr [ebx + 0x42], 0x8000  ; envvolDelay, **bez znamenka**
    C0FFB355  jb  C0FFB3A0
    C0FFB357  push 0xbfff                        ; ENVVOL = 0xBFFF

Modulacni obalka ma tutez dvojici na `C0FFB245` s poli `0x34` a `0x32`.
Nase `volInstant` / `modInstant` v `Synth.cpp` sedi na podminku presne.

### Cilovy objem - potvrzena tabulka

    C0FFB36B  movsx eax, word ptr [ebx + 0x26]   ; atten
              cdq / xor / sub                    ; |atten|
              and eax, 0xf
              xor / sub                          ; zpet se znamenkem
    C0FFB382  mov si, word ptr [edx*2 - 0x3efffe44]   ; tabulka na 0xC10001BC
    C0FFB38A  cdq / and edx, 0xf / add / sar eax, 4   ; deleni 16 k nule
    C0FFB395  shr si, cl

To je presne `Awe32Curves::VolumeTarget`. Jediny nedodelek: pro **zaporny**
atten ovladac bere `|atten| & 15` a deli k nule, my mame `a & 15` a `a >> 4`.
Pro atten >= 0 je to totozne; jestli zaporny atten vubec nastava, zmereno
neni.

### Kde se prevod **nedeje**

Ve vypisu kodu (28 KB kolem note-onu) neni jediny zapis na `+0x32` ani
`+0x42`. Blok u `EBX` se plni hromadnou kopii, takze prevod `ms -> registr`
probehne uz **pri nacitani banky**, ne pri note. Proto ho hledani kolem
note-onu nemohlo najit.

### Namerene dvojice

Registr nese `0x8000 - kroky`. Ze stop:

| skladba | pole | kroku ovladac | kroku my (drive) |
|---|---|---|---|
| RELAX | `ENVVOL` | 27 | 27 |
| RELAX | `ENVVAL` | 193 | 193 |
| JUMP | `ENVVAL` | **606** | 607 |

`SYNTHGM.SBK` obsahuje jen sest hodnot prodlevy: `delayModEnv` 10, 140, 440
a 710 ms, `delayVolEnv` 20 a 40 ms. Rozhoduje jedina z nich, **440 ms**:

    1200*log2(0,44) = -1421,31
    dolu   -> -1422 -> 2^((12516-1422)/1200) = 606,65 -> 606   ovladac
    k nule -> -1421 -> 2^((12516-1421)/1200) = 607,00 -> 607   my drive

Zaokrouhluje se tedy **dolu**. Ostatnich pet hodnot vychazi stejne tak i tak,
takze na nich to poznat neslo.

### Proc ne linearni konstanta

Krok 725 us (`floor(ms*1000/725)`) trefi vsech sest hodnot v bance take - ale
proti exponenciale se lisi u **19 400 z 24 000** celych milisekund. Na
SYNTHGM.SBK se oba shodnou jen nahodou, protoze bance staci sest hodnot. Na
jine bance by 725 us selhalo, proto je v kodu exponenciala.

Zbyva doplnit presnou 16.16 verzi `2^x` pres mantisu a posun; ta cast kodu
bezi az pri nacitani banky a ve vypisu pameti zatim neni. `exp2` se od ni
muze lisit o jednicku na hranach.

### Vysledek

| uroven | pred | po |
|---|---|---|
| `mezivysledky.jump` | 7/8 | **8/8** |
| `registry.jump` | 28/32 | **29/32** |
| `mezivysledky.georgia` | 8/8 | 8/8 |
| `registry.relax` | 29/32 | 29/32 |
| `cip.georgia` | 0 rozdilu | 0 rozdilu |

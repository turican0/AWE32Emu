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

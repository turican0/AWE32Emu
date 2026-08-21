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

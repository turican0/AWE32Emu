# SoundFont 1.0 (`.SBK`) - jak ho cist pro EMU8000

Odvozeno z `../AWE32EmuData/sbk/BULLFROG.SBK` (banka hry) v kombinaci s note-on rutinou
v `SBAWE32.DRV` (offset `0x038A` a dal) a s Programmer's Guide.

## Rozdily proti SF2

| | SF1.0 | SF2 |
|---|---|---|
| `ifil` | 1.x | 2.x |
| `shdr` | **16 B** = 4 dwordy (start, end, loopStart, loopEnd) | 46 B vcetne jmena, sample rate, root key |
| jmena vzorku | samostatny chunk **`snam`**, 20 B na vzorek | uvnitr `shdr` |
| jednotky generatoru | **primo registry EMU8000**, casy v **ms** | normalizovane (timecents, centibely, centy) |

## Vzorky v ROM vs v bance

Jmeno vzorku zacinajici `*` znamena **vzorek z ROM karty**, ne z chunku
`smpl` banky. `INFO/irom` rika, jaka ROM se ocekava (`1MGM`).

Adresy v `shdr` jsou uz **hotove adresy pro EMU8000**, vcetne posunu
zvukoveho fondu v ROM a vcetne korekce na interpolator. Overeno proti
`../AWE32EmuData/rom/1mgm.sf2`, kde tytez vzorky maji indexy v ramci `smpl`:

| pole | posun SBK vuci indexu v 1mgm.sf2 |
|---|---|
| start | +494 |
| end | +494 |
| loopStart | +493 |
| loopEnd | +492 |

Zvukovy fond v `awe32.raw` zacina na slove **495** (byte `0x3DE`, overeno
bajtovym porovnanim). Rozdil oproti +494 je prave ta korekce "-1", kterou
popisuje Programmer's Guide ("actual audio location is one word higher").
Loop body maji jeste dalsi -1 / -2.

**Prakticky dusledek:** u `.SBK` se hodnoty ze `shdr` daji zapsat do
CCCA/PSST/CSL primo, bez prepoctu. U `.sf2` se musi pricist 495 a odecist
korekce.

Vzorky bez `*` (tady `LOOP2`, `REV2`) jsou indexy do `smpl` banky, ktera se
nahrava do DRAM karty - jejich adresa je `kDramOffset + index`.

## Vyznam generatoru v SF1.0

Zjisteno z hodnot v `BULLFROG.SBK` a z toho, co s nimi dela note-on
v `SBAWE32.DRV`.

| generator | rozsah v bance | prevod na EMU8000 |
|---|---|---|
| `initialAttenuation` (48) | 127 | **0..127, kde 127 = bez utlumu**; `dB = (127 - v) * 0.375` -> IFATN lo. Presne to dela `SBAWE32.DRV` na `0x038A`: `ax = 0x7F - v; ax = ax*3; ax >>= 3` |
| `sustainVolEnv` (37) | 127 | primo do DCYSUSV bity 14..8 (0x7F = bez utlumu) |
| `sustainModEnv` (29) | 127 | primo do DCYSUS bity 14..8 |
| `holdVolEnv` (35) | 8191 | **ms** -> ATKHLDV bity 14..8 pres `(127 - h) * 92 ms` |
| `attackVolEnv` (34) | ms | -> ATKHLDV bity 6..0 pres tabulku attack (`11878 / k(r-1)`) |
| `decayVolEnv` (36) | 2034, 5940 | **ms** -> DCYSUSV bity 6..0 pres tabulku decay (`47513 / k(r-1)`) |
| `releaseVolEnv` (38) | 1192, 5940 | **ms** -> tataz tabulka, zapisuje se pri Note Off s bitem 15 |
| `modEnvToFilterFc` (11) | -5 az -30 | primo PEFE bity 7..0 (znamenkove) |
| `modLfoToVolume` (13) | 127 | primo TREMFRQ bity 15..8 |
| `modLfoToFilterFc` (10) | 127 | primo FMMOD bity 7..0 |
| `freqModLFO` (22) | 120 | primo TREMFRQ bity 7..0 (frekvence LFO1) |
| `reverbEffectsSend` (16) | 0..58 | primo PTRX bity 15..8 |
| `chorusEffectsSend` (15) | 0..254 | primo CSL bity 31..24 |
| `initialFilterQ` (9) | 127 | 0..127 -> CCCA bity 31..28 (0..15) **[?] overit** |
| `overridingRootKey` (58) | 60 | zakladni nota vzorku |
| `sampleModes` (54) | 0 / 1 | 1 = smycka |
| `keyRange` (43) | 12..107 | rozsah kláves zony |
| `sampleID` (53) | index do `shdr` |

### Neznamy generator 55

Kazda zona v `BULLFROG.SBK` ma `gen55 = 6000`. V SF2 je 55 nepouzity.
Protoze je u vsech zon stejny, na vysledny zvuk nic nerozlisuje - zatim se
ignoruje. **[?]** Pravdepodobne neco jako "sample rate" nebo priznak pro
AWE ovladac.

## Struktura BULLFROG.SBK

- 15 presetu, kazdy 1 zona, kazda zona 1 instrument s 1 zonou
- 12 vzorku: 10 z ROM (`*`), 2 vlastni (`LOOP2`, `REV2`, dohromady 84 794
  vzorku v `smpl`)
- presety: 3 (LOOP2), 0 (REV2), 4 (LOOP3), 5 (TBellD4Wave), 117-127

Skladby `midi/*_w.xmi` maji v `TIMB` chunku patche **5, 3, 4** (= presety
z teto banky) a **52** (= z ROM), coz potvrzuje, ze `_w` je varianta pro
AWE32 a ze se banka vrstvi nad ROM.

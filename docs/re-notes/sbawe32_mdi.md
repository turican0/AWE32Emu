# SBAWE32.MDI - AIL/Miles driver pro AWE32

Zdroj: `../AWE32EmuData/sbk/SBAWE32.MDI`, 36880 B, signatura `AIL3MDI` + 0x1A.
Je to ten ovladac, pres ktery hra kartu skutecne obsluhovala, takze
z hlediska "jak to znelo" je autoritativnejsi nez Windows driver.

## Struktura souboru

| offset | obsah |
|---|---|
| `0x000` | `AIL3MDI\x1A`, verze `0x0112` |
| `0x0BA` | jmeno zarizeni: `Creative Sound Blaster AWE32` |
| `0x10A` | tabulka vstupnich bodu (14 word offsetu: `116E`, `119C`, `11B1`, `11B8`, `1233`, `124D`, `12B6`, `12C6`, `12E6`, `12ED`, `137C`, `1383`, `138A`, `1391`) |
| `0x132` | zacatek kodu |
| `0x142D` | krivka expression (CC11), 128 B |
| `0x14AD` | krivka hlasitosti kanalu (CC7), 128 B |
| `0x152D` | krivka velocity, 128 B |
| `0x70E` | tabulka portu EMU8000: `0620 0A20 0E20` |

## Pristup k registrum - **tretí nezavisle potvrzeni**

| rutina | podpis |
|---|---|
| `0x177C` | `write_word(data, sel)`, `ret 4` |
| `0x17B8` | `read_word(sel) -> ax`, `ret 2` |
| `0x17F0` | `write_dword(lo, hi, sel)`, `ret 6` |
| `0x182C` | `read_dword(sel) -> dx:ax` |

Kodovani je **presne stejne** jako v `AWEUTIL.COM` a `SBAWE32.DRV`:

    pointer = ((sel & 0x7000) >> 7) | (sel & 0x1F)     ; = (reg << 5) | voice
    port    = portTable[(sel & 0x0C00) >> 9] | ((sel >> 8) & 2)

Pointer se zapisuje na `[0x712] + 2` = `0xE20 + 2` = **0xE22**. Bit 9 v `sel`
pridava +2 (alias "Data2" na `0xA22`), stejne jako v AWEUTILu.

## Note-on (`0x2102` az `0x2363`)

Poradi zapisu je **shodne s nasi implementaci** i se `SBAWE32.DRV`:

    DCYSUSV = 0x0080          ; hlas nejdriv umlcet
    VTFT    = 0x0000FFFF      ; hlasitost 0, filtr otevreny
    ENVVOL, ATKHLDV, ENVVAL, ATKHLD, DCYSUS
    IP, IFATN, LFO1VAL, LFO2VAL, PEFE, FMMOD, TREMFRQ, FM2FRQ2
    PTRX (read-modify-write kvuli reverb send), PSST, CSL, CCCA
    DCYSUSV = <sustain|decay> ; az tohle spousti notu

Dalsi potvrzene hodnoty:

- `0x1992`: `DCYSUSV = 0x807F` - okamzite ukonceni hlasu
- `0x1F12`: `IP = 0xE000` - reset vysky na jednotkovy prirustek

## Vypocet utlumu (`0x2102`) - prepsano 1:1

```
if (cc7 <= 10) atten = 0xFF;
else {
    atten = ( 8*(volDb[cc7] + velDb[velocity])
              + ((3*(0x7F - patchAtten)) & ~7) ) / 3;
    if (atten >= 0xFF) atten = 0xFF;
    else if (expression < 0x7F)
        atten += exprDb[expression] * (0xFF - atten) / 0x7F;
}
```

Utlum patche se pricita **rovnou v jednotkach registru** (jednotka 0.375 dB),
zatimco hlasitost kanalu a velocity jsou v dB a prepocitavaji se pomerem
8/3. Implementovano v `src/Awe32Curves.h`.

### Prevodni tabulky

Vsechny tri jsou **bajt po bajtu identicke** s tabulkami ve `SBAWE32.DRV`:

| ucel | MDI | DRV | rozsah |
|---|---|---|---|
| expression (CC11) | `0x142D` | `ds:0592` | 127 .. 0 |
| hlasitost kanalu (CC7) | `0x14AD` | `ds:0692` | 99 .. 0 |
| velocity | `0x152D` | `ds:0612` | 50 .. 0 |

Prvnich 11 polozek tabulky pro CC7 je 99, protoze hodnoty <= 10 stejne
zkratka vedou na plny utlum (test na zacatku vypoctu).

Krivka velocity je az na nejnizsi hodnoty presne `-40*log10(v/127)`
(napr. v=64 -> 11.9 vs tabulka 11, v=16 -> 35.9 vs tabulka 36) a dole se
zastropuje na 50 dB.

## SB16 mixer

`0x107E` = `set_midi_volume(al = levy, ah = pravy)` - zapisuje hornich 5
bitu do mixer registru `0x34` / `0x35` (MIDI volume L/R) na portu
`[0x5DC]`. `0x1062` je cteni. Pri inicializaci si driver aktualni hodnotu
precte a zapise zpet, takze **nenastavuje zadny pevny utlum** - zavisi na
tom, co v mixeru bylo.

## Co z toho nevyplynulo

Rozdil ~16 dB u dlouheho padu na kanalu 7 v `002_C2GAME3` (viz
[docs/CIL.md](../CIL.md)) tenhle ovladac nevysvetluje - jeho vypocet utlumu
mame ted 1:1 a pro CC7=90, velocity=127 a patch bez utlumu vychazi 6.0 dB,
stejne jako u nas.

Nejpravdepodobnejsi zbyvajici vysvetleni: **utlum patche `[si+0x60]`**
bere driver z vlastnich patch tabulek karty (ROM), ne ze SoundFontu.
`1mgm.sf2` u presetu 52 uvadi `initialAttenuation 0`, tedy bez utlumu.
Pokud ROM tabulka u tehoz patche uvadi vyssi utlum, vysvetlilo by to
presne ten rozdil. Overit by to slo jen reverzovanim patch tabulek v ROM.

# Wave ROM: `awe32.raw` vs `1mgm.sf2`

Porovnano 13.8.2026. Oba soubory lezi v `rom/` a **nejsou v repozitari**
(viz `.gitignore`) - jde o originalni data Creative/E-mu.

## Zaver

Jsou to **titiz vzorky ve dvou formatech**.

| | `awe32.raw` | `1mgm.sf2` |
|---|---|---|
| velikost | 1 048 576 B (presne 1 MB) | 1 090 280 B |
| obsah | hlavicka ROM + vzorkovy fond | RIFF sfbk: INFO + sdta/smpl + pdta |
| vzorky | od offsetu `0x3DE` | chunk `smpl` od offsetu `0x8E` |

Prekryv 1 047 586 bajtu (523 793 vzorku) je **bajt po bajtu shodny**,
MD5 obou useku `8ff0680989bfa4924fbccd4527302f03`.

Rozdily:

- `.sf2` ma na konci `smpl` o 2 bajty (`FFFF`) vic, nez se do 1 MB ROM vejde
  - jde o zakonceni chunku, ne o zvukova data
- `.raw` ma navic hlavicku ROM na `0x000..0x3DE`
- `.sf2` ma navic strukturu banky (`phdr`, `pbag`, `pgen`, `inst`, `ibag`,
  `igen`, `shdr`), kterou `.raw` obsahuje jen v proprietarnim formatu Creative

## Hlavicka ROM

Text v hlavicce je ulozeny po 16bitovych slovech, takze pri cteni po bajtech
vypada prehozene (`iNhgitgnla e` = `Nightingale`). Po prohozeni bajtu:

```
2.81MGM ... Nightingale     General MIDI    Copyright 1993 E-mu Systems,I...
```

**Vzorkova data prohozena nejsou** - jsou to 16bitove hodnoty v little-endian
a ctou se primo. Prohozeni se tyka jen ASCII retezcu v hlavicce.

## Jak to pouzit v emulaci

Pouziva se **`awe32.raw`**, protoze to je presne to, co vidi cip:

- ROM se mapuje na adresy zvukove pameti 0 .. 0x7FFFF (adresy jsou ve
  vzorcich, ne v bajtech; 1 MB = 524 288 slov)
- uzivatelska DRAM zacina az na `Emu8000::kDramOffset` = 0x200000

Struktura banky se cte z `1mgm.sf2`, protoze je v dokumentovanem formatu.
Prevod indexu je trivialni a diky prokazane shode dat presny:

```
adresa v EMU8000 = 0x1EF + index_vzorku_z_sf2_shdr
```

kde `0x1EF` = 495 slov = offset `0x3DE` v ROM.

Reverzovat vlastni preset tabulky z `.raw` by znamenalo dalsi RE
proprietarniho formatu bez jakekoli vyhody - vzorky by z toho vysly stejne.

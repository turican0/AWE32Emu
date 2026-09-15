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

## Update 2026-09-14: dump from a real card

A dump from the tester's AWE32 (AWEDUMP, `AWE32EmuData/rom/awe32rom.bin`,
md5 `1d8f7f3842f6fb19cfcb2247e9f45870`) starts with `0032` and equals
`awe32.raw` **shifted by one word** (0 of 524 287 words differ). `awe32.raw`
has an extra `0x1234` word in front, which 86Box already drops in
`emu8k_init`. On the card, sample index `i` of `1mgm.sf2` is therefore at
chip address `0x1EE + i`, not `0x1EF + i`. `Synth::LoadWaveRom` now drops
the extra word too, so both files load identically; the register values
(`kRomPoolBase` = 495 with the -1/-2/-3 correction, SF1 addresses) are
unchanged because they match what the drivers write.

Reverzovat vlastni preset tabulky z `.raw` by znamenalo dalsi RE
proprietarniho formatu bez jakekoli vyhody - vzorky by z toho vysly stejne.

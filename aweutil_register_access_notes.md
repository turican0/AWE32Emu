# AWEUTIL.COM – poznámky z disassembly (doplňkový zdroj, viz TODO sekce 10.5)

**Účel tohoto souboru:** referenční poznámky pro ověření/doplnění registrové mapy
EMU8000 v sekci 4 hlavního TODO. **Nejde o zdrojový kód pluginu** a tento soubor
se nikde nekompiluje ani jinak nenapojuje do buildu. Obsahuje jen popis chování
(pseudo-C, vlastními slovy), ne přepis assembleru.

Zdroj: IDA disassembly `AWEUTIL.COM` (verze nalezená v `SBAWE32\130-AWE\...`,
SHA256 `2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`),
analyzováno 13.8.2026.

Značení: **[ověřeno chováním ovladače]** vs. **[jen odvozeno z disassembly, nutno
ověřit proti Tech Ref Manuálu]**.

---

## 1. Registrový přístup přes Pointer/Data0/Data1 porty

Ovladač používá čtyři interní pomocné rutiny pro přístup k registrům čipu
(v IDA pojmenované `sub_10EAC`, `sub_10EFA`, `sub_10F46`, `sub_10F9C`).
Signatury (odvozeno z počtu argumentů a `retn N`):

- `write_word_reg(data, regSelect)` – zápis 16bit registru
- `read_word_reg(regSelect) -> word` – čtení 16bit registru
- `write_dword_reg(dataLo, dataHi, regSelect)` – zápis 32bit registru
  (zapisuje se low word, pak high word na port+2 – odpovídá dvojici
  Data0/Data1 portů zmiňované v obecné dokumentaci k EMU8000)
- `read_dword_reg(regSelect) -> dword` – čtení 32bit registru (analogicky)

**[jen odvozeno, nutno ověřit]** `regSelect` vypadá jako zabalená hodnota:

```
regSelect = (voice_or_channel_bits << offset) | register_index
```

kde `register_index` se v kódu maskuje na 5 bitů (`and ax, 1Fh`, tj. 0–31) a
zbylé bity `regSelect` (maskované `0x7000`, posunuté o 7) se skládají do horní
části adresy zapisované do Pointer registru. Přesné bitové pozice a jejich
vztah k číslu hlasu (0–31) je potřeba dohledat v Tech Ref Manuálu – v kódu
samotném nejsou pojmenované konstanty, jen bitové masky.

Báze portů: proměnná uložená v ovladači jako výchozí `0x220`/`0x330`
(typické SB16 I/O porty), přepisovaná hodnotou z `BLASTER` environment
proměnné při startu. Zápis/čtení EMU8000 registrů se z těchto hodnot
odvozuje aritmeticky (viz kód), ne pevnou konstantou – tzn. skutečná
adresa Pointer/Data portů se na cílovém systému může lišit podle
nastavení karty.

## 2. Doporučení pro implementaci (sekce 4 hlavního TODO)

- Pointer/Data0/Data1 register scheme odpovídá tomu, co popisuje obecná
  EMU8000 dokumentace – tzn. při psaní `Emu8000Core` (viz `src/Emu8000.h`)
  je rozumné napodobit **tento vzor přístupu** (jeden "select" + word/dword
  data), ne nutně stejné bitové konstanty.
- Přesnou bitovou skladbu `regSelect` a konkrétní čísla registrů (pitch,
  envelope, filter, LFO...) je potřeba dohledat v Tech Ref Manuálu (bod 0
  hlavního TODO) – tahle poznámka pouze potvrzuje, že se čip takhle
  adresuje, nedává kompletní registrovou mapu.
- Další shluky I/O přístupů v disassembly (kolem jiné bázové proměnné,
  pravděpodobně DRAM/wavetable paměť) zatím **neanalyzováno** – TODO pro
  příští průchod.

## 3. Co záměrně chybí

Tento soubor neobsahuje žádný přepis konkrétních assembler instrukcí ani
kompletní dekompilaci ovladače – jen shrnutí zjištěného mechanismu přístupu
k registrům, potřebné pro křížovou kontrolu s oficiální dokumentací.

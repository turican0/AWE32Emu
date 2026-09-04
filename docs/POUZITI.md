# AWE32Emu — jak se to ovládá

`AWE32Emu` je konzolový program. Dostane skladbu (`.mid` nebo `.xmi`), zvukovou
banku a případně wave ROM karty, a buď to rovnou přehraje, nebo zapíše do `.wav`.

```
AWE32Emu <soubor.mid|soubor.xmi> [volby]
AWE32Emu --help
```

Živé přehrávání jede přes `winmm`, takže je jen na Windows. Renderování do
souboru (`--wav`) funguje všude — na Linuxu je to jediný režim.

---

## Nejrychlejší start

Bez jakékoli banky program hraje generovanou sinusovku — poznáte, že sekvencer
běží, ale nezní to jako AWE32:

```bash
AWE32Emu skladba.mid --wav ven.wav
```

Skutečný zvuk začne až s ROM a bankou:

```bash
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --wav ven.wav
```

---

## Co je co: ROM, banka, „banka popisující ROM“

Tohle je jediné místo, kde se dá zbytečně zabloudit, tak k němu rovnou příklad.

| přepínač | co to je |
|---|---|
| `--rom` | **data**. Surový dump 1 MB wave ROM karty (`awe32.raw`), 16bit LE. Samotný nic nezní — je to jen zvuková paměť. |
| `--rombank` | **popis** obsahu ROM: které presety kde v ROM leží (`1mgm.sf2`, `SYNTHGM.SBK`). Vlastní vzorky neobsahuje. |
| `--sbk` | **uživatelská banka**. `.SBK` (SoundFont 1.0) i `.SF2`. Vzorky si nese s sebou a nahrají se do emulované DRAM karty. |

Typická plná sestava zní takto:

```bash
AWE32Emu skladba.mid \
    --rom     rom/awe32.raw \
    --rombank rom/1mgm.sf2 \
    --sbk     sbk/BULLFROG.SBK \
    --wav ven.wav
```

Banky se dají zadat vícekrát a **vrství se — pozdější přebíjí dřívější**. Přesně
tak to dělá i ovladač, když hra nahraje svou banku vedle obecné GM.

### Dvě banky najednou (výběr přes CC0)

Některé skladby počítají s tím, že si uživatel nahraje **víc bank do různých
slotů** a přepíná mezi nimi Bank Selectem (CC0). Slot se připíše k cestě
zavináčem:

```bash
AWE32Emu DANCESBK.MID \
    --rom rom/awe32.raw \
    --sbk SYNTHGM.SBK \
    --sbk SBK/9FTGRAND.SBK@1 \
    --sbk SBK/GMDRUM.SBK@2 \
    --wav ven.wav
```

Je to přepis toho, co k demu na SoundFont CD píše `DEMO/SBK.TXT`:

> 3. Dancesbk.mid — Load 9ftgrand.sbk on Bank 1, load Gmdrum.sbk on Bank 2.

Uživatelské banky mají v `phdr` číslo banky 0; `@N` jejich presety přesune do
MIDI banky N. Každá banka dostane svůj vlastní kus DRAM za tou předchozí, jako
u skutečné karty.

---

## Výstup

```bash
# přehrát v reálném čase (jen Windows)
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK

# zapsat do .wav (44 100 Hz, 16 bit, stereo)
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --wav ven.wav

# jen vybrané MIDI kanály — nejrychlejší způsob, jak najít, který nástroj zlobí
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --only-ch 1 --wav ch1.wav
```

---

## Která rodina ovladače

Creative měl dvě rodiny a **nejsou zaměnitelné** — liší se osmi hodnotami
v inicializačních polích, tabulkou velocity a vzorcem útlumu:

| `--driver` | ovladač | kde se potkáte |
|---|---|---|
| `win95` (výchozí) | `SBAWE.VXD` | Windows 95, Creative MIDI |
| `dos` | `SBAWE32.MDI` / `SBAWE32.DRV` | DOSové hry (Miles/AIL), dobové nahrávky |

Nahrávky ze skutečného železa, které používáme na ladění, jsou starší než
Win95, takže se k nim renderuje s `--driver dos`.

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sbk SYNTHGM.SBK --sbk BULLFROG.SBK \
    --driver dos --wav ven.wav
```

Podrobnosti o rozdílech jsou v `src/Awe32Driver.h`.

---

## Které jádro čipu

```bash
--chip nas      # naše jádro (výchozí) - float, laditelný filtr
--chip 86box    # nezměněný snd_emu8k.c z 86Boxu
```

`--chip 86box` je kontrolní jádro: je to **doslova tentýž soubor**, jaký se
překládá do 86Boxu, takže rozdíl mezi našimi a jeho výstupy je vždy rozdíl
v našem kódu, ne v opisování. Vyžaduje `--rom` a překládá se jen tehdy, když
je k dispozici datový adresář — viz [Sestavení](#sestavení) níže.

---

## Ladicí výpisy

```bash
# prvních 40 spuštěných hlasů i s registry, které do nich šly
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --debug-voices 40 --wav ven.wav

# CSV se všemi note-ony: sloupce odpovídají bloku parametrů v SBAWE.VXD,
# takže se dá položit vedle výpisu ze skutečného ovladače
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --dump-notes noty.csv --wav ven.wav

# záznam všech portových zápisů (srovnání s 86Boxem)
AWE32Emu skladba.mid --rom awe32.raw --sbk SYNTHGM.SBK --trace stopa.txt --wav ven.wav
```

`--dump-notes` je hlavní nástroj, když se dvě sestavení liší: srovnáním dvou
CSV se hned vidí, **který** registr se rozešel, místo dohadování ze zvuku.

---

## Počáteční stav kanálů (`--conf`)

Hra typicky pošle před první notou sadu control changů (hlasitost, panorama,
reverb…). Když se renderuje jen holý XMI, tenhle stav chybí a render začne
někde jinde než nahrávka. `--conf` ho dodá:

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sbk SYNTHGM.SBK --sbk BULLFROG.SBK \
    --driver dos --conf conf/mc2.conf --wav ven.wav
```

Volba `--master-volume N` (0–127) odpovídá hlavní hlasitosti sekvenceru AIL.

---

## Převod SBK → SF2

Banka ve formátu SoundFont 1.0 je dnes k ničemu: `.SBK` neumí skoro žádný
přehrávač a vzorky navíc často leží ve wave ROM karty, kterou nikdo nemá.
`--export-sf2` z toho udělá jeden samostatný soubor.

```bash
# samotná banka (vzorky z ROM se zapečou do souboru)
AWE32Emu --rom rom/awe32.raw --sbk sbk/BULLFROG.SBK --export-sf2 bullfrog.sf2

# víc bank do jedné (pozdější přebíjí dřívější, stejně jako při hraní)
AWE32Emu --rom rom/awe32.raw --sbk SYNTHGM.SBK --sbk sbk/BULLFROG.SBK \
    --export-sf2 vse.sf2
```

Skladbu k tomu zadávat nemusíte — při exportu se nic nehraje.

**Není to přebalení.** SF1 ukládá hodnoty v jiných jednotkách než SF2 (časy
v milisekundách, cutoff 0–127, útlum „127 = nic“, sustain přes `v * 4 / 3`) a
kdyby se jen přepsaly do SF2 obálky, banka by zněla špatně. Převádí se význam,
podle vzorců změřených proti skutečnému ovladači Creative.

Kontrola, že převod sedí: render z původní `.SBK` a render z exportované `.sf2`
se porovnají po jednotlivých notách. Na intru Magic Carpet 2 (671 not) sedí
všech dvacet sledovaných registrů a hladiny se shodují na pět desetinných míst.

---

## Ladicí volby zvuku

Tyhle se nepoužívají při běžném přehrávání — jsou tu proto, aby šlo měřit, jak
se která volba projeví proti nahrávkám ze skutečného železa.

| volba | výchozí | co dělá |
|---|---|---|
| `--interp linear\|cubic\|3point\|3pointc\|sinc` | `sinc` | interpolace vzorků |
| `--filter-top <Hz>` | 8000 | mezní kmitočet při registru 0xFF |
| `--filter-poles 1\|2\|4` | 2 | strmost filtru 6/12/24 dB na oktávu |
| `--filter-mode tpt\|86box` | `tpt` | podoba filtru (`86box` = přesně jako `snd_emu8k.c`) |
| `--q-base <x>` | 1.0 | základ rezonance (0.7071 = Butterworth) |
| `--cutoff-map exp\|lin` | `exp` | převod registru na mez filtru |
| `--pan linear\|power` | `linear` | křivka panoramy (`linear` = násobička jako v čipu) |
| `--loop-wrap on\|off` | `on` | zalamovat vzorky interpolace do smyčky |
| `--reverb 0..7`, `--chorus 0..7` | — | preset efektu |
| `--rev-room`, `--rev-damp`, `--rev-return`, `--cho-return` | — | ladění efektů |

Výchozí `sinc` není odhad. Přes 23 ověřených dvojic nahrávka/MIDI dává
průměrné skóre 5,2784 proti 5,2856 u `cubic` a 5,3212 u `3point`. Rozdíl proti
`cubic` dělá **jen** Hi-Octane (4,138 a 4,128 proti 4,221 a 4,265) — na zbytku
je `sinc` o ~0,007 horší. Bereme ho proto, že Hi-Octane je nejčistší materiál,
jaký máme. Postup měření je v `../AWE32EmuData/tests/tune.py`.

---

## Sestavení

### Windows, Visual Studio

Otevřete `AWE32Emu.sln` a přeložte konfiguraci `Release|x64`. Projekt přikládá
`snd_emu8k.c` přímo z datového adresáře (`../AWE32EmuData`), takže `--chip 86box`
je k dispozici. Jinou cestu k datům lze zadat proměnnou prostředí
`AWE32EMU_DATA`.

### CMake (Windows i Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Takhle sestavená binárka **neobsahuje** jádro z 86Boxu — `snd_emu8k.c` leží
v datovém adresáři mimo repozitář, takže `--chip 86box` jen ohlásí, že v téhle
binárce není. Všechno ostatní funguje beze změny. Když datový adresář máte:

```bash
cmake -B build -DAWE32EMU_WITH_86BOX=ON -DAWE32EMU_DATA=../AWE32EmuData
cmake --build build --config Release
```

### Hotové binárky

Každá značka začínající `v` (např. `v0.3`) spustí GitHub Action, která sestaví
Windows i Linux binárku a pověsí je na vydání — viz
`.github/workflows/release.yml`.

---

## Kam dál

- `docs/re-notes/` — co se o čipu a ovladačích zjistilo a jak (registrová mapa,
  převody jednotek, srovnání s 86Boxem, průběh ladění)
- `../AWE32EmuData/tests/` — měřicí skripty; `tune.py` je ten hlavní

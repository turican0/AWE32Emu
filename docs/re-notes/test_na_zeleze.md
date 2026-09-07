# Měřicí program pro skutečnou AWE32 — návrh obsahu

Cíl: jedna nahrávka ze skutečné karty, ze které se dá **odděleně** změřit každá
vlastnost čipu, kterou dnes jen modelujeme. Ne hudba — kalibrační signál.

## Zásadní rozhodnutí: obejít ovladač

Program **nebude hrát MIDI**. Bude zapisovat přímo do registrů EMU8000 přes
porty, po dokumentované inicializační sekvenci (máme ji z `AWEUTIL.COM`, viz
`emu8000_register_map.md`).

Proč to takhle: přes MIDI se nedá nastavit jedna hodnota a nechat ostatní na
místě — ovladač si registry počítá sám z banky, velocity a control changů.
Nešlo by tedy měřit „co dělá decay rate 0x40", protože se k němu nedá dostat
samostatně. Přímý zápis dává **jednu proměnnou na jeden tón**, a to je jediné,
z čeho se dá udělat kalibrace.

Do adresáře MC2 to patří kvůli `BULLFROG.SBK` — ta se v jednom bloku nahraje do
DRAM stejnou cestou jako ji tam nahrává hra.

## Testovací signály jsou z wave ROM

Nemusí se nic nahrávat do paměti karty. ROM obsahuje přesně to, co potřebujeme,
a je na každé AWE32 táž — navíc ji máme bajt po bajtu v `awe32.raw`, takže víme,
jak signál vypadá **před** čipem:

| vzorek | adresa | smyčka | k čemu |
|---|---|---|---|
| `whitenoisewave` | 0x0700F3–0x072158 | 8281 slov | filtr, interpolace (plné spektrum) |
| `sinewave` | 0x0690BF–0x06914A | 65 slov | hlasitost, panorama, výška, obálky |
| `sinetick` | 0x077E5A–0x077EA2 | 60 slov | synchronizační značky |

## Značky pro synchronizaci

Tři tiky (40 ms, mezera 60 ms) a pak 700 ms ticho — celkem asi 1 s. Na začátku
nahrávky a pak **každých 60 s**. Podle nich se záznam zarovná i kdyby se
rozcházel v tempu.

Na úplném začátku a konci navíc referenční tón (3 s, sinus, pevná hlasitost),
aby se poznalo, jestli se během nahrávání nezměnila úroveň.

## Obsah

Vše na jednom hlasu, kromě bloků 11 a 12, kde jde právě o součet víc hlasů.
Mezi tóny je vždy ticho, aby se daly měřit izolovaně.

| # | blok | co se mění | položek | délka |
|---|---|---|---|---|
| 1 | **Útlum** | IFATN spodní bajt 0–127 po 4 | 32 | 13 s |
| 2 | **Panorama** | PSST pan 0–255 po 16 | 17 | 7 s |
| 3 | **Výška, sinus** | IP přes 6 oktáv po půltónech | 36 | 14 s |
| 4 | **Výška, šum** | totéž se šumem = **interpolace** | 36 | 14 s |
| 5 | **Mez filtru** | šum, Q=0, cutoff 0–255 po 8 | 32 | 19 s |
| 6 | **Rezonance** | šum, Q 0–15 při 4 mezích | 64 | 38 s |
| 7 | **Obálka hlasitosti** | attack 16, hold 8, decay 16, sustain 8, release 16, delay 8 | 72 | 134 s |
| 8 | **Modulační obálka** | PEFE hi (výška), PEFE lo (filtr), vlastní časy | 24 | 48 s |
| 9 | **LFO1** | rychlost, → hlasitost, → výška, → filtr | 32 | 64 s |
| 10 | **LFO2 a zpoždění** | rychlost, hloubka, delay obou LFO | 22 | 44 s |
| 11 | **Reverb** | 8 presetů × 4 úrovně sendu | 32 | 48 s |
| 12 | **Chorus** | 8 presetů × 4 úrovně sendu | 32 | 48 s |
| 13 | **Smyčky** | dlouhé držení, 3 výšky, oba vzorky | 6 | 18 s |
| 14 | **Součet hlasů** | tentýž tón na 1, 2, 4, 8, 16, 32 hlasech | 6 | 9 s |
| 15 | **BULLFROG.SBK** | každý vzorek banky na 3 výškách + jeden přes filtr | 52 | 28 s |

Součet asi **9 minut 15 s**, se značkami a rezervou **10–11 minut**.

## Co která položka rozhodne

- **1, 2** — převod útlumu na dB (dnes 0,375 dB na krok) a zákon panoramy.
  Obojí je v našem kódu z dokumentace, ne ze železa.
- **3, 4** — interpolace. Šum na 36 výškách dá přenosovou funkci interpolátoru
  přímo; dnes o ní hádáme podle skóre.
- **5** — převod registru na mez filtru **a strmost** v jednom. Pokus změřit
  strmost z hudby selhal (viz `filter_slope.py`), protože se skupiny lišily
  i výškou not. Na šumu s jednou proměnnou to vyjde.
- **6** — tabulka rezonance z awe32faq tvrdí, že rezonance klesá s mezí
  (u Q=8 ze 17 na 7 dB). Ověří se, nebo vyvrátí.
- **7** — **nejdůležitější blok.** Rozklad zbytkové odchylky ukázal, že skoro
  polovina je chyba hlasitosti not, největší 50–150 ms po nástupu. Registry
  přitom sedí 100 %, takže chyba je v tom, jak rychle na ně čip reaguje.
- **8, 9, 10** — hloubky modulací máme z Programmer's Guide (`±1 oktáva`,
  `±6 oktáv`, `±12 dB`), nikdy neověřené.
- **11, 12** — efekty. Dnes je máme jako vlastní implementaci; zvuk dobových
  presetů neznáme.
- **13** — chování na švu smyčky, kde interpolátor čte přes konec.
- **14** — součet a případné ořezání při hodně hlasech.
- **15** — kontrola celé cesty na bance, kterou používá hra.

## Co potřebujeme od člověka, který to pustí

- Nahrávat **stereo** (blok 2 bez toho nemá smysl), 44,1 nebo 48 kHz, 16 bit.
- **Žádnou** ekvalizaci, normalizaci ani „vylepšení" — raději tišeji.
- Ideálně z linkového výstupu; když má možnost digitálního záznamu, tím líp.
- Napsat model karty (CT????), kolik má DRAM a verzi ovladače.
- Pustit i s prázdnou DRAM a znovu s nahranou `BULLFROG.SBK`, pokud by blok 15
  dělal potíže.

## Kdyby to mělo být kratší

Pořadí podle důležitosti, kdyby se mělo škrtat: nechat 7, 5, 6, 4, 3, 1.
Bloky 9–12 (LFO a efekty) jsou nejméně naléhavé — na ty se dá udělat druhá
nahrávka později.

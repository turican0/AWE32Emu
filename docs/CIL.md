# Cil projektu: z `.xmi` vyrobit `.wav`, ktery zni jako referencni `.ogg`

## Zadani

Vzit soubor z `midi/` (`.xmi`, pripadne `.mid`), prehrat ho pres emulaci
EMU8000 s originalnimi zvukovymi daty a dostat `.wav`, ktery je **sluchove
i mericky podobny** odpovidajici nahravce v `ogg/`.

Zvukova data:
- `../AWE32EmuData/rom/awe32.raw` - 1 MB wave ROM karty (zakladni GM sada, "1MGM")
- `../AWE32EmuData/sbk/BULLFROG.SBK` - uzivatelska banka hry (SoundFont 1.0, `irom=1MGM`,
  tj. **vrstvi se nad ROM**, nenahrazuje ji)

Postup je iterativni: vyrenderovat, porovnat s referenci, najit odchylku,
opravit, opakovat.

## Kriterium hotovo

Neni to "bit-exact" - `.ogg` je ztratova komprese nahravky z realne karty.
Cilem je shoda v tom, co jde overit:

1. **Delka a timing** - stejna delka skladby, udalosti na stejnych mistech
2. **Spravne nastroje** - kazdy MIDI kanal hraje ten patch, co ma
3. **Obalka casove** - naboj/doznivani ve stejnych casech
4. **Spektrum** - podobny prubeh spektra v case (korelace spektrogramu)
5. **Sluchove** - "je to ta sama skladba tim samym zvukem"

## Pozadavek na obecnost

Loader banky **nesmi byt sity na `BULLFROG.SBK`**. Musi zvladnout libovolny
`.SBK` i `.SF2`:

- detekce verze podle `INFO/ifil` (1.x = SF1, 2.x = SF2) a podle toho
  ruzna velikost `shdr` (16 vs 46 B), jmena v `snam` vs v `shdr`
  a ruzne jednotky generatoru (registrove/ms vs timecents/centibely)
- libovolna hierarchie preset -> zona -> instrument -> zona
- globalni zony (zona bez `sampleID` / bez `instrument` = default pro zbytek)
- `keyRange` / `velRange`, prekryvajici se zony, vice vrstev na jednu notu
- vzorky z ROM (`*` v nazvu) i z `smpl` banky, obojí soucasne
- banka bez ROM i ROM bez banky

## Kroky

- [x] **Parovani** - `_w` je varianta pro AWE32: `TIMB` chunk uvadi patche
      3, 4, 5 (presety z `BULLFROG.SBK`) a 52 (z ROM). `_f` = FM/Adlib,
      `_g` = General MIDI, `_r` = Roland. Referencni `.ogg` odpovida
      `midi/<stejne_jmeno>_w.xmi`.
- [x] **Oprava casovani XMI** - dve ruzna kodovani: delta-time je soucet
      bajtu < 0x80, ale **delka noty je standardni SMF VLQ**. Parser pouzival
      na obojí totez a rozsypal stream (293 udalosti misto 9948). Navic se
      musi **ignorovat tempo meta** - XMI bezi na pevnych 120 Hz.
      Vysledek: delky 0.995 / 0.987 / 1.000 / 1.000 vuci referenci.
- [x] **Dekodovat `.ogg`** do PCM (`soundfile`, viz `../AWE32EmuData/tests/pair_check.py`)
- [x] **Nacist ROM** - `--rom ../AWE32EmuData/rom/awe32.raw`, mapuje se od adresy 0,
      zvukovy fond zacina na slove 495
- [x] **SoundFont parser** - `SoundFont.h/.cpp`, obecny pro SF1.0 i SF2
- [x] **Prevod generatoru na registry** EMU8000
- [x] **Vrstveni bank** - `--rombank` (banka popisujici obsah ROM) a `--sbk`
      (uzivatelska banka do DRAM), obojí lze zadat vicekrat
- [x] **Mereni** - `../AWE32EmuData/tests/compare.py`, `../AWE32EmuData/tests/bands.py`, `../AWE32EmuData/tests/rom_pitch.py`,
      `../AWE32EmuData/tests/query_preset.py`, `../AWE32EmuData/tests/dump_sbk.py`
- [ ] **Iterace** - viz "Stav mereni" nize

## Kde se pracuje

- `../AWE32EmuData/tests/` - vsechny pokusy, vyrenderovane `.wav`, porovnavaci skripty
  a jejich vystupy. Neni to soucast buildu.

## Stav mereni (002_C2GAME3)

    AWE32Emu.exe ../AWE32EmuData/midi/002_C2GAME3_w.xmi --rom ../AWE32EmuData/rom/awe32.raw
        --rombank ../AWE32EmuData/rom/1mgm.sf2 --sbk ../AWE32EmuData/sbk/BULLFROG.SBK --wav ../AWE32EmuData/tests/out/x.wav

Co uz sedi:

- delka 241.5 s vs 240.0 s
- vsechny noty najdou realny vzorek, nic nespadne na nahradni sinus
- **melodie sedi** - v okne 6-14 s ma pasmo 400-800 Hz pomer 0.96 vuci referenci
- vyska ROM vzorku overena merenim: `kpianob1` ma 639 Hz = nota 75, coz presne
  odpovida `overridingRootKey 75` v `1mgm.sf2`. ROM vzorky jsou ulozene
  naladene nahoru a cip je pitchuje dolu.

Co nesedi:

- **kanal 7 (Choir Aahs, prog 52)** - dve noty dlouhe celych 240 s, tedy trvaly
  pad. U nas je o **~16 dB hlasitejsi** nez v referenci a hraje jinou vysku.
  My mame spicky na 65.4 a 69.3 Hz, coz jsou presne noty 36 a 37 ze souboru.
  Reference ma misto toho spicky na 98.2 a 99.6 Hz (dve blizke spicky =
  rozladeni chorusem) a na 98 Hz je o 19 dB hlasitejsi nez my.
  Rozdil je +6 az +7 pultonu, coz neni ladeni, ale jina nota nebo jiny vzorek.

## Lepsi referencni sada: `../AWE32EmuData/SAMPLES2/`

Demo skladby AWE32 od Creative - **bezztratove FLAC** plus odpovidajici
`.MID`, tedy mnohem lepsi kalibrace nez `.ogg`.

| MIDI | delka s tempo mapou | FLAC | pomer |
|---|---|---|---|
| `RELAX_BK.MID` | 218.4 s | 218.8 s | **0.998** |
| `CRAZY_BK.MID` | 175.2 s | 171.5 s | 1.021 |
| `JUMP_BK.MID` | 169.1 s | 180.9 s | 0.935 |
| `MARS_BK.MID` | 150.0 s | 202.8 s | 0.740 |
| `GEORG_BK.MID` | 282.4 s | 153.1 s | 1.844 |

`RELAX` sedi na 0.2 %, coz **potvrzuje, ze zpracovani tempo mapy SMF je
spravne**. U `GEORG_BK` je rozdil dany dlouhym tichem za posledni notou
(posledni tempo 62 BPM plati na 103360 tiku, ale noty konci mnohem driv).

`RELAX` ma navic vlastni banku `../AWE32EmuData/SAMPLES2/RELAX.SBK` (SF1.0, 32 presetu,
`irom=1MGM`), takze je to nejlepsi kalibracni par:

    AWE32Emu.exe ../AWE32EmuData/SAMPLES2/RELAX_BK.MID --rom ../AWE32EmuData/rom/awe32.raw
        --sbk ../AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK
        --sbk ../AWE32EmuData/SAMPLES2/RELAX.SBK --wav ../AWE32EmuData/tests/out/relax.wav

## Autenticka GM banka: `SYNTHGM.SBK`

Z instalacniho CD (`cdrom/Creative_AWE Install Disk.bin`, cesta
`/WIN95/DRIVERS/SYNTHGM.SBK`) se da vytahnout **banka, kterou samotne
ovladace pouzivaji** - SoundFont 1.0, "General MIDI", E-mu Systems 1993,
`irom=1MGM`, 153 vzorku, 128 presetu v bance 0 a jeden v bance 128.

Nema chunk `smpl` vubec: popisuje jen obsah wave ROM. Adresy v `shdr` jsou
uz hotove pro cip - `kpianob1` zacina na **494**, coz presne odpovida
posunu, ktery jsem odvodil z `BULLFROG.SBK` (v `1mgm.sf2` ma tentyz vzorek
index 0). Loader to pozna sam: SF1 banka bez `smpl` = vsechny vzorky v ROM.

Je to blizsi originalu nez `1mgm.sf2` (pozdejsi SF2 konverze), protoze
generatory jsou v nativnich jednotkach ovladace. Merenim se to potvrdilo:

| pasmo | s `1mgm.sf2` | se `SYNTHGM.SBK` |
|---|---|---|
| 200-400 Hz | 1.55x | **1.18x** |
| 800-1600 Hz | 2.70x | **1.59x** |
| 1600-3200 Hz | 1.38x | **1.03x** |
| 3200-6400 Hz | 0.47x | **1.80x** |
| 6400-12800 Hz | 0.37x | **1.66x** |

Prumerna absolutni logaritmicka odchylka pres vsechna pasma klesla
z 5.40 na 4.13.

## Verze ovladacu

Prevodni tabulky jsou **bajt po bajtu identicke napric tremi generacemi**:

| soubor | puvod |
|---|---|
| `SBAWE32.DRV` | Windows 3.x, driver disk |
| `SBAWE.VXD` | Windows 95, instalacni CD |
| `SBAWE32.MDI` | DOS AIL/Miles, ze hry |

Shoduji se krivky expression, hlasitosti kanalu i tabulka attack casu
(11878, 5939, 3959, ...). Verze ovladace tedy neni zdrojem odchylek.

Stav: delka 219.9 vs 218.8 s, korelace obalky 0.57, vsech 6000+ hlasu najde
realny vzorek (slap bass, saw stack, brass, strings, vlastni vzorky banky),
nic nespadne na nahradu. Zbyva spektralni vyvazeni:

Vyvoj spektralniho vyvazeni behem oprav (pomer nas/reference, cil je 1.0):

| pasmo | vychozi | nyni |
|---|---|---|
| 0-100 Hz | 0.40x | 0.68x |
| 100-200 Hz | 0.63x | 0.71x |
| 200-400 Hz | 1.91x | 1.55x |
| 400-800 Hz | 4.56x | 3.22x |
| 800-1600 Hz | 3.88x | 2.70x |
| 1600-3200 Hz | 1.90x | 1.38x |
| 3200-6400 Hz | 0.32x | 0.47x |
| 6400-12800 Hz | 0.10x | 0.37x |

Vsechna pasma se posunula smerem k jednicce. Co ty kroky byly:

1. **oprava filtru** - Chamberlinuv SVF byl pri Q=0.707 nestabilni nad 4 kHz
   (podminka `f + 1/Q < 2` neplatila), nahrazen TPT topologii. Manual navic
   rika, ze pri Q=0 a cutoff 0xFF se signal nemeni - to jsme nedelali
   a rezali na 5 kHz.
2. **velocity -> cutoff, key -> obalka, one-shot smycka** - tri chovani
   prepsana z `SBAWE32.DRV`, viz [driver_note_on.md](re-notes/driver_note_on.md)
3. **chorus a reverb** (`Emu8000Effects.h`) - sendy registrove presne
   (PTRX 15..8, CSL 31..24, pred panoramou), algoritmus je nahrada
4. **oprava vyberu bicich** - GM banka ma v bance 128 jediny preset
   "Standard", ale skladba posila na kanal 9 program 16. Fallback padal az
   na banku 0, takze celou bicí sadu nahradil jediny vzorek uzivatelske
   banky. Nove se u bicich zkousi jeste (128, 0).
5. **zisk reverbu** - hrebenovy filtr se zpetnou vazbou `f` ma stejnosmerne
   zesileni `1/(1-f)`, pri `f = 0.854` tedy 6.85x. Bez vstupniho skalovani
   `(1-f)` reverb nekolikanasobne zesiloval a vystup klipoval.

## Regresni test

`python ../AWE32EmuData/tests/regress.py` overi to, co uz je zmerene:

    [OK] nota 50 ladi na +-5 centu        146.69 Hz = -1.6 centu
    [OK] klavir neni ticho                amplituda 106.5
    [OK] delka do 2 % reference           219.9 s vs 218.8 s (pomer 1.005)
    [OK] zadny hlas nespadne na nahradu   0 hlasu na nahrade
    [OK] vystup neklipuje                 peak 0.925

Zamerne nemeri "nejsilnejsi spicku" - ta umi preskocit na harmonickou, kdyz
se zmeni barva zvuku, a jednou uz to falesne nahlasilo posun o oktavu.

## Co zbyva

- **Pasmo 400-800 Hz je 3.1x nad referenci.** Zmereno po kanalech: dela to
  z drtive vetsiny **kanal 4** (absolutne 6839 proti 1347 u druheho v poradi).
  Hraje program 62 "Synth Brass1" pres vzorek `sawstackwavems`, noty 67-74,
  tedy zakladni frekvence 392-587 Hz presne v tom pasmu. Preset ma
  `initialFilterFc 95` (u nas 589 Hz) a `initialFilterQ 44`, ale
  `modEnvToFilterFc 63` filtr otevira az o 3 oktavy, takze nerezе.
  Otazka je, jestli je ten kanal proste hlasitejsi, nez ma byt.
- **Basy 0.41x.** Reference je celkove hutnejsi (RMS 0.145 vs nasich 0.102).
- Bullfrog pad na kanalu 7 - viz vyse, nevysvetleno.

Co uz je vyloucene:

- kradeni hlasu (max souběžna polyfonie RELAXu je 21 z 30 dostupnych)
- verze ovladace (tabulky identicke napric tremi generacemi)
- vypocet utlumu (prepsany 1:1 z ovladace vcetne tabulek)
- bass/treble EQ karty - `AWEUTIL.TXT` zna jen `/R:nnn` a `/C:nnn`
  (reverb a chorus 0-100), zadny ekvalizer

## Poznamky k referenci

`.ogg` nejsou presna reference - je to ztratova nahravka a lisi se. Cilem
neni bitova shoda, ale aby vysledek znel jako ta sama skladba tim samym
zvukem.

Stopy `*_danger.ogg` maji **stejnou delku** jako zakladni verze, takze jde
o vrstveni v ramci tehoz XMI (bojova vrstva), ne o jiny soubor. RBRN chunk
v techto XMI neni.

## Otevrene otazky

- Jak se prepina "danger" vrstva? (nejspis mutovani kanalu ze strany hry)
- Pouzivala hra pri nahravani `.ogg` chorus/reverb? (zatim neimplementovano)
- Vyznam SF1.0 generatoru 55 (v `BULLFROG.SBK` vzdy 6000)
- `initialFilterQ` 0..127 -> CCCA Q 0..15: presna krivka

## Souvisejici

- [docs/re-notes/emu8000_register_map.md](re-notes/emu8000_register_map.md) -
  registrova mapa a co je z ceho odvozeno
- [docs/re-notes/rom_vs_sf2.md](re-notes/rom_vs_sf2.md) - vztah `.raw` a `.sf2`
- [docs/re-notes/soundfont1_sbk.md](re-notes/soundfont1_sbk.md) - jak cist
  SoundFont 1.0 a jak se jeho generatory mapuji na registry

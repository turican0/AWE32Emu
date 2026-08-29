# Evidence testu: nas render proti skutecnym ovladacum

Cil je 1:1 na registrech pri note-onu. Vsechno se meri, nic se neodhaduje.

## Nastroje

| nastroj | k cemu |
|---|---|
| `tests/matrix.py` | testovaci matice - nas render proti stopam ovladacu, vysledky do `matrix_results.json` |
| `tests/status.py` | prehled pres vsechny urovne (registry, mezivysledky, cip) proti `status_baseline.json` |
| `tests/trace_split.py` | rozdeli stopu s vice skladbami na useky a vypise okna `--dframes` |
| `tests/bank_sanity.py` | nacte kazdou SF1 banku a zkusi s ni odehrat - hleda pady a prazdne banky |
| `tests/notes_diff.py` | jadro porovnani, registr po registru |
| `tests/xmi_raw.py` | co je v XMI opravdu (bloky IFF, XMIDI ridici zpravy) |
| `tests/note_timing.py` | sedi casovani not? Rozhoduje, jestli ma smysl cist procenta shody |

Pridat pripad do matice = jeden radek v `CASES`.

## Stav matice

    python ../AWE32EmuData/tests/matrix.py

| pripad | rodina | prehravac | registry | not |
|---|---|---|---|---|
| georgia | win95 | Media Player | **32/32** | 3331 |
| jump | win95 | Media Player | **32/32** | 5077 |
| relax | win95 | Media Player | **32/32** | 6523 |
| minuet | win95 | Media Player | **32/32** | 242 |
| mc2-intro | dos | Magic Carpet 2 | **24/24** | 261 |
| mc2-menu | dos | Magic Carpet 2 | **24/24** | 112 |

Cip: `--chip 86box` je proti `emu8k_ref.exe` bajtove shodny (Georgia,
6 927 532 snimku, 0 rozdilu).

## Banky

`bank_sanity.py`: **34 bank SF1, vsechny se nactou a render dobehne,
zadny pad.** Vcetne cele sady `sbk/SFONT1/` (ACSTGTRM, BASTIMPS, CHAPSTKS,
DULCIMRS, ELPERC_M, ELSITARM, FUNKBASM, FUZZGTRS, HARMONIS, JAZZGTRS,
JAZZKITM, LATDRUMM, LATHANDS, MANDOLNS, MTLDRUMS, NYLNGTRL, ORCHHRPM,
PIZZBASM, POPDRUMS, POPGTRSS, RATDRUMS, ROKBASSM, SYNFX01M, SYNFX02M,
SYNTH01S..SYNTH05S, TWELVSTM) a `RELAX.SBK`.

Pozor: tohle **neni** test shody s ovladacem, jen ze parser nespadne.
Shodu s ovladacem lze u dalsich bank overit az behem ve VM s tou bankou.

## Na co si dat pozor pri mereni

**Nejdriv overit casovani, teprve pak cist procenta.** Kdyz se sled not
rozejde, `notes_diff.py` paruje podle poradi a rozdily pak vychazeji
v celych pultonech - vypada to jako chyba vysky, ale je to spatne parovani.
Slouzi k tomu `note_timing.py`.

**Stopa z behu VM casto obsahuje vic skladeb.** U Magic Carpet 2 konci intro
u noty 260 a po dvanactisekundove mezere zacne hudba z menu; po dalsich
~65 s necinnosti se **znovu spusti intro** (pozna se podle signatury
`F400 F400 F400 B959`). Okna se hledaji `trace_split.py`.

**Hlavni hlasitost neni pevna.** Magic Carpet 2 hraje intro na 100
a menu na 127 - s obema hodnotami sedi prislusna cast na 24/24. V konfiguraci
je 100, pro jinou cast se preda `--master-volume`.

**`AUTOEXEC.BAT` musi mit konce radku CRLF.** S unixovymi se davka
neprovede a v guestu zustane jen prompt.

**DOSMID je v korenu** (`C:\DOSMID.EXE`). Adresar `C:\DOSMID\` neexistuje,
prestoze na nej zaloha `AUTOEXEC.MID` v obrazu ukazuje - viz nize.

## DOSMID

### Cesta pres MPU-401 v teto VM fungovat nemuze

Ne kvuli konfiguraci, ale z principu. Zjisteno tim, ze se vystup AWEUTILu
nechal v guestu presmerovat do souboru a precetl se `fat16.py get`:

- `/EM:GM` **v nabídce je** (dokumentace rika, ze nepodporovane volby se
  nezobrazi), a TSR se i nainstaluje - jen hlasi `ERR014` a nenajde sva data.
- `AWEUTIL.TXT`: MIDI emulace potrebuje **propojku MFBEN** na karte, tedy
  zpetnou smycku, kterou karta vidi vlastni provoz na MPU.
- V 86Boxu **zadna takova smycka neni**: `snd_mpu401.c` posila vystup do
  `midi_raw_out_byte()`, tedy na hostitelske MIDI. Slovo MFBEN se v celem
  zdrojaku nevyskytuje.

Pozn.: `AWEUTIL.COM` je zabaleny, disassembly v `SBAWE32/AWEUTIL.COM.asm`
obsahuje jen rozbalovaci stub - staticky se z nej cesta nedocte.

### Cesta ven: DOSMID /awe

DOSMID umi ridit EMU8000 **primo**, bez MPU i bez AWEUTILu:

    C:\DOSMID.EXE /awe C:\TEST.MID

Tim se testuje **ctvrta nezavisla implementace ovladace** vedle
`SBAWE.VXD`, `SBAWE32.MDI` a AWEUTILu. Pozor pri vyhodnocovani: rozdily
proti nasi rodine `win95` nebo `dos` nemusi znamenat chybu u nas - DOSMID
je jiny program s vlastnimi rozhodnutimi.

### Dalsi pasti

- `AUTOEXEC.BAT` musi mit **CRLF**; s LF se davka neprovede.
- DOSMID je v **korenu** (`C:\DOSMID.EXE`), adresar `C:\DOSMID\` neexistuje,
  prestoze na nej zaloha `AUTOEXEC.MID` v obrazu ukazuje.
- `fat16.py` se musi volat s **windowsovymi** cestami; s cestami ve tvaru
  `/c/prenos/...` Python soubor nenajde.

## Zatezovy test renderu

    python ../AWE32EmuData/tests/render_sweep.py

Prozene renderem kolekci MIDI a hlida pady, prazdne vystupy a nesmyslne
pocty hlasu. **60 souboru, 0 problemu, 0 prazdnych.** Neni to test shody
s ovladacem, ale zatezovy test parseru a syntezy.

## Nalez: presetove generatory se **nescitaji**

Nasla to az pata skladba (CRAZY), kdyz ctyri predchozi vypadaly hotove.

Kanal 4 hraje program 97 "Soundtrack", ktery ma `coarseTune` na obou
urovnich - 1 u presetu a 3 u nastroje. Scitali jsme je podle specifikace
SF2 (4), ovladac pouziva 3 a hraje o pulton niz; nesedelo vsech 58 not
toho kanalu.

Poznalo se to podle detailu: u dvou vrstev tehoz tonu byl posun **-341
a -342**, tedy nestejny. To je podpis posunu v **centech pred prevodem**,
ne konstanty v jednotkach IP - a tim padla uvaha, ze jde o ohyb vysky.

Oprava: presetova zona uz jen **doplnuje** to, co zona nastroje nema.
Utlum zustava vyjimkou, ten ovladac scita az v jednotkach registru.
Po zmene sedi CRAZY 32/32 a nic jineho se nezhorsilo.

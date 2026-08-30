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
`SBAWE.VXD`, `SBAWE32.MDI` a AWEUTILu.

Funguje: TEST.MID (Georgia) dalo **3331 not, presne tolik co nas
render**. Casovani i pocty tedy sedi. **Jako reference pro 1:1 se ale
nehodi** - DOSMID interpretuje banku po svem:

| | DOSMID | my | rozdil |
|---|---|---|---|
| `CCCA` nota 0 | 0001E9 | 0001C0 | +41 |
| `CCCA` nota 3 | 004566 | 00453D | +41 |
| `IFATN` | FE50 | FE42 | utlum o 14 jednotek |

Adresa vzorku se lisi o **konstantnich 41 vzorku** a utlum o pevny kus -
je to tedy tataz banka, jen vlastni volby DOSMIDu (jiny pocatecni posun
a jina hlasitostni krivka). V matici je pripad `dosmid-georgia`
vedeny jako **informativni**, ne jako kriterium - sleduje se jen proto,
aby bylo videt, kdyz se cislo zmeni.

Stopa obsahuje pet skladeb za sebou; hranice najde `trace_split.py`.

### Doplneni MFBEN do 86Boxu

Do `src/sound/snd_mpu401.c` pribyla volitelna zpetna smycka. V rezimu
UART slo predtim jen `midi_raw_out_byte(val)`; ted se bajt da vratit
i na vstup pres `MPU401_RecQueueBuffer`, coz je presne to, co dela skutecna
karta s propojkou MFBEN a co rezidentni AWEUTIL potrebuje.

Zapina se `AWE32_MPU_LOOPBACK=1` a **vychozi stav je vypnuto**.

**Vysledek: nestacilo to.** Se zapnutou smyckou stale zadne noty, a to
ani se starym AWEUTILem (14 kB, v1.01 v guestu), ani s novejsim
z distribuce (28 kB, 95dosapp) doplnenym o `CTMIX.CFG`. AWEUTIL /EM:GM
krome toho hlasi `ERR014` - nenajde sva data. Emulace MIDI na AWE32
evidentne potrebuje z karty vic nez jen zpetnou smycku a 86Box to
nemodeluje.

Smycka v kodu **zustava** - je spravna a muze se hodit pozdeji - ale
tuhle cestu tim odblokovat nelze. Kdo by na tom chtel pokracovat, mel
by zacit tim, co presne AWEUTIL pri /EM cte z karty.

To neni opatrnost pro opatrnost: 86Box je nase **merici etalon**. Kdyz do nej
dopiseme chovani, ktere jsme si odvodili, a nechame ho zapnute pri beznem
mereni, riskujeme, ze se budeme "shodovat" s vlastni domnenkou misto se
skutecnym hardwarem. Zapinat proto jen na testy, kterych se to tyka, a
vysledky z nich neznamkovat stejne jako mereni proti Creative ovladacum.

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

## Test jine banky pres skutecny ovladac

Uzivatelskou banku nelze ve Win95 nahrat jinak nez grafickym ovladacim
panelem, ale jde to obejit: ovladac si pri startu nacita
`WINDOWS\SYSTEM\SYNTHGM.SBK`, takze staci ten soubor **vymenit**.

Pouzito `SYNTH02S.SBK` (542 kB, 38 presetu, vlastni vzorky - testuje se
tim i nahravani do DRAM) a prehran MINUET. Ovladac ho nacetl a zahral
242 not, presne tolik co nas render.

Nezapomenout pak `SYNTHGM.SBK` vratit, jinak dalsi mereni ve Win95 pojede
s cizi bankou.

### Nalez 1: +16 jednotek utlumu plati jen pro vzorky v ROM

V kodu byla podminka "banka se hlasi k ROM 1MGM -> pricti 16 jednotek
utlumu" s poznamkou, ze ovladac to jeste podminuje bajtovym priznakem,
ktery jsme nerozklicovali. **Ten priznak je "lezi vzorek v ROM?".**

SYNTH02S.SBK se take hlasi k 1MGM, ale ma vlastni vzorky v DRAM - a ovladac
tam tech 16 jednotek nepricetl. Bylo to videt na `IFATN` (o 16 vys u nas)
a na `VTFT^`/`CVCF^`, ktere vychazely presne dvojnasobne - 16 jednotek
je 6 dB, tedy faktor 2. Jedna pricina, tri registry.

Fyzikalne to sedi: vzorky ve wave ROM jsou o 6 dB hlasitejsi nez to, co si
ovladac sam nahraje do DRAM.

### Nalez 2: uzivatelska DRAM zacina u kazde rodiny jinde

Po oprave nalezu 1 zbyly tri adresni registry (`CCCA`, `PSST`, `CSL`),
vsechny presne o **34 vzorku** vedle. To je posun zacatku uzivatelske DRAM:

    SBAWE32.MDI (dos)    prvni vzorek na 0x200032   rezerva 50
    SBAWE.VXD   (win95)  prvni vzorek na 0x200010   rezerva 16

Padesatka byla zmerena proti MDI a pouzivala se pro obe rodiny. Rezerva je
ted podle rodiny (`Synth::kDramReserveDos` / `kDramReserveWin95`) a
dopocitava se pri prvnim nacteni banky - proto se ve `main.cpp` musi
varianta ovladace nastavit **pred** nactenim bank.

Po obou opravach sedi vymenena banka na **32/32** a nic jineho se
nezhorsilo.

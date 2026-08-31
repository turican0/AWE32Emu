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

### Zaver: neni to nas defekt, dorovnavat se nebude

Rozdily jsou zmerene a jednoznacne DOSMIDovy:

- **Adresa vzorku: +41 u vsech 3331 not.** Jedina konstanta pres celou
  skladbu - jine rozvrzeni DRAM, ne chyba vypoctu.
- **Utlum: rozdil kolisa** (8 az 16 jednotek), tedy jina hlasitostni
  krivka.

DOSMID je **cizi prehravac**, ne Creative ovladac. Nasi autoritou jsou
`SBAWE.VXD` a `SBAWE32.MDI`, kde sedime na 100 %. Dorovnat se na DOSMID
by znamenalo tu shodu **rozbit**. Pripad `dosmid-georgia` proto zustava
v matici jen jako sledovane cislo (18/32) - hlida se, aby se nemenilo,
a nic vic z nej nevyvozujeme.

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

### Zmereno: DOSMID na MPU vubec nepise

Do smycky v `snd_mpu401.c` byl doplnen zaznam `awe32_trace_note`, ktery
do stopy zapise kazdy bajt, co ji projde. Vysledek behu se zapnutou
smyckou: **nula zaznamu**.

DOSMID tedy na datovy port MPU-401 **nezapsal jediny bajt**. Retez se
trha uz tam, ne u chybejici zpetne smycky - jeho rezim `/mpu` se v tomhle
prostredi vubec nerozjede. Doplnene ovladace (CTMIDI.DRV, CTGS.DRV,
CTMT32.DRV) na tom nic nezmenily.

**Poucení k postupu:** nekolik cyklu jsem stravil odstranovanim
domnelych prekazek za mistem, kde se retez ve skutecnosti trhal.
Kdybych hned na zacatku zmeril, jestli vubec neco tece, usetril bych
je. Merit retez od zacatku, ne od predpokladaneho zlomu.

Smycka v kodu **zustava** - chybejici MFBEN je skutecne chybejici
chovani 86Boxu - ale tuhle cestu tim odblokovat nelze. Nema smysl v ni
pokracovat: rezim `/awe` funguje a ucel (testovat DOSMID) plni.

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
pocty hlasu.

Pruchod trva kolem hodiny, proto dve pomucky:

- `--resume` preskoci soubory, ktere uz jednou projely ciste
  (seznam je v `sweep_done.json`). Poskozene vstupy se do nej **neukladaji** -
  u tech se ma pokazde znovu overit, ze je render odmitne.
- Stav se po kazdem souboru zapisuje do `sweep_status.json` a da se
  kdykoli precist:

        python ../AWE32EmuData/tests/sweep_status.py
        python ../AWE32EmuData/tests/sweep_status.py --watch

  Ukaze ukazatel postupu, tempo, odhad zbytku a nalezene problemy.
  Kdyz se stav dlouho nemeni, rekne to - pozna se tak visici beh.

Pozn.: beh na pozadi **neposilat do roury** (`| tail`); roura drzi
vystup az do konce a hodinu neni videt nic. Od toho je ted stavovy soubor.

### Vysledek: cela kolekce projela

**232 souboru, 0 padu, 0 prazdnych vystupu, 0 nesmyslnych poctu hlasu.**
Jediny odmitnuty vstup je `title2.mid` z WarCraftu 2, a to spravne:
hlavicka stopy hlasi 11004 B, ale v souboru jich zbyva 8170 (je uriznuty).

Beh trval 42 minut na 160 souborech, zbytek uz mel odbyto z drivejska.
Neni to test shody s ovladacem - na to je `matrix.py` - ale zatezovy
test parseru a syntezy: rika, ze render na zadnem MIDI z kolekce
nespadne a vzdycky neco zahraje.

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

## Velka vlastni banka: RELAX.SBK (6,7 MB vokalu)

Stejny postup jako u SYNTH02S - vymenit `WINDOWS\SYSTEM\SYNTHGM.SBK` - ale
navic je potreba **zvysit pamet karty**. Vychozich 512 kB nestaci:

    [Sound Blaster AWE32 PnP]
    onboard_ram = 8192

**Pozor:** volby zarizeni patri do sekce pojmenovane podle zarizeni, ne do
`[Sound]`. 86Box je cte pres `device_get_config_int`, ktery se pta sekce
se jmenem zarizeni; v `[Sound]` se to tise ignoruje. Prvni pokus proto
skoncil tim, ze se banka nenahrala (3 zapisy `SMLD` a nic dal).

Se spravnym nastavenim se nahralo **3 372 947** zapisu `SMLD` a zahralo
5229 not - presne tolik, kolik jich udela nas render.

Dalsi past: v matici musi byt **jen ta jedna banka**, ne GM plus ona -
v guestu je SYNTHGM.SBK prepsana, takze ovladac ma taky jen ji.

### Nalez 1: chybejici program -> prvni preset banky

RELAX_VX pouziva GM programy az do 122, ale banka ma jen presety 0..31.
U chybejicich jsme pousteli **nahradni sinusovku** z `kDramOffset` (v CCCA
to bylo videt jako 0x1FFFFC), kdezto ovladac hraje **prvni preset banky**
(0x20000C). Hezky je to videt po kanalech: vetsina hraje 0x20000C, ale
kanaly, jejichz program v bance opravdu je (16, 21, 22, 30, 31), hraji sve
vlastni vzorky.

Nase nahrada zustava jen pro banku, ktera nema ani preset 0.

### Nalez 2: bicí kanal nesaha do banky 0

Po prvni oprave zbylo 1849 not - presne pocet not na kanalu 9. Nas retezec
hledani mel jako posledni krok "banka 0 se stejnym programem"; kanal 9 ma
v teto skladbe program 16 a banka preset 16 ma, tak jsme ho vzali.
**Ovladac na bicim kanalu do banky 0 nesahne** - kdyz sadu nenajde, jde
rovnou na preset 0.

Na skladbach s GM bankou se to projevit nemuze: SYNTHGM.SBK bicí banku 128
ma, takze se retezec ke tretimu kroku nikdy nedostane.

## Provozni poucení

**Nespoustet dlouhou ulohu podruhe, kdyz uz bezi.** Dve matice naraz pisi
do tychz souboru v `out/matrix/` a vysledky si prepisuji - jedna z nich pak
spadne a druhe se neda verit.

**Necekat ve smycce na vystup jine ulohy.** Kdyz se ta uloha zastavi,
cekani visi navzdy (jednou takhle bezelo 5,5 hodiny).

**`matrix.py --save` uz nemeri znovu** - kazdy beh se zapise do
`matrix_last.json` a `--save` ho jen povysi na zakladnu. Drive to znamenalo
projit celou matici jeste jednou, tedy ~13 minut nazmar.

## Cip: nas render a 86Box prekladaji tentyz soubor

Drive to tak nebylo, prestoze to tvrdil komentar v projektu. Byly to
**dve kopie**:

- `ref86box/upstream/snd_emu8k.c` - netknuty upstream, prekladal ho nas render
- `docs/86box-src/.../snd_emu8k.c` - z nej se stavi 86Box pro VM

Zvukove byly shodne (rozdil je jen stopovani a registr ID), takze se to
na mereni neprojevilo. Jakmile se ale zacne ladit EMU8000, zmena v jedne
kopii se do druhe nedostane a shoda by se **tise rozpadla**.

Ted projekt preklada primo soubor ze stromu 86Boxu. Volani `emu8k_trace_*`
jsou u nas prazdna (`ref86box/include/86box/snd_emu8k_trace.h`,
`awe32_trace.h`). Co se doladi, plati v obou projektech naráz.

Overeno: WAV pred prepnutim a po nem ma **tentyz otisk SHA-256**
(GEORG_BK.MID, 50 MB) - prepnuti nezmenilo ani bit.

Upstreamova kopie **zustava** a neprekláda se: je to doklad puvodu a
`verify_upstream.py` ji hlida proti GitHubu.

### Co je uz nase uprava a co jeste stock

    python ../AWE32EmuData/tests/chip_diff.py
    python ../AWE32EmuData/tests/chip_diff.py --full

Porovna nas cip proti netknutemu upstreamu a odchylky roztridi na
znamé a **nove**. Znamé jsou dve a ani jedna nemeni zvuk:

- stopovaci hacky - jen zapisuji, do stavu cipu nesahaji;
- registr ID cte `0x0C` misto `0x1C` - jen pro cteni, slouzi k detekci
  karty (bez toho hlasi AWEUTIL.COM ERR012).

Stav dnes: **32 zmenenych radku, znamych 32, novych 0.** Pri ladeni
EMU8000 ma tenhle vypis rikat presne to, co jsme prave zmenili.

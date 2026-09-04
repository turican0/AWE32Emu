# Ladeni EMU8000 proti nahravkam ze zeleza

## Pravidlo: co kam patri

Projekt ma dve urovne a kazda ma jiny etalon:

| uroven | etalon | stav |
|---|---|---|
| **ovladac** (Synth, banky, prevody) | originalni Creative ovladace bezici v 86Boxu | 1:1 (10/10 pripadu) |
| **cip** (EMU8000) | **skutecne nahravky**, ne 86Box | ladi se |

Z toho plynou dve veci, na ktere se lehko zapomene:

1. **Oprava cipu musi skoncit v obou implementacich.** Nase jadro
   (`Emu8000.cpp`) a `snd_emu8k.c` maji byt zamenitelne; kdyz se neco
   zlepsi u nas, patri to i do `snd_emu8k.c` (ktery uz je sdileny soubor
   s 86Boxem, viz `testy.md`). Cilem je, aby `--chip nas` a
   `--chip 86box` davaly totez.
2. **86Box neni etalon cipu.** Je etalon **ovladace** - bezi v nem originalni
   binarky. Jeho `snd_emu8k.c` je jen priblizeni a v necem se od zeleza
   lisi (napr. celociselna aritmetika a jeji sum). Kdyz se nase jadro od nej
   lisi, neni to samo o sobe chyba - rozhoduje az mereni proti nahravkam.


> **Data nejsou v repozitari.** Nahravky, banky a ROM lezi v `AWE32EmuData`.

Registrova uroven je hotova (10/10 pripadu proti skutecnym Creative
ovladacum, viz `testy.md`). Tohle je o patro niz: jestli **zni** jednotlive
noty stejne jako na skutecne karte.

## Proc se nemeri cely mix

Nahravky ze zeleza (`SAMPLES3/tracks/`, zaznam 486 se Sound Blaster AWE32
CT2760 pod Windows 3.11) prosly YouTubem, jinym predzesilovacem a jinou
ekvalizaci. Od naseho renderu se lisi **vzdycky** - to samo o sobe nerika nic.
Zajimave je az to, kdyz se **nektera nota lisi jinak nez zbytek**: tam uz
nejde o prenosovou cestu, ale o to, ze ji cip spocital jinak.

Proto se meri takhle:

1. `align2.py` - zarovnani **krivkou**, ne jednim posunem.
2. `note_probe.py` - rozdil spekter po notach, **minus zkresleni celku**.
3. `tune.py` - totez pres vsechny dvojice, protoze jedna skladba nestaci.

### Zarovnani musi byt krivka

Nahravky jsou ze skutecneho stroje, jehoz hodiny nejdou stejne rychle jako
nase. U DANCE sedi prvni tretina na 0 ms a pak to ujizdi o 267 ms, u STYLES1
dokonce o **1950 ms**. Jediny prevod (posun + meritko) by z mereni po notach
udelal nesmysl.

Zarovnava se na **naboj** (spektralni tok), ne na hlasitost - nastup tonu je
ostry v obou zaznamech i pri uplne jine barve. Odlehle kotvy se vyhazuji:
posun se meni pomalu a plynule, takze kotva utikajici od okoli je spatne
zaskocena a pokrivila by cely usek kolem sebe.

### Metoda je zkalibrovana

**Nulovy test (tyz render proti sobe) dava presne 0,0000** - ve vsech
rezimech vcetne stereo. Co sonda ukaze, je tedy signal, ne sum metody.
Reaguje i na zname zmeny: `--filter-poles 4` zhorsi skore ze 4,84 na 5,31.

## Overene dvojice

Sedmnact dvojic, zarovnanych **sestnact**. Shoda kotev pod ~0,4 by znamenala,
ze to neni tataz skladba.

### Zaznam 486 z YouTube (ztratovy)

| | kotev | shoda |
|---|---|---|
| dance / pop / starman / styles1 | 38 / 16 / 20 / 46 | 0,62 / 0,51 / 0,53 / 0,58 |
| styles2 / symphony / canon | 61 / 20 / 27 | 0,56 / 0,51 / 0,56 |
| nutcrack / violin / mozart | 48 / 10 / 13 | 0,60 / 0,48 / 0,50 |

**Oprava proti `pairs.txt`:** `02_GMAMOZAR` patri k `CLASSIC/GMMOZART.MID`,
ne k `DEMO/MOZART.MID`. Potvrzuje to `tracklist.txt` i shoda kotev.

Nejmene spolehlivy par je **violin** (jen 10 kotev, shoda 0,480) - zaver,
ktery stoji hlavne na nem, je treba brat s rezervou.

### Demo CD AWE32 - **bezztratove FLAC** (nejlepsi material)

V `SAMPLES2/` lezi nahravky primo pojmenovane podle skladeb. Nic je
neprohnalo kodekem, takze na nich neni ta cast rozdilu, ktera jde na vrub
YouTube. Tytez MIDI navic uz sedi **32/32 na registrove urovni**.

| | kotev | shoda | ujeti |
|---|---|---|---|
| georgia | 71 | 0,596 | 46 ms |
| jump | 72 | **0,666** | 17 ms |
| relax | 51 | 0,617 | 493 ms |
| crazy | 78 | 0,642 | 23 ms |
| mars | 6 | 0,429 | 6 ms |

**mars** je slaby: nahravka ("2 - Vocal intro-Mars") ma pred skladbou jeste
vokalni intro, takze se zarovna jen kratky usek.

### Magic Carpet 2

| | kotev | shoda |
|---|---|---|
| mc2-menu | 146 | **0,844** - nejlepsi ze vsech |
| mc2-intro | - | **nezarovna se**, viz nize |

### Zarovnani muselo umet najit cast v celku

Puvodni hruby odhad koreloval cely zaznam proti celemu a na obou pripadech
MC2 i na Marsu selhal: jeden zaznam casto pokryva jen **cast** druheho
(nahravka Marsu ma pred skladbou vokalni intro, nase XMI se naopak smyckuje
a je 359 s proti 140 s zaznamu). Ted se bere kus z **kratsiho** zaznamu a
hleda se v delsim.

## Magic Carpet 2, intro: nemame k nemu referenci

Intro v nasem enginu zni spatne - to je pozorovani od uzivatele a sedi na nej
i mereni. Nas render ma proti nahravce:

- o **12 dB min** energie kolem 400 Hz,
- o **7 az 12 dB vic** nad 3 kHz.

Rozbor po kanalech ukazal viníka: **kanal 1** (504 z 670 not, program 5) ma
teziste energie na **12 kHz**, zatimco nahravka ma teziste pod 100 Hz.

Co uz je ale zmerene a **vylucuje nase jadro**:

1. **Nas cip a 86Box hraji ten kanal stejne** (rozdil do 1 dB ve vsech
   pasmech). Neni to tedy chyba naseho cipu.
2. **Registry sedi 24/24 proti stope ze hry** vcetne CCCA, takze vybiráme
   tyz vzorek jako skutecny ovladac hry.
3. Adresa `0x0498ED` = 301 293 je konec vzorku `filtersnap` a 46 slov
   pred zacatkem `belltree` (301 339); nas `loopEnd` sedi na belltree.
   Hrajeme tedy zvonkohru - a ta **ma** byt jasna, cimz se ty vysky
   vysvetluji.
4. XMI ma presne ty kanaly, ktere hrajeme (504+80+80+1+4 = 669 not proti
   nasim 670), takze nic nevynechavame.

### `_f`/`_g`/`_r` vs `_w`: nejsou to ruzne zvukovky, jsou to ruzne aranze

Rozdil neni v tom, cim se to prehrava - je to **jina skladba** pro jine
zarizeni. Porovnano primo z udalosti v XMI (`xmi_events.py`):

| varianta | kanaly (program: not) | bicí (kanal 9) |
|---|---|---|
| f | ch3:47(195), ch4:36(2), ch9:0(**1090**) | 1090 not |
| g | ch3:47(195), ch4:52(2), ch9:0(**1092**) | 1092 not |
| r | ch3:112(195), ch4:34(2), ch5:121(210), ch9:0(**882**) | 882 not |
| **w** | ch1:5(504), ch2:3(80), ch3:4(80), ch5:52(1), ch6:4(4) | **zadne** |

`_f`/`_g`/`_r` maji bubenicky kanal 9 s ~900-1100 notami bicich.
`_w` bicí **nema vubec** - jen zvonkohru (program 5, 504 not) a tri
klavirni/pianove kanaly (programy 3/4/4). Neni to tedy stejna hudba pro
jine zvukovky, je to **jina, ridsi aranz bez bicí sekce**.

`_w` je prokazatelne spravna varianta pro AWE32 (registry 24/24 proti
stope ze hry, shoda 0,76-0,79 proti zaznamu z bezici hry v 86Boxu - viz
vyse). Kdyz zni ridceji/jinak, nez si uzivatel pamatuje, je pravdepodobnejsi
vysvetleni, ze srovnava proti GM/MT-32 verzi s bicimi, ne ze je nekde chyba
v emulaci - Bullfrog tuhle skladbu pro AWE32 ocividne zkomponoval jinak.

**Nahravka `004_C2INTRO.ogg` se nam nezarovna ani rytmicky** (0,046 pri
kalibraci "sum saha do 0,25"). Zkouseno i proti renderu menu a proti
nahravkam 003 a 005 - zadna kombinace nesedi, takze to neni prohozene
oznaceni. Menu pritom sedi na 0,844, takze metoda na tomhle materialu
funguje.

Zaver: pro intro **zatim nemame platnou referenci**. Bud je ta nahravka
z jineho zarizeni (v `midi/` jsou varianty `_f`/`_g`/`_r`/`_w`, tedy ctyri
ruzne cilove zvukovky), nebo je to jina aranz. Dokud se to nerozhodne, nema
smysl podle ni cokoli ladit - meril by se rozdil proti cizimu pristroji.

**Dalsi krok:** zachytit zvuk primo z 86Boxu, kde hra bezi, a porovnat ho
s tou nahravkou. Kdyz se rozejde i 86Box, nahravka neni z AWE32.

## Hi-Octane: tri nove overene dvojice a vlastni banka

Nahravky `HO_TR1_2` / `HO_TR3_4` / `HO_TR5_6` lezely v `SAMPLES3`
jako **neprokazane** - nebylo k nim MIDI. Z ISO hry se ale da vytahnout
vsechno potrebne:

    python ../AWE32EmuData/tests/iso_list.py HIOCTANE.ISO --extract .../files
    python ../AWE32EmuData/tests/split_musicdat.py .../SOUND/MUSIC.DAT -o midi/hioctane

`MUSIC.DAT` je proste **20 XMI za sebou** (kazde zacina `FORM....XDIR`);
`split_musicdat.py` je rozdeli. Hra ma navic **vlastni banku 450 kB**
(`SOUND/BULLFROG.SBK`, 17 vzorku) - proti MC2, ktere ma jen 12 vzorku
odkazujicich do ROM, je to prvni velka uzivatelska banka s vlastnimi daty
v tehle sade.

`ho_pairs.py` nasel dvojice:

| skladba | nahravka | kotev | shoda | ujeti |
|---|---|---|---|---|
| ho_12 | HO_TR5_6 | 79 | **0,813** | 12 ms |
| ho_11 | HO_TR3_4 | 96 | **0,725** | 17 ms |
| ho_10 | HO_TR1_2 | 90 | **0,703** | 12 ms |

Ujeti 12-17 ms je radove lepsi nez u YouTube zaznamu (267-1950 ms) - tyhle
nahravky jsou z mnohem cistsiho zdroje. Kazda drzi **dve** skladby za sebou,
takze se zarovna jen jeji cast; zbytek not `note_probe.py` preskoci.

Skore proti nim: **4,47 / 4,83 / 3,26** - `ho-tr5` je nejlepsi dvojice,
jakou mame (pro srovnani `dance` 4,56).

Slabsi nalezy (`ho_00`, `ho_02`, `ho_05`...) se shodou 0,41-0,54 a 6-45
kotvami do mereni **nepatri** - vypadaji jako planý poplach.

### Zbyva: mame min vysek nez zelezo

Pres celou delku (ne kratke okno!):

| pasma [%] | <100 | 400 | 1600 | 6400 | 22k |
|---|---|---|---|---|---|
| nas ho-tr5 | 33,6 | 57,5 | 2,8 | 1,9 | 4,2 |
| **nahravka** | 25,5 | 52,2 | 3,2 | **4,2** | **14,9** |

Nad 6,4 kHz mame 6,1 % proti 19,1 % na zeleze. Stejny smer se ukazal
i proti zaznamu ze hry v 86Boxu - je to zatim nejsilnejsi otevrena stopa.

## Export SBK -> SF2: tri chyby, ktere nasel round-trip

Prevod banky do SF2 se da overit tvrde: nacist ji zpatky a porovnat, co z ni
vyleze **po notach**. Render intra Magic Carpet 2 (671 not) z puvodni
`BULLFROG.SBK` a z exportovane `.sf2`; `--dump-notes` da ke kazde note
dvacet registru. Kdyz sedi vsechny, prevod je v poradku.

Napoprve nesedelo nic a zvuk byl o 2,6 dB vedle. Tri ruzne priciny:

**1. Adresy o 1/2/3 slova vedle.** SF1 uklada rovnou adresy pro cip (uz
s korekci na interpolator), SF2 uklada indexy - a Creative ve svych vlastnich
bankach tyz vzorek popisuje o 1/2/3 slova jinak. Nase cteni to zohlednuje
(`- 1`, `- 2`, `- 3` ve vetvi pro SF2), takze zapis musi kompenzovat opacne.
Bez toho vysla smycka o **dve slova kratsi**: `loopEnd - ccca` bylo 0x1D50
misto 0x1D52 u 668 z 671 not. Po oprave sedi rozdil na vsech 670 merenych.

**2. Sustain se nepocital radou ovladace.** Mel jsem `0x7F - v`, ovladac ale
dela `v * 4 / 3` (orez na 0x7F) - zmereno na `SYNTHGM`, kterou mame v obou
formatech. U sustainu 99 by z me rady vyslo 28 kroku poklesu misto nuly.
Na teto konkretni bance to nebylo videt (vsechny zony maji 127, kde se obe
rady shoduji), ale na jinych by to bylo slysitelne.

**3. Hvezdicka ve jmenu vzorku.** Tohle byla ta hlavni. Vzorky ve wave ROM maji
u Creative jmeno s hvezdickou (`*BellTree`) a nase nacitani podle ni pozna,
ze ma sahat do ROM. Kdyz vzorek zapecem do souboru, uz v ROM neni - jenze
hvezdicka ve jmenu zustala, takze si ho prehravac sel zase hledat do ROM na
adresu 0x14D1F, kde jsou uplne jina data. Kanal 1 intra (zvonkohra) hral
**2,2x hlasiteji**.

Pozoruhodne na tom je, ze **vsech dvacet registru pritom sedelo**. Chyba byla
jen v tom, odkud se ctou vzorky - a to zadny registrovy vypis neukaze. Naslo
se to az rozpadem po kanalech: ch1 mel odchylku +7,7 dB, ostatni pod -20 dB.

Po opravach: hladiny 0,09067 proti 0,09068, kanal se zapecenym ROM vzorkem
-129 dB (tedy presne), zbytek -22 az -36 dB. Ten zbytek uz neni chyba prevodu -
jsou to jednorazove vzorky, kterym ovladac kladé smycku **za** vzorek
(`end+4`..`end+8`); v puvodni bance tam byla jeji vlastni vypln, v exportu je
nasich 46 nul, jak SF2 predepisuje.

## Dve banky najednou

Na SoundFont CD (`SAMPLES2/2/CD.iso`) je `DEMO/SBK.TXT`, ktery ke kazde
demo skladbe rika, kterou banku nahrat do ktereho slotu - a u dvou z nich jsou
**dve**:

> 3. Dancesbk.mid - Load 9ftgrand.sbk on Bank 1, load Gmdrum.sbk on Bank 2.

U nas to uz umime: `--sbk soubor@N` presune presety banky do MIDI banky N
a kazda banka dostane svuj kus DRAM za tou predchozi. Overeno na
`DANCESBK.MID`: proti renderu jen s GM se lisi 99 % vzorku, takze se obe banky
opravdu pouziji.

## Novy material: tri zaznamy tehoz DANCE.MID

`SAMPLES4/` - tri nahravky z jedne karty (CT3980), ktere se lisi jen zpusobem
zaznamu a verzi ovladace. `pair_finder.py` je vsechny tri priradil k
`DEMO/DANCE.MID` se shodou kotev 0,95-0,97 a **odstupem 2,4-2,5x** od druheho
kandidata; prevod casu vysel 1,000000 * audio + 0,35 s, tedy bez driftu tempa.
Takovy odstup nema zadny jiny material, ktery mame. Zarovnani: 44-45 kotev,
shoda 0,78-0,80, ujeti 6-12 ms.

Cenne je na nich to, ze dve z nich jsou digitalni zaznam ("bit perfect"), takze
u nich odpada ekvalizace nahravaci cesty - hlavni zdroj nasich nejvetsich
odchylek (viz sekce vyse). Rozdil mezi `dance-hw` a `dance-bp` je pritom
meritko toho, co je jeste sum cesty a co uz vlastnost cipu.

Ostatni nahravky z te davky se **neprokazaly**: Doom a Duke Nukem 3D k nim
nemame spravne MIDI (nejlepsi kandidat mel odstup 1,0-1,1x, coz je sum), a
`DOOMAWE.WAV` / `AWEWARCT.WAV` / `HERETAWE.WAV` z CD jsou herni montaze se
zvukovymi efekty, ne cisty render MIDI - `pair_finder.py` u nich sice hlasil
shodu, ale s korelaci 0,00 a odstupem 1,1x, takze je to plany poplach.

## Nejvetsi odchylky jsou ekvalizace nahravky - pozna se to podle znamenka

Zadani znelo "najdi nejvetsi odchylky, ktere **nejsou** z ekvalizace nahravky".
Po oprave metriky vysly nejvetsi rozdily v nejnizsich pasmech - a ukazalo se,
ze meni znamenko **podle zdroje nahravky**, ne podle skladby:

| zdroj | rozdil na 60 Hz (nahravka minus my) |
|---|---|
| **FLAC z demo CD** (georgia, relax, crazy, jump, mars) | +2,4 az **+10,1 dB** |
| YouTube zaznam (dance, pop, starman, styles1/2, symphony) | -2,1 az **-7,1 dB** |
| Hi-Octane mp3 (ho-tr1/3/5) | -7,9 / -8,0 / -7,9 |

Ztratove zdroje maji uriznute basy, bezztratove CD ne. Kdyby slo o nasi
chybu, znamenko by bylo u vsech stejne. Presne kvuli tomuhle `note_probe.py`
celkovou krivku odecita - rozhoduje az **vazeny rozkyv**, co zbyde po ni.

Po odecteni vychazi u spolehlivych dvojic 3,1-7,3. Vyssi maji jen ty se
znamou vadou reference: `violin` 12,8 (jen 10 kotev), `mc2-game2` 10,9
(hra ztlumuje kanaly), `concer` 8,0 (11 kotev).

**Zaver: po oprave panoramy a metriky nezbyla na spolehlivem materialu zadna
hruba vada jednotlivych nastroju.** Median odchylky po skupinach je 2,1 dB a
Hi-Octane - nejcistsi dvojice - nema ani jednu skupinu nad 4 dB.

### Planý poplach: posun -46 slov

V zebricku nejhorsich vzorku meli **vsichni** `ccca` presne 46 slov pred
zacatkem nasledujiciho vzorku v bance. Vypadalo to na chybu o jeden vzorek
vedle. Kontrola na dobre sedicich vzorcich ale ukazala tyz posun u **vsech**
(v `dance` u vsech osmi nejcastejsich) - je to proste vyplň, kterou
SoundFont predepisuje mezi vzorky. Nic z toho neplyne.

## Dve pasti v mereni, na ktere jsem se chytil

**1. Kratke okno.** Dvakrat jsem vyvodil zavěr z okna, ktere nepokryvalo
to, co jsem merit chtel:

- smycka `belltree` ma 1197 vzorku, meril jsem 4096 - ze 71 % tedy data
  **za koncem vzorku**, a vyslo z toho "smycka je basova" (neni, ma teziste
  17,4 kHz);
- spektrum renderu Hi-Octane jsem vzal z prvnich 6 s, coz byla vysoka
  predehra, a vyslo z toho "chybi nam uplne basy" (nechybi, pres celou
  delku sedi na 43,0 proti 41,1 %).

**2. Skore rostlo s poctem not.** Ve vazenem skore jsem secetl pres vsechny
noty i pasma, ale vydelil jen souctem vah - vysledek proto rostl s
odmocninou z poctu not (667 not = faktor 25,8). Projevilo se to az na
Hi-Octane, kde skore vyslo 94-172 misto obvyklych 4-8. Dvojice s ruznym
poctem not tim byly neporovnatelne; **uvnitr** jedne dvojice je faktor
konstantni, takze porovnani variant zustalo platne.

## Nejvetsi odchylky (`worst.py`)

    python ../AWE32EmuData/tests/worst.py --top 22

Seskupuje po **vzorku a kanalu** a scita pres vsechny dvojice: kdyz je tyz
vzorek vedle ve trech skladbach, je to vlastnost vzorku, ne nahoda mixu.

Median odchylky je **2,4 dB**; co je nad ~7 dB, uz nejde o nuanci. Nejhorsi
nalezy jsou v `mars` (10 skupin nad 4 dB) a `symphony` (7), zatimco
`dance`, `starman`, `georgia`, `crazy` a `mc2-menu` nemaji ani jednu.

Pozor na `mars`: ma jen 6 kotev, takze cast tech odchylek muze byt spatne
zarovnani, ne skutecny rozdil zvuku. Nez se podle nej neco rozhodne, chce to
lepsi zarovnani te dvojice.

## Nalezy z `analyze/EMU8000-next.md`

### C - reset filtru: chyba potvrzena, ale neslysitelna

Pri startu noty se nulovaly jen `filtIc1/filtIc2`, ne uz `filtIc3/filtIc4`
(druhy stupen kaskady) a `filtLp1` (jednopolovy rezim). Opraveno.

**Na skore to nema zadny vliv** (4,8388 pred i po). Ty stavy se pri vychozich
dvou polech vubec nepouzivaji, takze k uniku energie mezi notami nemuze dojit.
Oprava ma smysl jen pro `--filter-poles 1|4`. Analyza to vedla jako
"slysitelny artefakt" - zmereno to tak nevychazi.

### D - zaklad Q: nalez sedi, oprava nepomaha

Nalez je spravny a jemnejsi, nez vypada: v kodu bylo
`max(0.7071, pow(10, res/20))`, jenze `pow` je pro `res >= 0` vzdycky `>= 1`,
takze se 0,7071 **nikdy** neuplatnila. Aby Q = 0 znamenalo Butterworth, musi
se zaklad **nasobit**, ne maximovat.

Udelan prepinac `--q-base` a zmereno: rozdil je v obou smerech a v prumeru
0,015 dB. **Nerozhoduje.** Vychozi zustava 1,0.

### J - interpolace: mirne svedci pro linearni

| varianta | prumer pres 10 dvojic |
|---|---|
| Catmull-Rom (vychozi) | 6,105 |
| **linearni** | **6,014** |
| 86Box | 6,081 |

Linearni je lepsi na 5 dvojicich z 10, ale **vyhry jsou vetsi nez prohry**
(nejvetsi zlepseni 0,53, nejvetsi zhorseni 0,04). Sedi to na to, co rika
Rossum o zlevnene "G0.5" verzi G-chipu.

**Ale:** polovinu toho zlepseni dela sam `violin`, tedy nejmene spolehlivy
par. Bez nej zbyva 0,042 - porad ve stejnem smeru, uz ale slabe. Nez se to
prehodi natrvalo, chce to cileny test na hluboko transponovanych vzorcich,
jak navrhuje `analyze` (delsi konvoluce se pozna podle energie nad 15 kHz).

## Past: jedna skladba lze

Dvakrat po sobe se stalo, ze zaver z jedne skladby pres celou sadu neplatil.

**`--filter-top`.** Na DANCE skore monotonne klesalo (8000 -> 4,839,
20000 -> 4,789) a vypadalo to na systematicky prilis tmavy filtr. Pres
vsech deset dvojic to ale jde **nahoru** (6,105 -> 6,116 -> 6,145); canon a
nutcrack se vyrazne zhorsi. Vychozich 8000 Hz zustava.

**Nase jadro proti 86Boxu.** Na DANCE je 86Box vyrazne blize zelezu
(4,33 vs 4,84) a vypadalo to jako latka, kterou mame podlezt. Pres celou
sadu jsou ale **vyrovnane** (6,081 vs 6,105, rozdil 0,4 %): my jsme lepsi na
nutcrack, symphony, mozart a styles1, 86Box na dance, pop, violin a canon.

Zaver: **nic se nerozhoduje pod peti dvojicemi.**

## Nalez: panorama - opraveno, prenesno do obou souboru

Zachyt zvuku primo z 86Boxu (viz nize) ukazal na intru MC2 plochy rozdil
6,1 dB mezi nasim jadrem a `snd_emu8k.c` pri **totoznych zapisech
registru** (24/24 na vsech 262 notach). Prohledanim `snd_emu8k.c` se nasla
prescina:

```c
emu_voice->vol_l = emu_voice->psst_pan;        // 0..255
emu_voice->vol_r = 255 - (emu_voice->psst_pan);
(*buf++) += (dat * emu_voice->vol_l) >> 8;
```

Cip ma panoramu jako **prostou nasobicku**, ne constant-power `sin/cos`,
ktery jsme meli my. Uprostred panoramy dela nasobicka -6 dB do kazdeho
kanalu, sin/cos jen -3 dB - rozdil 3,01 dB presne odpovida namerenym
3,37-3,41 dB na izolovanych notach.

Je to nalez F z `analyze/EMU8000-next.md` (odhadoval "az ~3 dB", ted
zmereno). **86Box uz to mel spravne** - opravovalo se jen nase jadro
(`Emu8000.cpp`), do `snd_emu8k.c` se nesahalo. Linearni panorama je od
2026-09-02 vychozi (`--pan power` vraci puvodni chovani na porovnani).

**Overeno proti 20 dvojicim nahravka/MIDI, ne jen proti 86Boxu:**
linearni panorama 6,026, puvodni constant-power 6,089 (vyssi = horsi).

## Nalez: interpolace - "3 Point sample interpolation" z dokumentace

`analyze/hackpedia.txt` (Vu, Un-official AWE32 Programming Guide, 1995)
uvadi u cipu primo "3 Point sample interpolation" - trebaze `analyze`
vedl typ interpolace jako nerozhodnutou otazku (nalez J), tenhle zdroj
davu jasnou odpoved.

Implementovana kvadraticka (Lagrangeova) interpolace pres tri body, ve
dvou varinatach lisicich se tim, ktere vzorky se berou (`Point3` dopredu,
`Point3c` soumerne) - v mereni k nerozeznani.

**Zmereno proti 20 dvojicim:** Point3 5,937 proti 6,025 u puvodni kubicke
(Catmull-Rom). Zavedeno jako vychozi (`--interp cubic|linear|3pointc` pro
porovnani).

### Prenesno i do `snd_emu8k.c`

Podle pravidla "oprava cipu = oprava v obou souborech" pribyla stejna
kvadraticka interpolace (`EMU8K_READ_INTERP_POINT3`) i do sdileneho
`docs/86box-src/master-full/src/sound/snd_emu8k.c` a nahradila tam vychozi
`RESAMPLER_CUBIC`. Puvodni cesta zustava dostupna prepnutim definice.

`chip_diff.py` ted rozeznava tri druhy odchylek od ciste upstreamove kopie:
znama (stopovani, registr ID - beze zmeny zvuku), **ladena** (zamerne
zmeny podle tohohle dokumentu) a nova (neplanovana - to je varovny signal).

**Nezapomenout:** tenhle sdileny soubor pohani i skutecny `86Box.exe` pro
VM (`ref86box/build_86box.sh`, zdroj `docs/86box-src/master-full`). Po zmene
v cipu je potreba VM prekompilovat, jinak zachyty ze hry porad hrajou
starym kodem.

### Zbytek po oprave: cip vs cip, primo (bez zarovnani)

S totoznym panoramatem i interpolaci klesl rozkyv krivky mezi nasim
jadrem a `snd_emu8k.c` na intru MC2 z **6,1 dB na 3,4 dB**. Zbytek je
plochy (+3 az +3,7 dB od ~500 Hz vys, ~0 dB pod 120 Hz) - vypada na filtr.

**Duvod:** ovladac zapisuje cutoff jako `cutoff << 8` = `0xFF00`, ne
`0xFFFF`. Podminka v `snd_emu8k.c`, kdy se filtr **preskoci** (`filterq_idx == 0
&& cvcf_curr_filt_ctoff == 0xFFFF`) proto nikdy neplati - filtr bezi
i pri "plne otevreno". My jsme se drzeli Programmer's Guide a filtr jsme
v tehle situaci vypinali.

**Dulezita oprava vlastniho drivejsiho omylu:** prvni pokus opravit tohle
implementoval spatnou vetev - `FILTER_INITIAL`, ktera je v `snd_emu8k.c` zakomentovana (`#if 0`). Skutecne aktivni je **`FILTER_MOOG`** - 4stupnova
kaskada jednopolovych filtru s kladnou zpetnou vazbou (Moog ladder), zcela
jiny model. Test s FILTER_INITIAL zhorsil skore i rozkyv (8,5 -> 10,3 dB) -
spravny zaver z toho je "to neni ta chyba", ne "filtr nehraje roli".
**Port FILTER_MOOG zustava jako dalsi krok**, neni jeste hotovy.

## Kde nas cip stoji jinak nez 86Box

Obe varianty pohani **tataz vrstva se stejnymi zapisy do registru**, takze
rozdil je ciste v cipu. Meri se bez zarovnani (posun mezi rendery je
4 vzorky, tedy 0,09 ms).

Rozptyl po notach je **3,5 dB**, prestoze celkova krivka je placata
(rozkyv 1,0 dB). Neni to tedy ekvalizace, ale rozdil od noty k note.

Na samostatnych notach (`make_probe_mid.py` + `env_cmp.py`, 20 not,
4 nastroje, 5 oktav) je videt cim:

| | rozdil (86Box minus my) |
|---|---|
| vrchol | **-3,41 dB** |
| energie | **-3,37 dB** |
| doba nabehu | 0,0 ms |
| pokles o 20 dB | -7,3 ms |

**Nas cip je o ~3,4 dB hlasitejsi**, nabeh ma stejny a doznivani jen o chlup
pomalejsi. Neni to chyba **sklonu** hlasitostni krivky: po skupinach utlumu
odchylka neroste (0,2-0,6 dB napric, jen krajni skupina 1,3), je to plochy
posun.

Ktera hodnota je spravna, se z nahravek **rozhodnout neda** - celkova uroven
je dana nahravaci cestou. Rozhodnout to muze jen odvozeni z ovladace nebo
z Programmer's Guide.

## Noty zacinaji lip, nez pokracuji

Sonda umi merit i posunutym oknem (`--delay`) a zvlast panorama
(`--stereo`). Na DANCE proti zelezu:

| okno | skore |
|---|---|
| nastup | 4,84 |
| panorama | 4,50 |
| doznivani +0,25 s | **5,15** |
| doznivani +0,6 s | **5,10** |

Nastup sedi lip nez doznivani. To ukazuje na **obalku a efektovy retez**,
ne na vzorky ani na filtr pri nastupu - a sedi to na nalez H (rychlosti
obalky se ctou jen jednou za blok, tedy ~23 ms zpozdeni na release).

## Nastroje

    python ../AWE32EmuData/tests/tune.py --warp          # jednou: zarovnat
    python ../AWE32EmuData/tests/tune.py --try "--interp linear"
    python ../AWE32EmuData/tests/tune.py --only dance --detail --by sample

    python ../AWE32EmuData/tests/note_probe.py --warp w.json --trace n.trace \
        --notes n.csv --ours nas.wav --ref ref.wav [--stereo|--delay 0.25]
        [--by sample|ch|note|atten|vel]

    python ../AWE32EmuData/tests/make_probe_mid.py probe.mid --program 0
    python ../AWE32EmuData/tests/env_cmp.py a.wav b.wav --trace probe.trace

Jedna varianta pres vsech deset dvojic trva **~3,5 minuty**.

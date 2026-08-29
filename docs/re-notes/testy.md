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

**DOSMID je v `C:\DOSMID\`,** ne v korenu. Kopie v korenu existuje, ale
nespusti se spravne.

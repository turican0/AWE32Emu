# EMU8000 - registrova mapa odvozena z AWEUTIL.COM

Zdroj: IDA disassembly `../AWE32EmuData/SoundBlaster AWE32/SBAWE32/AWEUTIL.COM.asm`
(AWEUTIL TSR Version 1.01, francouzska lokalizace,
SHA256 `2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`).

## Zdroje

1. **AWE32/EMU8000 Programmer's Guide**, Revision 1.00, Dave Rossum,
   E-mu/Creative Technology Ltd. 1994-1996 - *primarni zdroj pravdy* pro
   vyznam registru a bitovych poli.
   <https://www.dosdays.co.uk/media/creative/emu8kpgm.pdf>
2. **Linuxovy ALSA driver** - `sound/isa/sb/emu8000.c` a hlavicka
   `include/sound/emu8000_reg.h` (registrova mapa, inicializacni sekvence).
   <https://github.com/torvalds/linux/blob/master/sound/isa/sb/emu8000.c>
3. `AWEUTIL.COM` (DOS TSR, verze 1.01) - disassembly, pristup k registrum
   a inicializace na realnem hardwaru.
4. `WINDRV/SBAWE32.DRV` (Windows 3.x AWE32 MIDI driver, NE, 45 kB) -
   prevodni tabulky obalek a note-on sekvence.

Znaceni:
- **[PG]** = Programmer's Guide (primarni zdroj)
- **[ASM]** = primo odvozeno z kodu ovladace (jista vec)
- **[ALSA]** = potvrzeno hlavickou `emu8000_reg.h`
- **[?]** = odhad, nutno overit merenim

---

## 1. I/O porty  [ASM]

`word_105F0` = bazovy port Sound Blasteru (v souboru inicializovan na `220h`,
za behu prepsan z promenne `BLASTER`). `word_105F2` = MPU-401 baze (`330h`).

Ctyri pristupove rutiny (`sub_10EAC` zapis word, `sub_10EFA` cteni word,
`sub_10F46` zapis dword, `sub_10F9C` cteni dword) pocitaji porty takto:

    ; pointer port
    DH   = LOBYTE(base) - 2
    DX   = (DH:DL) & 0x0C00
    DX   = DX | (base + 0x0E)
    DL   = DL & 0xF3
    -> pointer = base + 0xC02

    ; datovy port
    DX   = ((sel >> 8) & 2) + base      ; base + 0 nebo base + 2
    BX   = (sel & 0x0C00)  + base       ; base + 0/0x400/0x800/0xC00
    port = DX | BX

Pro `base = 220h` vychazi:

| sel bity 11..9 | port | jmeno |
|---|---|---|
| 010 | `620h` | Data0 (low word 32bit registru) |
| 011 | `622h` | Data0 high |
| 100 | `A20h` | Data1 (low word 32bit registru) |
| 101 | `A22h` | Data1 high / samostatny 16bit port (v dokumentaci "Data2") |
| 110 | `E20h` | Data3 (16bit) |
| 111 | `E22h` | **Pointer** |

Dulezite: 32bit zapis (`sub_10F46`) posle low word na `port` a high word na
`port+2`. Pro Data1 to znamena, ze horni polovina 32bit registru fyzicky
lezi na `A22h`, tj. na stejnem portu jako "Data2". To neni chyba, tak je
cip navrzen.

## 2. Kodovani `sel`  [ASM]

Ovladac predava registr jako jedno 16bit cislo:

    sel = (regIndex << 12) | (portSel << 9) | voice
            bity 15..12       bity 11..9      bity 4..0

Pointer registr pak dostane:

    pointer = ((sel & 0x7000) >> 7) | (sel & 0x1F)
            = (regIndex << 5) | voice

tj. **pointer: bity 0..4 = cislo hlasu (0-31), bity 5..7 = index registru (0-7)**.

Potvrzeno nezavisle dvakrat:
- `SBAWE32.DRV` ma stejny vypocet inline (`sub_11CA`: `ax = reg<<5 | voice`)
  a tabulku portu na `ds:0712`:
  `0220 0222 0620 0622 0A20 0A22 0E20 0E22` - tj. index 2..6 = Data0..Data3,
  index 7 = pointer.
- ALSA `emu8000_reg.h`: `#define EMU8000_CMD(reg, chan) ((reg)<<5 | (chan))`
  a porty `DATA0=port1, DATA1=port2, DATA2=port2+2, DATA3=port3, PTR=port3+2`.

## 3. Registry potvrzene ovladacem

Odvozeno z inicializacni sekvence (`sub_12B40` -> `sub_126E8`, `sub_127AE`,
`sub_1288C`, `sub_12A20`).

### Data0 (`620h`/`622h`), 32bit

| reg | sel | jmeno | vyznam |
|---|---|---|---|
| 0 | `04xx` | CPF | Current Pitch (hi16, linearni) + Fractional address (lo16) [PG] |
| 1 | `14xx` | PTRX | Pitch Target (hi16), Reverb send (bity 15..8), aux (bity 7..0) [PG] |
| 2 | `24xx` | CVCF | Current Volume (hi16) + Current Filter cutoff (lo16) [PG] |
| 3 | `34xx` | VTFT | Volume Target (hi16) + Filter cutoff Target (lo16) [PG] |
| 4 | `44xx` | - | nepouzito [?] |
| 5 | `54xx` | - | nepouzito [?] |
| 6 | `64xx` | PSST | Pan (31..24, 0 = vpravo) + Loop Start (23..0) [PG] |
| 7 | `74xx` | CSL | Chorus send (bity 31..24) + Loop End address (bity 23..0) [PG] |

### Data1 (`A20h`), 32bit / 16bit

| reg | sel | jmeno | vyznam |
|---|---|---|---|
| 0 | `08xx` | CCCA | Filter Q (31..28), DMA/WR/RIGHT (26/25/24), Current address (23..0) [PG] |
| 1 | `18xx` | viz nize | registry adresovane cislem "hlasu" |
| 2 | `28xx` | INIT1 | init pole 1 [ASM] |
| 3 | `38xx` | INIT3 | init pole 3 [ASM] |
| 4 | `48xx` | ENVVOL | zpozdeni (delay) volume envelope [ASM init=0] |
| 5 | `58xx` | DCYSUSV | decay/sustain volume envelope [ASM init=0080h] |
| 6 | `68xx` | ENVVAL | zpozdeni modulation envelope [ASM init=0] |
| 7 | `78xx` | DCYSUS | decay/sustain modulation envelope [ASM init=0] |

Registry na `reg 1`, kde "cislo hlasu" slouzi jako index registru [ASM]:

| voice | sel | jmeno | zapisovana hodnota pri init |
|---|---|---|---|
| 9 | `1809` | HWCF4 | `00000000` (dword) |
| 10 | `180A` | HWCF5 | `00000083` (dword) |
| 13 | `180D` | HWCF6 | `00008000` (dword) |
| 14 | `180E` | HWCF7 | `00000000` (dword) |
| 20 | `1814` | SMALR | 0 |
| 21 | `1815` | SMARR | 0 |
| 22 | `1816` | SMALW | 0 |
| 26 | `181A` | SMLD | (cteni/zapis DRAM) |
| 29 | `181D` | HWCF1 | `0059` |
| 30 | `181E` | HWCF2 | `0020` |
| 31 | `181F` | HWCF3 | `0004` |

### Data2 (`A22h`), 16bit

| reg | sel | jmeno |
|---|---|---|
| 1 | `1A1B` | WC - wave counter (voice 27), pouzity jako casova zakladna pri cekacich smyckach [ASM] |
| 2 | `2Axx` | INIT2 [ASM] |
| 3 | `3Axx` | INIT4 [ASM] |
| 4 | `4Axx` | ATKHLDV - attack/hold volume envelope [ASM init=0] |
| 5 | `5Axx` | LFO1VAL - zpozdeni LFO1 [ASM init=0] |
| 6 | `6Axx` | ATKHLD - attack/hold modulation envelope [ASM init=0] |
| 7 | `7Axx` | LFO2VAL - zpozdeni LFO2 [ASM init=0] |

### Data3 (`E20h`), 16bit

| reg | sel | jmeno | init |
|---|---|---|---|
| 0 | `0Cxx` | IP - Initial Pitch | 0 |
| 1 | `1Cxx` | IFATN - Initial Filter cutoff (hi8) + Attenuation (lo8) | `FF00` |
| 2 | `2Cxx` | PEFE - Pitch/Filter envelope amount | 0 |
| 3 | `3Cxx` | FMMOD - LFO1 -> pitch (hi8) / filter (lo8) | 0 |
| 4 | `4Cxx` | TREMFRQ - LFO1 -> volume (hi8) / frekvence LFO1 (lo8) | `0018` |
| 5 | `5Cxx` | FM2FRQ2 - LFO2 -> pitch (hi8) / frekvence LFO2 (lo8) | `0018` |
| 6 | `6Cxx` | nezname | 0 |
| 7 | `7C00` | ID registr - detekce cipu, ocekava se `000Ch` [ASM] |

## 4. Inicializacni sekvence  [ASM, `sub_12B40`]

    1. read  sel 7C00            ; ocekava 0x0C, jinak "neni AWE"
    2. write HWCF1 = 0059h
    3. write HWCF2 = 0020h
    4. write HWCF3 = 0004h
    5. sub_126E8: pro kazdy hlas 0..31 zapsat
          DCYSUSV=0080h, ATKHLD=0, DCYSUS=0, IP=0, IFATN=FF00h, PEFE=0,
          FMMOD=0, TREMFRQ=0018h, FM2FRQ2=0018h, (Data3 reg6)=0,
          LFO2VAL=0, LFO1VAL=0, ATKHLDV=0, ENVVOL=0, ENVVAL=0
    6. sub_127AE: cekat na WC (sel 1A1B), pak pro kazdy hlas 0..31 zapsat dword
          PTRX=0, VTFT=0000FFFF, PSST=0, CSL=0, CPF=0, CVCF=0000FFFF,
          CCCA=0, (Data0 reg5)=0, (Data0 reg4)=0
    7. sub_1288C: SMALR/SMARR/SMALW = 0, pak odeslani 4 init poli (viz nize)
    8. sub_12A20: nastaveni hlasu 30 a 31 jako "DRAM refresh" kanalu
    9. write HWCF3 = 0004h
    10.read HWCF2, bit 6 -> priznak (typ karty / velikost pameti)

Pozor na `0000FFFF` vs `FFFFFFFF`: v kroku 6 se hornich 16 bitu nuluje
pres `xor dx,dx`, takze VTFT i CVCF dostavaji **hlasitost 0 a filtr plne
otevreny**. Naproti tomu v kroku 8 je pouzito `cwd`, ktere 0xFFFF
znamenkove rozsiri, takze tam skutecne jde o `FFFFFFFF`. Totez plati pro
`PSST(30) = FFFFFFE0`. `SBAWE32.DRV` (sub_1320) zapisuje v kroku 6 stejne
hodnoty `0000FFFF`.

### Krok 8 - "DRAM refresh" hlasy 30/31  [ASM]

    PSST(30)=0000FFE0  CSL(30)=00FFFFE8  PTRX(30)=0  CPF(30)=0  CCCA(30)=00FFFFE3
    PSST(31)=00FFFFF0  CSL(31)=00FFFFF8  PTRX(31)=000000FF CPF(31)=00008000
    CCCA(31)=00FFFFF3
    ; pak primy port I/O: pointer=003Eh, Data0=0, cekani na bit 12 pointeru,
    ; Data0+2=4828h, pointer=003Ch, Data1=0
    VTFT(30)=FFFFFFFF  VTFT(31)=FFFFFFFF

### Init pole  [ASM]

Tri sady po 128 slovech (4 registry x 32 hlasu) na offsetech `341Ch`,
`351Ch`, `361Ch` v COM souboru. Posilaji se pres INIT1..INIT4:

    sada A -> init1   (offset 341Ch)
      cekani ~0x401 tiku WC
    sada B -> init2   (offset 351Ch)
    sada C -> init4   (offset 361Ch, u lichych hlasu OR 8000h)
      HWCF4=0, HWCF5=83h, HWCF6=8000h, HWCF7=0
    sada C -> init3   (offset 361Ch, bez OR)

Sada A zacina `03FF 0030 07FF 0130 0BFF 0230 ...`, sada B je totez s `8000h`
v lichych slovech, sada C zacina `0C10 8470 14FE B488 167F A470 18E7 84B5 ...`.

Sada C obsahuje parametry reverbu prolozene s "microcode" hodnotami; pred
odeslanim se do ni na 8 mist zapisou parametry chorusu z tabulky
(`word_1377C..word_1378A`, default `C280 C380 0001 821E D280 031E D380 0001`).
Sest dalsich presetu chorusu lezi na offsetu `371Ch`.

Patchovana mista v sade C (index slova v ramci 128):

| index | reg/hlas | zdroj |
|---|---|---|
| 81 | INIT3 v17 | `word_13782` |
| 83 | INIT3 v19 | `word_13784` |
| 91 | INIT3 v27 | `word_13786` |
| 97 | INIT4 v1  | `word_1377C` |
| 103 | INIT4 v7  | `word_13788` |
| 113 | INIT4 v17 | `word_1377E` |
| 117 | INIT4 v21 | `(word_13780 + word_1378A) + 263h` |
| 125 | INIT4 v29 | `(word_13780 + word_1378A) - 7C9Dh` |

**Pro softwarovou emulaci jsou init pole neprenositelna** - konfiguruji
interni DSP realneho cipu (reverb/chorus), ne chovani hlasu. Emulace je
prijima a ignoruje; reverb/chorus se resi vlastnim algoritmem podle
vyznamu parametru, ne prehranim tabulky. Diky tomu take v repozitari
nemusi byt zadna binarni data Creative.

## 5. Vyznam bitu (podle Programmer's Guide)

| registr | pole | vyznam |
|---|---|---|
| CPF | 31-16 | current pitch, **linearni**, 0x4000 = bez posunu [PG] |
| CPF | 15-0 | zlomkova cast adresy |
| PTRX | 31-16 | pitch target | 
| PTRX | 15-8 | reverb send (0 = nic, 0xFF = max) |
| PTRX | 7-0 | aux byte, nepouzity |
| CVCF/VTFT | 31-16 | aktualni / cilova hlasitost |
| CVCF/VTFT | 15-0 | aktualni / cilovy mezni kmitocet |
| PSST | 31-24 | pan, **0 = zcela vpravo, 0xFF = zcela vlevo** [PG] |
| PSST | 23-0 | loop start |
| CSL | 31-24 | chorus send |
| CSL | 23-0 | loop end |
| CCCA | 31-28 | filter Q, 0 = bez rezonance, 15 = cca 24 dB |
| CCCA | 27 | vzdy 0 |
| CCCA | 26 | DMA |
| CCCA | 25 | WR (1 = zapis) |
| CCCA | 24 | RIGHT (1 = pravy DMA proud) |
| CCCA | 23-0 | aktualni adresa |
| IP | - | 0xE000 = bez posunu, 0x1000 = oktava, **logaritmicky** |
| IFATN | 15-8 | mezni kmitocet filtru, ctvrt pultony, 0x00 = 125 Hz |
| IFATN | 7-0 | utlum po 0.375 dB, 0xFF = 96 dB |
| DCYSUSV | 15 | 0 = zapisuje se decay, 1 = release |
| DCYSUSV | 14-8 | sustain level po 0.75 dB (0x7F = bez utlumu) |
| DCYSUSV | 7 | envelope generator vypnut |
| DCYSUSV | 6-0 | decay/release rate (0 = bez decay) |
| ATKHLDV | 14-8 | hold po 92 ms (0x7F = bez prodlevy, 0 = 11.68 s) |
| ATKHLDV | 6-0 | attack (0 = nikdy, 1 = 11.88 s, 0x7F = 6 ms) |
| ENVVOL/ENVVAL/LFOnVAL | - | 0x8000 = bez prodlevy, po 725 us dolu |
| PEFE | 15-8 | mod. obalka -> pitch, +-1 oktava pri 0x7F/0x80 |
| PEFE | 7-0 | mod. obalka -> filtr, +-6 oktav |
| FMMOD | 15-8 | LFO1 vibrato, +-1 oktava |
| FMMOD | 7-0 | LFO1 -> filtr, +-3 oktavy |
| TREMFRQ | 15-8 | LFO1 tremolo, +-12 dB |
| TREMFRQ | 7-0 | frekvence LFO1 po 0.042 Hz (0xFF = 10.72 Hz) |
| FM2FRQ2 | 15-8 | LFO2 vibrato, +-1 oktava |
| FM2FRQ2 | 7-0 | frekvence LFO2, stejne jednotky |

Adresy v CCCA/PSST/CSL: skutecne misto ve zvukove pameti je o jedno slovo
vys, nez rika registr ("interpolator offset") [PG].

### Casove konstanty obalek [ASM + PG]

`SBAWE32.DRV` obsahuje dve prevodni tabulky po 128 slovech (v ms):
attack na `ds:1552`, decay/release na `ds:1650`. Vyhledavaci rutiny
`sub_2BC0` a `sub_2BF0` obe indexuji tabulku hodnotou `rate - 1`.

Obe tabulky presne odpovidaji jednomu vzorci `cas = base / k(i)`:

```
k(i):  i = 0..127
       skupina g = i / 16, pozice m = i % 16
       g == 0  ->  k = m + 1                 (1..16)
       g >= 1  ->  k = (m + 17) << (g - 1)   (17..32, 34..64, 68..128, ...)

attack_ms(r)       = 11878 / k(r-1)     r = 1..127,  r = 0 znamena "nikdy"
decay_release_ms(r)= 47513 / k(r-1)     r = 1..127,  r = 0 znamena "bez decay"
```

Overeno proti obema tabulkam polozku po polozce - **0 odchylek**, takze do
kodu staci vzorec a nemusi se kopirovat zadna data z ovladace.

Decay/release cas z tabulky odpovida prubehu pres **100 dB**, ne pres cely
96dB rozsah: Programmer's Guide udava rate 0x7F = 240 us/dB (tabulka 24 ms)
a rate 0x01 = 470 ms/dB (tabulka 47513 ms). Obalka se tedy pocita jako
rychlost v dB/s, ne jako pevny celkovy cas.

## 6. Co jeste chybi

- Prevod SoundFont generatoru -> registry (v `SBAWE32.DRV` kolem `0x2C1E`,
  struktura patche na `[si+...]`, viz note-on na `0x040A`)
- Rozpor v Programmer's Guide u meze filtru: "ctvrt pultony od 125 Hz"
  vs "0xFF = 8 kHz" (255 ctvrt pultonu = 4966 Hz, na 8 kHz je potreba 288).
  Emulace pouziva ctvrt pultony, protoze na tu skalu sedi hloubky modulaci.
- Tri prevodni krivky MIDI -> dB v `SBAWE32.DRV` (`ds:0592`, `ds:0612`,
  `ds:0692`, po 128 bajtech, indexovane 0..127, vysledek v dB) - tykaji se
  MIDI vrstvy ovladace, ne chovani cipu
- Chorus/reverb

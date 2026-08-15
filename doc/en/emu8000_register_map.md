# EMU8000 - registrova mapa odvozena z AWEUTIL.COM

Zdroj: IDA disassembly `SoundBlaster AWE32/SBAWE32/AWEUTIL.COM.asm`
(AWEUTIL TSR Version 1.01, francouzska lokalizace,
SHA256 `2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`).

Znaceni:
- **[ASM]** = primo odvozeno z kodu ovladace (jista vec)
- **[DOC]** = doplneno z verejne dokumentace k EMU8000, ovladacem nepotvrzeno
- **[?]** = odhad, nutno overit dalsim RE (SBAWE32.DRV) nebo merenim

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
    
## 3. Registry potvrzene ovladacem
    
Odvozeno z inicializacni sekvence (`sub_12B40` -> `sub_126E8`, `sub_127AE`,
`sub_1288C`, `sub_12A20`).

### Data0 (`620h`/`622h`), 32bit

| reg | sel | jmeno | vyznam |
|---|---|---|---|
| 0 | `04xx` | CPF | Current Pitch (hi16) + Fractional address (lo16) [ASM zapis, DOC vyznam] |
| 1 | `14xx` | PTRX | Pitch Target (hi16), Reverb send (bity 15..8), aux (bity 7..0) [ASM/DOC] |
| 2 | `24xx` | CVCF | Current Volume (hi16) + Current Filter cutoff (lo16) [DOC] |
| 3 | `34xx` | VTFT | Volume Target (hi16) + Filter cutoff Target (lo16) [ASM `341E`/`341F` = FFFFFFFF] |
| 4 | `44xx` | - | nepouzito [?] |
| 5 | `54xx` | - | nepouzito [?] |
| 6 | `64xx` | PSST | Pan (bity 31..24) + Loop Start address (bity 23..0) [ASM `641E`=0000FFE0] |
| 7 | `74xx` | CSL | Chorus send (bity 31..24) + Loop End address (bity 23..0) [ASM `741E`=00FFFFE8] |

### Data1 (`A20h`), 32bit / 16bit

| reg | sel | jmeno | vyznam |
|---|---|---|---|
| 0 | `08xx` | CCCA | Filter Q (bity 31..28), control (27..24), Current address (23..0) [ASM `081E`=00FFFFE3] |
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

## 4. Initialization sequence [ASM, `sub_12B40`]

1. read sel 7C00 ; expects 0x0C, otherwise "not AWE"
2. write HWCF1 = 0059h
3. write HWCF2 = 0020h
4. write HWCF3 = 0004h
5. sub_126E8: for each voice 0..31 write
DCYSUSV=0080h, ATKHLD=0, DCYSUS=0, IP=0, IFATN=FF00h, PEFE=0,
FMMOD=0, TREMFRQ=0018h, FM2FRQ2=0018h, (Data3 reg6)=0,
LFO2VAL=0, LFO1VAL=0, ATKHLDV=0, ENVVOL=0, ENVVAL=0
6. sub_127AE: wait for WC (sel 1A1B), then for each voice 0..31 write dword
PTRX=0, VTFT=FFFFFFFF, PSST=0, CSL=0, CPF=0, CVCF=FFFFFFFF,
CCCA=0, (Data0 reg5)=0, (Data0 reg4)=0
7. sub_1288C: SMALR/SMARR/SMALW = 0, then send 4 init fields (see below)
8. sub_12A20: set voice 30 and 31 as "DRAM refresh" channel
9. write HWCF3 = 0004h
10.read HWCF2, bit 6 -> flag (card type / memory size)

Step 6 writes `2400h` (= CVCF) and `3400h` (= VTFT) with `FFFFFFFF`,
the rest with zero - i.e. voices start with full volume/open filter
in current and target registers, but with envelope off engine
(DCYSUSV=0080h).

### Step 8 - "DRAM refresh" votes 30/31 [ASM]

PSST(30)=0000FFE0 CSL(30)=00FFFFE8 PTRX(30)=0 CPF(30)=0 CCCA(30)=00FFFFE3
PSST(31)=00FFFFF0 CSL(31)=00FFFFF8 PTRX(31)=000000FF CPF(31)=00008000
CCCA(31)=00FFFFF3
; then direct port I/O: pointer=003Eh, Data0=0, waiting for bit 12 of pointer,
; Data0+2=4828h, pointer=003Ch, Data1=0
VTFT(30)=FFFFFFFF VTFT(31)=FFFFFFFF

### Init field [ASM]

Three sets of 128 words (4 registers x 32 voices) at offsets `341Ch`,
`351Ch`, `361Ch` in COM file. They are sent via INIT1..INIT4:

set A -> init1 (offset 341Ch)
waiting for ~0x401 tick WC
set B -> init2 (offset 351Ch)
set C -> init4 (offset 361Ch, for odd voices OR 8000h)
HWCF4=0, HWCF5=83h, HWCF6=8000h, HWCF7=0
set C -> init3 (offset 361Ch, without OR)

Set A starts `03FF 0030 07FF 0130 0BFF 0230 ...`, set B is the same with `8000h`
in odd words, set C starts `0C10 8470 14FE B488 167F A470 18E7 84B5 ...`.

Set C contains reverb parameters interspersed with "microcode" values; before being sent, the chorus parameters from the table are written to 8 locations
(`word_1377C..word_1378A`, default `C280 C380 0001 821E D280 031E D380 0001`).
Six more chorus presets are located at offset `371Ch`.

Patched locations in set C (word index in frame 128):

| index | reg/voice | source |
|---|---|---|
| 81 | INIT3 v17 | `word_13782` |
| 83 | INIT3 v19 | `word_13784` |
| 91 | INIT3 v27 | `word_13786` |
| 97 | INIT4 v1 | `word_1377C` |
| 103 | INIT4 v7 | `word_13788` |
| 113 | INIT4 v17 | `word_1377E` |
| 117 | INIT4 v21 | `(word_13780 + word_1378A) + 263h` |
| 125 | INIT4 v29 | `(word_13780 + word_1378A) - 7C9Dh` |

**For software emulation, init fields are non-portable** - they configure the internal DSP of the real chip (reverb/chorus), not the voice behavior. Emulation is
accepted and ignored; reverb/chorus is solved by its own algorithm according to the meaning of the parameter, not by playing the table. Thanks to this, no Creative binary data need to be in the repository.

## 5. What else is missing?

- Exact envelope time constants (attack/decay/release tables) - search 
v `SBAWE32.DRV` (Windows AWE32 MIDI driver, NE format, 45 KB)
- Conversion of SoundFont generator -> registries (there too)
- Filter curve (IFATN hi8 -> Hz) and Q (CCCA bits 31..28)
- Meaning of CCCA control bit 27..24

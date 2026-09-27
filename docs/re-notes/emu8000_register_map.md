# EMU8000 — register map derived from AWEUTIL.COM

Source: IDA disassembly of `AWEUTIL.COM` (AWEUTIL TSR Version 1.01, French
localisation, SHA256
`2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`).

## Sources

1. **AWE32/EMU8000 Programmer's Guide**, Revision 1.00, Dave Rossum,
   E-mu/Creative Technology Ltd. 1994-1996 — *the primary source* for the
   meaning of registers and bit fields.
   <https://www.dosdays.co.uk/media/creative/emu8kpgm.pdf>
2. **The Linux ALSA driver** — `sound/isa/sb/emu8000.c` and the header
   `include/sound/emu8000_reg.h` (register map, initialisation sequence).
   <https://github.com/torvalds/linux/blob/master/sound/isa/sb/emu8000.c>
3. `AWEUTIL.COM` (DOS TSR, version 1.01) — disassembly, register access and
   initialisation on real hardware.
4. `WINDRV/SBAWE32.DRV` (Windows 3.x AWE32 MIDI driver, NE, 45 kB) —
   envelope conversion tables and the note-on sequence.

Marking:
- **[PG]** = Programmer's Guide (primary source)
- **[ASM]** = derived directly from the driver code (certain)
- **[ALSA]** = confirmed by the header `emu8000_reg.h`
- **[?]** = an estimate, to be verified by measurement

---

## 1. I/O ports  [ASM]

`word_105F0` = the Sound Blaster base port (initialised to `220h` in the
file, overwritten from the `BLASTER` variable at run time). `word_105F2` =
the MPU-401 base (`330h`).

The four access routines (`sub_10EAC` write word, `sub_10EFA` read word,
`sub_10F46` write dword, `sub_10F9C` read dword) compute the ports like
this:

    ; pointer port
    DH   = LOBYTE(base) - 2
    DX   = (DH:DL) & 0x0C00
    DX   = DX | (base + 0x0E)
    DL   = DL & 0xF3
    -> pointer = base + 0xC02

    ; data port
    DX   = ((sel >> 8) & 2) + base      ; base + 0 or base + 2
    BX   = (sel & 0x0C00)  + base       ; base + 0/0x400/0x800/0xC00
    port = DX | BX

For `base = 220h` this gives:

| sel bits 11..9 | port | name |
|---|---|---|
| 010 | `620h` | Data0 (low word of a 32-bit register) |
| 011 | `622h` | Data0 high |
| 100 | `A20h` | Data1 (low word of a 32-bit register) |
| 101 | `A22h` | Data1 high / a separate 16-bit port ("Data2" in the documentation) |
| 110 | `E20h` | Data3 (16-bit) |
| 111 | `E22h` | **Pointer** |

Important: a 32-bit write (`sub_10F46`) sends the low word to `port` and the
high word to `port+2`. For Data1 that means the upper half of the 32-bit
register physically lies at `A22h`, i.e. at the same port as "Data2". That
is not an error; that is how the chip is designed (only CCCA and HWCF are
really 32-bit on Data1 — see `Emu8000.cpp`).

## 2. The `sel` encoding  [ASM]

The driver passes a register as one 16-bit number:

    sel = (regIndex << 12) | (portSel << 9) | voice
            bits 15..12       bits 11..9      bits 4..0

The Pointer register then gets:

    pointer = ((sel & 0x7000) >> 7) | (sel & 0x1F)
            = (regIndex << 5) | voice

i.e. **pointer: bits 0..4 = voice number (0-31), bits 5..7 = register
index (0-7)**.

Confirmed independently twice:
- `SBAWE32.DRV` has the same computation inline (`sub_11CA`:
  `ax = reg<<5 | voice`) and a port table at `ds:0712`:
  `0220 0222 0620 0622 0A20 0A22 0E20 0E22` — i.e. index 2..6 =
  Data0..Data3, index 7 = pointer.
- ALSA `emu8000_reg.h`: `#define EMU8000_CMD(reg, chan) ((reg)<<5 | (chan))`
  and the ports `DATA0=port1, DATA1=port2, DATA2=port2+2, DATA3=port3,
  PTR=port3+2`.

## 3. Registers confirmed by the driver

Derived from the initialisation sequence (`sub_12B40` -> `sub_126E8`,
`sub_127AE`, `sub_1288C`, `sub_12A20`).

### Data0 (`620h`/`622h`), 32-bit

| reg | sel | name | meaning |
|---|---|---|---|
| 0 | `04xx` | CPF | Current Pitch (hi16, linear) + Fractional address (lo16) [PG] |
| 1 | `14xx` | PTRX | Pitch Target (hi16), Reverb send (bits 15..8), aux (bits 7..0) [PG] |
| 2 | `24xx` | CVCF | Current Volume (hi16) + Current Filter cutoff (lo16) [PG] |
| 3 | `34xx` | VTFT | Volume Target (hi16) + Filter cutoff Target (lo16) [PG] |
| 4 | `44xx` | Z2 | cleared by the drivers at every note; meaning unknown |
| 5 | `54xx` | Z1 | the same |
| 6 | `64xx` | PSST | Pan (31..24, 0 = right) + Loop Start (23..0) [PG] |
| 7 | `74xx` | CSL | Chorus send (bits 31..24) + Loop End address (bits 23..0) [PG] |

### Data1 (`A20h`), 32-bit / 16-bit

| reg | sel | name | meaning |
|---|---|---|---|
| 0 | `08xx` | CCCA | Filter Q (31..28), DMA/WR/RIGHT (26/25/24), Current address (23..0) [PG] |
| 1 | `18xx` | see below | registers addressed by the "voice" number |
| 2 | `28xx` | INIT1 | init array 1 [ASM] |
| 3 | `38xx` | INIT3 | init array 3 [ASM] |
| 4 | `48xx` | ENVVOL | volume envelope delay [ASM init=0] |
| 5 | `58xx` | DCYSUSV | volume envelope decay/sustain [ASM init=0080h] |
| 6 | `68xx` | ENVVAL | modulation envelope delay [ASM init=0] |
| 7 | `78xx` | DCYSUS | modulation envelope decay/sustain [ASM init=0] |

Registers at `reg 1`, where the "voice number" serves as the register index
[ASM]:

| voice | sel | name | value written at init |
|---|---|---|---|
| 9 | `1809` | HWCF4 | `00000000` (dword) |
| 10 | `180A` | HWCF5 | `00000083` (dword) |
| 13 | `180D` | HWCF6 | `00008000` (dword) |
| 14 | `180E` | HWCF7 | `00000000` (dword) |
| 20 | `1814` | SMALR | 0 |
| 21 | `1815` | SMARR | 0 |
| 22 | `1816` | SMALW | 0 |
| 26 | `181A` | SMLD | (DRAM read/write) |
| 29 | `181D` | HWCF1 | `0059` |
| 30 | `181E` | HWCF2 | `0020` |
| 31 | `181F` | HWCF3 | `0004` |

### Data2 (`A22h`), 16-bit

| reg | sel | name |
|---|---|---|
| 1 | `1A1B` | WC — wave counter (voice 27), used as the time base in wait loops [ASM] |
| 2 | `2Axx` | INIT2 [ASM] |
| 3 | `3Axx` | INIT4 [ASM] |
| 4 | `4Axx` | ATKHLDV — volume envelope attack/hold [ASM init=0] |
| 5 | `5Axx` | LFO1VAL — LFO1 delay [ASM init=0] |
| 6 | `6Axx` | ATKHLD — modulation envelope attack/hold [ASM init=0] |
| 7 | `7Axx` | LFO2VAL — LFO2 delay [ASM init=0] |

### Data3 (`E20h`), 16-bit

| reg | sel | name | init |
|---|---|---|---|
| 0 | `0Cxx` | IP — Initial Pitch | 0 |
| 1 | `1Cxx` | IFATN — Initial Filter cutoff (hi8) + Attenuation (lo8) | `FF00` |
| 2 | `2Cxx` | PEFE — Pitch/Filter envelope amount | 0 |
| 3 | `3Cxx` | FMMOD — LFO1 -> pitch (hi8) / filter (lo8) | 0 |
| 4 | `4Cxx` | TREMFRQ — LFO1 -> volume (hi8) / LFO1 frequency (lo8) | `0018` |
| 5 | `5Cxx` | FM2FRQ2 — LFO2 -> pitch (hi8) / LFO2 frequency (lo8) | `0018` |
| 6 | `6Cxx` | unknown | 0 |
| 7 | `7C00` | ID register — chip detection, `000Ch` expected [ASM] |

## 4. Initialisation sequence  [ASM, `sub_12B40`]

    1. read  sel 7C00            ; expects 0x0C, otherwise "no AWE"
    2. write HWCF1 = 0059h
    3. write HWCF2 = 0020h
    4. write HWCF3 = 0004h
    5. sub_126E8: for every voice 0..31 write
          DCYSUSV=0080h, ATKHLD=0, DCYSUS=0, IP=0, IFATN=FF00h, PEFE=0,
          FMMOD=0, TREMFRQ=0018h, FM2FRQ2=0018h, (Data3 reg6)=0,
          LFO2VAL=0, LFO1VAL=0, ATKHLDV=0, ENVVOL=0, ENVVAL=0
    6. sub_127AE: wait on WC (sel 1A1B), then for every voice 0..31 write dwords
          PTRX=0, VTFT=0000FFFF, PSST=0, CSL=0, CPF=0, CVCF=0000FFFF,
          CCCA=0, (Data0 reg5)=0, (Data0 reg4)=0
    7. sub_1288C: SMALR/SMARR/SMALW = 0, then send the 4 init arrays (below)
    8. sub_12A20: set up voices 30 and 31 as "DRAM refresh" channels
    9. write HWCF3 = 0004h
    10.read HWCF2, bit 6 -> a flag (card type / memory size)

Watch `0000FFFF` vs `FFFFFFFF`: in step 6 the upper 16 bits are cleared
through `xor dx,dx`, so VTFT and CVCF get **volume 0 and the filter fully
open**. In step 8, on the other hand, `cwd` is used, which sign-extends
0xFFFF, so there it really is `FFFFFFFF`. The same holds for
`PSST(30) = FFFFFFE0`. `SBAWE32.DRV` (sub_1320) writes the same values
`0000FFFF` in step 6. (Both Creative drivers write `0000FFFF` for
VTFT(30/31) in step 8 too — see the comment in `Emu8000.cpp`.)

### Step 8 — "DRAM refresh" voices 30/31  [ASM]

    PSST(30)=0000FFE0  CSL(30)=00FFFFE8  PTRX(30)=0  CPF(30)=0  CCCA(30)=00FFFFE3
    PSST(31)=00FFFFF0  CSL(31)=00FFFFF8  PTRX(31)=000000FF CPF(31)=00008000
    CCCA(31)=00FFFFF3
    ; then direct port I/O: pointer=003Eh, Data0=0, wait on bit 12 of the pointer,
    ; Data0+2=4828h, pointer=003Ch, Data1=0
    VTFT(30)=FFFFFFFF  VTFT(31)=FFFFFFFF

### Init arrays  [ASM]

Three sets of 128 words (4 registers x 32 voices) at offsets `341Ch`,
`351Ch`, `361Ch` in the COM file. They are sent through INIT1..INIT4:

    set A -> init1   (offset 341Ch)
      wait ~0x401 WC ticks
    set B -> init2   (offset 351Ch)
    set C -> init4   (offset 361Ch, OR 8000h on odd voices)
      HWCF4=0, HWCF5=83h, HWCF6=8000h, HWCF7=0
    set C -> init3   (offset 361Ch, without the OR)

Set A starts `03FF 0030 07FF 0130 0BFF 0230 ...`, set B is the same with
`8000h` in the odd words, set C starts `0C10 8470 14FE B488 167F A470 18E7
84B5 ...`.

Set C holds the reverb parameters interleaved with "microcode" values;
before it is sent, the chorus parameters from a table are patched into 8
places (`word_1377C..word_1378A`, default
`C280 C380 0001 821E D280 031E D380 0001`). Six more chorus presets lie at
offset `371Ch`.

Patched places in set C (word index within the 128):

| index | reg/voice | source |
|---|---|---|
| 81 | INIT3 v17 | `word_13782` |
| 83 | INIT3 v19 | `word_13784` |
| 91 | INIT3 v27 | `word_13786` |
| 97 | INIT4 v1  | `word_1377C` |
| 103 | INIT4 v7  | `word_13788` |
| 113 | INIT4 v17 | `word_1377E` |
| 117 | INIT4 v21 | `(word_13780 + word_1378A) + 263h` |
| 125 | INIT4 v29 | `(word_13780 + word_1378A) - 7C9Dh` |

**The init arrays cannot be interpreted directly by a software emulation**
— they configure the internal DSP of the real chip (reverb/chorus), not the
voice behaviour. Our own core stores them and ignores them; 86Box decodes the
reverb/chorus parameters and the equalizer slots from them, which is why
they must be sent in the correct order (`Awe32InitArrays.h`).

## 5. Bit meanings (per the Programmer's Guide)

| register | field | meaning |
|---|---|---|
| CPF | 31-16 | current pitch, **linear**, 0x4000 = no shift [PG] |
| CPF | 15-0 | fractional part of the address |
| PTRX | 31-16 | pitch target |
| PTRX | 15-8 | reverb send (0 = none, 0xFF = max) |
| PTRX | 7-0 | aux byte (the drivers put an auxiliary pan there) |
| CVCF/VTFT | 31-16 | current / target volume |
| CVCF/VTFT | 15-0 | current / target filter cutoff |
| PSST | 31-24 | pan, **0 = fully right, 0xFF = fully left** [PG] |
| PSST | 23-0 | loop start |
| CSL | 31-24 | chorus send |
| CSL | 23-0 | loop end |
| CCCA | 31-28 | filter Q, 0 = no resonance, 15 = about 24 dB |
| CCCA | 27 | always 0 |
| CCCA | 26 | DMA |
| CCCA | 25 | WR (1 = write) |
| CCCA | 24 | RIGHT (1 = right DMA stream) |
| CCCA | 23-0 | current address |
| IP | - | 0xE000 = no shift, 0x1000 = an octave, **logarithmic** |
| IFATN | 15-8 | filter cutoff (see below for the measured scale) |
| IFATN | 7-0 | attenuation in 0.375 dB steps, 0xFF = 96 dB |
| DCYSUSV | 15 | 0 = decay is written, 1 = release |
| DCYSUSV | 14-8 | sustain level in 0.75 dB steps (0x7F = no attenuation) |
| DCYSUSV | 7 | envelope generator off |
| DCYSUSV | 6-0 | decay/release rate (0 = no decay) |
| ATKHLDV | 14-8 | hold in 92 ms steps (0x7F = no delay, 0 = 11.68 s) |
| ATKHLDV | 6-0 | attack (0 = never, 1 = 11.88 s, 0x7F = 6 ms) |
| ENVVOL/ENVVAL/LFOnVAL | - | 0x8000 = no delay, lower = delay in 725 us steps |
| PEFE | 15-8 | mod. envelope -> pitch, +-1 octave at 0x7F/0x80 |
| PEFE | 7-0 | mod. envelope -> filter, +-6 octaves |
| FMMOD | 15-8 | LFO1 vibrato, +-1 octave |
| FMMOD | 7-0 | LFO1 -> filter, +-3 octaves |
| TREMFRQ | 15-8 | LFO1 tremolo, "+-12 dB" (measured: 12 dB in total, attenuation only) |
| TREMFRQ | 7-0 | LFO1 frequency in 0.042 Hz steps (0xFF = 10.72 Hz) |
| FM2FRQ2 | 15-8 | LFO2 vibrato, +-1 octave |
| FM2FRQ2 | 7-0 | LFO2 frequency, same units |

Addresses in CCCA/PSST/CSL: the real location in sound memory is one word
higher than the register says ("interpolator offset") [PG].

### Envelope time constants [ASM + PG]

`SBAWE32.DRV` contains two conversion tables of 128 words (in ms): attack at
`ds:1552`, decay/release at `ds:1650`. The lookup routines `sub_2BC0` and
`sub_2BF0` both index the table with `rate - 1`.

Both tables match exactly one formula `time = base / k(i)`:

```
k(i):  i = 0..127
       group g = i / 16, position m = i % 16
       g == 0  ->  k = m + 1                 (1..16)
       g >= 1  ->  k = (m + 17) << (g - 1)   (17..32, 34..64, 68..128, ...)

attack_ms(r)        = 11878 / k(r-1)     r = 1..127,  r = 0 means "never"
decay_release_ms(r) = 47513 / k(r-1)     r = 1..127,  r = 0 means "no decay"
```

Verified against both tables entry by entry — **0 deviations**, so the code
needs only the formula and no data has to be copied from the driver.

The decay/release time of the table corresponds to a run over **100 dB**,
not the whole 96 dB range: the Programmer's Guide gives rate 0x7F =
240 us/dB (table 24 ms) and rate 0x01 = 470 ms/dB (table 47513 ms). So the
envelope is computed as a rate in dB/s, not as a fixed total time.

### Filter cutoff scale (measured later)

The Programmer's Guide contradicts itself on the cutoff: "quarter semitones
from 125 Hz" vs "0xFF = 8 kHz" (255 quarter semitones = 4966 Hz; 8 kHz would
need 288). The pair `SYNTHGM.SBK` / `SYNTHGM.SF2` of the DOS SDK settles it:
register 0 = **101.81 Hz**, one register step = **29.3843 cents**, register
0xFF = 7717 Hz — see `Emu8000Regs.h`. The tester's card confirmed it
(AWETST25 blocks 6 and 7, with a Chamberlin filter).

## 6. What was still missing at the time

- The SoundFont generator -> register conversion (in `SBAWE32.DRV` around
  `0x2C1E`, patch structure at `[si+...]`, see the note-on at `0x040A`) —
  done since, see `SoundFont.cpp` and `driver_note_on.md`.
- The three MIDI -> dB conversion curves in `SBAWE32.DRV` (`ds:0592`,
  `ds:0612`, `ds:0692`, 128 bytes each) — done, `Awe32Curves.h`.
- Chorus/reverb — fitted to recordings of the card, see
  `emu8000_tuning.md`.

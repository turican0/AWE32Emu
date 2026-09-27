# Comparison with 86Box and the real drivers — state and findings

> **The most important results** (all measured against the real Creative
> drivers running under our build of 86Box; how, see
> [TESTING.md](../TESTING.md)):
>
> - **Initialisation: 1611 of 1611 writes equal** to `AWEUTIL.COM /S`.
>   Section 5.
> - **Note-on: ALL registers match 242/242** in the `--driver win95` variant.
>   Sections 11 and 12.
> - **The `--driver dos` family against `SBAWE32.MDI`** (Magic Carpet 2 in a
>   separate DOS machine): **ALL registers match 255/255**. Sections 14 to 16.
> - **The layer parameter block is a direct array of SoundFont generators**
>   indexed by `generator*2` — and both drivers contain a table of generator
>   defaults (MDI 0x16AD, VXD 0x6D60). Sections 16.2 and 16.3.
> - **An instruction tracer** in our 86Box — CPU and memory state at every
>   port access, optionally every instruction. Section 10.
> - **The note-on routine is reverse engineered** (in `SBAWE.VXD`, not in the
>   `.DRV`) — register map, writer and computations. Section 9.
> - **Solved: why we lacked high frequencies:** `initialFilterFc` is 7-bit in
>   SF1 and the driver doubles it. After the fix the spread between bands up
>   to 6.4 kHz fell from 13 dB to 0.8 dB. Sections 9.6 and 9.8.
> - **Solved: why we were loud and clipped:** when the bank refers to the ROM
>   "1MGM", the driver adds 16 units = **6 dB** to the attenuation (for ROM
>   samples). Section 10.2.
> - Later: the voice allocation of all three families and the third family
>   (`sdk`). Section 17.

The basis: the upstream `snd_emu8k.c`, verified to be **byte for byte
identical with 86Box master** (`verify_upstream.py --online`).

## 1. Fixes made on the basis of the comparison

### 1.1 A 32-bit write to DCYSUSV cleared LFO1VAL

`Emu8000Core::Write()` treated **all** registers on Data1 as 32-bit and sent
the upper half to `A22h`. But for registers 2..7 a completely different
register lies at `A22h`, not an upper half:

| reg | `A20h` (Data1) | `A22h` (Data2) |
|---|---|---|
| 0 | CCCA | CCCA high |
| 1 | HWCF4..7, SMALR/SMARR/SMALW/SMLD | HWCF high, SMRD, WC |
| 2 | INIT1 | INIT2 |
| 3 | INIT3 | INIT4 |
| 4 | ENVVOL | ATKHLDV |
| 5 | DCYSUSV | LFO1VAL |
| 6 | ENVVAL | ATKHLD |
| 7 | DCYSUS | LFO2VAL |

Confirmed in 86Box (`snd_emu8k.c`, `case 0xA00` vs `case 0xA02`) and in ALSA
(`emu8000_reg.h`, `EMU8000_DATA1` vs `EMU8000_DATA2`).

Consequence: `Synth::StartVoice()` writes LFO1VAL and right after it
DCYSUSV, so the 32-bit DCYSUSV write always overwrote LFO1VAL with zero —
**the LFO1 delay was cleared on every note**. Fixed with `IsReg32()`: only
Data0 (all eight) and registers 0 (CCCA) and 1 (HWCF) on Data1 are 32-bit.

### 1.2 The port level did not send the pointer nor the high word

`WriteReg16()` set `m_pointer` directly and `WriteReg32()` wrote the high
word directly into the register array. So the port path (which the class
declares as "exactly what the driver does") skipped two of three real
writes. Now everything goes through `PortOut16()` in the order pointer -> low
-> high, as `sub_10F46` in AWEUTIL does. No effect on the sound, but without
it no usable trace could be taken from the port level.

### 1.3 The init arrays INIT1..INIT4 were not sent

INIT1..INIT4 used to be skipped deliberately ("they are DSP coefficients,
they do not carry over to a software emulation"). But 86Box decodes the
**whole** reverb and chorus from them, so without them no comparison was
possible at all. The arrays are now in `AWE32Emu/src/Awe32InitArrays.h`
(4 x 128 values) and `PowerOnInit()` sends them in the order of ALSA
`init_arrays()`: init1, init2, init3, HWCF4/5/6, init4. Originally taken from
ALSA; section 5.3 corrects them against the real AWEUTIL.

## 2. Documentation fixes

A note claimed that 86Box's default filter was `FILTER_INITIAL` +
`FILTER_CONSTANT`. It is not. The defaults in `snd_emu8k.c` are:

```c
#define FILTER_MOOG
#define RESAMPLER_CUBIC
```

`FILTER_INITIAL`, `FILTER_CONSTANT` and `RESAMPLER_LINEAR` are in the code
but wrapped in `#if 0`. For the comparison, 86Box runs **a Moog ladder filter
and a cubic resampler**. The reference copy is identical with master but
**matches no release** — `v5.3` and `v6.0` lack some changes.

## 3. The deviation at the time

RELAX (`RELAX_BK.MID`, GM bank + the song's bank), 219.9 s, both renders
from the same port-write trace:

| quantity | value |
|---|---|
| RMS ours | -19.66 dB |
| RMS 86Box | -21.65 dB |
| RMS of the difference | -20.74 dB |
| error to signal ratio | -1.08 dB |
| volume envelope correlation | 0.955 |

Deviation per band (positive = we have more):

| band Hz | difference dB |
|---|---|
| 0-100 | +2.5 |
| 100-200 | +2.1 |
| 200-400 | +1.6 |
| 400-800 | +0.9 |
| 800-1600 | -0.3 |
| 1600-3200 | +2.8 |
| 3200-6400 | +2.8 |
| 6400-12800 | +1.1 |
| 12800+ | +1.4 |

### 3.1 MINUET (GM bank only, samples only from ROM) — a clear finding

`MINUET.MID`, 48.2 s, only `SYNTHGM.SBK`, so no DRAM and no effect of a bank
upload:

| quantity | value |
|---|---|
| volume envelope correlation | **0.990** |
| RMS ours | -22.56 dB |
| RMS 86Box | -26.42 dB |

| band Hz | difference dB (positive = we have more) |
|---|---|
| 0-100 | +3.1 |
| 100-200 | +3.3 |
| 200-400 | +3.9 |
| 400-800 | +4.6 |
| 800-1600 | -1.1 |
| 1600-3200 | **-7.4** |
| 3200-6400 | **-9.2** |
| 6400-12800 | **-25.0** |
| 12800+ | **-38.4** |

The time course matched almost perfectly (0.99), the colour did not. Above
6 kHz our output was practically silent.

> **SOLVED, see 9.6 and 9.8.** It was not the filter topology but the
> mapping: `initialFilterFc` is 7-bit in SF1 and the driver doubles it before
> writing to `IFATN`. After the fix the cutoff matches 242/242 and the
> deviation up to 6.4 kHz is even (+3.0 to +3.8 dB), just a difference of the
> overall level.

## 4. What remained to go through (at the time)

In order of impact:

1. **`emu8k_update()`** — the main loop. Fixed-point arithmetic, order of
   operations, envelopes in steps. Ours is in float with time in seconds.
2. **Filter** — 86Box `FILTER_MOOG`; we had a TPT state-variable.
3. **Reverb and chorus** — 86Box decodes them from the init arrays.
4. **`emu8k_vol_slide()`** — the approach to the target volume.
5. **Summing 32 voices** — 86Box has fixed-point saturation, we float.
6. **Voice stealing** — our own heuristic then.

**Keep ours, do not take from 86Box:** the decay/release tables. 86Box uses
the table from the Linux driver (`45120, 22614, 15990, 11307, ...`), we have
the table from the Creative driver (`47513, 23756, 15838, 11878, ...`),
byte for byte identical across three driver generations.

(All of this was later resolved on the evidence of the tester's card — see
`emu8000_tuning.md`. The default chip is now `snd_emu8k.c` with measured
corrections.)

---

# 5. Verification against the real driver (AWEUTIL.COM in DOS)

Until then everything was compared with the register trace generated by
**our** code, so a common error in the driver layer would not show. So we
built our own 86Box with a write trace and ran `AWEUTIL.COM /S` under DOS in
it — the binary our `PowerOnInit()` is derived from.

## 5.1 Result

After the fixes below the match is **complete**:

```
ours: 1611 writes, AWEUTIL: 1611 writes, equal: 1611
```

The sequence `(register, voice, value)` is compared; times are ignored.

## 5.2 Chip ID: 86Box returns a wrong value

AWEUTIL first detects the card. `sub_12B40`:

```asm
mov     ax, 7C00h        ; register 7, Data3, voice 0
push    ax
call    sub_10EFA        ; read word
cmp     ax, 0Ch          ; FULL 16-bit compare, no masking
jz      short loc_12B50
xor     ax, ax           ; otherwise quit
retn
```

86Box returns `0x1c` on this register:

```c
case 7: /*ID?*/
    return 0x1c | ((emu8k->id & 0x0002) ? 0xff02 : 0);
```

So the detection fails and AWEUTIL prints `ERR012: Echec de l'initialisation
de AWE32`. The trace shows exactly that — a single write `E22 = 00E0`
(pointer to register 7) and a read `E20 -> 001C`.

**Our value `0x000C` is right**, confirmed directly by the driver code. The
Linux driver has the same test (`(U1_READ & 0x000f) != 0x000c`), only
commented out. Our build of 86Box has it fixed to `0x0c` — otherwise no
original DOS driver would start in it. A candidate for a bug report to
86Box.

## 5.3 Init arrays: three sources, two variants

> **Corrected later.** It originally said "ALSA is wrong and AWEUTIL is
> right". It is not so. When the initialisation of the Windows
> `SBAWE32.DRV` was measured too, **ALSA and SBAWE32.DRV turned out to agree
> completely**, and **AWEUTIL** differs from them. There is no single right
> set — two Creative drivers configure the DSP differently. See 7.1.

| array | match (ALSA vs AWEUTIL) |
|---|---|
| `kInit1` | 128/128 |
| `kInit2` | 128/128 |
| `kInit3` | 120/128 |
| `kInit4` | 120/128 |

The **same indices** differ in both arrays — 83, 97, 103, 109, 113, 115, 121,
123:

| index | ALSA (kInit3) | AWEUTIL | ALSA (kInit4) | AWEUTIL |
|---|---|---|---|---|
| 83 | `D208` | `D280` | `D208` | `D280` |
| 97 | `C208` | `C280` | `C208` | `C280` |
| 103 | `D308` | `D380` | `D308` | `D380` |
| 109 | `D26E` | `D2E6` | `D26E` | `D2E6` |
| 113 | `C308` | `C380` | `C308` | `C380` |
| 115 | `B2FF` | `B27F` | `32FF` | `327F` |
| 121 | `D36E` | `D3E6` | `D36E` | `D3E6` |
| 123 | `B3FF` | `B37F` | `33FF` | `337F` |

`Awe32InitArrays.h` holds AWEUTIL's values (because `PowerOnInit()` is a
transcription of its sequence); the SBAWE32 variant is next to it as
`kAltInit3Sbawe`/`kAltInit4Sbawe`. (Later measured on the card: these are the
equalizer slots, and the two variants sound the same within 0.1 dB — see
`Emu8000Effects.h`.)

## 5.4 Three writes we were missing

1. **SMARR a second time.** AWEUTIL writes `SMALR, SMARR, SMALW, SMARR` —
   `SMARR` twice and `SMARW` not at all. ALSA does
   `SMALR, SMARR, SMALW, SMARW` there. We had only the first three.
2. **`PTRX` of voice 30 = `0x48280000`** and
3. **Data1 register 1, voice 28 = `0x0000`** (the undocumented register
   HWCF).

Points 2 and 3 were described in `emu8000_register_map.md`, but it was not
clear where in the sequence they belong: between the voice 31 block and the
final `VTFT` of voices 30 and 31.

## 5.5 What it means

This verifies **the whole initialisation part** of the driver layer,
including the register map, the `sel` encoding, the ports, the write order
and the init arrays. Still unverified then: the MIDI -> register conversion
(`Synth.cpp`) and the SoundFont generator mapping (`SoundFont.cpp`) — that
needs the guest to really play.

---

# 6. Verifying note-on against the Win95 driver

The second, more important part: let the guest really play and compare which
registers the driver sets on every note. `MINUET.MID` was played through the
Windows 95 Media Player.

```bash
python tests/notes_diff.py tests/out/ours_init.trace tests/out/win95.trace
```

**Which binary was measured** is essential: the guest has a newer driver set
than the installation CD. All files that really ran during the measurement
were extracted with their hashes, and only those are used for disassembly.
An attempt to deploy the older `SBAWE32.DRV` from the CD ended with the guest
no longer playing (0 notes instead of 242).

Important for validity: **`SYNTHGM.SBK` and `awe32.raw` are byte for byte
identical** with what our player uses.

Real notes are recognised by `IFATN != FF00` — with this value the driver
silences all 32 voices at the start. **Our player and the driver made exactly
242 note-ons**, so they can be paired in order.

## 6.1 What matched (18 registers, 242/242)

`IP`, `PSST`, `PEFE`, `FMMOD`, `FM2FRQ2`, `LFO1VAL`, `LFO2VAL`, `ENVVOL`,
`ENVVAL`, `CVCF^`, `CSL^`, `CCCA^`, `VTFT^`, `Z1`, `Z1^`, `Z2`, `Z2^`, `CPF`.

The most important: **`IP` — the pitch computation is exact on all 242
notes**.

## 6.2 What did not match — 12 systematic rules

Every deviation is a clean, repeatable relation, not noise:

| register | relation | note |
|---|---|---|
| `IFATN` cutoff | **driver = ours x 2.0** (242/242) | filter cutoff |
| `IFATN` atten | driver = ours +13 (150x) or +14 (92x) | attenuation |
| `VTFT` low | **= `IFATN` cutoff << 8** (242/242) | we wrote `FFFF` |
| `CVCF` low | the same as `VTFT` | we wrote `FFFF` |
| `PTRX^` | **= `CPF^`** at the driver (242/242) | we put `IP` there |
| `CPF^` | the driver writes a linear increment | we left 0 |
| `PTRX` low | constant `1C81` | reverb send — we did not set it at all |
| `TREMFRQ` | constant `0080` | we wrote 0 |
| `PSST^` (pan) | driver `7F`, ours `80` | the centre is 0x7F, not 0x80 |
| `DCYSUSV` sustain | driver always `00`, ours `7F` | sustain 0 = decay to silence |
| `DCYSUSV` decay | driver = ours -1 | |
| `DCYSUS` | constant `007F` | modulation envelope |
| `ATKHLDV` | constant `797D`; hold +1, attack -2 against us | |
| `ATKHLD` | constant `7F7D` | |
| `CSL` | driver = ours +1 | loop end |
| `CCCA` low | driver = ours -4 | start address |

## 6.3 The most important finding: the filter cutoff is 2x off

We wrote **exactly half** the filter cutoff of the driver, on all 242 notes.
That explains section 3.1 — half the cutoff on the filter's logarithmic scale
means an octave lower. The driver also writes the target cutoff (`VTFT` and
`CVCF` low) equal to the initial one, while we put `FFFF` there (fully open).

## 6.4 The next step then

Fix `Synth.cpp` per the table in 6.2 and after every change run `regress.py`
and `notes_diff.py`. The goal: all registers at 242/242. Only then does it
make sense to return to spectral measurements.

---

# 7. AWEUTIL.COM against SBAWE32.DRV

Question: are the DOS and the Windows driver identical as far as the sound is
concerned? **They are not** — and they are not even the same programs.
Measured on the initialisation, which is recorded for both.

| | AWEUTIL.COM /S | SBAWE32.DRV (Windows) |
|---|---|---|
| register writes at init | 1611 | 2400 (up to the first note) |
| INIT1, INIT2 | 128 + 128 | 128 + 128, **same values** |
| INIT3, INIT4 | 128 + 128 | 131 + 137, **8 values differ** |
| `SMLD` (DRAM upload) | 0 | 3 — uploads a bank |
| `SMALR^`, `SMALW^`, `SMARR^`, `SMARW^` | 0 | yes — uses the upper halves too |
| Data1 reg 1, voices 0..19 | only voice 28 | all 20 + upper halves |
| ID register read | yes | yes |

## 7.1 Eight values in the init arrays

In the **resulting register state** (not in the write order — SBAWE32
overwrites some values later) exactly eight differ:

| register | AWEUTIL | SBAWE32.DRV | ALSA (ADIP) |
|---|---|---|---|
| `INIT3` v19 | `D280` | `D208` | `D208` |
| `INIT4` v1 | `C280` | `C208` | `C208` |
| `INIT4` v7 | `D380` | `D308` | `D308` |
| `INIT4` v13 | `D2E6` | `D26E` | `D26E` |
| `INIT4` v17 | `C380` | `C308` | `C308` |
| `INIT4` v19 | `327F` | `32FF` | `32FF` |
| `INIT4` v25 | `D3E6` | `D36E` | `D36E` |
| `INIT4` v27 | `337F` | `33FF` | `33FF` |

**ALSA and SBAWE32.DRV agree in all eight.** The differences cannot be
described by one bit transformation (`08`->`80` and `6E`->`E6` swap bits 3
and 7, but `FF`->`7F` only clears bit 7), so it is not a typo in one source.

## 7.2 What was not measured then

The note-on of the DOS family. AWEUTIL has its own MIDI engine in `/EM:GM`
mode, which we did not get running (see `tests.md`). Resolved later through
the game's own driver `SBAWE32.MDI` — sections 14-16.

---

# 8. Where the note-on logic is: in the .VXD, not in the .DRV

The attempt to reverse the note-on in `SBAWE32.DRV` hit a wall at once:

- The whole binary (45008 B, 4 code segments, 84 functions) has **not a
  single `out`/`in` instruction**. Verified by a per-function disassembly
  (`tests/ne_disasm.py <drv> --funcs`).
- The attenuation conversion tables are **not in it either**.

Both are in `SBAWE.VXD` (86054 B). In the Windows AWE32 architecture the
`.DRV` only handles the interface to the MIDI mapper; the real work —
computing the registers and accessing the ports — is in the VXD, a 32-bit VxD
in the LE (Linear Executable) format.

## 8.1 What was confirmed from the VXD

Two of three conversion tables are **byte for byte identical** with our
`Awe32Curves.h`:

| table | offset in the VXD | match |
|---|---|---|
| `kChannelVolumeDb` | `0x8F10` | 128/128 |
| `kExpressionDb` | `0x8F90` | 128/128 |
| `kVelocityDb` | `0x8E90` | **127/128** |

In `kVelocityDb` a single byte differs — index 0:

| source | `kVelocityDb[0]` |
|---|---|
| `SBAWE32.MDI` (Miles) | 50 |
| `SBAWE32.DRV` 45632 B (WINDRV) | 50 |
| `SBAWE.VXD` 86054 B (measured) | **99** |

Velocity 0 is a note-off in MIDI, so it is never used musically.

## 8.2 Provenance of old notes

`driver_note_on.md` and `Awe32Curves.h` quote offsets like
`SBAWE32.DRV ds:0592` and `0x021E`. Those refer to the copy
`SBAWE32/WINDRV/SBAWE32.DRV` (**45632 B**), yet another version than the
measured one (45008 B) and the one on the installation CD (44176 B).

Versions encountered:

| file | size | contains the attenuation tables | port I/O |
|---|---|---|---|
| `SBAWE32.DRV` (measured, guest) | 45008 | no | no |
| `SBAWE32.DRV` (installation CD) | 44176 | no | no |
| `SBAWE32.DRV` (WINDRV) | 45632 | **yes** | ? |
| `SBAWE32.DRV` (SDK, Win 3.1) | 38720 | no | ? |
| `SBAWE.VXD` (measured, guest) | 86054 | **yes** | **yes** |
| `SBAWE32.MDI` (Miles/AIL) | 36880 | **yes** | yes |

---

# 9. The note-on routine in SBAWE.VXD — what has been read

Tool: `tests/le_disasm.py <vxd>` (LE parser, page map, xref, 32-bit
disassembly).

VxD structure: 5 objects, page 4096, data from `0x2000`.

| object | virt_size | used for |
|---|---|---|
| 1 | `0x7b68` | **note-on and the conversion tables** |
| 2 | `0x37` | a small stub |
| 3 | `0x21c6` | 75 port accesses |
| 4 | `0x5a7d` | hardware initialisation |
| 5 | `0xdd8` | data (rw) |

## 9.1 The register writer — object 1, `0x0A05`

```asm
0A05: push ebp; mov ebp,esp; push esi; pushfd; cli
0A0B: mov esi,[ebp+8]              ; arg1: device structure
0A0E: mov eax,[ebp+0xC]            ; arg2: "sel"
0A11: movzx edx,word [esi+4]       ; base port from the structure
0A15: add edx,2
0A1A: out dx,al                    ; low byte of sel -> pointer port
0A1B: and ch,0xFE
0A1E: and eax,0x100
0A23: shr ecx,8
0A26: movzx edx,word [ecx+esi]     ; data port from a table in the structure
0A2A: shr eax,7                    ; bit 8 of sel -> +2
0A2D: or edx,eax
0A2F: mov eax,[ebp+0x10]           ; arg3: value
0A32: out dx,ax
0A37: ret 0xC
```

So `write_reg16(dev, sel, value)`, where:

- `sel & 0xFF` is the pointer, i.e. `(reg << 5) | voice`
- `(sel >> 8) & 0xFE` is the index into the port table in that structure
- bit 8 of `sel` adds 2 to the port (high word / "Data2")

**A different `sel` encoding than in AWEUTIL** (there
`(reg<<12)|(portSel<<9)|voice`). stdcall, 114 call sites in object 1.

## 9.2 The note-on block — object 1, from `~0x1F40`

(The first register assignment here was partly wrong; the corrected table is
in 9.5.)

### Known fields of the per-voice structure (`ebx`)

See 11.1 for the verified list.

## 9.3 A computation at `0x2029`

First read as the attenuation; it is actually **LFO1 -> pitch** for FMMOD
(see 9.5).

## 9.4 What remained

1. The port table in the device structure (done in 9.5).
2. Who fills the cutoff field — the answer to why our cutoff was exactly
   half (done in 9.6).
3. Whether the reverb send `0x1C` and `TREMFRQ = 0x0080` come from the bank
   or are driver constants (driver defaults — 11.2).
4. The attenuation (done in 10.2).

## 9.5 The port table completed

From the writer `0x0A05` the mapping follows exactly. The pointer port is
`[esi+4] + 2`, the data port `[esi + ((sel>>8) & ~1)]`, and bit 8 of `sel`
adds 2:

| index | port | note |
|---|---|---|
| 0 | `0x620` / `0x622` | Data0, 32-bit registers (through `0x0A6F`) |
| 2 | `0xA20` / `0xA22` | Data1 / Data2 |
| 4 | `0xE20` | Data3, 16-bit only; bit 8 never used here |

`[esi+4] = 0xE20`, so the pointer comes out at `0xE22`. Four helper routines:

| address | function |
|---|---|
| `0x0A05` | `write_reg16(dev, sel, val)` |
| `0x0A3A` | `read_reg16(dev, sel)` |
| `0x0A6F` | `write_reg32(dev, sel, val)` — a single `out dx, eax` |
| `0x0A95` | `read_reg32(dev, sel)` |

The right assignment:

| `sel` OR | register | value |
|---|---|---|
| `0x2C0` | ENVVAL | `[ebx+0x32]` |
| `0x2E0` | DCYSUS | `[ebx+0x3A]<<8 \| [ebx+0x38]` |
| `0x380` | ATKHLDV | `[ebx+0x46]<<8 \| [ebx+0x44]` |
| `0x3A0` | LFO1VAL | `[ebx+0x2A]` |
| `0x3C0` | ATKHLD | `[ebx+0x36]<<8 \| [ebx+0x34]` |
| `0x3E0` | LFO2VAL | `[ebx+0x2E]` |
| `0x400` | IP | `[ebp-0x10]` |
| `0x420` | **IFATN** | `[ebx+0x10]<<8 \| [ebx+0x26]` |
| `0x440` | PEFE | `[ebx+0x0E]<<8 \| [ebx+0x16]` |
| `0x460` | FMMOD | `(modwheel+patch, max 0x7F)<<8 \| [ebx+0x14]` |
| `0x480` | TREMFRQ | `[ebx+0x1A]<<8 \| [ebx+0x2C]` |
| `0x4A0` | FM2FRQ2 | `[ebx+0x0C]<<8 \| [ebx+0x30]` |

Also confirmed: at the start of note-on `0x0000FFFF` is written to `VTFT`
and `CVCF` (`0x1F3B`, `0x1F4F`), and the one-shot loop is placed at
`end+4 .. end+8` (`0x1F1C`) — exactly as the old notes say.

## 9.6 SOLVED: the cutoff is 7-bit in SF1

The driver reads `initialFilterFc` from the bank and **doubles** it.
Evidenced three times, independently:

1. **Code:** `SBAWE.VXD` clips the cutoff field to `0..0xFF`
   (`0x2547`..`0x255E`), so the register is 8-bit.
2. **Data:** `SYNTHGM.SBK` has no `initialFilterFc` above 127 (the highest is
   exactly 127) — the generator is 7-bit. Piano 1 has 110, the key zone
   58..66 has 89.
3. **Trace:** the driver wrote 220 and 178, exact doubles, on all 242 notes.

Fixed in `SoundFont.cpp`. The cutoff now matches **242/242**.

## 9.7 Velocity -> cutoff, the exact form

`SBAWE.VXD` object 1, `0x1CF6`:

```asm
1CF6: cmp word [esi+0x44], 0x7D    ; attack
1CFB: jge skip
1CFD: movsx eax, word [esi+0x5E]   ; velocity
1D01: cmp eax, 0x46
1D06: mov eax, 0x46                ; max(velocity, 0x46)
1D0B: movsx ecx, word [esi+0x10]   ; cutoff
1D0F: imul ecx, eax
1D12: add ecx, 0xA0
1D18: shr ecx, 7
1D1B: mov word [esi+0x10], cx
```

i.e. `cutoff = (cutoff * max(velocity, 0x46) + 0xA0) >> 7`. We had `+0x40`
and a division by `0x7F` (which later turned out to be the DOS family's
form).

## 9.8 Effect on the spectrum

MINUET against 86Box, before and after the cutoff fix:

| band Hz | before | after |
|---|---|---|
| 0-100 | +3.1 | +3.0 |
| 100-200 | +3.3 | +3.1 |
| 200-400 | +3.9 | +3.1 |
| 400-800 | +4.6 | +3.2 |
| 800-1600 | -1.1 | +3.5 |
| 1600-3200 | **-7.4** | **+3.8** |
| 3200-6400 | **-9.2** | **+3.4** |
| 6400-12800 | **-25.0** | **-4.8** |
| 12800+ | **-38.4** | **-31.7** |
| envelope correlation | 0.990 | **0.9991** |

Up to 6.4 kHz the deviation is now even (+3.0 to +3.8 dB) — just a difference
of the overall level. The spread between bands went from 13 dB to 0.8 dB.

---

# 10. The instruction tracer in 86Box

The static disassembly was slow and misled several times (see the fixes in
9.5). So our build of 86Box has a tracer that can:

1. **On every access to the EMU8000 ports** dump the complete CPU state and
   a memory window around `EBX` and `EBP`. So for every register write it is
   visible what the driver was working with.
2. **Dump every instruction** in a given linear address range with the
   registers.

It is switched on with environment variables and costs nothing without them:

```
AWE32_TRACE_FILE   output path
AWE32_TRACE_MEM    how many bytes around EBX/EBP (default 128)
AWE32_TRACE_INSN   1 = single instructions too
AWE32_TRACE_LO/HI  linear address range
AWE32_TRACE_MAX    line cap
```

The instruction mode needs a build **without the dynarec**:

```bash
C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=MINGW64 AWE32_DYNAREC=OFF \
  AWE32_BUILDDIR=<...>/ref86box/build86box_int <...>/ref86box/build_86box.sh"
```

Careful: the hook must be in **two** loops — `exec386_dynarec_int()` and
`exec386()`. A Pentium without the dynarec goes through the second; when it
was forgotten, the trace held not a single instruction.

The trace viewer is `tests/insn_view.py` — it adds the disassembly from the
VXD image to the linear addresses. The patch for 86Box is in the `sources`
branch (`ref86box/86box-patch/`).

## 10.1 Where the VXD is loaded

Object 1 of `SBAWE.VXD` lies at linear address **`0xC0FF8B48`** — found by
matching the callers' return addresses (read from `[EBP+4]` in the memory
window) against 12 call sites found statically.

## 10.2 SOLVED: the attenuation, the missing 6 dB

The complete formula from `SBAWE.VXD` object 1, `0x1C54`..`0x1CE7`:

```asm
1C54: mov  [ebp-0x10], 0x18            ; divisor 24
1C5B: movsx ecx, word [esi+0x5E]       ; velocity
1C62: movzx ecx, byte [ecx+0x408E90]   ; velDb[velocity]
1C69: movzx eax, byte [edx+0x408F10]   ; volDb[CC7]
1C70: add  ecx, eax
1C72: movsx eax, word [esi+0x60]       ; another attenuation source
1C76: add  eax, 0xC
1C7A: idiv dword [ebp-0x10]            ; (x + 12) / 24
1C7D: add  ecx, eax
1C81: lea  eax, [ecx*8]
1C8D: div  ecx=3                       ; * 8 / 3
1C8F: movzx ecx, word [edi+0x10]       ; global attenuation
1C93: add  ecx, eax
1C95: cmp  ecx, 0xFF                   ; clip
1CA0: mov  al, byte [eax+8]            ; expression (CC11)
1CA3: cmp  al, 0x7F / jae skip
1CAA: movzx edx, byte [eax+0x408F90]   ; exprDb[expression]
1CB1: mov  eax, 0x100                  ; 256
1CB9: sub  eax, ecx
1CBB: imul eax, edx
1CBE: shr  eax, 7                      ; * exprDb / 128
1CC1: add  ecx, eax
1CCB: cmp  dword [edi+0x158E], 0x4D474D31   ; "1MGM"
1CD7: add  ecx, 0x10                    ; +16 units = 6 dB
1CE7: mov  word [esi+0x26], cx
```

Three deviations from our `Awe32Curves.h` then:

1. **`+16` when the bank refers to the ROM "1MGM"** — 6 dB we lacked
   entirely. In the trace `ecx` went from `0x18` to `0x28` exactly here.
   The `IFATN` difference fell from 14 units to 2. (Later: the condition is
   "does the sample lie in ROM", see `tests.md`.)
2. **The patch attenuation is added directly** — see 13.1.
3. **Expression:** the driver `(256 - atten) * exprDb / 128`, we
   `(255 - atten) * exprDb / 127`.

**Side effect:** those 6 dB were exactly the missing headroom. RELAX clipped
before the fix (peak 1.000), after it the peak is 0.592.

## 10.3 Register state at note-on then

**25 of 33 registers matched 242/242.** Remaining:

| register | ours -> driver | note |
|---|---|---|
| `IFATN` | `DC2A` -> `DC28` | only 2 units, see 10.2 |
| `TREMFRQ` | `0000` -> `0080` | LFO1 frequency default |
| `PTRX^`, `CPF^` | `CD5C` -> `21A0` | linear pitch increment, not `IP` |
| `PTRX` low | `0000` -> `1C81` | reverb send + the second byte |
| `DCYSUSV` | `7F05` -> `0004` | sustain and decay |
| `DCYSUS` | `7F7F` -> `007F` | mod envelope sustain |

---

# 11. Input to the EMU8000: 31 of 33 registers match 242/242

Everything below is read from `SBAWE.VXD` (statically and with the
instruction trace), not estimated from measured values.

## 11.1 Fields of the voice structure

At every port write the tracer dumps the memory window around `EBX`, so the
whole voice structure can be read at once. For the first note of MINUET:

| offset | meaning | value |
|---|---|---|
| `+0x10` | filter cutoff | `00DC` |
| `+0x12` | filter Q | `0000` |
| `+0x1E` | chorus send | `0000` |
| `+0x20` | reverb send | `001C` |
| `+0x24` | auxiliary pan | `0081` |
| `+0x26` | attenuation | `0028` |
| `+0x2A`, `+0x2E` | LFO1, LFO2 delay | `8000` |
| `+0x2C` | LFO1 frequency | `0080` |
| `+0x34`, `+0x36` | mod envelope attack, hold | `007D`, `007F` |
| `+0x38`, `+0x3A` | mod envelope decay, sustain | `007F`, `0000` |
| `+0x44`, `+0x46` | volume envelope attack, hold | `007D`, `0079` |
| `+0x48`, `+0x4A` | volume envelope decay, sustain | `0004`, `0000` |
| `+0x5C` | note number | |
| `+0x5E` | velocity | `006D` |

(The AWE32 SDK header `SFTYPE.H` later gave the official names of these
fields — see `driver_note_on.md`.)

## 11.2 Driver defaults

`SYNTHGM.SBK` has **no** `freqModLFO`, `sustainVolEnv` nor
`reverbEffectsSend` generators for the piano (the last not in the whole
bank), and MINUET sends no CC91. Still the driver writes non-zero values —
its defaults:

| missing generator | the driver uses | we had |
|---|---|---|
| `freqModLFO` | **128** (TREMFRQ lo) | 0 |
| `sustainVolEnv`, `sustainModEnv` | **0** = decay to silence | `0x7F` = hold |
| `reverbEffectsSend` | **28** | 0 |

The low byte of `PTRX` gets **`256 - pan`** (so `0x81` at pan `0x7F`).

## 11.3 Pitch: PTRX and CPF carry a linear increment

The upper half of `PTRX` and `CPF` is not the logarithmic `IP` but a linear
increment. `SBAWE.VXD` object 1, `0x212E`:

```asm
212E: mov  esi, 1
2133: mov  ecx, [ebp-0x10]        ; IP
2136: shr  ecx, 0xC
2139: shl  esi, cl                ; 2^(IP>>12)
213B: test byte [ebp-0xF], 8      ; bit 11
2148: imul eax, esi, 0x102E
214F: idiv 0x2710
2151: add  esi, eax               ; * (1 + 0.41420)
2153: test byte [ebp-0xF], 4      ; bit 10 -> 0x764/0x2710 = 0.18920
216B: test byte [ebp-0xF], 2      ; bit  9 -> 0x389/0x2710 = 0.09050
218C: sar  eax, 2
218F: add  esi, eax               ; * 1.25
2191: cmp  esi, 0xFFFF            ; clip
```

It is `2^(IP/4096)` in fixed point: the integer part gives a shift, the top
three fraction bits add `2^(1/2)`, `2^(1/4)` and `2^(1/8)` through fractions
with the denominator 10000, finally a multiplication by 1.25. Verified on
three notes bit-exact (`IP=CD5C -> 21A0`, `BE17 -> 1254`, `D006 -> 2800`).
Implemented as `PitchIncrement()` in `Synth.cpp`.

## 11.4 Two registers remained

| register | ours -> driver | difference |
|---|---|---|
| `IFATN` | `DC2A` -> `DC28` | 2 attenuation units (0.75 dB) |
| `DCYSUSV` | `0005` -> `0004` | one decay step |

(Both resolved in section 13.)

## 11.5 Spectrum

MINUET against 86Box after all fixes: up to 6.4 kHz +2.8 to +4.1 dB (only
the overall level — 86Box does not apply the SB16 mixer), 6400-12800 Hz
-4.8 dB, above 12.8 kHz -30.5 dB. Envelope correlation **0.998**. The band
above 12.8 kHz is **no register question** any more — it is the filter.

---

# 12. Both driver families in the code, the `--driver` switch

Creative has driver families that **deliberately** differ in several points,
so both are in the code. The switch is `--driver dos|win95|sdk`; the default
is `win95`. The starting point is `AWE32Emu/src/Awe32Driver.h`.

| what | `dos` | `win95` | where in the code |
|---|---|---|---|
| 8 values in `INIT3`/`INIT4` | AWEUTIL | ALSA == VXD | `Awe32InitArrays.h`, `kAltInit*Sbawe` |
| `kVelocityDb[0]` | 50 | 99 | `Awe32Curves.h`, `VelocityDb()` |
| attenuation formula | MDI `0x2102` | VXD `0x1C54` | `Awe32Curves.h` |
| attenuation `+16` for ROM samples | no | yes | `Synth.cpp` |

`dos` = `AWEUTIL.COM` (initialisation) + `SBAWE32.MDI` (Miles/AIL, note-on).
`win95` = `SBAWE.VXD`.

## 12.1 Uncertainties at the time

The `dos` family was not verified on note-on then; that came with sections
14-16. The VXD attenuation formula was transcribed but not yet used (its
patch attenuation field was not understood — resolved in 13.1).

---

# 13. The `win95` variant matches completely

```
python tests/notes_diff.py tests/out/ours_win95.trace tests/out/win95.trace
  -> all registers match
```

All 32 tracked registers at note-on match on all 242 notes. The last two
pieces, both read from the instruction trace:

## 13.1 The patch attenuation is in the field `[esi+0x60]` in 1/20 dB

In the trace that field holds **150** for a preset with
`initialAttenuation` 107. And `150 * 0.05 dB = 7.5 dB = (127 - 107) *
0.375 dB`, so it is the same attenuation in other units. Our
`patchAttenUnits` is in register units (0.375 dB); the conversion is
`* 15 / 2`.

What matters is **where** it is added in the formula: into the dB sum before
the conversion to register units, truncated by the integer division by 24.
The Dos variant adds it only after the conversion through `(3*p) & ~7`.
Hence the permanent difference of two units.

Check on the first note: `volDb[127] = 0`, `velDb[109] = 3`, `X = 150`.

```
sum    = 0 + 3 + (150 + 12) / 24 = 9
atten  = 9 * 8 / 3 = 24 = 0x18
+ 16 for ROM "1MGM"          = 0x28
```

The driver wrote `0x28`. The field `[edi+0x10]` (one more, global
attenuation) was 0 on all 242 notes, so it is not modelled.

## 13.2 The ms -> envelope step conversion rounds to the longer time

The driver picks the table entry whose time is **longer than or equal to**
the given one, while we took the first shorter one. In the trace:

```
+01D4E  edx = [esi+0x5C] = 0x45      ; note 69
        eax = 60 - 69 = -9
+01D59  edx = [esi+0x50] = 0xB7      ; keynumToDecay 183
        eax = -9 * 183 = -1647
        ecx = 12600 - 1647 = 10953   ; ms
+01D9D  [esi+0x48] = 4               ; rate
```

Rate 4 has 11878 ms in the table, rate 5 only 9502 ms. So for 10953 ms the
driver picks 4. Fixed in `DecayRateFromMs()`.

## 13.3 What it did to the sound

The regression passes, RELAX peaks at 0.597. Against 86Box (MINUET) the
spectrum did not change — differences of tenths of a dB. **The input to the
EMU8000 was thereby done**; what remained was the core, specifically the
filter.

---

# 14. The `dos` family: what was measured from `SBAWE32.MDI`

A separate DOS virtual machine ran **Magic Carpet 2**, which uses
`SBAWE32.MDI`. The file in the guest is byte for byte identical with ours,
so it is exactly the binary the `dos` family is derived from.

Trace: `dos_mdi.trace`, 195 236 register writes, 341 musical note-ons.

## 14.1 What the MDI writes at note-on

All 24 registers on all 341 notes — the same set as the VXD **except
CVCF**, which it does not write at all. One note:

```
ATKHLD=7F7D  ATKHLDV=267D  CCCA=98ED  CCCA^=0004  CSL=B63F  CSL^=FE04
DCYSUS=7F7F  DCYSUSV=7F07  ENVVAL=8000  ENVVOL=8000  FM2FRQ2=0000
FMMOD=0000   IFATN=FF0A    IP=F400     LFO1VAL=8000 LFO2VAL=8000
PEFE=0000    PSST=B191     PSST^=0F04  PTRX=7200   PTRX^=9837
TREMFRQ=0080 VTFT=FFFF     VTFT^=0000
```

## 14.2 Three measured differences from the VXD

| thing | `SBAWE.VXD` | `SBAWE32.MDI` |
|---|---|---|
| `VTFT` at the end of note-on | rewritten to `cutoff << 8` | left at `0x0000FFFF` |
| `CVCF` | written | **not written at all** |
| low byte of `PTRX` (aux pan) | `256 - pan` | **0** |

The third is visible directly: in the trace `PSST^` has pan `0x0F`, but
`PTRX` has the low byte `0x00` — with `256 - pan` it would be `0xF1`.
`TREMFRQ = 0x0080` holds for both families.

## 14.3 Pitch: the MDI does not compute it at all, it reads it from the chip

> **Correction.** It originally said the MDI uses its own, more precise
> conversion. It does not — after reversing the whole note-on routine it
> turned out it **does no conversion at all**.

`SBAWE32.MDI`, note-on routine, `0x227C`:

```asm
227C: mov ax, di / or ah, 0x10   ; sel = PTRX (Data0 reg 1, 32-bit)
2282: call 0x182C                ; read_reg32 -> dx:ax
2285: sub ah, ah                 ; keep only the low byte
2287: mov [bp-8], ax
228A: mov [bp-6], dx             ; put the high word aside
2290: mov al, [bx+4]             ; channel reverb
2293: add ax, [si+0x20]          ; + patch reverb, clipped to 255
22A9: mov ah, [bp-0xA]           ; reverb into the high byte
22AC: sub al, al
22AE: or  ax, [bp-8]             ; keep the original low byte
22B1: push dx                    ; keep the original high word
22B3: call 0x17F0                ; write_reg32
```

The function at `0x182C` **reads a 32-bit register**, it is not a pitch
conversion. So the MDI reads PTRX, leaves its high word (the target pitch)
and the low byte unchanged, and rewrites **only the reverb send byte**. The
target pitch is kept by the chip itself.

Evidence: the `PTRX^` values in the trace match exactly what 86Box computes
(`ptrx_pit_target = freqtable[ip] >> 18`):

| `IP` | `PTRX^` in the trace | `freqtable[ip] >> 18` |
|---|---|---|
| `F400` | `9837` | `9837` |
| `F000` | `8000` | `8000` |
| `EEAA` | `78CD` | `78CD` |
| `EC00` | `6BA2` | `6BA2` |
| `B959` | `0BFE` | `0BFE` |

All five exact. The 1.25 formula (11.3) is the **Windows** driver's own
approximation of what the chip does. Implemented: the `dos` variant does a
read-modify-write on PTRX, the `win95` variant computes `PitchIncrement()`.

## 14.4 Bank upload through SMLD

The MDI uploads the game's bank to DRAM sample by sample through the `SMLD`
register — 84 851 writes against three for the VXD. We put the samples into
DRAM with `memcpy`; the replay harness loads them through `--dram`. No
effect on the note-on registers, but worth knowing when comparing whole
traces. (Our `--replay` handles SMALW/SMLD uploads now.)

## 14.5 What was still missing

A note-by-note comparison like for the VXD — see section 15.

## 14.6 The `sel` encoding in the MDI

The Miles driver uses yet another encoding than AWEUTIL and the VXD. `sel`
is `voice | (NN << 8)`, where `NN = (reg << 4) | portCode`:

| portCode | port |
|---|---|
| 0 | Data0 (`0x620`), 32-bit registers through `0x17F0` |
| 4 | Data1 (`0xA20`) |
| 6 | Data2 (`0xA22`) |
| 8 | Data3 (`0xE20`) |

Helper routines: `0x177E` 16-bit write, `0x17F0` 32-bit write, `0x182C`
32-bit read. The port table is in the data at `0x70E`, the pointer port at
`[0x712] + 2`.

Write order in the note-on (from `0x2175`):

```
DCYSUSV=0x80 (silence)  VTFT=0x0000FFFF  ENVVOL  ATKHLDV  ENVVAL  ATKHLD
DCYSUS  IP  IFATN  LFO1VAL  LFO2VAL  PEFE  FMMOD  TREMFRQ  FM2FRQ2
PTRX (read-modify-write)  ...  and DCYSUSV last
```

Neither `CVCF` nor `CPF` are among them.

## 14.7 What is in the code

| difference | `dos` | `win95` |
|---|---|---|
| `CVCF` at note-on | not written | written |
| `CPF` at note-on | not written | writes the increment |
| `VTFT` at the end | leaves `0x0000FFFF` | rewritten to `cutoff << 8` |
| `PTRX` | read-modify-write, reverb only | computed and written whole |
| low byte of `PTRX` | kept (0) | `256 - pan` |
| attenuation formula | MDI `0x2102` | VXD `0x1C54` |
| `+16` for ROM samples | no | yes |
| 8 values in the init arrays | AWEUTIL | ALSA == VXD |
| `kVelocityDb[0]` | 50 | 99 |

Verified that the `win95` variant **still matches 242/242 on all registers**
after all these changes.

---

# 15. The `dos` family number by number

The song was identified from the data: our trace of `004_C2INTRO_w.xmi` with
`BULLFROG.SBK` has `IP=F400`, `PSST=B191` and `CSL=B63F` on the first note,
exactly as the recording from the game.

```bash
AWE32Emu 004_C2INTRO_w.xmi --rom awe32.raw --sf BULLFROG.SBK \
    --wav out.wav --trace mc_intro.trace --driver dos

python tests/notes_diff.py mc_intro.trace dos_mdi.trace --pair
```

## 15.1 Why pairing and not order

The game's recording does not start at the beginning of the song and our
trace has 668 notes against 407 in the recording, so order pairing does not
fit. The `--pair` mode pairs every driver note with our note of the **same
pitch and sample**. 270 of 407 notes were paired. (See 16.1 for what was
wrong with this key.)

## 15.2 Result: 19 of 24 registers match 270/270

Matching: `ATKHLD`, `ATKHLDV`, `CCCA^`, `CSL`, `CSL^`, `DCYSUS`, `DCYSUSV`,
`ENVVAL`, `ENVVOL`, `FM2FRQ2`, `FMMOD`, `IP`, `LFO1VAL`, `LFO2VAL`, `PEFE`,
`PSST`, `TREMFRQ`, `VTFT`, `VTFT^`. **The envelopes match completely** — on
another piece of music and another bank than the one the `win95` variant
was tuned on.

## 15.3 Five remaining differences

| register | relation | note |
|---|---|---|
| `CCCA` | driver = ours **-42** (270/270) | start address |
| `PTRX` | driver = ours **-0x8C00** (270/270) | reverb send: ours `0xFE`, driver `0x72` |
| `PTRX^` | ours 0, the driver has the target pitch | our core did not compute `ptrx_pit_target`, see 14.3 |
| `IFATN` | +10 on 233 notes, 0 on 37 | attenuation |
| `PSST^` | 233 match, 37 differ in pan (`0x0F` vs `0x7F`) | |

## 15.4 Fixes made from this comparison

**`PTRX^` (target pitch) — solved in the core, not in the driver.** Our
`Emu8000Core` did not compute the target pitch at all, so the dos
read-modify-write returned zero. Now an `IP` write computes it as in 86Box.
Two details: the intermediate value must be **64-bit** (for high `IP` it
exceeds 2^32), and `PTRX` must be read **after** the `IP` write (the MDI has
exactly that order: IP at 0x21DC, PTRX at 0x227C). `PTRX^` now matches
**270/270**.

**The reverb send** is now added (`channel + patch`) per `0x2290`, instead of
taking the larger of the two.

## 15.5 State after section 16: both families match completely

| | `win95` vs `SBAWE.VXD` | `dos` vs `SBAWE32.MDI` |
|---|---|---|
| song | MINUET, `SYNTHGM.SBK` | C2INTRO, `BULLFROG.SBK` |
| notes | 242 | 255 paired of 261 |
| match | **all 24 registers** | **all 24 registers** |

Two of the five differences above were not conversion differences but errors
in **how we measured** — see 16.1.

---

# 16. Finishing the `dos` family — and two measurement errors

## 16.1 First the method fix

**1) The driver recording held more than one song.** Magic Carpet 2 played
the intro, then another piece and then the intro again during the
measurement:

| frames | what plays |
|---|---|
| 2 810 017 - 5 986 589 | intro (`ccca=98ED`, `pan=0F`, `atten=10`) |
| 6 516 243 - 9 331 847 | **another piece** (`ccca=ECD1`/`E7DA`, `rev=8E`) |
| 9 388 955 - on | the intro again |

The middle section uses the same instrument on a channel with another
`CC10`, so notes that are not in our input at all got into the comparison.
That was the "pan difference on 37 notes". `notes_diff.py` has the switches
`--dframes` and `--oframes`:

```bash
python tests/notes_diff.py ours.trace driver.trace --pair --dframes 0:6000000
```

**2) The pairing key contained the very registers to be compared.** Pairing
was by `(IP, PSST, CSL)`, so a driver note only paired with our note that
already had the **same** `PSST` and `CSL` — a difference in them could never
show, and all drums were silently lost. The key is now `(IP, sample address
with a tolerance of 256 words)`.

After both fixes the comparison grew from 270 notes of one sample to 255
notes of three samples, and two differences we had not known about appeared
(the loop of one-shot samples, the DRAM offset).

## 16.2 Where things are in `SBAWE32.MDI`

The whole note-on is one function from **0x1E76**:

| address | what it does |
|---|---|
| 0x1E76 | entry, arguments `[bp+4]` note, `[bp+6]` velocity, `[bp+8]` channel |
| 0x1E84 | `si = 0xE46 + channel*0x1C` — **the channel record**, kept in `[bp-0x1e]` |
| 0x1E97 | call to 0x1A8C — finds the preset layers and fills the parameter blocks |
| 0x1EFD | `[bp-2] = 0xBC6 + voice*0x14` — **the voice record** |
| 0x1ED1 | `[bp-0x1a] = 0x9D6` — **the layer parameter block**, step 0x86 |
| 0x1FF4 | sample addresses, loop / one-shot branch |
| 0x2102 | attenuation (tables + expression) |
| 0x2175 | writes to the chip registers |

The key finding is at 0x1CD0 inside 0x1A8C:

```
1CCB:  mov si, 0x16ad
1CD0:  push cs ; pop ds
1CD2:  mov cx, 0x43
1CD5:  rep movsw                  ; 67 words = generator defaults
...
1CDD:  [bx+0x5c] = note           ; generator 46
1CE3:  [bx+0x5e] = velocity       ; generator 47
1CFE:  for each generator of the bank:  [block + gen*2] = value
```

So the layer parameter block is **a direct array of SoundFont generators,
indexed by the generator number times two**. That explains all the
mysterious offsets:

| offset | generator | meaning |
|---|---|---|
| `[si+0x10]` | 8 | initialFilterFc |
| `[si+0x1e]` | 15 | chorusEffectsSend |
| `[si+0x20]` | 16 | reverbEffectsSend |
| `[si+0x22]` | 17 | pan |
| `[si+0x5c]` | 46 | keynum (note) |
| `[si+0x5e]` | 47 | velocity |
| `[si+0x60]` | 48 | initialAttenuation |
| `[si+0x6c]` | 54 | sampleModes |
| `[si+0x76]` | - | sample start (computed) |
| `[si+0x7a]` | - | sample end |
| `[si+0x7e]` | - | loop start |
| `[si+0x82]` | - | loop end |

`SBAWE.VXD` has **exactly the same layout** (only 32-bit), so this map holds
for both families — e.g. at 0x1ECF: `eax = [ebx+0x76]` and
`test byte ptr [ebx+0x6c], 1`.

## 16.3 The table of generator defaults

The one copied through `rep movsw` lies in the MDI at **0x16AD** and in the
VXD (object 1) at **0x6D60**. The same values except one:

| generator | | MDI 0x16AD | VXD 0x6D60 |
|---|---|---|---|
| 8 | initialFilterFc | 255 | 255 |
| 15 | chorusEffectsSend | 0 | 0 |
| 16 | reverbEffectsSend | 28 | 28 |
| 17 | pan | **64** | **64** |
| 22 | freqModLFO | 128 | 128 |
| 26 | attackModEnv | 125 | 125 |
| 27 | holdModEnv | 127 | 127 |
| 28 | decayModEnv | 127 | 127 |
| 30 | releaseModEnv | 127 | 127 |
| 34 | attackVolEnv | 125 | 125 |
| 38 | releaseVolEnv | 127 | 127 |
| 43 | keyRange | 0..127 | 0..127 |
| 44 | velRange | 0..127 | 0..127 |
| 48 | initialAttenuation | **110** | **127** |
| 54 | sampleModes | **0** | **0** |
| 58 | overridingRootKey | 60 | 60 |

Two things matter:

- **`sampleModes` defaults to zero**, i.e. a one-shot sample. We had one
  (loop) as the SF1 default. The Magic Carpet 2 bank shows it: the presets
  `LOOP2` and `LOOP3` have no `sampleModes`, and the driver really puts their
  loop after the sample. The preset names mislead.
- **`initialAttenuation` differs between the families** (110 vs 127).

## 16.4 Difference by difference

### CCCA: `-46` for DOS, `-4` for Windows

```
SBAWE32.MDI 0x1FF4:  ax:dx = [si+0x76];  sub ax, 0x2e   (46)
SBAWE.VXD   0x1ECF:  eax   = [ebx+0x76]; sub eax, 4
```

Not a typo nor a measurement error — against the MDI it was exactly 42 words
on all notes, and against the VXD `CCCA` matches with four on 242 notes.

### Reverb and chorus: the channel share is scaled to 90 %

The controller handlers in the MDI are four in a row:

```
242A:  mov ax,0x5a; mul si; mov cx,0x64; div cx;  [channel+4] = al   ; CC91 reverb
244C:  mov ax,0x5a; mul si; mov cx,0x64; div cx;  [channel+5] = al   ; CC93 chorus
246E:  [channel+6] = al                                              ; CC10 pan
2482:  [channel+8] = al                                              ; CC7 volume
```

Pan and volume are not scaled, reverb and chorus are. `127 * 90 / 100 =
114` = `0x72`, exactly what was in the trace. The VXD has it literally the
same (object 1, 0x314D and 0x3174, into `[channel+0x447]` and `[+0x448]`).
Combined with the bank value as a sum clipped to 255 (MDI 0x2290 for reverb,
0x230A for chorus).

### Pan: `0x17F - 2*(bankPan + CC10)`

```
MDI 0x22B6:  ax = 0x17F;  cx = [channel+6] + [si+0x22];  cx += cx;  ax -= cx
             if (ax >= 0xFE) ax = 0xFF;   if (negative) ax = 0
VXD 0x4253:  the same formula, only the lower limit is  if (ax <= 1) ax = 0
```

Before, we added the finished register value from the bank to an offset
from CC10. It came out the same only because the `pan` generator is missing
in both measured banks and the default 64 gives the same result. The first
attempt with this formula came out **worse** — because our register value
(default 127) was substituted instead of the raw generator value (default
64).

### CSL: the one is added only for a looped sample

```
MDI 0x2019 (loop):        loopEnd_reg = [si+0x82] + 1
MDI 0x208B (one-shot):    loopEnd_reg = end + 8      ; without the one
```

We added `+1` unconditionally, so `CSL` came out one higher on 52 drum notes.

### DRAM: the first sample does not start at the beginning

The driver leaves **50 words** before the first sample in DRAM — its first
sample starts at `0x200032`, ours at `0x200000`. It makes sense: when `CCCA`
points 46 words before the start of the sample, without a reserve it would
still point into the card's ROM. The difference was exactly 50 on all 52
drum notes. (The Win95 driver reserves 16 — see `tests.md`.)

## 16.5 IFATN: not the formula, the game's music volume

The attenuation came out 10 units lower than the driver's on all notes. The
formula was right; the input was the problem.

```
if (CC7 <= 10) return 255;
atten = ( 8*(volDb[CC7] + velDb[velocity]) + ((3*(127 - patchAtten)) & ~7) ) / 3;
if (atten >= 255) return 255;
if (CC11 < 127) atten += exprDb[CC11] * (255 - atten) / 127;
```

The XMI sends `CC7 = 127` and all notes have velocity 127; `volDb[127]` and
`velDb[127]` are zero, so we got 0. But the driver wrote 10, which
corresponds to `volDb` = 4, i.e. **an effective CC7 around 100**.

Check on another channel: channel 5 sends `CC7 = 90` and the driver wrote an
attenuation of 29 there, which corresponds to `volDb` = 11, i.e. an
effective CC7 = 70. The only master volume that fits both at once is **99 or
100 of 127**:

| master volume | CC7 127 -> | atten | CC7 90 -> | atten |
|---|---|---|---|---|
| 99 | 99 | 10 | 70 | 29 |
| 100 | 100 | 10 | 70 | 29 |
| 101 | 101 | 10 | 71 | **26** |

The scaling is not done by the driver — `0x2482` stores `CC7` raw — but by
the AIL sequencer (`AIL_set_XMIDI_master_volume`), i.e. the game itself. So
the player has `--master-volume N` (0..127, default 127) to reproduce the
measurement.

## 16.6 Result

```
paired 255 of 261 driver notes
OK ATKHLD  ATKHLDV  CCCA  CCCA^  CSL  CSL^  DCYSUS  DCYSUSV  ENVVAL  ENVVOL
OK FM2FRQ2  FMMOD  IFATN  IP  LFO1VAL  LFO2VAL  PEFE  PSST  PSST^  PTRX
OK PTRX^  TREMFRQ  VTFT  VTFT^
all registers match
```

The `win95` variant stayed at 242/242 on all registers and `regress.py`
passes.

## 16.7 New tools

| script | used for |
|---|---|
| `tests/xmi_events.py` | MIDI events of an XMI — programs, controllers, velocities per channel |
| `tests/sbk_dump.py` | preset and instrument generators of an SBK/SF2 |

`notes_diff.py` got `--dframes` / `--oframes` and the new pairing (16.1).

---

# 17. Later additions (summary)

The sections above cover the register values at note-on. Later work extended
the driver layer to the whole life of a note and to a third family; the
details are in the comments of `Synth.cpp`, `Awe32Driver.h` and
`Awe32Curves.h`.

- **Voice allocation, per family, transcribed from the code:**
  - `win95` (`SBAWE.VXD` 0xC0FF9C68): three passes over the voices in steps
    of three (0, 3, .. 30 / 1, 4, .. 31 / 2, 5, .. 29); score = the voice's
    volume target `VTFT^`, +0x200 when assigned, +0x300 when held by the
    pedal, otherwise +0x1300 when its volume envelope is not in release; the
    lowest wins, a later voice wins a tie; a free silent voice or one past
    its sample end is taken at once; while at most two ROM voices play, ROM
    voices are not taken. The chosen voice gets `DCYSUSV 0x00FF`. `win95`
    uses all 32 voices; the others 30 (30/31 are the DRAM refresh).
  - `dos` (`SBAWE32.MDI`): its own allocation that reads the chip state.
  - `sdk`: two passes by twos (even, then odd); an earlier voice wins a tie.
- **Voices 30/31:** both Creative drivers write `VTFT = 0x0000FFFF` for them
  (AWEUTIL's `cwd` gives 0xFFFFFFFF); with 0xFFFF the VXD allocation would
  never pick them. `HWCF3` ends at 0x0004 (MDI, AWEUTIL) or 0x0006 (VXD).
- **win95 note life:** pan glide for CC10 (target `0x17F - 2*(bankPan +
  CC10)`, one step at once and then every 227 frames), exclusive classes,
  channel pressure, CC121, both envelopes released at note-off (`DCYSUSV`
  and `DCYSUS`).
- **The third family `sdk`** — the Creative AWE32 DOS SDK (`RAWE32L.LIB`,
  module `midieng.c`), used by DOSMid and by AWETEST itself. The SDK's GM
  bank (`embed.c`) has exactly the format of the GM tables in
  `SBAWE32.MDI`, so the MDI loader reads it once an `AIL3MDI` header is put
  in front. With it DOSMid's Georgia matches 32/32. The SDK is the ancestor of
  the VXD: the same write order, attenuation via its own tables with
  `(127 - patchAtten)*25/80`, pitch increment `1.5 * 2^(pitch>>12) * ...`,
  the start address offset 5.
- **86Box bugs found on the way** (all reported in `snd_emu8k.c` comments
  marked `AWE32Emu:`): the chip ID register (5.2), the filter cutoff scale
  (1.8-2.5 octaves too high against the card), and an IFATN workaround that
  ignored attenuation 0 on a silent voice.
- **Verification beyond register values:** a replay of the real driver's
  trace through the same chip against our MIDI render (same chip ->
  difference = driver layer). Georgia (win95): level median +0.09 dB, p95
  0.9 dB; MC2 intro (dos): 0.00 dB, p95 0.48 dB. This caught a bug the
  register matrix missed (a ROM-voice flag not cleared on release made all
  notes after 18 s land on voice 0).

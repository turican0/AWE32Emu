# AWEUTIL.COM — notes from the disassembly (register access)

**Purpose of this file:** reference notes for verifying and completing the
EMU8000 register map (see [TODO.md](../TODO.md), section 10). It is **not
source code** of the emulator and is not compiled anywhere. It holds only a
description of the behaviour (pseudo-C, in our own words), not a transcript
of the assembler.

Source: IDA disassembly of `AWEUTIL.COM` (the version found in
`SBAWE32\130-AWE\...`, SHA256
`2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`),
analysed on 2026-08-13.

Marking: **[verified by driver behaviour]** vs. **[only derived from the
disassembly, to be verified against the Programmer's Guide]**. (Everything
here was later confirmed; see `emu8000_register_map.md` and
`86box_comparison.md` section 5.)

---

## 1. Register access through the Pointer/Data0/Data1 ports

The driver uses four internal helper routines to access the chip registers
(named `sub_10EAC`, `sub_10EFA`, `sub_10F46`, `sub_10F9C` in IDA).
Signatures (derived from the argument count and `retn N`):

- `write_word_reg(data, regSelect)` — write a 16-bit register
- `read_word_reg(regSelect) -> word` — read a 16-bit register
- `write_dword_reg(dataLo, dataHi, regSelect)` — write a 32-bit register
  (the low word is written, then the high word to port+2 — matching the
  Data0/Data1 port pairs mentioned in the general EMU8000 documentation)
- `read_dword_reg(regSelect) -> dword` — read a 32-bit register

**[derived, verified later]** `regSelect` is a packed value:

```
regSelect = (register_index << 12) | (port_select << 9) | voice
```

The voice is masked to 5 bits (`and ax, 1Fh`, i.e. 0–31); the remaining bits
(masked with `0x7000`, shifted by 7) form the upper part of the value
written to the Pointer register, which is `(register_index << 5) | voice`.
The port select picks Data0 (0x620), Data0+2, Data1 (0xA20), Data1+2 or
Data3 (0xE20) relative to the base.

Port base: a variable stored in the driver with the default `0x220`/`0x330`
(typical SB16 I/O ports), overwritten from the `BLASTER` environment
variable at start-up. The EMU8000 ports are derived arithmetically from
it (+0x400, +0x800, +0xC00), so the real Pointer/Data addresses depend on the
card's settings.

## 2. Consequences for the implementation

- The Pointer/Data scheme matches the general EMU8000 documentation — so
  `Emu8000Core` (see `AWE32Emu/src/Emu8000.h`) imitates **this access
  pattern** (one "select" + word/dword data).
- The exact register numbers (pitch, envelope, filter, LFO, …) come from the
  Programmer's Guide; see `emu8000_register_map.md`.
- The other clusters of I/O accesses (DRAM/wavetable memory upload through
  SMALW/SMLD) were analysed later — see `emu8000_register_map.md`.

## 3. What is deliberately missing

This file holds no transcript of specific assembler instructions and no
complete decompilation of the driver — only a summary of the register
access mechanism, needed for cross-checking the official documentation.

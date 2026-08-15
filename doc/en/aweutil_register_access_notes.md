# AWEUTIL.COM – disassembly notes (additional source, see TODO section 10.5)

**Purpose of this file:** Reference notes for verifying/adding the EMU8000 register map in section 4 of the main TODO. **This is not the plugin source code** and this file
is not compiled or otherwise linked into the build anywhere. It only contains a description of the behavior
(pseudo-C, in your own words), not an assembler transcript.

Source: IDA disassembly `AWEUTIL.COM` (version found in `SBAWE32\130-AWE\...`,
SHA256 `2D3DCA0506FE4551BBC3BF56398A8076680358F57A3827D1FB748BC680B95988`),
analyzed 13.8.2026.

Marking: **[verified by driver behavior]** vs. **[only derived from disassembly, must
verify against Tech Ref Manual]**.

---

## 1. Register access via Pointer/Data0/Data1 ports

The driver uses four internal helper routines to access the chip registers
(in IDA named `sub_10EAC`, `sub_10EFA`, `sub_10F46`, `sub_10F9C`).
Signatures (derived from the number of arguments and `retn N`):

- `write_word_reg(data, regSelect)` – write 16bit register
- `read_word_reg(regSelect) -> word` – read 16bit register
- `write_dword_reg(dataLo, dataHi, regSelect)` – write 32bit register
(low word is written, then high word on port+2 – corresponds to the pair
of Data0/Data1 ports mentioned in the general documentation for EMU8000)
- `read_dword_reg(regSelect) -> dword` – read 32bit register (analogously)

**[only derived, must be verified]** `regSelect` looks like a wrapped value:

```
regSelect = (voice_or_channel_bits << offset) | register_index
```

where `register_index` is masked to 5 bits in the code (`and ax, 1Fh`, i.e. 0–31) and
the remaining bits of `regSelect` (masked `0x7000`, shifted by 7) are added to the upper
part of the address written to the Pointer register. The exact bit positions and their
relation to the voice number (0–31) need to be found in the Tech Ref Manual – the code
itself does not contain named constants, only bit masks.

Port base: a variable stored in the driver as the default `0x220`/`0x330`
(typical SB16 I/O ports), overwritten by the value from the `BLASTER` environment
variable at startup. Writing/reading EMU8000 registers is derived from these values
arithmetically (see code), not by a fixed constant – i.e. the actual
address of the Pointer/Data ports on the target system may vary according to
the card settings.

## 2. Implementation recommendations (section 4 of the main TODO)

- The Pointer/Data0/Data1 register scheme corresponds to what is described in the general
EMU8000 documentation – i.e. when writing `Emu8000Core` (see `src/Emu8000.h`)
it is reasonable to imitate **this access pattern** (one "select" + word/dword
data), not necessarily the same bit constants.
- The exact bit composition of `regSelect` and the specific register numbers (pitch,
envelope, filter, LFO...) need to be found in the Tech Ref Manual (point 0
of the main TODO) – this note only confirms that the chip is
addressed this way, it does not give a complete register map.
- Other clusters of I/O accesses in the disassembly (around another base variable,
probably DRAM/wavetable memory) **not analyzed** yet – TODO for
the next pass.

## 3. What is intentionally missing

This file does not contain any transcript of specific assembler instructions or
a complete decompilation of the driver – just a summary of the discovered mechanism of access
to registers, needed for cross-checking with official documentation.

# Wave ROM: `awe32.raw` vs `1mgm.sf2`

Compared on 2026-08-13. Neither file is in the repository — they are
original Creative/E-mu data (see [USAGE.md](../USAGE.md) for where to get
`awe32.raw`).

## Conclusion

They are **the same samples in two formats**.

| | `awe32.raw` | `1mgm.sf2` |
|---|---|---|
| size | 1 048 576 B (exactly 1 MB) | 1 090 280 B |
| contents | ROM header + sample pool | RIFF sfbk: INFO + sdta/smpl + pdta |
| samples | from offset `0x3DE` | chunk `smpl` from offset `0x8E` |

The overlap of 1 047 586 bytes (523 793 samples) is **byte for byte
identical**, MD5 of both ranges `8ff0680989bfa4924fbccd4527302f03`.

Differences:

- the `.sf2` has 2 more bytes (`FFFF`) at the end of `smpl` than fit into the
  1 MB ROM — a chunk terminator, not sound data
- the `.raw` additionally has the ROM header at `0x000..0x3DE`
- the `.sf2` additionally has the bank structure (`phdr`, `pbag`, `pgen`,
  `inst`, `ibag`, `igen`, `shdr`), which the `.raw` holds only in Creative's
  proprietary format

## ROM header

The text in the header is stored in 16-bit words, so read byte by byte it
looks swapped (`iNhgitgnla e` = `Nightingale`). After swapping the bytes:

```
2.81MGM ... Nightingale     General MIDI    Copyright 1993 E-mu Systems,I...
```

**The sample data are not swapped** — they are 16-bit little-endian values
and are read directly. The swap concerns only the ASCII strings in the
header.

## How the emulation uses it

**`awe32.raw`** is used, because it is exactly what the chip sees:

- the ROM is mapped to sound memory addresses 0 .. 0x7FFFF (addresses are in
  samples, not bytes; 1 MB = 524 288 words)
- the user DRAM starts only at `Emu8000::kDramOffset` = 0x200000

The bank structure can be read from `1mgm.sf2`, because it is in a
documented format. The index conversion is trivial and, thanks to the proven
match of the data, exact:

```
EMU8000 address = 0x1EF + sample index from the sf2 shdr
```

where `0x1EF` = 495 words = offset `0x3DE` in the ROM.

## Update 2026-09-14: dump from a real card

A dump from the tester's AWE32 (AWEDUMP, md5
`1d8f7f3842f6fb19cfcb2247e9f45870`) starts with `0032` and equals
`awe32.raw` **shifted by one word** (0 of 524 287 words differ). `awe32.raw`
has an extra `0x1234` word in front, which 86Box already drops in
`emu8k_init`. On the card, sample index `i` of `1mgm.sf2` is therefore at
chip address `0x1EE + i`, not `0x1EF + i`. `Synth::LoadWaveRom` now drops
the extra word too, so both files load identically; the register values
(`kRomPoolBase` = 495 with the -1/-2/-3 correction, SF1 addresses) are
unchanged because they match what the drivers write.

Reverse engineering the preset tables inside the `.raw` would mean more RE of
a proprietary format without any benefit — the samples would come out the
same.

# SoundFont 1.0 (`.SBK`) — how to read it for the EMU8000

Derived from `BULLFROG.SBK` (the Magic Carpet 2 bank) combined with the
note-on routine in `SBAWE32.DRV` (offset `0x038A` onwards) and the
Programmer's Guide. Several conversions were refined later against the
drivers — the authoritative versions are in `AWE32Emu/src/SoundFont.cpp`.

## Differences from SF2

| | SF1.0 | SF2 |
|---|---|---|
| `ifil` | 1.x | 2.x |
| `shdr` | **16 B** = 4 dwords (start, end, loopStart, loopEnd) | 46 B including name, sample rate, root key |
| sample names | separate chunk **`snam`**, 20 B per sample | inside `shdr` |
| generator units | **EMU8000 registers directly**, times in **ms** | normalised (timecents, centibels, cents) |

## Samples in ROM vs in the bank

A sample name starting with `*` means **a sample from the card's ROM**, not
from the bank's `smpl` chunk. `INFO/irom` says which ROM is expected
(`1MGM`). (Later finding: the asterisk is only a habit of banks saved through
SFSTORE.DLL. Creative's own banks put the ROM samples first without an
asterisk; the reliable rule is "samples before the first one that starts at
address 0 are ROM samples" — see the comment in `SoundFont.cpp`.)

The addresses in `shdr` are already **finished EMU8000 addresses**,
including the offset of the sample pool in ROM and the interpolator
correction. Verified against `1mgm.sf2`, where the same samples have indices
within `smpl`:

| field | SBK offset against the index in 1mgm.sf2 |
|---|---|
| start | +494 |
| end | +494 |
| loopStart | +493 |
| loopEnd | +492 |

The sample pool in `awe32.raw` starts at word **495** (byte `0x3DE`,
verified by a byte comparison). The difference against +494 is exactly the
"-1" correction described by the Programmer's Guide ("actual audio location
is one word higher"). The loop points have another -1 / -2.

**Practical consequence:** for `.SBK` the `shdr` values can be written to
CCCA/PSST/CSL directly, without conversion. For `.sf2`, 495 has to be added
and the correction subtracted.

Samples without `*` (here `LOOP2`, `REV2`) are indices into the bank's
`smpl`, which is loaded into the card's DRAM — their address is
`kDramOffset + index`.

## Meaning of the SF1.0 generators

Found from the values in `BULLFROG.SBK` and from what the note-on in
`SBAWE32.DRV` does with them.

| generator | range in the bank | conversion to EMU8000 |
|---|---|---|
| `initialAttenuation` (48) | 127 | **0..127, where 127 = no attenuation**; `dB = (127 - v) * 0.375` -> IFATN lo. Exactly what `SBAWE32.DRV` does at `0x038A`: `ax = 0x7F - v; ax = ax*3; ax >>= 3` |
| `sustainVolEnv` (37) | 127 | whole dB above silence -> DCYSUSV bits 14..8 as `v * 4 / 3` (later finding, see SoundFont.cpp) |
| `sustainModEnv` (29) | 127 | the same for DCYSUS bits 14..8 |
| `holdVolEnv` (35) | 8191 | **ms** -> ATKHLDV bits 14..8 as `127 - ms / 92` (truncating) |
| `attackVolEnv` (34) | ms | -> ATKHLDV bits 6..0 through the attack table (`11878 / k(r-1)`) |
| `decayVolEnv` (36) | 2034, 5940 | **ms** -> DCYSUSV bits 6..0 through the decay table (`47513 / k(r-1)`) |
| `releaseVolEnv` (38) | 1192, 5940 | **ms** -> the same table, written at Note Off with bit 15 |
| `modEnvToFilterFc` (11) | -5 to -30 | PEFE bits 7..0 (signed), doubled for SF1 |
| `modLfoToVolume` (13) | 127 | TREMFRQ bits 15..8, doubled for SF1 |
| `modLfoToFilterFc` (10) | 127 | FMMOD bits 7..0, doubled for SF1 |
| `freqModLFO` (22) | 120 | TREMFRQ bits 7..0 (LFO1 frequency), doubled for SF1 |
| `reverbEffectsSend` (16) | 0..58 | PTRX bits 15..8 |
| `chorusEffectsSend` (15) | 0..254 | CSL bits 31..24 |
| `initialFilterQ` (9) | 127 | 0..127 -> CCCA bits 31..28 (0..15) as `v >> 3` (measured) |
| `overridingRootKey` (58) | 60 | root note of the sample |
| `sampleModes` (54) | 0 / 1 | 1 = loop |
| `keyRange` (43) | 12..107 | key range of the zone |
| `sampleID` (53) | index into `shdr` | |

### Generator 55

Every zone in `BULLFROG.SBK` has `gen55 = 6000`. In SF2, 55 is unused.
Later finding: it is the sample rate / pitch component the driver bakes into
the pitch (see `SoundFont.cpp`, "The sample rate is converted to cents
separately").

## Structure of BULLFROG.SBK

- 15 presets, each with 1 zone, each zone 1 instrument with 1 zone
- 12 samples: 10 from ROM (`*`), 2 own (`LOOP2`, `REV2`, together 84 794
  samples in `smpl`)
- presets: 3 (LOOP2), 0 (REV2), 4 (LOOP3), 5 (TBellD4Wave), 117-127

The songs `midi/*_w.xmi` have patches **5, 3, 4** (= presets of this bank)
and **52** (= from ROM) in their `TIMB` chunk, which confirms that `_w` is the
AWE32 variant and that the bank is layered over the ROM.

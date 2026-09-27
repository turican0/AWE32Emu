# SBAWE32.MDI — the AIL/Miles driver for the AWE32

Source: `SBAWE32.MDI` from the Magic Carpet 2 directory, 36880 B, signature
`AIL3MDI` + 0x1A. It is the driver through which the game really drove the
card, so for "how it sounded" it is more authoritative than the Windows
driver. (This file is the first pass; the complete picture of the `dos`
family is in `86box_comparison.md` sections 14–16.)

## File structure

| offset | contents |
|---|---|
| `0x000` | `AIL3MDI\x1A`, version `0x0112` |
| `0x0BA` | device name: `Creative Sound Blaster AWE32` |
| `0x10A` | entry point table (14 word offsets: `116E`, `119C`, `11B1`, `11B8`, `1233`, `124D`, `12B6`, `12C6`, `12E6`, `12ED`, `137C`, `1383`, `138A`, `1391`) |
| `0x132` | start of the code |
| `0x142D` | expression curve (CC11), 128 B |
| `0x14AD` | channel volume curve (CC7), 128 B |
| `0x152D` | velocity curve, 128 B |
| `0x70E` | EMU8000 port table: `0620 0A20 0E20` |

## Register access — the **third independent confirmation**

| routine | signature |
|---|---|
| `0x177C` | `write_word(data, sel)`, `ret 4` |
| `0x17B8` | `read_word(sel) -> ax`, `ret 2` |
| `0x17F0` | `write_dword(lo, hi, sel)`, `ret 6` |
| `0x182C` | `read_dword(sel) -> dx:ax` |

The encoding is **exactly the same** as in `AWEUTIL.COM` and `SBAWE32.DRV`:

    pointer = ((sel & 0x7000) >> 7) | (sel & 0x1F)     ; = (reg << 5) | voice
    port    = portTable[(sel & 0x0C00) >> 9] | ((sel >> 8) & 2)

The pointer is written to `[0x712] + 2` = `0xE20 + 2` = **0xE22**. Bit 9 of
`sel` adds +2 (the "Data2" alias at `0xA22`), as in AWEUTIL.

## Note-on (`0x2102` to `0x2363`)

The write order is **the same as in our implementation** and in
`SBAWE32.DRV`:

    DCYSUSV = 0x0080          ; silence the voice first
    VTFT    = 0x0000FFFF      ; volume 0, filter open
    ENVVOL, ATKHLDV, ENVVAL, ATKHLD, DCYSUS
    IP, IFATN, LFO1VAL, LFO2VAL, PEFE, FMMOD, TREMFRQ, FM2FRQ2
    PTRX (read-modify-write for the reverb send), PSST, CSL, CCCA
    DCYSUSV = <sustain|decay> ; only this starts the note

Other confirmed values:

- `0x1992`: `DCYSUSV = 0x807F` — immediate end of a voice
- `0x1F12`: `IP = 0xE000` — pitch reset to the unit increment

## Attenuation (`0x2102`) — transcribed 1:1

```
if (cc7 <= 10) atten = 0xFF;
else {
    atten = ( 8*(volDb[cc7] + velDb[velocity])
              + ((3*(0x7F - patchAtten)) & ~7) ) / 3;
    if (atten >= 0xFF) atten = 0xFF;
    else if (expression < 0x7F)
        atten += exprDb[expression] * (0xFF - atten) / 0x7F;
}
```

The patch attenuation is added **directly in register units** (unit
0.375 dB), while the channel volume and velocity are in dB and converted
with the ratio 8/3. Implemented in `AWE32Emu/src/Awe32Curves.h`.

### Conversion tables

All three are **byte for byte identical** to the tables in `SBAWE32.DRV`:

| purpose | MDI | DRV | range |
|---|---|---|---|
| expression (CC11) | `0x142D` | `ds:0592` | 127 .. 0 |
| channel volume (CC7) | `0x14AD` | `ds:0692` | 99 .. 0 |
| velocity | `0x152D` | `ds:0612` | 50 .. 0 |

The first 11 entries of the CC7 table are 99, because values <= 10 lead to
full attenuation anyway (the test at the start of the computation).

The velocity curve is, except for the lowest values, exactly
`-40*log10(v/127)` (e.g. v=64 -> 11.9 vs table 11, v=16 -> 35.9 vs table
36) and it is capped at 50 dB at the bottom.

## SB16 mixer

`0x107E` = `set_midi_volume(al = left, ah = right)` — writes the top 5 bits
to the mixer registers `0x34` / `0x35` (MIDI volume L/R) at port `[0x5DC]`.
`0x1062` reads. At initialisation the driver reads the current value and
writes it back, so it **sets no fixed attenuation** — it depends on what was
in the mixer.

## What did not follow from it

The ~16 dB difference on the long pad of channel 7 in `002_C2GAME3` (see
`early_measurements.md`) is not explained by this driver — its attenuation
formula is now 1:1 and for CC7=90, velocity=127 and a patch without
attenuation it gives 6.0 dB, the same as ours.

The most likely remaining explanation at the time: the patch attenuation
`[si+0x60]` comes from the driver's own patch tables, not from the
SoundFont. Later finding: the game indeed does not use `1mgm.sf2` or
`SYNTHGM.SBK` at all — its GM presets are compiled into this driver
(`--sf SBAWE32.MDI`), and with them the Magic Carpet 2 intro matches the game
on all 24 registers.

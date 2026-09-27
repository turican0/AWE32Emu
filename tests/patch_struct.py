#!/usr/bin/env python
"""Decodes the driver's **internal voice parameter block** from the 86Box CPU trace.

The port trace shows only what the driver finally wrote to the registers.
This tool goes one level down: it takes the memory windows around `EBX`
recorded by `awe32_trace.c` at every EMU8000 port access and splits them into
named fields. `EBX` points to the parameter block of one voice (0x94 bytes;
`SBAWE.VXD` moves it with `add ebx, 0x94`), so the **intermediate values** of
the SoundFont -> register conversion are visible in it, not just the result.

    python tests/patch_struct.py tests/out/cpu.trace
    python tests/patch_struct.py cpu.trace --limit 5 --raw

The field offsets are read from the disassembly of `SBAWE.VXD` object 1 (base
0xC0FF8B49, see insn_view.py) and **verified against the trace**: on the 242
note-ons of MINUET `[0x26]` matches the low byte of the written `IFATN`,
`[0x24]` 0x81 (= 256 - pan) and `[0x44]` 0x7D (the default attack).

Careful: the `EIP` of port writes is always inside a small I/O routine of the
driver (0xA05 / 0xA6F), not in the note-on code - so it cannot be filtered by
it. `EBX` survives the call, though, so the window at it is still the right
block.

    0x0E  used when computing the target filter       (0x20BC)
    0x12  filter Q, goes to CCCA through `shl eax,0x1c` (0x228C)
    0x20  reverb send   -> middle byte of PTRX        (0x22BD)
    0x24  auxiliary pan -> low byte of PTRX            (0x22C1)
    0x26  attenuation; goes to the low byte of IFATN and
          through the table 0x409010 to the target volume (0x21BF)
    0x32  ENVVAL - mod envelope delay                 (0x20A0)
    0x34  mod envelope attack                         (0x2099)
    0x42  ENVVOL - volume envelope delay              (0x21A3)
    0x44  volume envelope attack                      (0x219C)
    0x48  decay of the volume envelope                (0x22EA)
    0x4A  sustain of the volume envelope              (0x22E6)
"""
import argparse
import sys

# Names and offsets are from `SFTYPE.H` of the AWE32 SDK (WINDOWS/INCLUDE) -
# it is **exactly this block**, struct `_SFTYPE`, 59 fields of type short
# (0x76 B). All eleven offsets read earlier from the instruction trace match
# the header. Env1 is the modulation envelope, env2 the volume one.
FIELDS = [
    (0x0A, "lfo1ToPitch"),
    (0x0E, "env1ToPitch"),
    (0x10, "initialFilterFc"),
    (0x12, "initialFilterQ"),
    (0x20, "reverbEffectsSend"),
    (0x22, "panEffectsSend"),
    (0x24, "auxEffectsSend"),
    (0x26, "sampleVolume"),
    (0x2A, "delayLfo1"),
    (0x2E, "delayLfo2"),
    (0x32, "delayEnv1"),
    (0x34, "attackEnv1"),
    (0x42, "delayEnv2"),
    (0x44, "attackEnv2"),
    (0x48, "decayEnv2"),
    (0x4A, "sustainEnv2"),
]


def parse(path):
    """A stream of (line number, P line, address, bytes of the block at EBX).

    **A generator, not a list.** The CPU trace of a whole piece is over 400 MB
    and loading it into memory means several GB of Python objects - the
    machine runs out of breath. The caller walks the trace only once anyway.
    """
    pending = None
    with open(path, "r", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            if line.startswith("P "):
                pending = (lineno, line.split())
            elif line.startswith("M ebx ") and pending is not None:
                parts = line.split()
                addr = int(parts[2], 16)
                data = bytearray()
                for w in parts[3:]:
                    if w.startswith("?"):
                        data += b"\0\0\0\0"
                    else:
                        data += int(w, 16).to_bytes(4, "little")
                yield (pending[0], pending[1], addr, bytes(data))
                pending = None


def word(buf, off):
    if off + 2 > len(buf):
        return None
    return int.from_bytes(buf[off:off + 2], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--port", help="only writes to this port, e.g. E20")
    ap.add_argument("--raw", action="store_true", help="also print the whole block in hex")
    args = ap.parse_args()

    shown = 0
    total = 0
    for lineno, p, addr, buf in parse(args.trace):
        total += 1
        if args.port and len(p) > 2 and p[2].upper() != args.port.upper():
            continue
        vals = []
        for off, name in FIELDS:
            v = word(buf, off)
            vals.append("%s=%s" % (name, "----" if v is None else "%04X" % v))
        print("radek %-8d %s %s %s  EBX=%08X" % (lineno, p[1], p[2], p[3], addr))
        print("   " + "  ".join(vals))
        if args.raw:
            print("   " + " ".join("%02X" % b for b in buf))
        shown += 1
        if shown >= args.limit:
            break
    print()
    print("trace walked, %d+ port accesses with a window at EBX" % total)


if __name__ == "__main__":
    main()

#!/usr/bin/env python
"""Extracts from a trace which **pitch bend range** the driver really holds.

The port trace is not enough: `bend 640 with range 22` and `bend 641 with
range 22` give a different result, but the `range` and the `moment of the
bend` cannot be told apart from it. So a window on the driver's channel
structures is added to the CPU trace (`awe32_trace.c`,
`AWE32_TRACE_CH_OFF/LEN`).

Channels lie at `edi + ch*0x24`, the fields are (SBAWE.VXD 0x35DD and 0x3D3B):

    +0x44f  byte   pitch bend range in semitones (0 -> 2 is used)
    +0x450  word   channel tuning, added to IP
    +0x452  word   fine tuning (RPN 1)
    +0x454  word   coarse tuning (RPN 2)
    +0x456  dword  computed bend offset = (bend * range) / 24

    python tests/bend_range.py cpu.trace --ch 7
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patch_struct import parse  # noqa: E402

CH_STRIDE = 0x24


def field(buf, base, off, size):
    """The window bytes start at address `base`; returns the field at the absolute `off`."""
    i = off - base
    if i < 0 or i + size > len(buf):
        return None
    v = int.from_bytes(buf[i:i+size], "little")
    if size == 2 and v >= 0x8000:
        v -= 0x10000
    if size == 4 and v >= 0x80000000:
        v -= 0x100000000
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cpu_trace")
    ap.add_argument("--ch", type=int, default=7)
    ap.add_argument("--limit", type=int, default=15)
    args = ap.parse_args()

    # the "chan" window is not stored in parse(), so the trace is walked directly
    base = None
    rows = []
    with open(args.cpu_trace, "r", errors="replace") as f:
        pending = None
        for line in f:
            if line.startswith("P "):
                pending = line.split()
            elif line.startswith("M chan ") and pending is not None:
                parts = line.split()
                addr = int(parts[2], 16)
                data = bytearray()
                for w in parts[3:]:
                    data += (0 if w.startswith("?") else int(w, 16)).to_bytes(4, "little")
                rows.append((pending, addr, bytes(data)))
                pending = None
    print("IP writes with a window on the channels: %d" % len(rows))
    if not rows:
        print("the 'chan' window is not in the trace - did 86Box run with AWE32_TRACE_CH_OFF/LEN?")
        return

    c = args.ch
    seen = {}
    for p, addr, buf in rows:
        rng = field(buf, addr, 0x44f + c * CH_STRIDE, 1)
        tune = field(buf, addr, 0x450 + c * CH_STRIDE, 2)
        off = field(buf, addr, 0x456 + c * CH_STRIDE, 4)
        key = (rng, tune)
        seen.setdefault(key, [0, off])
        seen[key][0] += 1
    print()
    print("ch%d - which combinations the driver held:" % c)
    for (rng, tune), (n, off) in sorted(seen.items(), key=lambda t: -t[1][0]):
        print("   range=%s  tuning=%s  (last offset %s)  x%d" % (rng, tune, off, n))

    print()
    print("first %d IP writes:" % args.limit)
    for p, addr, buf in rows[:args.limit]:
        rng = field(buf, addr, 0x44f + c * CH_STRIDE, 1)
        off = field(buf, addr, 0x456 + c * CH_STRIDE, 4)
        print("   IP=%s  ch%d range=%s offset=%s" % (p[3][-4:], c, rng, off))


if __name__ == "__main__":
    main()

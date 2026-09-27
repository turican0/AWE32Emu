#!/usr/bin/env python
"""Makes a test MIDI of **isolated** notes.

In music notes overlap and ring into each other, so the envelope of one note
cannot be pulled out. This file plays one note after another with silence
between them - so the course of **the whole note** can be compared, not only
its onset.

    python tests/make_probe_mid.py out.mid
    python tests/make_probe_mid.py out.mid --program 0 --notes 36,48,60,72,84
    python tests/make_probe_mid.py out.mid --vel 32,64,100,127

The default set goes through several octaves of one instrument; `--program`
can be repeated to have several instruments in a row in one file.
"""
import argparse
import struct


def vlq(n):
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--program", action="append", type=int, default=[],
                    help="program number; can be repeated")
    ap.add_argument("--notes", default="36,48,60,72,84")
    ap.add_argument("--vel", default="100")
    ap.add_argument("--hold", type=float, default=1.5, help="note length [s]")
    ap.add_argument("--gap", type=float, default=1.5,
                    help="silence after a note [s] - must be enough for the decay")
    ap.add_argument("--ch", type=int, default=0)
    args = ap.parse_args()

    programs = args.program or [0]
    notes = [int(x) for x in args.notes.split(",")]
    vels = [int(x) for x in args.vel.split(",")]

    # 1 tick = 1 ms; tempo 1 000 000 us per quarter at 1000 ticks per quarter.
    tpq = 1000
    ev = bytearray()
    ev += b"\x00\xff\x51\x03" + struct.pack(">I", 1000000)[1:]

    delta = 0
    for prog in programs:
        ev += vlq(delta) + bytes([0xC0 | (args.ch & 15), prog & 0x7F])
        delta = 0
        for v in vels:
            for n in notes:
                ev += vlq(delta) + bytes([0x90 | (args.ch & 15), n, v])
                ev += vlq(int(args.hold * 1000)) + bytes([0x80 | (args.ch & 15), n, 0])
                delta = int(args.gap * 1000)

    ev += vlq(delta) + b"\xff\x2f\x00"

    trk = b"MTrk" + struct.pack(">I", len(ev)) + bytes(ev)
    hdr = b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq)
    with open(args.out, "wb") as f:
        f.write(hdr + trk)

    n = len(programs) * len(vels) * len(notes)
    print("%s: %d notes, %d instruments, %.1f s in total"
          % (args.out, n, len(programs), n * (args.hold + args.gap)))


if __name__ == "__main__":
    main()

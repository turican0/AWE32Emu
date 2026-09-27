#!/usr/bin/env python
"""Compares the driver's **intermediate values** with ours, field by field.

The left side is the driver's voice parameter block, extracted from the 86Box
CPU trace (the memory window at `EBX`, see patch_struct.py). The right side is
our `--dump-notes` of `AWE32Emu.exe`, whose columns are deliberately named
the same.

    AWE32Emu song.mid ... --dump-notes ours.csv
    powershell -File ref86box/run_trace.ps1 -Trace p.trace \
        -CpuTrace cpu.trace -Mode win95 -Seconds 330
    python tests/patch_cmp.py cpu.trace ours.csv

Notes are paired by order, as in notes_diff.py.
"""
import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patch_struct import parse, word  # noqa: E402

# our column -> offset in the driver's block (struct _SFTYPE from SFTYPE.H)
#
# Careful: `0x48`/`0x4A` are **not** attack and hold. They make up the value
# sent to register 0x2A0, which is DCYSUSV (0x280 is ENVVOL, 0x2C0 ENVVAL,
# a step of 0x20 per register) - i.e. sustain in the high byte and decay in
# the low byte. The SDK header confirms it: 0x48 is `decayEnv2` and 0x4A
# `sustainEnv2`.
MAP = [
    ("Q",           0x12),
    ("reverb",      0x20),
    ("panAux",      0x24),
    ("atten",       0x26),
    ("envvalDelay", 0x32),
    ("modAttack",   0x34),
    ("envvolDelay", 0x42),
    ("volAttack",   0x44),
]


def driver_notes(path):
    """The block at EBX for every **real note**.

    Pairing by the order of IFATN writes is not enough - the driver touches it
    elsewhere than at note-on too, and the trace then shifts by one note and
    everything diverges. So a note is recognised as in notes_diff.py: by the
    starting DCYSUSV write, and only when the voice has a non-zero sample
    address (CCCA) and envelope (ATKHLDV).
    """
    rows = parse(path)
    ptr = 0
    pend = {}          # voice -> {"regs": {...}, "buf": the block at EBX}
    out = []
    for _lineno, p, _addr, buf in rows:
        if p[1] != "w":
            continue
        port = int(p[2], 16)
        val = int(p[3], 16) & 0xFFFF
        if (port & 0xF02) == 0xE02:
            ptr = val
            continue
        reg = (ptr >> 5) & 7
        voice = ptr & 0x1F
        sel = port & 0xF02
        st = pend.setdefault(voice, {"regs": {}, "buf": None})
        if sel == 0xE00 and reg == 1:              # IFATN
            st["buf"] = buf
        elif sel == 0xA00 and reg == 0:            # CCCA
            st["regs"]["CCCA"] = val
        elif sel == 0xA02 and reg == 4:            # ATKHLDV
            st["regs"]["ATKHLDV"] = val
        elif sel == 0xA00 and reg == 5:            # DCYSUSV - starts the note
            if not (val & 0x8000) and not (val & 0x0080):
                if (st["regs"].get("CCCA", 0) and st["regs"].get("ATKHLDV", 0)
                        and st["buf"] is not None):
                    out.append(st["buf"])
            pend[voice] = {"regs": {}, "buf": None}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cpu_trace")
    ap.add_argument("ours_csv")
    ap.add_argument("--limit", type=int, default=10, help="how many differences to print")
    args = ap.parse_args()

    drv = driver_notes(args.cpu_trace)
    with open(args.ours_csv, newline="") as f:
        ours = list(csv.DictReader(f))
    n = min(len(drv), len(ours))
    print("driver %d note-ons, ours %d, pairing %d" % (len(drv), len(ours), n))
    if n == 0:
        return

    print()
    print("%-14s %8s %8s   %s" % ("pole", "match", "z", "examples of differences"))
    for name, off in MAP:
        same = 0
        ex = []
        for i in range(n):
            a = word(drv[i], off)
            b = int(ours[i][name], 16)
            if a == b:
                same += 1
            elif len(ex) < 3:
                ex.append("note %d: driver %04X, ours %04X" % (i, a, b))
        flag = "OK" if same == n else "  "
        print("%s %-11s %8d %8d   %s" % (flag, name, same, n, "; ".join(ex)))


if __name__ == "__main__":
    main()

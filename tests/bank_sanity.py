#!/usr/bin/env python
"""Loads every SF1 bank and tries to play something with it.

It does not look for a match with the driver - that is `matrix.py`. This only
verifies that the parser reads the bank without error and that the render
finishes and plays something. It catches crashes and banks that come out
empty.

    python tests/bank_sanity.py
    python tests/bank_sanity.py --midi other.mid
"""
import argparse
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
ROOT = os.path.join(os.path.dirname(DATA), "AWE32Emu")
EXE = os.path.join(ROOT, "bin", "x64", "Release", "AWE32Emu.exe")
ROM = os.path.join(DATA, "rom", "awe32.raw")
GM = os.path.join(DATA, "cdrom", "2", "WIN95", "DRIVERS", "SYNTHGM.SBK")
OUT = os.path.join(DATA, "tests", "out", "banks")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--midi", default=os.path.join(
        DATA, "SAMPLES", "MIDI", "BACH", "MINUET.MID"))
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    banks = sorted(set(
        glob.glob(os.path.join(DATA, "**", "*.SBK"), recursive=True) +
        glob.glob(os.path.join(DATA, "**", "*.sbk"), recursive=True)))
    banks = [b for b in banks if os.sep + "tests" + os.sep not in b]

    print("%-22s %-7s %-7s %-7s %-7s %s" %
          ("bank", "preset", "instr.", "samples", "not", "note"))
    bad = 0
    for b in banks:
        name = os.path.basename(b)
        tr = os.path.join(OUT, name + ".trace")
        r = subprocess.run(
            [EXE, args.midi, "--rom", ROM, "--sf", GM, "--sf", b,
             "--wav", os.devnull, "--trace", tr, "--driver", "win95"],
            capture_output=True, text=True, errors="replace")
        out = r.stdout + r.stderr
        m = re.search(re.escape(name) +
                      r"[^\n]*?(\d+) presets, (\d+) instruments, (\d+) samples",
                      out)
        note = ""
        if r.returncode != 0:
            note = "CRASH (return code %d)" % r.returncode
            bad += 1
        elif not m:
            note = "not loaded"
            bad += 1
        notes = 0
        if os.path.exists(tr):
            with open(tr, errors="replace") as f:
                notes = sum(1 for l in f if l.startswith("2") or l[:1].isdigit())
        print("%-22s %-7s %-7s %-7s %-7d %s" % (
            name,
            m.group(1) if m else "-", m.group(2) if m else "-",
            m.group(3) if m else "-", notes, note))

    print()
    print("banks %d, problematic %d" % (len(banks), bad))
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main()

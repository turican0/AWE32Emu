#!/usr/bin/env python
"""Shows how our chip differs from the unmodified snd_emu8k.c of 86Box.

Our render and the 86Box for the VM compile **the same** chip code, so they
cannot diverge. But it can diverge from upstream - and that is the
interesting number when tuning the EMU8000: what is already our change and
what is still stock 86Box.

The untouched upstream lies in `ref86box/upstream/snd_emu8k.c`; its origin
is checked against GitHub by `verify_upstream.py`.

    python tests/chip_diff.py            # summary
    python tests/chip_diff.py --full     # the whole diff
"""
import argparse
import difflib
import io
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
STOCK = os.path.join(DATA, "ref86box", "upstream", "snd_emu8k.c")
OURS = os.path.join(DATA, "docs", "86box-src", "master-full", "src", "sound",
                    "snd_emu8k.c")

# The known deviations fall into two groups:
#
# KNOWN - no effect on the sound:
#   1) Tracing hooks - they only write, they do not touch the chip state.
#   2) The ID register reads 0x0C instead of 0x1C. It is **read only**, used
#      for card detection (without it AWEUTIL.COM reports ERR012) and does
#      not enter the sound computation.
#
# TUNING - chip changes that ARE deliberate and measured against recordings
# from real hardware (see docs/re-notes/emu8000_tuning.md). They DO affect
# the sound, but they are no accidents - so they are reported separately, not
# as "NEW". (This script predates most of the tuning; its patterns cover only
# the first changes.)
#
# Everything else is printed as NEW - that is the useful thing when tuning:
# it shows at once what was just changed and what was there before.
ZNAME = re.compile(
    r"emu8k_trace_|awe32_trace_|snd_emu8k_trace\.h|awe32_trace\.h"
    r"|emu8k_inw|AWE32Emu:|0x0c \||0x1c \||AWEUTIL|mov ax|so the register"
    # A bare return type on its own line - belongs to the renamed function
    # and its wrapper, nothing more.
    r"|^[-+]\s*[{}]?\s*$|^[+-]uint16_t\s*$"
    r"|^[+-]\s*(const uint16_t val|return val;|va_|})")

# Captured broadly line by line - the diff chops our contiguous change
# into several pieces (comment, function body, #if switch), and that is fine;
# it only has to cover more key fragments so the block does not get split
# between "tuned" and "new".
TUNING = re.compile(
    r"RESAMPLER_POINT3|EMU8K_READ_INTERP_POINT3|POINT3"
    r"|3 [Pp]oint sample interpolation|3bodov|interpolace pres tri"
    r"|Catmull-Rom|kubick|prumerne skore|interpolator offset"
    r"|const float  g |int32_t a0 |int32_t a1 |int32_t a2 "
    r"|float  l0 |float  l1 |float  l2 |return \(int32_t\)"
    r"|CUBIC \(Catmull|zustava dostupny prepnutim")


def lines(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s.replace(chr(13) + chr(10), chr(10)).splitlines(keepends=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true", help="print the whole diff")
    args = ap.parse_args()

    a = lines(STOCK)
    b = lines(OURS)
    diff = list(difflib.unified_diff(a, b, "stock", "nas", n=2))

    zmeny = [d for d in diff if d[:1] in "+-" and d[:3] not in ("+++", "---")]
    laden = [d for d in zmeny if TUNING.search(d)]
    zname = [d for d in zmeny if d not in laden and ZNAME.search(d)]
    ostatni = [d for d in zmeny if d not in laden and not ZNAME.search(d)]

    print("stock : %s (%d lines)" % (os.path.relpath(STOCK, DATA), len(a)))
    print("ours  : %s (%d lines)" % (os.path.relpath(OURS, DATA), len(b)))
    print()
    print("changed lines %d - known %d, tuned %d, new %d"
          % (len(zmeny), len(zname), len(laden), len(ostatni)))

    if args.full:
        print()
        print("".join(diff), end="")
        return

    if laden:
        print()
        print("Deliberate tuning (measured against recordings, see emu8000_tuning.md):")
        for d in laden:
            print("   " + d.rstrip())

    if ostatni:
        print()
        print("NEW deviations - these may sound different from stock 86Box:")
        for d in ostatni:
            print("   " + d.rstrip())
    else:
        print()
        print("No unplanned deviation - apart from the deliberate tuning the chip computes")
        print("exactly like stock 86Box. (The known deviations are the tracing and")
        print("the ID register; they do not affect the sound.)")


if __name__ == "__main__":
    main()

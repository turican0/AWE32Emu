#!/usr/bin/env python
"""Splits a driver trace into sections by the gaps between notes.

When several pieces play in a row in one VM run, the result is one trace.
This finds the boundaries and prints frame windows that can go straight into
`notes_diff.py --dframes` or into `matrix.py`.

    python tests/trace_split.py out/dosmid_multi.trace
    python tests/trace_split.py some.trace --gap 5
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from notes_diff import real_notes                    # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--gap", type=float, default=4.0,
                    help="gap in seconds that separates pieces (default 4)")
    args = ap.parse_args()

    notes = real_notes(args.trace)
    if not notes:
        print("no notes")
        return
    print("%d not, cas %.1f..%.1f s"
          % (len(notes), notes[0][0] / 44100.0, notes[-1][0] / 44100.0))

    gap = args.gap * 44100
    segs = []
    start = 0
    for i in range(1, len(notes)):
        if notes[i][0] - notes[i - 1][0] > gap:
            segs.append((start, i - 1))
            start = i
    segs.append((start, len(notes) - 1))

    print()
    print("%-4s %-9s %-9s %-8s %-9s %s"
          % ("section", "from note", "to note", "not", "cas [s]", "--dframes"))
    for k, (a, b) in enumerate(segs):
        f0, f1 = notes[a][0], notes[b][0]
        # the window is widened by half a gap on both sides, so nothing gets cut off
        lo = max(0, f0 - 44100)
        hi = f1 + 44100
        print("%-4d %-9d %-9d %-8d %-9s %d:%d"
              % (k, a, b, b - a + 1,
                 "%.1f-%.1f" % (f0 / 44100.0, f1 / 44100.0), lo, hi))


if __name__ == "__main__":
    main()

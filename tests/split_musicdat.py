#!/usr/bin/env python
"""Splits `MUSIC.DAT` (AIL/Miles) into single XMI pieces.

Bullfrog games keep their music in one file: it is simply several XMI in a
row, each starting with `FORM....XDIR`. This script finds the boundaries and
writes files our render can play.

    python tests/split_musicdat.py MUSIC.DAT -o midi/hioctane
"""
import argparse
import os
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--prefix", default="")
    args = ap.parse_args()

    d = open(args.src, "rb").read()
    # The start of a piece is recognised by `FORM` + length + `XDIR`; `FORM`
    # alone also occurs inside (the XMID wrapper), so `XDIR` is what marks the
    # start of a new piece.
    starts = [m.start() for m in re.finditer(rb"FORM....XDIR", d, re.S)]
    if not starts:
        raise SystemExit("no XMI inside - is it really MUSIC.DAT?")

    os.makedirs(args.out, exist_ok=True)
    bounds = starts + [len(d)]
    print("found %d pieces" % len(starts))
    for i in range(len(starts)):
        blob = d[bounds[i]:bounds[i + 1]]
        name = "%s%02d.xmi" % (args.prefix, i)
        p = os.path.join(args.out, name)
        open(p, "wb").write(blob)
        print("  %-14s %8d B" % (name, len(blob)))


if __name__ == "__main__":
    main()

#!/usr/bin/env python
"""Pairs the Hi-Octane recordings with the XMI pieces extracted from `MUSIC.DAT`.

The recordings `HO_TR1_2` / `HO_TR3_4` / `HO_TR5_6` were unproven - there was
no MIDI for them. Now they can be extracted from the game's ISO
(`iso_list.py` + `split_musicdat.py`), so the pairs can be found.

By the names each recording holds **a pair** of pieces in a row, so every XMI
is tried against every recording and the anchor match decides.

    python tests/ho_pairs.py
    python tests/ho_pairs.py --only ho_03
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
HOBANK = os.path.join(DATA, "cdrom", "hioctane", "files", "SOUND", "BULLFROG.SBK")
XMI = os.path.join(DATA, "midi", "hioctane")
REC = os.path.join(DATA, "SAMPLES3", "new")
OUT = os.path.join(DATA, "tests", "out", "ho")


def render(mid, wav):
    if os.path.exists(wav):
        return True
    r = subprocess.run(
        [EXE, mid, "--rom", ROM, "--sf", GM, "--sf", HOBANK,
         "--driver", "dos", "--wav", wav],
        capture_output=True, text=True, errors="replace")
    return r.returncode == 0 and os.path.exists(wav)


def align(ours, ref):
    best = (0.0, 0, "-")
    for rys in ("onset", "chroma"):
        r = subprocess.run(
            [sys.executable, os.path.join(HERE, "align2.py"), ours, ref,
             "--feature", rys],
            capture_output=True, text=True, errors="replace")
        n = re.search(r"after the monotonicity check (\d+)", r.stdout)
        sc = re.search(r"mean anchor match ([\d.]+)", r.stdout)
        if sc and float(sc.group(1)) > best[0]:
            best = (float(sc.group(1)), int(n.group(1)) if n else 0, rys)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    mids = sorted(glob.glob(os.path.join(XMI, "*.xmi")))
    if args.only:
        mids = [m for m in mids if args.only in os.path.basename(m)]
    recs = sorted(glob.glob(os.path.join(REC, "HO_*.mp3")))
    print("pieces %d, recordings %d" % (len(mids), len(recs)), flush=True)
    print("%-10s %-14s %6s %7s %-7s" %
          ("piece", "recording", "anchors", "match", "feature"), flush=True)

    nalezeno = 0
    for mid in mids:
        name = os.path.basename(mid)[:-4]
        wav = os.path.join(OUT, name + ".wav")
        if not render(mid, wav):
            print("%-10s render failed" % name, flush=True)
            continue
        for rec in recs:
            sc, n, rys = align(wav, rec)
            if sc >= 0.40:
                nalezeno += 1
                print("%-10s %-14s %6d %7.3f %-7s  <== match"
                      % (name, os.path.basename(rec)[:-4], n, sc, rys),
                      flush=True)

    print()
    print("pairs found: %d" % nalezeno)
    print("An anchor match below ~0.4 means it is not the same piece.")


if __name__ == "__main__":
    main()

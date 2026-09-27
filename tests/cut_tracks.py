#!/usr/bin/env python
"""Cuts a long recording into pieces by a list of times.

The times come from a file with `m:ss name ...` on every line (lines
starting with `#` are ignored). The end of a piece is the start of the next.

    python tests/cut_tracks.py \\
        "samples3/recording.wav" \\
        samples3/tracklist.txt \\
        --out samples3/tracks

The cut is refined: the start moves to the first sound after the given time
and the end is trimmed of the final silence, so no piece of the neighbour
gets in. Times from a video description are usually rounded to seconds, so
without it every piece would start cut off or with the tail of the previous
one.
"""
import argparse
import os
import re
import sys

import numpy as np

try:
    import soundfile as sf
except ImportError:
    sys.exit("the soundfile module is missing (pip install soundfile)")

RX = re.compile(r"^\s*(\d+):(\d\d)\s+(\S+)")


def parse_list(path):
    out = []
    for line in open(path, encoding="utf-8"):
        if line.lstrip().startswith("#"):
            continue
        m = RX.match(line)
        if m:
            out.append((int(m.group(1)) * 60 + int(m.group(2)), m.group(3)))
    return out


def trim(x, sr, thresh_db=-50.0, win=0.05):
    """Returns (from, to) in samples without the leading and trailing silence."""
    n = int(sr * win)
    frames = len(x) // n
    if frames < 2:
        return 0, len(x)
    e = np.sqrt(np.mean(np.square(x[:frames * n].reshape(frames, n)), axis=1))
    loud = 20 * np.log10(e + 1e-12) > thresh_db
    if not loud.any():
        return 0, len(x)
    a = int(np.argmax(loud))
    b = frames - int(np.argmax(loud[::-1]))
    return a * n, min(len(x), b * n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio")
    ap.add_argument("tracklist")
    ap.add_argument("--out", required=True)
    ap.add_argument("--pad", type=float, default=2.0,
                    help="how many seconds before the given time to still look for the start")
    ap.add_argument("--no-trim", action="store_true")
    args = ap.parse_args()

    marks = parse_list(args.tracklist)
    if len(marks) < 2:
        sys.exit("the list has fewer than two times")

    x, sr = sf.read(args.audio, always_2d=True, dtype="float32")
    os.makedirs(args.out, exist_ok=True)
    print(f"{args.audio}: {len(x)/sr:.1f} s, {len(marks)-1} sections\n")

    for i in range(len(marks) - 1):
        t0, name = marks[i]
        t1 = marks[i + 1][0]
        if name in ("-", "KONEC", "BOOTUP"):
            continue
        a = max(0, int((t0 - args.pad) * sr))
        b = min(len(x), int(t1 * sr))
        seg = x[a:b]
        if not args.no_trim:
            s, e = trim(seg.mean(axis=1), sr)
            seg = seg[s:e]
        base = re.sub(r"[^A-Za-z0-9_.-]", "_", name).replace(".MID", "")
        out = os.path.join(args.out, f"{i:02d}_{base}.wav")
        sf.write(out, seg, sr)
        print(f"  {t0//60:02d}:{t0%60:02d}  {len(seg)/sr:7.1f} s  {os.path.basename(out)}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python
"""Evaluates a sound capture straight from 86Box, where the game runs.

What it is for: the recording `ogg/004_C2INTRO.ogg` does not align with our
render of the intro at all, and without a reference one cannot tell whether
the error is ours or the recording comes from elsewhere. A capture from 86Box
decides it:

  - **the capture matches our render but not the recording** -> the recording
    is not from an AWE32 (or it is another arrangement) and we have no
    reference for the intro yet;
  - **the capture matches the recording but not our render** -> the error is
    ours, and it shows in which bands;
  - **neither matches** -> a bad capture, repeat.

The capture takes `emu8k_trace_wav` from the end of `emu8k_update()`, i.e.
the chip output **before the card mixer** - directly comparable with what
our render produces.

    python tests/cap_cmp.py capture.wav --ours ours.wav --ref ref.ogg
"""
import argparse
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 44100
EDGES = [0, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 22050]


def load(p):
    x, sr = sf.read(p, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    return x


def music_span(x, thresh_db=-70.0, block=1.0):
    """Finds the section where something plays - the capture starts with a long silence after boot."""
    n = int(block * SR)
    lvl = np.array([20 * np.log10(max(np.sqrt((x[i:i + n] ** 2).mean()), 1e-12))
                    for i in range(0, len(x) - n, n)])
    if len(lvl) == 0:
        return 0, len(x)
    hlas = lvl > (lvl.max() + thresh_db)
    idx = np.where(hlas)[0]
    if len(idx) == 0:
        return 0, len(x)
    return idx[0] * n, min(len(x), (idx[-1] + 1) * n)


def bands(x, lab):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    e = np.array([S[(f >= EDGES[i]) & (f < EDGES[i + 1])].sum()
                  for i in range(len(EDGES) - 1)])
    d = np.array([10 * np.log10(v) if v > 0 else -99.0 for v in e])
    d -= d.max()
    print("%-16s" % lab, " ".join("%5.1f" % v for v in d))
    return d


def align(a, b, feature="onset"):
    r = subprocess.run(
        [sys.executable, os.path.join(HERE, "align2.py"), a, b,
         "--feature", feature],
        capture_output=True, text=True, errors="replace")
    m = re.search(r"mean anchor match ([\d.]+)", r.stdout)
    n = re.search(r"after the monotonicity check (\d+)", r.stdout)
    return (float(m.group(1)) if m else 0.0, int(n.group(1)) if n else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cap", help="capture from 86Box")
    ap.add_argument("--ours", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--out", help="where to save the trimmed capture")
    args = ap.parse_args()

    x = load(args.cap)
    a, b = music_span(x)
    print("capture %.1f s, music from %.1f s to %.1f s (%.1f s)"
          % (len(x) / SR, a / SR, b / SR, (b - a) / SR))
    if b - a < 5 * SR:
        raise SystemExit("almost nothing plays in the capture - did the game start?")

    out = args.out or os.path.join(os.path.dirname(args.cap), "cap_trim.wav")
    sf.write(out, x[a:b], SR)
    print("trimmed to %s" % out)
    print()

    print("bands:           ", "  ".join("%5s" % s for s in
                                         ["<100", "200", "400", "800", "1k6",
                                          "3k2", "6k4", "12k", "22k"]))
    d_cap = bands(x[a:b], "capture 86box")
    d_our = bands(load(args.ours), "nas render")
    d_ref = bands(load(args.ref), "recording")
    print()
    print("%-16s %5.1f dB" % ("capture vs ours:", np.abs(d_cap - d_our).mean()))
    print("%-16s %5.1f dB" % ("capture vs rec.:", np.abs(d_cap - d_ref).mean()))
    print("   (mean band deviation; smaller = more similar)")
    print()

    for lab, other in (("nas render", args.ours), ("recording", args.ref)):
        best = max(align(out, other, f) for f in ("onset", "chroma"))
        print("alignment of the capture with %-12s match %.3f (anchors %d)"
              % (lab, best[0], best[1]))

    print()
    print("An anchor match below ~0.4 means it is not the same arrangement.")


if __name__ == "__main__":
    main()

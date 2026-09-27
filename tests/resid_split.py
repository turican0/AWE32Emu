#!/usr/bin/env python
"""Splits the residual deviation into the **loudness** of a note and its **colour**.

The `note_probe.py` score is one number and does not tell apart two quite
different defects:

  * a note plays with the right colour but somewhat louder or quieter
    (an envelope, attenuation or pan error) - in the deviation that is a
    **flat offset**, the same in all bands,
  * a note has the right loudness but another colour (a filter,
    interpolation or sample error) - that is what remains after subtracting
    the flat offset.

Without this distinction a completely wrong thing can get tuned. So for every
pair the script prints both components separately; they add in quadrature to
the total score.

    python tests/resid_split.py --base out/tune/dance-bp --ref "...mp3"
    python tests/resid_split.py --base out/tune/dance-bp --ref "...mp3" \\
        --ours "...hw.mp3" --warp-ours out/tune/dance-hw_warp.json
"""
import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from note_probe import SR, band_db, band_energy, load, warp_fn, warp_range  # noqa: E402
from notes_diff import real_notes  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--ours")
    ap.add_argument("--warp-ours", dest="warp_ours")
    ap.add_argument("--win", type=float, default=0.12)
    ap.add_argument("--gap", type=float, default=0.05)
    # Window shift after the note onset. When the loudness error **grows** with
    # time, the envelope rate is wrong; when it stays the same, it is a static
    # gain.
    ap.add_argument("--delay", type=float, default=0.0)
    a = ap.parse_args()

    ank = json.load(open(a.base + "_warp.json", encoding="utf-8"))["anchors"]
    warp = warp_fn(ank)
    t_lo, t_hi = warp_range(ank)
    warp_o = None
    if a.warp_ours:
        _o = json.load(open(a.warp_ours, encoding="utf-8"))["anchors"]
        warp_o = warp_fn(_o)

    notes = real_notes(a.base + ".trace")
    rows = list(csv.DictReader(open(a.base + ".csv")))
    n = min(len(notes), len(rows))

    ours = load(a.ours or (a.base + ".wav"))
    ref = load(a.ref)

    t = np.array([notes[i][0] for i in range(n)], dtype=float) / SR
    gap = np.diff(np.concatenate([[-1e9], t]))
    w = int(a.win * SR)

    D, W = [], []
    for i in range(n):
        if gap[i] < a.gap or not (t_lo <= t[i] <= t_hi):
            continue
        td = t[i] + a.delay
        t_o = td if warp_o is None else float(warp_o(np.array([td]))[0])
        a0 = int(t_o * SR)
        b0 = int(float(warp(np.array([td]))[0]) * SR)
        if a0 < 0 or b0 < 0 or a0 + w > len(ours) or b0 + w > len(ref):
            continue
        sa, sb = ours[a0:a0 + w], ref[b0:b0 + w]
        if np.sqrt((sa ** 2).mean()) < 1e-5 or np.sqrt((sb ** 2).mean()) < 1e-5:
            continue
        D.append(band_db(sb) - band_db(sa))
        eb = band_energy(sb)
        W.append(eb / max(eb.sum(), 1e-300))

    D = np.array(D)
    W = np.array(W)
    R = D - np.median(D, axis=0)

    # The flat offset of every note = a weighted mean over the bands.
    posun = (W * R).sum(axis=1) / np.maximum(W.sum(axis=1), 1e-300)
    barva = R - posun[:, None]

    def vaz(x):
        return float(np.sqrt((W * x ** 2).sum() / max(W.sum(), 1e-300)))

    celkem = vaz(R)
    hl = float(np.sqrt((W.sum(axis=1) * posun ** 2).sum()
                       / max(W.sum(), 1e-300)))
    ba = vaz(barva)
    print("not %d" % len(R))
    print("  total score          %.4f dB" % celkem)
    print("  of which loudness    %.4f dB   (flat offset of the whole note)" % hl)
    print("  of which colour      %.4f dB   (what remains after subtracting it)" % ba)
    print("  sum check            %.4f dB" % np.sqrt(hl ** 2 + ba ** 2))
    print()
    print("  median offset        %+.2f dB, spread %.2f dB"
          % (float(np.median(posun)), float(np.std(posun))))


main()

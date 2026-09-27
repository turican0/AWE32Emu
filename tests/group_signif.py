#!/usr/bin/env python
"""Tells which groups of `note_probe.py` really deviate - and which are just small.

A trap that is easy to fall into: a group's deviation is computed as the
median over its notes, and a median of **few** notes is noisy by itself. A
group of 19 notes therefore comes out "worse" than a group of 180 even when
the error on them is exactly the same. That is exactly what the table looked
like from which the filter was blamed - while it was only that there are few
notes with resonance.

So for each group this script computes **what would come out by pure
chance**: it draws a group of the same size from the same notes, many times.
The ratio of the real deviation to the random one (column `ratio`) is the
number that says something. Around 1.0 = indistinguishable from chance.

    python tests/group_signif.py --base out/tune/dance-bp \\
        --ref "...mp3" --by sample
"""
import argparse
import csv
import json
import os
import sys
from collections import defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from note_probe import (EDGES, SR, band_db, band_energy, load,  # noqa: E402
                        warp_fn, warp_range)
from notes_diff import real_notes  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--by", default="sample",
                    choices=["sample", "ch", "note", "q", "cutoff", "vel"])
    ap.add_argument("--win", type=float, default=0.12)
    ap.add_argument("--gap", type=float, default=0.05)
    ap.add_argument("--min-notes", type=int, default=8)
    ap.add_argument("--draws", type=int, default=400)
    # When the --base side is a recording too, its time is converted as well.
    # That gives the same ratio between two recordings of the same card - a
    # control experiment whether a ratio around 2 is not simply a property of
    # the metric.
    ap.add_argument("--ours", help="a sound other than <base>.wav")
    ap.add_argument("--warp-ours", dest="warp_ours")
    a = ap.parse_args()

    ank = json.load(open(a.base + "_warp.json", encoding="utf-8"))["anchors"]
    warp = warp_fn(ank)
    t_lo, t_hi = warp_range(ank)

    notes = real_notes(a.base + ".trace")
    rows = list(csv.DictReader(open(a.base + ".csv")))
    n = min(len(notes), len(rows))

    ours = load(a.ours or (a.base + ".wav"))
    warp_o = None
    if a.warp_ours:
        _o = json.load(open(a.warp_ours, encoding="utf-8"))["anchors"]
        warp_o = warp_fn(_o)
    ref = load(a.ref)

    t = np.array([notes[i][0] for i in range(n)], dtype=float) / SR
    gap = np.diff(np.concatenate([[-1e9], t]))
    w = int(a.win * SR)

    D = []
    W = []
    idx = []
    for i in range(n):
        if gap[i] < a.gap or not (t_lo <= t[i] <= t_hi):
            continue
        t_o = t[i] if warp_o is None else float(warp_o(np.array([t[i]]))[0])
        a0 = int(t_o * SR)
        b0 = int(float(warp(np.array([t[i]]))[0]) * SR)
        if a0 < 0 or b0 < 0 or a0 + w > len(ours) or b0 + w > len(ref):
            continue
        sa = ours[a0:a0 + w]
        sb = ref[b0:b0 + w]
        if np.sqrt((sa ** 2).mean()) < 1e-5 or np.sqrt((sb ** 2).mean()) < 1e-5:
            continue
        D.append(band_db(sb) - band_db(sa))
        eb = band_energy(sb)
        W.append(eb / max(eb.sum(), 1e-300))
        idx.append(i)

    D = np.array(D)
    W = np.array(W)
    R = D - np.median(D, axis=0)

    def klic(r):
        if a.by == "sample":
            return "sample %s" % r["ccca"]
        if a.by == "ch":
            return "channel %s" % r["ch"]
        if a.by == "note":
            return "note %s" % r["note"]
        if a.by == "q":
            return "Q %d" % int(r["Q"], 16)
        if a.by == "cutoff":
            c = int(r["cutoff"], 16)
            return "cutoff %02X-%02X" % (c & ~0x1F, (c & ~0x1F) + 31)
        return "velocity %d" % (int(r["vel"]) // 16 * 16)

    skupiny = defaultdict(list)
    for poz, i in enumerate(idx):
        skupiny[klic(rows[i])].append(poz)

    def sila(pozice):
        m = np.median(R[pozice], axis=0)
        wv = W[pozice].mean(axis=0)
        wv = wv / max(wv.sum(), 1e-300)
        return float(np.sqrt((wv * m ** 2).sum()))

    rng = np.random.default_rng(1)
    vsechny = np.arange(len(R))
    out = []
    for k, poz in skupiny.items():
        if len(poz) < a.min_notes:
            continue
        s = sila(poz)
        nahodne = [sila(rng.choice(vsechny, size=len(poz), replace=False))
                   for _ in range(a.draws)]
        oc = float(np.median(nahodne))
        out.append((s / max(oc, 1e-9), s, oc, k, len(poz)))

    out.sort(reverse=True)
    print("%-18s %5s %9s %9s %7s"
          % ("group", "not", "deviation", "chance", "ratio"))
    for pomer, s, oc, k, npoz in out:
        print("%-18s %5d %7.2f dB %7.2f dB %7.2f" % (k, npoz, s, oc, pomer))
    print()
    print("A ratio around 1.0 means the group is not distinguishable from a random"
          " selection of the same size.")


main()

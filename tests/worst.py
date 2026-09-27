#!/usr/bin/env python
"""Finds sounds that sound **completely different** from the real card.

`tune.py` gives one number for the whole piece, which is good for deciding
between variants, but the essential gets lost in it: one specific instrument
can be 15 dB off while the rest matches. This script goes after those
exceptions.

It groups by **sample** (the address in CCCA) and channel - i.e. by what the
chip actually plays. The result is summed over all pairs: when the same
sample is off in three pieces, it is a property of the sample, not an
accident of the mix.

    python tests/worst.py
    python tests/worst.py --only georgia --top 30
    python tests/worst.py --delay 0.25    # decay

The "deviation" column is what remains after subtracting the distortion of the
whole, so the equalisation of the recording path does not enter it.
"""
import argparse
import csv
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from note_probe import EDGES, SR, band_db, band_energy, load, warp_fn  # noqa: E402
from notes_diff import real_notes                          # noqa: E402
import tune                                                # noqa: E402


def one(p, win, gap, delay):
    """Returns {(sample, channel, program): [note deviations]} for one pair."""
    base = os.path.join(tune.OUT, p["name"])
    if not os.path.exists(base + "_warp.json"):
        return None
    warp = warp_fn(json.load(open(base + "_warp.json"), encoding=None)["anchors"]
                   if False else
                   json.load(open(base + "_warp.json", encoding="utf-8"))["anchors"])
    ours = load(base + ".wav")
    ref = load(p["ref"])
    notes = real_notes(base + ".trace")
    rows = list(csv.DictReader(open(base + ".csv", encoding="utf-8")))
    n = min(len(rows), len(notes))
    if n == 0:
        return None

    t = np.array([notes[i][0] for i in range(n)], dtype=float) / SR
    prev = np.diff(np.concatenate([[-1e9], t]))
    w = int(win * SR)

    keys = []
    diffs = []
    weights = []
    for i in range(n):
        if prev[i] < gap:
            continue
        a0 = int((t[i] + delay) * SR)
        b0 = int(float(warp(np.array([t[i] + delay]))[0]) * SR)
        if a0 < 0 or b0 < 0 or a0 + w > len(ours) or b0 + w > len(ref):
            continue
        sa = ours[a0:a0 + w]
        sb = ref[b0:b0 + w]
        if np.sqrt((sa ** 2).mean()) < 1e-5 or np.sqrt((sb ** 2).mean()) < 1e-5:
            continue
        keys.append((rows[i]["ccca"], rows[i]["ch"]))
        diffs.append(band_db(sb) - band_db(sa))
        eb = band_energy(sb)
        weights.append(eb / max(eb.sum(), 1e-300))

    if not diffs:
        return None
    D = np.array(diffs)
    R = D - np.median(D, axis=0)      # away with the distortion of the whole
    out = {}
    outw = {}
    for k, r, ww in zip(keys, R, weights):
        out.setdefault(k, []).append(r)
        outw.setdefault(k, []).append(ww)
    return out, outw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--win", type=float, default=0.12)
    ap.add_argument("--gap", type=float, default=0.05)
    ap.add_argument("--delay", type=float, default=0.0)
    ap.add_argument("--min-notes", type=int, default=6)
    args = ap.parse_args()

    pairs = [p for p in tune.PAIRS
             if not args.only or args.only in p["name"]]

    # key -> [(pair name, note count, median deviation)]
    vse = {}
    for p in pairs:
        res = one(p, args.win, args.gap, args.delay)
        if res is None:
            print("%-11s skipped (alignment missing)" % p["name"], flush=True)
            continue
        r, rw = res
        big = 0
        for k, v in r.items():
            if len(v) < args.min_notes:
                continue
            m = np.median(np.array(v), axis=0)
            # Weighted by band energy - otherwise an almost silent top band wins, where
            # a few dB of absolute difference explode in relative decibels
            # (found on canon: "20 dB" at 12191 Hz had 0.1 % of the note energy
            # in that band - the weighted difference was 1.2 dB).
            imp = np.mean(rw[k], axis=0)
            imp = imp / max(imp.sum(), 1e-300)
            sila = float(np.sqrt((imp * m ** 2).sum() / max(imp.sum(), 1e-300)))
            vse.setdefault(k, []).append((p["name"], len(v), sila, m, imp))
            if sila > 4:
                big += 1
        print("%-11s groups %-4d of which above 4 dB: %d"
              % (p["name"], len(r), big), flush=True)

    print()
    print("LARGEST DEVIATIONS (sample+channel, summed over pairs)")
    print("%-9s %-4s %6s %6s %8s   %s"
          % ("sample", "ch", "not", "kde", "deviation", "in what"))

    rank = []
    for (ccca, ch), lst in vse.items():
        notes = sum(x[1] for x in lst)
        sila = float(np.median([x[2] for x in lst]))
        m = np.median(np.array([x[3] for x in lst]), axis=0)
        imp = np.median(np.array([x[4] for x in lst]), axis=0)
        j = int(np.argmax(imp * np.abs(m)))
        rank.append((sila, ccca, ch, notes, len(lst), EDGES[j], m[j],
                     ",".join(sorted(set(x[0] for x in lst))[:3])))
    rank.sort(reverse=True)
    for sila, ccca, ch, notes, kolik, fq, val, kde in rank[:args.top]:
        print("%-9s %-4s %6d %6d %7.1f dB   %5.0f Hz %+.1f dB  %s"
              % (ccca, ch, notes, kolik, sila, fq, val, kde))

    if rank:
        med = float(np.median([r[0] for r in rank]))
        print()
        print("median deviation %.1f dB - above %.1f dB it is no longer a nuance"
              % (med, med * 3))


if __name__ == "__main__":
    main()

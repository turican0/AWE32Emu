#!/usr/bin/env python
"""Compares **the course of single notes** between two renders.

Unlike `note_probe.py`, which measures the colour in a window after the
onset, this one follows the whole note in time: how fast it rises, how it
falls, when it goes silent. It needs a file of isolated notes
(`make_probe_mid.py`) - in music notes overlap and the envelope of one
cannot be pulled out of the mix.

    python tests/env_cmp.py a.wav b.wav --notes probe.csv
    python tests/env_cmp.py a.wav b.wav --notes probe.csv --plot 3

Prints per note: the difference of the peak, rise time, decay time by 20 dB
and total energy. Negative = the second render is quieter/slower.
"""
import argparse
import csv

import numpy as np
import soundfile as sf

SR = 44100


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    return x


def envelope(x, hop=64):
    """Envelope in dB, step 1.45 ms."""
    n = len(x) // hop
    e = np.sqrt((x[:n * hop].reshape(n, hop) ** 2).mean(axis=1))
    return 20 * np.log10(np.maximum(e, 1e-9)), hop / SR


def segments(x, thresh_db=-60, min_gap=0.3):
    """Finds sections where something sounds - note boundaries."""
    e, dt = envelope(x)
    on = e > (e.max() + thresh_db)
    out = []
    i = 0
    gap = int(min_gap / dt)
    while i < len(on):
        if on[i]:
            j = i
            k = i
            while j < len(on):
                if on[j]:
                    k = j
                elif j - k > gap:
                    break
                j += 1
            out.append((i * dt, (k + 1) * dt))
            i = j
        else:
            i += 1
    return out


def describe(x, t0, t1):
    """Peak, rise time to the peak, drop by 20 dB, energy."""
    a = int(t0 * SR)
    b = min(len(x), int(t1 * SR))
    seg = x[a:b]
    if len(seg) < SR // 50:
        return None
    e, dt = envelope(seg)
    peak = e.max()
    ip = int(np.argmax(e))
    attack = ip * dt
    # Drop by 20 dB below the peak, searched only after the peak.
    after = e[ip:]
    below = np.where(after <= peak - 20)[0]
    decay = below[0] * dt if len(below) else float("nan")
    energy = 10 * np.log10(max((seg ** 2).sum(), 1e-12))
    return peak, attack, decay, energy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--notes", help="csv from --dump-notes, only for labels")
    ap.add_argument("--trace", help="trace - exact onset times (more reliable"
                                    " than searching in the envelope)")
    ap.add_argument("--thresh", type=float, default=-60.0)
    args = ap.parse_args()

    A = load(args.a)
    B = load(args.b)

    if args.trace:
        # Note boundaries from the trace. Searching them in the envelope did not
        # work: decay and reverb fill the gap between notes and two notes merge
        # into one.
        import os
        import sys
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from notes_diff import real_notes
        t = [n[0] / SR for n in real_notes(args.trace)]
        segs = [(t[i], t[i + 1] if i + 1 < len(t) else len(A) / SR)
                for i in range(len(t))]
    else:
        segs = segments(A, args.thresh)
    rows = []
    if args.notes:
        rows = list(csv.DictReader(open(args.notes, encoding="utf-8")))

    print("found %d notes" % len(segs))
    print("%-6s %-9s %8s %8s %9s %9s"
          % ("#", "note", "peak", "rise", "drop20", "energy"))
    print("%-6s %-9s %8s %8s %9s %9s"
          % ("", "", "dB", "ms", "ms", "dB"))
    da = []
    for i, (t0, t1) in enumerate(segs):
        ra = describe(A, t0, t1)
        rb = describe(B, t0, t1)
        if not ra or not rb:
            continue
        popis = ""
        if i < len(rows):
            popis = "%s/%s" % (rows[i].get("note", "?"), rows[i].get("vel", "?"))
        d = (rb[0] - ra[0], (rb[1] - ra[1]) * 1000, (rb[2] - ra[2]) * 1000,
             rb[3] - ra[3])
        da.append(d)
        print("%-6d %-9s %+8.2f %+8.1f %+9.1f %+9.2f"
              % (i, popis, d[0], d[1], d[2], d[3]))

    if da:
        m = np.array(da)
        print()
        print("median difference:  peak %+.2f dB   rise %+.1f ms"
              "   drop20 %+.1f ms   energy %+.2f dB"
              % (np.median(m[:, 0]), np.median(m[:, 1]),
                 np.nanmedian(m[:, 2]), np.median(m[:, 3])))
        print("spread (standard deviation): peak %.2f dB, energy %.2f dB"
              % (m[:, 0].std(), m[:, 3].std()))


if __name__ == "__main__":
    main()

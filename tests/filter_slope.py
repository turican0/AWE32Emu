#!/usr/bin/env python
"""Measures **the filter slope** directly from a recording of real hardware.

The Programmer's Guide does not say how many dB per octave the EMU8000 filter
takes away, and neither does the awe32faq. It can be measured: take notes
with a low cutoff in the register and look at how the spectrum falls above it
in the recording.

So that it does not measure the spectrum of the **sample** instead of the
filter, two groups of notes of the same sample are always compared - one with
the filter open, the other closed. The difference of their spectra is only
the filter's transfer function, whatever the colour of the sample.

    python tests/filter_slope.py --base out/tune/dance-bp \\
        --ref "../SAMPLES4/....mp3"

(Found not to work on music: the two groups also differ in pitch and
velocity. AWETEST blocks 6/7 measure it properly.)
"""
import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from note_probe import SR, load_mono, warp_fn, warp_range  # noqa: E402
from notes_diff import real_notes  # noqa: E402

NFFT = 8192


def spektrum(x):
    if len(x) < NFFT:
        x = np.pad(x, (0, NFFT - len(x)))
    return np.abs(np.fft.rfft(x[:NFFT] * np.hanning(NFFT))) ** 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True, help="tests/out/tune/<name>")
    ap.add_argument("--ref", required=True)
    ap.add_argument("--win", type=float, default=0.12)
    ap.add_argument("--gap", type=float, default=0.05)
    ap.add_argument("--min-notes", type=int, default=10)
    a = ap.parse_args()

    ank = json.load(open(a.base + "_warp.json", encoding="utf-8"))["anchors"]
    warp = warp_fn(ank)
    t_lo, t_hi = warp_range(ank)

    notes = real_notes(a.base + ".trace")
    rows = list(csv.DictReader(open(a.base + ".csv")))
    n = min(len(notes), len(rows))

    ours = load_mono(a.base + ".wav")
    ref = load_mono(a.ref)

    t = np.array([notes[i][0] for i in range(n)], dtype=float) / SR
    gap = np.diff(np.concatenate([[-1e9], t]))
    w = int(a.win * SR)

    # groups: (sample, cutoff) -> list of spectra
    from collections import defaultdict
    nase = defaultdict(list)
    zelezo = defaultdict(list)
    for i in range(n):
        if gap[i] < a.gap or not (t_lo <= t[i] <= t_hi):
            continue
        a0 = int(t[i] * SR)
        b0 = int(float(warp(np.array([t[i]]))[0]) * SR)
        if a0 + w > len(ours) or b0 + w > len(ref) or a0 < 0 or b0 < 0:
            continue
        # The cutoff is binned by 32 steps - velocity moves it for the same note
        # (the driver does cutoff*max(vel,0x46)/0x7F), so a fine division would
        # break the groups into single notes.
        k = (rows[i]["ccca"], int(rows[i]["cutoff"], 16) & ~0x1F,
             int(rows[i]["Q"], 16))
        nase[k].append(spektrum(ours[a0:a0 + w]))
        zelezo[k].append(spektrum(ref[b0:b0 + w]))

    # For every sample find a pair "open filter" x "closed filter".
    podle_vzorku = defaultdict(list)
    for (ccca, cut, q), v in nase.items():
        if len(v) >= a.min_notes:
            podle_vzorku[ccca].append((cut, q))

    f = np.fft.rfftfreq(NFFT, 1.0 / SR)
    print("sample    open      closed   |  slope above the cutoff (dB/octave)")
    print("                             |     recording     our render")
    nasel = False
    for ccca, sady in podle_vzorku.items():
        sady.sort()
        if len(sady) < 2:
            continue
        nizky, vysoky = sady[0], sady[-1]
        if vysoky[0] - nizky[0] < 48:      # the cutoff difference must be large enough
            continue
        nasel = True
        # Average spectrum of each set, then the ratio = the filter transfer.
        def prenos(d, kl_niz, kl_vys):
            # The dictionary keys are triples (sample, cutoff, Q).
            A = np.mean(d[(ccca,) + tuple(kl_vys)], axis=0)   # open
            B = np.mean(d[(ccca,) + tuple(kl_niz)], axis=0)   # closed
            return 10 * np.log10(np.maximum(B, 1e-30) / np.maximum(A, 1e-30))

        # Cutoff of the closed filter in Hz (our series: 101.81 Hz * 2^(reg*29.3843/1200))
        fc = 101.81 * 2 ** (nizky[0] * 29.3843 / 1200.0)
        # The slope is measured one to two octaves above the cutoff, where the filter decides.
        m1 = (f >= fc * 1.5) & (f <= fc * 3.0)
        m2 = (f >= fc * 3.0) & (f <= fc * 6.0)
        out = []
        for d in (zelezo, nase):
            p = prenos(d, nizky, vysoky)
            if not m1.any() or not m2.any():
                out.append(float("nan"))
                continue
            out.append(float(np.median(p[m2]) - np.median(p[m1])))
        print(f"{ccca:8s}  0x{vysoky[0]:02X}      0x{nizky[0]:02X}     |"
              f"   {out[0]:8.1f}      {out[1]:8.1f}   (cutoff {fc:.0f} Hz,"
              f" {len(nase[(ccca,)+tuple(nizky)])} and {len(nase[(ccca,)+tuple(vysoky)])} notes)")
    if not nasel:
        print("(no sample has enough notes in both filter positions)")


main()

#!/usr/bin/env python
"""Compares two recordings by the **sequence of tones**, independent of tempo.

`align2.py` looks for a time transform that is monotonic and almost linear.
When the recording is the same music but in another tempo, with sections of
other lengths or transposed, such an alignment fails - which does not yet
mean it is another piece.

This script does not look at time: it takes the **chroma** of both sides (how
much energy is in which of the twelve semitones) and compares the sequences
with DTW, which can stretch and squeeze one side arbitrarily. It also tries
all twelve transpositions, so it finds a piece played in another key too.

The result is the mean distance along the best path (0 = identical, 1 =
nothing in common). **Without a control pair the number says nothing** - so a
pair known to match is always measured too.

    python tests/chroma_dtw.py a.wav b.ogg
    python tests/chroma_dtw.py a.wav b.ogg --hop 0.25
"""
import argparse

import numpy as np
import soundfile as sf

SR = 44100


def chroma(path, hop_s, nfft=8192):
    x, sr = sf.read(path, always_2d=True, dtype="float32")
    x = x.mean(axis=1)
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    hop = int(hop_s * SR)
    frames = 1 + max(0, (len(x) - nfft) // hop)
    if frames < 8:
        raise SystemExit("the recording is too short: %s" % path)
    win = np.hanning(nfft).astype(np.float32)
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    keep = (f > 55) & (f < 5000)
    cls = np.zeros(len(f), dtype=int)
    cls[keep] = np.mod(np.round(69 + 12 * np.log2(
        np.maximum(f[keep], 1e-9) / 440.0)).astype(int), 12)

    out = np.zeros((frames, 12))
    for i in range(frames):
        seg = x[i * hop:i * hop + nfft]
        if len(seg) < nfft:
            break
        S = np.abs(np.fft.rfft(seg * win)) ** 2
        for c in range(12):
            m = keep & (cls == c)
            if m.any():
                out[i, c] = S[m].sum()
    # in every frame only the shape, not the volume
    n = out.sum(axis=1, keepdims=True)
    out = np.where(n > 0, out / np.maximum(n, 1e-30), 0.0)
    return out


def dtw_cost(A, B, band=0.25):
    """Mean distance along the best path. The band limits how far the
    path may leave the diagonal - without it DTW "wins" by squeezing one
    side into a single frame."""
    n, m = len(A), len(B)
    # cosine distance between all frames
    An = A / np.maximum(np.linalg.norm(A, axis=1, keepdims=True), 1e-12)
    Bn = B / np.maximum(np.linalg.norm(B, axis=1, keepdims=True), 1e-12)
    D = 1.0 - An @ Bn.T

    w = max(int(band * max(n, m)), 8)
    INF = 1e18
    prev = np.full(m + 1, INF)
    prev[0] = 0.0
    cnt_prev = np.zeros(m + 1)
    for i in range(1, n + 1):
        cur = np.full(m + 1, INF)
        cnt = np.zeros(m + 1)
        j0 = max(1, int(i * m / n) - w)
        j1 = min(m, int(i * m / n) + w)
        for j in range(j0, j1 + 1):
            d = D[i - 1, j - 1]
            best = prev[j]
            bc = cnt_prev[j]
            if prev[j - 1] < best:
                best = prev[j - 1]
                bc = cnt_prev[j - 1]
            if cur[j - 1] < best:
                best = cur[j - 1]
                bc = cnt[j - 1]
            if best >= INF:
                continue
            cur[j] = best + d
            cnt[j] = bc + 1
        prev, cnt_prev = cur, cnt
    if prev[m] >= INF or cnt_prev[m] == 0:
        return 1.0
    return prev[m] / cnt_prev[m]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--hop", type=float, default=0.5)
    ap.add_argument("--band", type=float, default=0.25)
    args = ap.parse_args()

    A = chroma(args.a, args.hop)
    B = chroma(args.b, args.hop)
    print("frames: %d and %d (step %.2f s)" % (len(A), len(B), args.hop))

    best = (1e9, 0)
    for t in range(12):
        Bt = np.roll(B, t, axis=1)
        c = dtw_cost(A, Bt, args.band)
        if c < best[0]:
            best = (c, t)
        print("   transposition %+3d semitones   distance %.4f" % (t, c))
    print()
    print("best: %.4f at transposition %+d semitones" % (best[0], best[1]))


if __name__ == "__main__":
    main()

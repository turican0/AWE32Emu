#!/usr/bin/env python
"""Aligns our render precisely with a recording from real hardware.

`align.py` finds only one offset, which is not enough here. The recordings
come from a real 486 and most went through YouTube: besides another offset
they also have another **time scale**, and as DANCE showed, the scale is
**not even constant** (the first third fits at 0 ms, towards the end it drifts
by 157 ms). A single transform would turn every per-note measurement into
nonsense.

So the alignment is a **curve**: in overlapping windows a local offset is
found, reliable points are kept and interpolated in between. The result is a
list of anchors (time_ours -> time_in_recording), used by `note_probe.py`.

It aligns on the **onset** (spectral flux), not on the volume: the onset of a
tone is sharp in both recordings even with a completely different timbre, so
it is robust against differences in equalisation.

    python tests/align2.py ours.wav ref.wav
    python tests/align2.py ours.wav ref.wav --json warp.json
"""
import argparse
import json

import numpy as np
import soundfile as sf

SR = 44100
HOP = 256          # 5.8 ms - fine enough for a note onset
NFFT = 1024
SEC = HOP / SR


def chroma(path):
    """Energy distribution into the twelve semitones, regardless of octave.

    The onset (spectral flux) aligns by the **onsets** of tones and fails on
    legato music - GMMVIOL has so few onsets that it does not lock. Chroma
    instead follows **which tones sound**, so slow music does not bother it.
    Timbre enters it only weakly (summed over octaves), so it stays robust
    against the recording sounding different from our render.
    """
    x, sr = sf.read(path, always_2d=True, dtype="float32")
    x = x.mean(axis=1)
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    frames = 1 + (len(x) - NFFT) // HOP
    if frames < 8:
        raise SystemExit("the recording is too short: %s" % path)
    win = np.hanning(NFFT).astype(np.float32)
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(frames)[:, None]
    S = np.abs(np.fft.rfft(x[idx] * win, axis=1)) ** 2

    f = np.fft.rfftfreq(NFFT, 1.0 / SR)
    keep = (f > 55) & (f < 5000)
    # semitone number from A0; %12 gives the pitch class
    midi = np.zeros(len(f))
    midi[keep] = 69 + 12 * np.log2(np.maximum(f[keep], 1e-9) / 440.0)
    cls = np.mod(np.round(midi).astype(int), 12)

    out = np.zeros((frames, 12))
    for c in range(12):
        m = keep & (cls == c)
        if m.any():
            out[:, c] = S[:, m].sum(axis=1)
    # In every frame only the **shape**, not the volume - otherwise the
    # dynamics would drive the alignment instead of the harmony.
    n = out.sum(axis=1, keepdims=True)
    out = np.where(n > 0, out / np.maximum(n, 1e-12), 0.0)
    return out


def onset_env(path):
    """Spectral flux: the positive energy increase in the bands, summed."""
    x, sr = sf.read(path, always_2d=True, dtype="float32")
    x = x.mean(axis=1)
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    frames = 1 + (len(x) - NFFT) // HOP
    if frames < 8:
        raise SystemExit("the recording is too short: %s" % path)
    win = np.hanning(NFFT).astype(np.float32)
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(frames)[:, None]
    S = np.abs(np.fft.rfft(x[idx] * win, axis=1))
    S = np.log1p(1000.0 * S)
    flux = np.diff(S, axis=0)
    flux[flux < 0] = 0.0
    e = flux.sum(axis=1)
    k = 200
    base = np.convolve(e, np.ones(k) / k, mode="same")
    e = e - base
    e[e < 0] = 0.0
    return e.astype(np.float64)[:, None]


def xcorr_best(a, b, max_lag=None):
    """Best offset of b against a; returns (offset_in_frames, score -1..1).

    The inputs are 2D (frames x dimensions) - the onset has one dimension,
    chroma twelve. Correlations are summed over the dimensions and normalised
    in total.
    """
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    ac = a - a.mean(axis=0)
    bc = b - b.mean(axis=0)
    c = np.zeros(n)
    for d in range(a.shape[1]):
        A = np.fft.rfft(ac[:, d], n)
        B = np.fft.rfft(bc[:, d], n)
        c += np.fft.irfft(A * np.conj(B), n)
    c = np.concatenate([c[-(len(b) - 1):], c[:len(a)]])
    lags = np.arange(-(len(b) - 1), len(a))
    if max_lag is not None:
        keep = np.abs(lags) <= max_lag
        c = c[keep]
        lags = lags[keep]
    if len(c) == 0:
        return 0, 0.0
    i = int(np.argmax(c))
    norm = np.sqrt(np.sum(ac ** 2) * np.sum(bc ** 2))
    return int(lags[i]), (float(c[i]) / norm if norm > 0 else 0.0)


def resample_env(e, scale):
    n = int(round(len(e) * scale))
    if n < 8:
        return e
    idx = np.linspace(0, len(e) - 1, n)
    src = np.arange(len(e))
    return np.stack([np.interp(idx, src, e[:, d])
                     for d in range(e.shape[1])], axis=1)


def _scan(seg, dst, lo, hi):
    """Goes through the scales from coarse to fine and returns the best (scale, score, offset)."""
    best = (1.0, -1.0, 0)
    for step, span in ((0.005, None), (0.001, 0.006), (0.0002, 0.0012)):
        s0 = lo if span is None else best[0] - span
        s1 = hi if span is None else best[0] + span
        x = s0
        while x <= s1 + 1e-12:
            lag, score = xcorr_best(dst, resample_env(seg, x))
            if score > best[1]:
                best = (x, score, lag)
            x += step
    return best


def coarse(a, b, lo, hi, prah=0.15):
    """A rough estimate of scale and offset - the starting point for the curve.

    **First the whole against the whole.** A longer section gives much more
    leverage on the scale, so when both recordings cover the same music this
    is more precise.

    When that does not lock (score below the threshold), **a piece in the
    whole** is tried: one recording often covers only part of the other - the
    Mars recording has a vocal intro before the song and our XMI loops
    instead (359 s against 140 s of recording). A piece of the **shorter**
    recording is taken and searched for in the longer one.
    """
    scale, score, lag = _scan(a, b, lo, hi)
    if score >= prah:
        return scale, score, lag

    swap = len(b) < len(a)
    src, dst = (b, a) if swap else (a, b)
    n = len(src)
    seg = src[n // 4: n // 2]
    if len(seg) < 100:
        seg = src

    scale, score, lag = _scan(seg, dst, lo, hi)
    # `lag` applies to the start of the section, not to the start of the recording.
    lag -= int(scale * (n // 4))
    if swap:
        # We found where the recording falls in our render; we need the opposite.
        scale = 1.0 / scale
        lag = int(-lag * scale)
    return scale, score, lag


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("ref")
    ap.add_argument("--json", help="save the anchors to a file")
    ap.add_argument("--win", type=float, default=6.0, help="anchor window [s]")
    ap.add_argument("--hop", type=float, default=2.0, help="anchor step [s]")
    ap.add_argument("--min-score", type=float, default=0.35,
                    help="below this an anchor is dropped as unreliable")
    ap.add_argument("--tol", type=float, default=0.15,
                    help="max. deviation of an anchor from its neighbours [s]")
    ap.add_argument("--feature", default="onset", choices=["onset", "chroma"],
                    help="what to align by: note onsets (default),"
                         " or sounding tones - for legato music")
    ap.add_argument("--lo", type=float, default=0.96, help="smallest scale")
    ap.add_argument("--hi", type=float, default=1.04, help="largest scale")
    args = ap.parse_args()

    rys = chroma if args.feature == "chroma" else onset_env
    a = rys(args.ours)
    b = rys(args.ref)

    scale, score, lag0 = coarse(a, b, args.lo, args.hi)
    print("rough estimate: scale %.5f  offset %+.3f s  match %.3f"
          % (scale, lag0 * SEC, score))

    # Local anchors. They are searched around the rough estimate, so a window
    # cannot "snap" a bar elsewhere - the limit is +-0.5 s.
    w = int(args.win / SEC)
    h = int(args.hop / SEC)
    max_lag = int(0.5 / SEC)
    ank = []
    for s0 in range(0, len(a) - w, h):
        seg = a[s0:s0 + w]
        if not np.any(seg):
            continue
        # Where this section should fall per the rough estimate.
        mid = int(scale * s0) + lag0
        r0 = max(0, mid - max_lag)
        r1 = min(len(b), mid + int(scale * w) + max_lag)
        if r1 - r0 < w or mid < -w or mid > len(b):
            continue
        lag, sc = xcorr_best(b[r0:r1], seg, max_lag=None)
        if sc < args.min_score:
            continue
        t_ours = (s0 + w / 2.0) * SEC
        t_ref = (r0 + lag + w / 2.0) * SEC
        ank.append((t_ours, t_ref, sc))

    print("anchors %d (of %d windows)" % (len(ank), 1 + (len(a) - w) // h))
    if len(ank) < 3:
        raise SystemExit("too few reliable anchors - the pair probably does not match")

    # Outlying anchors out. The real offset changes slowly and smoothly (clock
    # difference), so an anchor that runs away from its neighbours by more
    # than --tol is mis-snapped - and since anchors are interpolated, a single
    # such one would bend the whole section around it.
    ank.sort()
    off = np.array([r - o for o, r, _ in ank])
    k = 9
    med = np.array([np.median(off[max(0, i - k // 2):i + k // 2 + 1])
                    for i in range(len(off))])
    keep = np.abs(off - med) <= args.tol
    vyhozeno = int((~keep).sum())
    ank = [x for x, ok in zip(ank, keep) if ok]
    if vyhozeno:
        print("outlying anchors dropped %d" % vyhozeno)
    if len(ank) < 3:
        raise SystemExit("not enough points left after dropping outlying anchors")

    # Time must go forward; an anchor that would turn it back is wrong.
    mono = [ank[0]]
    for t_o, t_r, sc in ank[1:]:
        if t_r > mono[-1][1] + 1e-3:
            mono.append((t_o, t_r, sc))
    print("after the monotonicity check %d" % len(mono))

    d = [(r - o) * 1000.0 for o, r, _ in mono]
    print("offset: from %+.0f ms to %+.0f ms, i.e. a drift of %.0f ms over %.0f s"
          % (d[0], d[-1], abs(d[-1] - d[0]), mono[-1][0] - mono[0][0]))
    print("mean anchor match %.3f" % (sum(s for _, _, s in mono) / len(mono)))
    print()
    print("course of the offset (every 5th anchor):")
    for i in range(0, len(mono), 5):
        t_o, t_r, sc = mono[i]
        print("   %7.1f s -> %7.1f s   %+7.1f ms   shoda %.2f"
              % (t_o, t_r, (t_r - t_o) * 1000.0, sc))

    if args.json:
        json.dump(dict(ours=args.ours, ref=args.ref,
                       anchors=[[o, r] for o, r, _ in mono],
                       score=sum(s for _, _, s in mono) / len(mono)),
                  open(args.json, "w", encoding="utf-8"), indent=1)
        print()
        print("saved to %s" % args.json)


if __name__ == "__main__":
    main()

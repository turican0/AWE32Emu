#!/usr/bin/env python
"""Compares our output with a real recording of the card (FLAC/WAV).

Unlike `cmp86box.py`, where both sides are equally long sample for sample, a
real recording starts elsewhere, has another volume, is slightly noisy and
above all **may run at another tempo**. So the script:

  1. aligns the start by the volume envelope,
  2. matches the volume by the RMS ratio (not by least squares - with a
     mismatched phase that gives nonsense),
  3. aligns every section once more separately, so a possible **tempo drift**
     shows as a growing offset instead of a collapse of the correlation.

    python tests/cmp_real.py ours.wav "SAMPLES2/3 - Relax.flac"
    python tests/cmp_real.py ours.wav ref.flac --segments 20
    python tests/cmp_real.py ours.wav ref.flac --from 30 --to 60

What each line means:

  offset             How much the recording is ahead. The difference between
                     the first and the last section = tempo drift.
  gain               How many times louder the recording is. Around 1 means
                     the attenuation matches; a CD recording is usually
                     normalised, though, so the absolute value is unreliable.
  envelope corr.     Does the volume course match - notes, envelopes,
                     lengths. Below 0.9 there is another note or instrument.
  bands              Where energy is missing or in excess.
"""
import argparse
import sys

import numpy as np

try:
    import soundfile as sf
except ImportError:
    sys.exit("the soundfile module is missing (pip install soundfile)")

HOP_MS = 10.0


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    return x.mean(axis=1), sr


def envelope(x, sr, hop_ms=HOP_MS):
    hop = max(1, int(sr * hop_ms / 1000.0))
    n = len(x) // hop
    if n == 0:
        return np.zeros(0), hop
    return np.sqrt(np.mean(np.square(x[:n * hop].reshape(n, hop)), axis=1)), hop


def env_lag(a, b, sr, max_lag_s=None):
    """Offset of `b` against `a` in samples, from the normalised envelope correlation."""
    ea, hop = envelope(a, sr)
    eb, _ = envelope(b, sr)
    if len(ea) < 4 or len(eb) < 4:
        return 0, 0.0
    ea = (ea - ea.mean()) / (ea.std() + 1e-12)
    eb = (eb - eb.mean()) / (eb.std() + 1e-12)
    n = min(len(ea), len(eb))
    c = np.correlate(eb, ea, mode="full") / n
    lags = np.arange(-len(ea) + 1, len(eb))
    if max_lag_s is not None:
        lim = int(max_lag_s * sr / hop)
        m = np.abs(lags) <= lim
        c, lags = c[m], lags[m]
    i = int(np.argmax(c))
    return int(lags[i]) * hop, float(c[i])


def bands(x, sr, edges):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freq = np.fft.rfftfreq(len(x), 1.0 / sr)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (freq >= lo) & (freq < hi)
        out.append(20 * np.log10(np.sqrt(np.mean(spec[m] ** 2)) + 1e-12) if m.any() else -999.0)
    return out


def db(x):
    return 20 * np.log10(np.sqrt(np.mean(np.square(x))) + 1e-12)


def env_corr(a, b, sr):
    ea, _ = envelope(a, sr)
    eb, _ = envelope(b, sr)
    n = min(len(ea), len(eb))
    if n < 4 or ea[:n].std() == 0 or eb[:n].std() == 0:
        return float("nan")
    return float(np.corrcoef(ea[:n], eb[:n])[0, 1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("ref")
    ap.add_argument("--segments", type=int, default=12)
    ap.add_argument("--offset", type=int, help="force an offset in samples")
    ap.add_argument("--from", dest="t0", type=float, default=0.0, help="from second")
    ap.add_argument("--to", dest="t1", type=float, help="to second")
    args = ap.parse_args()

    a, sr = load(args.ours)
    b, srb = load(args.ref)
    if sr != srb:
        sys.exit(f"different sample rates: {sr} vs {srb}")

    print(f"nas:      {args.ours}   {len(a)/sr:.2f} s")
    print(f"reference {args.ref}   {len(b)/sr:.2f} s")

    off = args.offset if args.offset is not None else env_lag(a, b, sr)[0]
    if off > 0:
        b = b[off:]
    elif off < 0:
        a = a[-off:]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    if args.t0 or args.t1:
        s = int(args.t0 * sr)
        e = int(args.t1 * sr) if args.t1 else n
        a, b, n = a[s:e], b[s:e], e - s

    g = float(np.sqrt(np.mean(b ** 2) / (np.mean(a ** 2) + 1e-24)))
    a = a * g
    print(f"alignment: offset {off:+d} samples ({off/sr*1000:+.0f} ms), "
          f"overlap {n/sr:.1f} s")
    print(f"gain of the reference against us: {g:.3f}  ({20*np.log10(g+1e-12):+.2f} dB)")
    print()

    print(f"{'':22} {'RMS dB':>8}  {'peak':>7}")
    print(f"{'ours (after gain)':22} {db(a):8.2f}  {np.max(np.abs(a)):7.4f}")
    print(f"{'reference':22} {db(b):8.2f}  {np.max(np.abs(b)):7.4f}")
    print(f"\nvolume envelope correlation: {env_corr(a, b, sr):.4f}")

    edges = [0, 100, 200, 400, 800, 1600, 3200, 6400, 12800, sr // 2]
    ba, bb = bands(a, sr, edges), bands(b, sr, edges)
    print(f"\n{'band Hz':>14} {'ours dB':>9} {'ref dB':>9} {'diff':>8}")
    for i in range(len(ba)):
        print(f"{edges[i]:6}-{edges[i+1]:<7} {ba[i]:9.1f} {bb[i]:9.1f} {ba[i]-bb[i]:+8.1f}")

    if args.segments > 0:
        step = n // args.segments
        print(f"\nsections (each aligned separately, search within +-1 s):")
        print(f"{'section s':>14} {'offset ms':>9} {'ours dB':>8} {'ref dB':>8} "
              f"{'diff':>8} {'corr.':>9}")
        for i in range(args.segments):
            s, e = i * step, (i + 1) * step
            sa, sb = a[s:e], b[s:e]
            lag, _ = env_lag(sa, sb, sr, max_lag_s=1.0)
            if lag > 0:
                sb2, sa2 = sb[lag:], sa[:len(sb) - lag]
            elif lag < 0:
                sa2, sb2 = sa[-lag:], sb[:len(sa) + lag]
            else:
                sa2, sb2 = sa, sb
            k = min(len(sa2), len(sb2))
            sa2, sb2 = sa2[:k], sb2[:k]
            c = env_corr(sa2, sb2, sr)
            mark = "  <<<" if not (c >= 0.85) else ""
            print(f"{s/sr:6.1f}-{e/sr:<7.1f} {lag/sr*1000:+9.0f} {db(sa2):8.2f} "
                  f"{db(sb2):8.2f} {db(sa2)-db(sb2):+8.2f} {c:9.3f}{mark}")


if __name__ == "__main__":
    main()

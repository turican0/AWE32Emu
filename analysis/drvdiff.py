# -*- coding: utf-8 -*-
"""Difference check: our MIDI render vs the replay of a real driver trace.

Both go through the same chip (snd_emu8k.c), so what differs is the driver
layer and the timing. The renders are aligned on the envelope (10 ms,
cross-correlation over the first 30 s), then compared per window:
level difference, 1/3-octave band difference (rms over bands) and the
residual of the aligned waveforms relative to the signal.

    python drvdiff.py ours.wav replay.wav [--win 2] [--until 60]
"""
import argparse

import numpy as np
import soundfile as sf

ap = argparse.ArgumentParser()
ap.add_argument('ours')
ap.add_argument('ref')
ap.add_argument('--win', type=float, default=2.0)
ap.add_argument('--until', type=float, default=1e9)
a = ap.parse_args()
SR = 44100


def load(p):
    x, sr = sf.read(p, dtype='float64')
    assert sr == SR
    return x.mean(axis=1) if x.ndim > 1 else x


o, r = load(a.ours), load(a.ref)


def trim(x):
    # drop the leading silence (the VM trace starts long before the music)
    # music start: first 1 s window louder than -30 dBFS (a lone click does
    # not count), then back to its first sample above -60 dBFS
    h = SR
    n = len(x) // h * h
    e = np.sqrt((x[:n] ** 2).reshape(-1, h).mean(axis=1))
    w = int(np.argmax(e > 0.0316)) * h
    lo = max(0, w - 2 * SR)
    i = lo + int(np.argmax(np.abs(x[lo:w + h]) > 1e-3))
    return x[max(0, i - SR // 10):]


o, r = trim(o), trim(r)


def env(x, h=441):
    n = len(x) // h * h
    return np.sqrt((x[:n] ** 2).reshape(-1, h).mean(axis=1))


eo, er = env(o[:30 * SR]), env(r[:30 * SR])
# shift of the reference relative to ours in 10 ms steps, then refine per sample
best, lag = -1, 0
for L in range(-600, 601):
    x = eo[max(0, L):]
    y = er[max(0, -L):]
    n = min(len(x), len(y))
    if n < 500:
        continue
    c = np.corrcoef(x[:n], y[:n])[0, 1]
    if c > best:
        best, lag = c, L
shift = lag * 441
seg_o = o[max(0, shift):]
seg_r = r[max(0, -shift):]
# fine alignment +-441 samples on the first 20 s
n0 = min(len(seg_o), len(seg_r), 20 * SR)
fb, fl = -1, 0
for d in range(-441, 442, 3):
    x = seg_o[max(0, d):max(0, d) + n0 - 1000]
    y = seg_r[max(0, -d):max(0, -d) + n0 - 1000]
    c = np.dot(x, y) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-12)
    if c > fb:
        fb, fl = c, d
seg_o = seg_o[max(0, fl):]
seg_r = seg_r[max(0, -fl):]
n = min(len(seg_o), len(seg_r), int(a.until * SR))
seg_o, seg_r = seg_o[:n], seg_r[:n]
print('alignment: ours starts %.3f s later, envelope r %.3f, waveform r %.3f' % ((shift + fl) / SR, best, fb))

edges = 50.0 * 2 ** (np.arange(0, 28) / 3.0)


def bands(x):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    return np.array([10 * np.log10(X[(f >= lo) & (f < hi)].sum() + 1e-12) for lo, hi in zip(edges[:-1], edges[1:])])


W = int(a.win * SR)
rows = []
for s in range(0, n - W, W):
    x, y = seg_o[s:s + W], seg_r[s:s + W]
    ly = 10 * np.log10(np.mean(y ** 2) + 1e-14)
    if ly < -70:
        continue
    lx = 10 * np.log10(np.mean(x ** 2) + 1e-14)
    bx, by = bands(x), bands(y)
    m = by > by.max() - 50
    brms = np.sqrt(np.mean((bx[m] - by[m]) ** 2))
    res = 10 * np.log10(np.mean((x - y) ** 2) / np.mean(y ** 2) + 1e-14)
    rows.append((s / SR, lx - ly, brms, res))
rows = np.array(rows)
print('windows %d x %.0f s' % (len(rows), a.win))
print('level ours-ref: median %+.2f dB, p95 |.| %.2f dB' % (np.median(rows[:, 1]), np.percentile(np.abs(rows[:, 1]), 95)))
print('band rms diff : median %.2f dB, p95 %.2f dB' % (np.median(rows[:, 2]), np.percentile(rows[:, 2], 95)))
print('residual/signal: median %.1f dB, worst %.1f dB' % (np.median(rows[:, 3]), rows[:, 3].max()))
worst = rows[np.argsort(-rows[:, 2])[:6]]
print('worst windows (t, level diff, band rms, residual):')
for t, dl, br, rs in worst:
    print('  %6.1f s  %+5.2f dB  %5.2f dB  %6.1f dB' % (t, dl, br, rs))

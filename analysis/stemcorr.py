# -*- coding: utf-8 -*-
"""Which channel stems are present in the recording, per window.

For each window, the recording's 10 ms log-energy envelope (band 200 Hz -
10 kHz) is regressed on the stems' envelopes (linear power, non-negative
least squares by clipping) and the correlation of each stem's onset flux
with the recording's flux is printed. A stem that plays in the recording
correlates; a muted one does not.

    python stemcorr.py rec.flac stem1.wav stem2.wav ... [--offset 0.917] [--win 10]
"""
import argparse
import os

import numpy as np
import soundfile as sf

ap = argparse.ArgumentParser()
ap.add_argument('rec')
ap.add_argument('stems', nargs='+')
ap.add_argument('--offset', type=float, default=0.917)
ap.add_argument('--win', type=float, default=10.0)
a = ap.parse_args()
SR = 44100
HOP = 441
NFFT = 1024


def flux(x):
    n = (len(x) - NFFT) // HOP
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(n)[:, None]
    spec = np.abs(np.fft.rfft(x[idx] * np.hanning(NFFT), axis=1))
    f = np.fft.rfftfreq(NFFT, 1.0 / SR)
    spec = spec[:, (f > 200) & (f < 10000)]
    ls = np.log1p(spec * 100)
    fl = np.maximum(np.diff(ls, axis=0), 0).sum(axis=1)
    return np.concatenate([[0], fl])


rec = sf.read(a.rec, dtype='float64')[0].mean(axis=1)
fr = flux(rec)
off = int(round(a.offset * SR / HOP))
stems = []
for p in a.stems:
    x = sf.read(p, dtype='float64')[0].mean(axis=1)
    fs = flux(x)
    fs = np.concatenate([np.zeros(off), fs])
    stems.append((os.path.basename(p), fs))

W = int(a.win * SR / HOP)
print('window    ' + ' '.join('%10s' % n[:10] for n, _ in stems))
for s in range(0, len(fr) - W, W):
    row = []
    for n, fs in stems:
        seg = fs[s:s + W]
        r = fr[s:s + W]
        if len(seg) < W or seg.std() == 0:
            row.append('         -')
            continue
        row.append('%10.2f' % np.corrcoef(seg, r)[0, 1])
    print('%4.0f-%4.0f ' % (s * HOP / SR, (s + W) * HOP / SR) + ' '.join(row))

# -*- coding: utf-8 -*-
"""Map of a recording: what plays in which part (intro / menu / nothing).

    python segmap.py recording.wav render1.wav [render2.wav ...]

The recording is cut into windows WIN with a step STEP and every window is
searched in the whole renders. The onset (spectral flux in log-mel bands,
normalised) is compared, which is insensitive to colour and volume. For every
window the best match and where it lies in the render is printed. An offset
that holds across neighbouring windows is reliable; a randomly jumping offset
= the window fits nowhere.
"""
import sys

import numpy as np
import soundfile as sf

WIN, STEP = 5.0, 2.5
SR = 44100
HOP = 441          # 10 ms
NFFT = 2048


def flux(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == SR
    n = (len(x) - NFFT) // HOP
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(n)[:, None]
    spec = np.abs(np.fft.rfft(x[idx] * np.hanning(NFFT), axis=1))
    f = np.fft.rfftfreq(NFFT, 1 / SR)
    edges = np.geomspace(60, 12000, 25)
    bands = np.stack([spec[:, (f >= a) & (f < b)].sum(axis=1) for a, b in zip(edges[:-1], edges[1:])], axis=1)
    lb = np.log(bands + 1e-6)
    fl = np.maximum(np.diff(lb, axis=0), 0).sum(axis=1)
    fl = fl - np.convolve(fl, np.ones(50) / 50, mode='same')     # subtract the slow trend
    return fl


rec = flux(sys.argv[1])
refs = [(p.split('\\')[-1].split('/')[-1], flux(p)) for p in sys.argv[2:]]
w = int(WIN * 100)
s = int(STEP * 100)
print('recording window  ' + '   '.join('%-26s' % n for n, _ in refs))
for start in range(0, len(rec) - w, s):
    seg = rec[start:start + w]
    seg = (seg - seg.mean()) / (seg.std() + 1e-9)
    cells = []
    for name, r in refs:
        # normalised cross-correlation over the whole render
        c = np.correlate(r - r.mean(), seg, mode='valid')
        rs = np.sqrt(np.convolve((r - r.mean()) ** 2, np.ones(w), mode='valid')) + 1e-9
        c = c / rs / w
        k = int(np.argmax(c))
        cells.append('match %.2f @ %6.1f s (offset %+6.1f)' % (c[k], k / 100.0, k / 100.0 - start / 100.0))
    print('%5.1f-%5.1f s   ' % (start / 100.0, (start + w) / 100.0) + '   '.join(cells))

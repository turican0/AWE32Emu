# -*- coding: utf-8 -*-
"""Is the slow tail after the tick a DC step through the analog coupling?
Block 42, feedback 0x00, card R (chorus R tap). Coherent average of the raw
waveform (aligned on the tick onset, each tick's own sign), low-passed at
60 Hz, 0..200 ms after the R copy of the note-off; plus the DC of the tick
sample in the render (mean of the render return during the tick)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loop42c.py'), encoding='utf-8').read().split('noise = db(')[0])


def lowpass(x, fc):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[f > fc] = 0
    return np.fft.irfft(X, len(x))


for ipk in ('IP 49152', 'IP 57344', 'IP 65535'):
    cs, rs, dc = [], [], []
    for e in ev:
        if e.block != 42 or not e.text.startswith('feedback 0x00') or ipk not in e.text:
            continue
        tc = onset(card[:, 0], ctime(e))
        tr = onset(dry_l, rtime(e))
        a = int((tc - 0.05) * SR)
        cs.append(lowpass(card[a:a + int(0.40 * SR), 1], 60.0))
        b = int((tr + 0.024) * SR)
        dc.append(np.mean(wet_r[b + int(0.005 * SR):b + int(0.025 * SR)]))
        rs.append(np.sqrt(np.mean(wet_r[b + int(0.005 * SR):b + int(0.025 * SR)] ** 2)))
    c = np.mean(cs, axis=0)
    t0 = int((0.05 + 0.054) * SR)   # R copy of the note-off
    pts = [t0 + int(ms * SR / 1000) for ms in (-20, -10, 0, 2, 5, 10, 20, 40, 60, 80, 120, 160, 200)]
    print('%s: render tick DC %+.4f, RMS %.4f (DC %.1f dB vs RMS)' % (ipk, np.mean(dc), np.mean(rs),
                                                                       20 * np.log10(abs(np.mean(dc)) / np.mean(rs))))
    print('   card R low-passed, ms from note-off copy -20 -10 0 2 5 10 20 40 60 80 120 160 200:')
    print('   ' + ' '.join('%+.4f' % c[p] for p in pts))

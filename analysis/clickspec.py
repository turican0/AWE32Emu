# -*- coding: utf-8 -*-
"""Spectrum of the card's early reverb response to the 10 ms noise click
(AWETST28 block 45, presets 0-5, line out R) in the windows 0-12, 12-24 and
24-48 ms, against the dry click spectrum (L, 0-10 ms), octave bands. Energy
per window (sum over the window, not mean), so the windows add up."""
import os
import sys

import numpy as np

sys.argv = sys.argv[:1]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'clickfit.py'), encoding='utf-8').read().split('target = {p:')[0])

OCT = [250, 500, 1000, 2000, 4000, 8000]


def spec_energy(x, n=4096):
    X = np.abs(np.fft.rfft(x, n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.array([X[(f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)].sum() for c in OCT])


print('octaves ' + ' '.join('%6d' % c for c in OCT))
acc = {}
for p, tc, tr in clicks:
    d = spec_energy(card[int(tc * SR):int((tc + 0.010) * SR), 0])
    for a, b in ((0.0, 0.012), (0.012, 0.024), (0.024, 0.048)):
        e = spec_energy(card[int((tc + a) * SR):int((tc + b) * SR), 1])
        rel = 10 * np.log10(e / d)
        acc.setdefault((a, b), []).append(rel)
        print('p%d %2d-%2d ms ' % (p, a * 1000, b * 1000) + ' '.join('%+6.1f' % v for v in rel))
for k, v in acc.items():
    print('mean %2d-%2d ms ' % (k[0] * 1000, k[1] * 1000) + ' '.join('%+6.1f' % x for x in np.mean(v, axis=0)))

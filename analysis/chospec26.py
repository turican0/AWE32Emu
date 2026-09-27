# -*- coding: utf-8 -*-
"""Frequency response of the chorus return path: AWETST26 block 40, noise
3 s with chorus send 255 (line out 39-41_ext.wav). Card R (return alone)
against the render's return R, third-octave bands, card - render; and the
same for the dry noise (card L vs render dry L, the left chorus tap adds
the same return on both sides) as the reference of the dry path."""
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
src_code = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chorus26.py')).read()
exec(src_code.split('ref_level = {}')[0])

mix26 = sf.read(D + r'\render26.wav', dtype='float64')[0]
BANDS = [100 * 2 ** (i / 3) for i in range(0, 23)]


def spec(x):
    n = 8192
    acc = 0
    for i in range(0, len(x) - n, n // 2):
        acc = acc + np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.array([10 * np.log10(acc[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].mean() + 1e-20) for c in BANDS])


print('bands Hz   ' + ' '.join('%5d' % c for c in BANDS))
for p in range(1, 5):
    nz = [e for e in ev if e[0] == 40 and e[1].startswith('preset %d, noise' % p)][0]
    tc, tr = when('card', nz[2]), when('render', nz[2])
    a, b = 0.5, 2.8
    cr = spec(card[int((tc + a) * SR):int((tc + b) * SR), 1])
    rr = spec(wet[int((tr + a) * SR):int((tr + b) * SR), 1])
    cl = spec(card[int((tc + a) * SR):int((tc + b) * SR), 0])
    rl = spec(mix26[int((tr + a) * SR):int((tr + b) * SR), 0])
    d = cr - rr
    d -= d[BANDS.index(1000 * 2 ** 0) if 1000 in BANDS else 10]
    dl = cl - rl
    dl -= dl[10]
    print('p%d return  ' % p + ' '.join('%+5.1f' % v for v in d))
    print('   L (mix) ' + ' '.join('%+5.1f' % v for v in dl))

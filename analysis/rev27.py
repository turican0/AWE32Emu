# -*- coding: utf-8 -*-
"""AWETST27 block 22 on the LINE OUT: reverb return of the tick (send 255),
card R (return alone) vs render return R (mix - no reverb sends). Both
high-passed at 100 Hz (the line out's AC coupling turns the tick's DC into
a slow step, see dctail42.py). dB against the dry tick (max 40 ms energy of
the left channel in the first 100 ms).

  windows  0-25, 25-60, 60-150, 150-300, 300-600, 600-1200 ms
  bands    return spectrum 20-300 ms, octave bands, card - render, after
           removing the mean difference
"""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align27.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

WET = mix[:, 1] - norev[:, 1]
WIN = ((0, 25), (25, 60), (60, 150), (150, 300), (300, 600), (600, 1200))
OCT = (125, 250, 500, 1000, 2000, 4000, 8000)


def hp(x, fc=100.0):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[f < fc] = 0
    return np.fft.irfft(X, len(x))


def onset(x, tg):
    a = int((tg - 0.15) * SR)
    seg = x[a:a + int(0.3 * SR)]
    h = int(0.0005 * SR)
    e = 10 * np.log10(np.array([np.mean(seg[i:i + h] ** 2) for i in range(0, len(seg) - h, h)]) + 1e-14)
    return (a + int(np.argmax(e > e.max() - 20)) * h) / SR


def dref(x):
    w = int(0.04 * SR)
    e = np.convolve(x[:int(0.1 * SR)] ** 2, np.ones(w) / w, 'valid')
    return 10 * np.log10(e.max())


def wins(r, ref):
    return [10 * np.log10(np.mean(r[int(a * SR / 1000):int(b * SR / 1000)] ** 2) + 1e-14) - ref for a, b in WIN]


def bands(r):
    x = r[int(0.020 * SR):int(0.300 * SR)]
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    return np.array([10 * np.log10(X[(f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)].mean() + 1e-20) for c in OCT])


print('windows ms: ' + '  '.join('%d-%d' % w for w in WIN))
for e in ev:
    if e.block != 22 or 'tick' not in e.text:
        continue
    p = int(e.text.split()[1].rstrip(','))
    tc = onset(card[:, 0], ctime(e))
    tr = onset(norev[:, 0], rtime(e))
    n = int(1.3 * SR)
    c = hp(card[int(tc * SR):int(tc * SR) + n, 1])
    r = hp(WET[int(tr * SR):int(tr * SR) + n])
    cref = dref(hp(card[int(tc * SR):int(tc * SR) + n, 0]))
    rref = dref(hp(norev[int(tr * SR):int(tr * SR) + n, 0]))
    cw, rw = wins(c, cref), wins(r, rref)
    bd = bands(c) - bands(r)
    bd -= bd.mean()
    print('preset %d card ' % p + ' '.join('%6.1f' % v for v in cw))
    print('         rend ' + ' '.join('%6.1f' % v for v in rw) + '   diff ' + ' '.join('%+5.1f' % (a - b) for a, b in zip(cw, rw)))
    print('         bands 125..8k card-rend ' + ' '.join('%+5.1f' % v for v in bd))

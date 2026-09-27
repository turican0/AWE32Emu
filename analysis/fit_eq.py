# -*- coding: utf-8 -*-
"""Fitting the measured EQ curves (block 35) with shelving filters.

For every position treble 0..11 and bass 0..11 it fits
  a) a 1st-order shelf:  |H|^2 = G^2 (f^2 + fz^2) / (f^2 + fp^2) (treble) ...
  b) a 2nd-order shelf (RBJ biquad, S = 1):  gain, f0
and picks what fits better. Prints a table for C++ and the residual in dB.
Measured points: third-octave bands 60 Hz .. 16 kHz, difference against flat.
"""
import itertools
import sys

import numpy as np

sys.argv = [sys.argv[0]]
exec(open(__file__.replace('fit_eq.py', 'eq35.py'), encoding='utf-8').read().split("flat = spec['flat']")[0])
flat = spec['flat']
F = CENT
FS = 44100.0


def rbj_shelf(f, gain_db, f0, high, S=1.0):
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * np.pi * f0 / FS
    alpha = np.sin(w0) / 2 * np.sqrt((A + 1 / A) * (1 / S - 1) + 2)
    cw = np.cos(w0)
    if high:
        b0 = A * ((A + 1) + (A - 1) * cw + 2 * np.sqrt(A) * alpha)
        b1 = -2 * A * ((A - 1) + (A + 1) * cw)
        b2 = A * ((A + 1) + (A - 1) * cw - 2 * np.sqrt(A) * alpha)
        a0 = (A + 1) - (A - 1) * cw + 2 * np.sqrt(A) * alpha
        a1 = 2 * ((A - 1) - (A + 1) * cw)
        a2 = (A + 1) - (A - 1) * cw - 2 * np.sqrt(A) * alpha
    else:
        b0 = A * ((A + 1) - (A - 1) * cw + 2 * np.sqrt(A) * alpha)
        b1 = 2 * A * ((A - 1) - (A + 1) * cw)
        b2 = A * ((A + 1) - (A - 1) * cw - 2 * np.sqrt(A) * alpha)
        a0 = (A + 1) + (A - 1) * cw + 2 * np.sqrt(A) * alpha
        a1 = -2 * ((A - 1) + (A + 1) * cw)
        a2 = (A + 1) + (A - 1) * cw - 2 * np.sqrt(A) * alpha
    z = np.exp(-2j * np.pi * f / FS)
    h = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z)
    return 20 * np.log10(np.abs(h)), (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def fit(meas, high):
    best = None
    for g in np.arange(-14, 14.01, 0.1):
        for f0 in np.geomspace(100, 12000, 120):
            for S in (0.5, 0.7, 1.0):
                model, _ = rbj_shelf(F, g, f0, high, S)
                err = np.sqrt(np.mean((model - meas) ** 2))
                if best is None or err < best[0]:
                    best = (err, g, f0, S)
    return best


print('%-9s %6s %8s %4s %7s   max|zbytek|' % ('position', 'gain', 'f0', 'S', 'rms dB'))
rows = []
for kind, high in (('t', True), ('b', False)):
    for i in range(12):
        if (kind, i) not in spec:
            continue
        meas = spec[(kind, i)] - flat
        if kind == 't':
            meas = meas - np.mean(meas[F <= 150])      # ignore the LF noise at treble 10/11
        err, g, f0, S = fit(meas, high)
        model, coef = rbj_shelf(F, g, f0, high, S)
        rows.append((kind, i, g, f0, S))
        print('%-9s %+6.1f %8.0f %4.1f %7.2f   %.2f' % ('%s%d' % ('treble ' if high else 'bass ', i),
                                                      g, f0, S, err, np.max(np.abs(model - meas))))
print()
print('// {gainDb, f0Hz, S} for treble 0..11, then bass 0..11')
for kind in ('t', 'b'):
    print('{ ' + ', '.join('{%.1f, %.0f, %.1f}' % (g, f0, S) for k, i, g, f0, S in rows if k == kind) + ' }')

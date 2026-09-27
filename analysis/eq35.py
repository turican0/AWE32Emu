# -*- coding: utf-8 -*-
"""Block 35 (recc): the EMU8000 equalizer measured with noise.

Every note: noise from ROM at 1:1, filter wide open, 3 s. Settings:
  SDK (bass 5, treble 9), flat (5,5), the game driver's bytes (C280...),
  treble 0..11 at bass 5, bass 0..11 at treble 5, SDK again.
Spectrum = mean power 1.0-2.8 s after the onset, third-octave bands.
EQ curve = setting minus flat (the capture path and the filter cancel).
Also prints: SDK minus flat (what the card always adds) and its slope in
dB/oct above 1 kHz - against the +2.6 dB/oct tilt left in the recordings.
"""
import sys

import numpy as np

sys.path.insert(0, r'C:\prenos\AWE32Emu\tools\awetest')
import awe25  # noqa: E402
from awe25 import SR, events, seg  # noqa: E402

awe25.LATENCY = 0.003
CENT = 1000.0 * 2 ** (np.arange(-14, 14) / 3.0)          # 125 Hz .. 16 kHz
CENT = CENT[(CENT >= 60) & (CENT <= 17000)]
BASS_DB = [-12, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12]


def spectrum(x):
    n = 8192
    win = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    k = 0
    for i in range(0, len(x) - n, n // 2):
        acc += np.abs(np.fft.rfft(x[i:i + n] * win)) ** 2
        k += 1
    f = np.fft.rfftfreq(n, 1.0 / SR)
    out = []
    for c in CENT:
        m = (f >= c / 2 ** (1 / 6.0)) & (f < c * 2 ** (1 / 6.0))
        out.append(10 * np.log10(acc[m].mean() / max(k, 1) + 1e-20))
    return np.array(out)


spec = {}
for e in events(35):
    t = e.text
    x = seg(e, 1.0, 2.8)
    if len(x) < SR:
        continue
    if t.startswith('EQ as the SDK'):
        key = 'sdk'
    elif t.startswith('EQ flat'):
        key = 'flat'
    elif t.startswith('EQ bytes as the game driver'):
        key = 'driver'
    elif t.startswith('EQ treble index'):
        key = ('t', int(t.split()[3].rstrip(',')))
    elif t.startswith('EQ bass index'):
        key = ('b', int(t.split()[3].rstrip(',')))
    elif t.startswith('EQ back to the SDK'):
        key = 'sdk2'
    else:
        continue
    spec[key] = spectrum(x)

flat = spec['flat']
print('band Hz   ' + ' '.join('%6.0f' % c for c in CENT))


def row(label, v):
    print('%-10s' % label + ' '.join('%+6.1f' % d for d in v))


row('SDK-flat  ', spec['sdk'] - flat)
row('SDK2-flat', spec['sdk2'] - flat)
row('drv-flat ', spec['driver'] - flat)
for i in range(12):
    if ('t', i) in spec:
        row('treble %2d' % i, spec[('t', i)] - flat)
for i in range(12):
    if ('b', i) in spec:
        row('bass %2d' % i, spec[('b', i)] - flat)

d = spec['sdk'] - flat
m = CENT >= 1000
slope = np.polyfit(np.log2(CENT[m] / 1000.0), d[m], 1)[0]
print('\nSDK minus flat: slope above 1 kHz %+.2f dB/oct, at 8 kHz %+.1f dB, 12.7 kHz %+.1f dB'
      % (slope, d[np.argmin(abs(CENT - 8000))], d[np.argmin(abs(CENT - 12700))]))
print('repeatability: SDK at the start minus SDK at the end: max |difference| %.1f dB (below 10 kHz)'
      % np.max(np.abs((spec['sdk'] - spec['sdk2'])[CENT < 10000])))
hi = CENT >= 4000
for i in range(12):
    if ('t', i) in spec:
        print('treble %2d: mean 4-16 kHz %+5.1f dB' % (i, np.mean((spec[('t', i)] - flat)[hi])))
lo = CENT <= 250
for i in range(12):
    if ('b', i) in spec:
        print('bass %2d (ALSA %+d dB): prumer do 250 Hz %+5.1f dB' % (i, BASS_DB[i], np.mean((spec[('b', i)] - flat)[lo])))

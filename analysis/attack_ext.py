# -*- coding: utf-8 -*-
"""Attack shape in three paths: card internal capture, card line-out (FLAC)
and our render, on identical register writes (block 8).

Amplitude = noise-subtracted RMS over a window of max(5 ms, T/50) centred at
t = f * T, normalised to the plateau after the attack. T = 11.878 s /
RateDivisor(r - 1). If both card paths show the same non-linear shape, it is
the chip, not the capture chain.

    python attack_ext.py [--ours replay_cham.wav]
"""
import argparse

import numpy as np
import soundfile as sf

import replay25 as R
import ext25

ap = argparse.ArgumentParser()
ap.add_argument('--ours', default='replay_cham.wav')
a = ap.parse_args()

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)
SR = 44100
FR = np.array([0.02, 0.04, 0.06, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.7,
               0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.2])


def rd(i):
    g, m = (i >> 4) & 7, i & 15
    return (m + 1) if g == 0 else ((m + 17) << (g - 1))


def shape(x, T, pre):
    """x starts `pre` s before the note."""
    x = x.astype(np.float64)
    noise = np.mean(x[int(0.02 * SR):int((pre - 0.02) * SR)] ** 2)
    h = max(int(0.005 * SR), int(T / 50 * SR))

    def amp(t):
        c = int((pre + t) * SR)
        s = x[max(c - h // 2, 0):c + h // 2]
        return np.sqrt(max(np.mean(s ** 2) - noise, 0.0))
    plateau = np.median([amp(T * 1.4 + 0.05 + 0.02 * i) for i in range(10)])
    return np.array([amp(f * T) for f in FR]) / plateau, 10 * np.log10(noise / plateau ** 2 + 1e-14)


PRE = 0.3
rows = {'int': [], 'ext': [], 'ren': []}
print('block 8: amplitude / plateau at t/T')
print('%-15s %s' % ('rate  path', ' '.join('%5.2f' % f for f in FR)))
cnt = {}
for ev in ext25.events(8):
    i = cnt.get(ev.text, 0)
    cnt[ev.text] = i + 1
    if not ev.text.startswith('attack'):
        continue
    r = int(ev.text.split('0x')[1], 16)
    T = 11.878 / rd(r - 1)
    if T < 0.25 or T > 4.0:
        continue
    m = vmk.get((8, ev.text, i))
    if m is None or m.rt is None:
        continue
    dur = PRE + T * 1.4 + 0.4
    c, f = ext25.fseg(ev, -PRE, dur - PRE)
    fr = int(round(k * m.rt + q - PRE * SR))
    o = ours[fr:fr + len(c)]
    for key, x in (('int', c), ('ext', f), ('ren', o)):
        if x is None or len(x) < len(c):
            print('0x%02X  %s      missing' % (r, key))
            continue
        v, nfl = shape(x, T, PRE)
        rows[key].append(v)
        print('0x%02X  %s %5.0f %s' % (r, key, nfl, ' '.join('%5.2f' % u for u in v)))
print('\nmedian over rates')
for key in ('int', 'ext', 'ren'):
    if rows[key]:
        print('      %s       %s' % (key, ' '.join('%5.2f' % u for u in np.median(rows[key], axis=0))))

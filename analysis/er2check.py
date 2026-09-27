# -*- coding: utf-8 -*-
"""Click response with the new early reflections: card vs render, 2 ms
bins 0-60 ms, both against their own full left channel over the click
(card L = dry + reverb L; render the same, mix L). Optional per-preset ER
gain scale: python er2check.py s0 s1 s2 s3 s4 s5 (multiplies the table
gains, via EMU8K_RV_P)."""
import os
import sys

import numpy as np

SC = [float(a) for a in sys.argv[1:7]] if len(sys.argv) >= 7 else [1.0] * 6
sys.argv = sys.argv[:1]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'clickfit.py'), encoding='utf-8').read().split('target = {p:')[0])

ROOM = {0: [0.613, 0.045, 0.347], 1: [0.583, 0.08, 0.587], 2: [0.647, 0.03, 0.620],
        3: [0.692, 0.09, 0.849], 4: [0.672, 0.06, 0.988], 5: [0.694, 0.10, 0.912]}
ERG = [0.875, 0.642, 0.660, 0.647, 0.568, 0.750]
env = {'EMU8K_RV_P%d' % p: '%.4f,%.3f,%.4f,%.4f' % (v[0], v[1], v[2], ERG[p] * SC[p]) for p, v in ROOM.items()}
mix = render(B45, os.path.join(D28, 'b45_er2.wav'), env)
wet = mix[:, 1] - dry[:, 1]
tot = 0.0
for p, tc, tr in clicks:
    c = envc(card[:, 0], card[:, 1], tc)
    r = envc(mix[:, 0], wet, tr)
    dd = np.clip(r, -40, 99) - np.clip(c, -40, 99)
    tot += np.abs(dd[:20]).mean()
    print('p%d scale %.2f  early 2-18 ms %+5.1f  20-40 ms %+5.1f  40-60 ms %+5.1f' % (
        p, SC[p], np.mean(dd[1:9]), np.mean(dd[10:20]), np.mean(dd[20:30])))
    print('    card   ' + ' '.join('%4.0f' % v for v in c[:30:2]))
    print('    render ' + ' '.join('%4.0f' % v for v in r[:30:2]))
print('mean |diff| 0-40 ms %.2f' % (tot / len(clicks)))

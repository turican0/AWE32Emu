# -*- coding: utf-8 -*-
"""Fine damping grid for reverb presets 0-5 against the line out colour
(revspec45.py): error = mean |card - render - analog| over 5-12.8 kHz,
both windows, rows relative to their 200 Hz - 4 kHz mean. Analog droop of
the card's output (block 44, same at all pitches): -0.6 dB 8 kHz ..."""
import os
import sys

import numpy as np

DGRID = [float(a) for a in sys.argv[1:]] or [0.0, 0.01, 0.02, 0.03, 0.045, 0.06, 0.08]
sys.argv = sys.argv[:1]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'damp45.py'), encoding='utf-8').read().split('cardw = {p:')[0])

ANALOG = {5079: -0.3, 6400: -0.4, 8063: -0.6, 10159: -1.0, 12800: -1.6, 16126: -2.6}
mid = [i for i, c in enumerate(BANDS) if 190 <= c <= 4200]
hi = [i for i, c in enumerate(BANDS) if 5000 <= c <= 13000]
an = np.array([ANALOG[int(round(BANDS[i]))] if int(round(BANDS[i])) in ANALOG else 0.0 for i in range(len(BANDS))])


def colour(l, r, t):
    d = bands(l[int((t + 0.02) * SR):int((t + 0.19) * SR)])
    out = []
    for a, b in ((0.55, 0.90), (0.90, 1.70)):
        w = bands(r[int((t + a) * SR):int((t + b) * SR)]) - d
        out.append(w - w[mid].mean())
    return out


cc = {p: colour(card[:, 0], card[:, 1], tc) for p, tc, tr in bursts}
best = {}
for dp in DGRID:
    env = {'EMU8K_RV_P%d' % p: '%.4f,%.4f,%.4f,%.4f' % (v[0], dp, v[2], v[3]) for p, v in P.items()}
    mix = render(B45, os.path.join(D28, 'b45_mixd.wav'), env)
    wet = mix[:, 1] - dry[:, 1]
    row = []
    for p, tc, tr in bursts:
        rc = colour(dry[:, 0], wet, tr)
        diffs = [(c - r - an)[hi] for c, r in zip(cc[p], rc)]
        err = float(np.mean(np.abs(np.concatenate(diffs))))
        row.append('p%d %.2f' % (p, err))
        if p not in best or err < best[p][1]:
            best[p] = (dp, err, diffs)
    print('damp %.3f  ' % dp + '  '.join(row), flush=True)
print('best:')
for p in sorted(best):
    dp, err, diffs = best[p]
    print('  p%d damp %.3f err %.2f  early ' % (p, dp, err) + ' '.join('%+5.1f' % v for v in diffs[0])
          + ' | late ' + ' '.join('%+5.1f' % v for v in diffs[1]))

# -*- coding: utf-8 -*-
"""Block 22 tick windows (fit22x.py definitions) with the new early
reflections; the comb output gain is moved to the 150-600 ms level
(iterations), size and damping stay, ER gains = table x scale."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fit22x.py'), encoding='utf-8').read().split('params = {')[0])

SC = [0.73, 0.65, 0.62, 0.63, 0.57, 0.58]
ERG = [0.875, 0.642, 0.660, 0.647, 0.568, 0.750]
params = {0: [0.613, 0.045, 0.347], 1: [0.583, 0.08, 0.587], 2: [0.647, 0.03, 0.620],
          3: [0.692, 0.09, 0.849], 4: [0.672, 0.06, 0.988], 5: [0.694, 0.10, 0.912]}
for it in range(4):
    env = {'EMU8K_RV_P%d' % p: '%.4f,%.3f,%.4f,%.4f' % (v[0], v[1], v[2], ERG[p] * SC[p]) for p, v in params.items()}
    mix22 = render(B22, os.path.join(HERE, 'vmdos27', 'b22_mix.wav'), env)
    wet = mix22[:, 1] - dry22[:, 1]
    tot = 0.0
    for p, tr in sorted(ticks.items()):
        n = int(1.3 * SR)
        rw = np.array(wins(hp(wet[int(tr * SR):int(tr * SR) + n]), dref(hp(dry22[int(tr * SR):int(tr * SR) + n, 0]))))
        d = rw - card_w[p]
        ok = card_w[p] > -45
        tot += np.abs(d[ok]).mean()
        print('it %d p%d gain %.3f diff ' % (it, p, params[p][2]) + ' '.join('%+5.1f' % v for v in d) + '  err %.2f' % np.abs(d[ok]).mean())
        late = [i for i in (3, 4) if ok[i]]
        params[p][2] *= 10 ** (-np.mean(d[late]) / 20 * 0.8)
    print('it %d total %.2f' % (it, tot), flush=True)

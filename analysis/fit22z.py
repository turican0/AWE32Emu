# -*- coding: utf-8 -*-
"""Joint refit of presets 0-5 on the block 22 tick windows with the
line-out damping fixed: size, gain, ER gain and ER late weight, each
iteration one render (EMU8K_RV_P<n> + EMU8K_RV_ERL<n>)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fit22x.py'), encoding='utf-8').read().split('params = {')[0])

params = {0: [0.613, 0.045, 0.347, 0.152, 1.0], 1: [0.583, 0.08, 0.587, 0.114, 1.91],
          2: [0.647, 0.03, 0.620, 0.152, 1.0], 3: [0.692, 0.09, 0.849, 0.129, 1.28],
          4: [0.672, 0.06, 0.988, 0.093, 2.45], 5: [0.694, 0.10, 0.912, 0.107, 2.92]}


def evaluate():
    env = {}
    for p, v in params.items():
        env['EMU8K_RV_P%d' % p] = '%.4f,%.3f,%.4f,%.4f' % tuple(v[:4])
        env['EMU8K_RV_ERL%d' % p] = '%.4f' % v[4]
    mix22 = render(B22, os.path.join(HERE, 'vmdos27', 'b22_mix.wav'), env)
    wet = mix22[:, 1] - dry22[:, 1]
    out = {}
    for p, tr in ticks.items():
        n = int(1.3 * SR)
        out[p] = np.array(wins(hp(wet[int(tr * SR):int(tr * SR) + n]), dref(hp(dry22[int(tr * SR):int(tr * SR) + n, 0]))))
    return out


for it in range(10):
    rw = evaluate()
    tot = 0.0
    for p in sorted(params):
        d = rw[p] - card_w[p]
        ok = card_w[p] > -45
        err = np.abs(d[ok]).mean()
        tot += err
        if it in (0, 9):
            print('it %d p%d  %s  diff %s  err %.2f' % (it, p, ' '.join('%.3f' % v for v in params[p]),
                                                       ' '.join('%+5.1f' % v for v in d), err))
        sz, dp, gn, er, erl = params[p]
        late = [i for i in (3, 4) if ok[i]]
        if late:
            gn *= 10 ** (-np.mean(d[late]) / 20 * 0.8)
        er *= 10 ** (-d[0] / 20 * 0.8)
        erl = max(0.0, erl * 10 ** (-d[1] / 20 * 1.2))
        if ok[5] and ok[3]:
            sz = float(np.clip(sz - 0.012 * (d[5] - d[3]), 0.0, 0.95))
        params[p] = [sz, dp, gn, er, erl]
    print('it %d total %.2f' % (it, tot), flush=True)

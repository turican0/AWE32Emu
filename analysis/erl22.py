# -*- coding: utf-8 -*-
"""Late early-reflection weight (taps >= 25 ms) of the room presets 0-5
against the line out's 25-60 ms window (fit22x.py setup, EMU8K_RV_ERL<n>)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fit22x.py'), encoding='utf-8').read().split('params = {')[0])

late = {p: 1.0 for p in range(6)}
for it in range(5):
    env = {'EMU8K_RV_ERL%d' % p: '%.4f' % v for p, v in late.items()}
    mix22 = render(B22, os.path.join(HERE, 'vmdos27', 'b22_mix.wav'), env)
    wet = mix22[:, 1] - dry22[:, 1]
    tot = 0.0
    for p, tr in sorted(ticks.items()):
        n = int(1.3 * SR)
        rw = np.array(wins(hp(wet[int(tr * SR):int(tr * SR) + n]), dref(hp(dry22[int(tr * SR):int(tr * SR) + n, 0]))))
        d = rw - card_w[p]
        tot += np.abs(d[:4]).sum()
        print('it %d p%d late %.3f  diff %s' % (it, p, late[p], ' '.join('%+5.1f' % v for v in d)))
        # the 25-60 ms window: ER taps and the start of the combs; move the
        # late taps by the window's deficit (damped), never below 0
        late[p] = max(0.0, late[p] * 10 ** (-d[1] / 20 * 1.5))
    print('it %d sum|diff| 0-300 ms %.2f' % (it, tot), flush=True)

# -*- coding: utf-8 -*-
"""Block 40 ticks: dry LEFT channel envelope (card L vs render dry L) and the
return (card R vs render wet R), averaged over the 8 ticks of each preset,
2 ms steps, dB against each own peak. Is the card's slow tail already in the
dry note (release), or only in the chorus return?"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
src_code = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chorus26.py')).read()
exec(src_code.split('for p in range(1, 5):')[0])

HOP = int(0.002 * SR)
N = 110
chans = {'card L': card[:, 0], 'card R': card[:, 1], 'rend L': dry[:, 0], 'rend R': wet[:, 1]}
for p in range(1, 5):
    ticks = [e for e in ev if e[0] == 40 and e[1].startswith('preset %d, tick' % p)]
    print('\npreset %d  (2 ms steps, every 2nd shown, from the tick onset)' % p)
    for name, x in chans.items():
        k = name.split()[0].replace('rend', 'render')
        acc = np.zeros(N)
        for e in ticks:
            tt = when(k, e[2])
            w = x[int(tt * SR):int(tt * SR) + N * HOP]
            acc += np.array([np.mean(w[i * HOP:(i + 1) * HOP] ** 2) for i in range(N)])
        env = 10 * np.log10(np.maximum(acc / acc.max(), 1e-12))
        print('  %-6s ' % name + ' '.join('%3.0f' % v for v in env[0:N:2]))

# -*- coding: utf-8 -*-
"""AWETST28 events of one block with render / card times; with --env PREFIX a
100 ms envelope (dBFS) of card L/R and render L/R from 0.2 s before each
matching event, 5 s long.

    python ev28.py BLOCK [--env PREFIX]
"""
import os
import sys

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align28.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

blk = int(sys.argv[1])
want = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--env' else None
H = int(0.1 * SR)
for e in ev:
    if e.block != blk:
        continue
    print('%-58s render %8.3f  card %8.3f' % (e.text[:58], rtime(e), ctime(e)))
    if want and e.text.startswith(want):
        for name, x, t in (('card L', card[:, 0], ctime(e)), ('card R', card[:, 1], ctime(e)),
                           ('rend L', mix[:, 0], rtime(e)), ('rend R', mix[:, 1], rtime(e))):
            a = int((t - 0.2) * SR)
            s = x[a:a + 50 * H]
            en = 10 * np.log10((s ** 2).reshape(-1, H).mean(axis=1) + 1e-14)
            print('   %-6s ' % name + ' '.join('%4.0f' % v for v in en))

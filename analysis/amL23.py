# -*- coding: utf-8 -*-
"""Block 23 sustained tone, send 255: modulation of the LEFT output (dry +
left chorus tap on both the card and the render, clipping alike), p97-p3
of 20 ms frames, and the mean level against the send-0 twin."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cho23x.py'), encoding='utf-8').read().split('rows = {}')[0])
base = {}
for e in ev:
    if e.block != 23 or 'sustained' not in e.text:
        continue
    p = int(e.text.split()[1].rstrip(','))
    tr = onset(DRY, rtime(e))
    tc = onset(card[:, 0], ctime(e))
    r = hp(mix[int((tr + 0.5) * SR):int((tr + 2.3) * SR), 0])
    c = hp(card[int((tc + 0.5) * SR):int((tc + 2.3) * SR), 0])
    if e.text.endswith('send 0'):
        base[p] = (db(c), db(r))
        continue
    print('p%d  L level vs send 0: card %+5.1f render %+5.1f | modulation card %4.1f render %4.1f dB'
          % (p, db(c) - base[p][0], db(r) - base[p][1], am(c), am(r)))

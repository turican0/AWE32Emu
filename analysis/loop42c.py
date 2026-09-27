# -*- coding: utf-8 -*-
"""AWETST27 block 42 (chorus preset 6 without LFO, feedback 00..C0): card
line out R (chorus return alone) against the render's return (mix - no
chorus sends), right channel. Echo levels of the R tap (24 + n*64 ms after
the tick, 40 ms windows) and the gaps between them, dB against the dry tick
(left, first 30 ms). Two repeats averaged (power)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align27.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

NE = 12
wet_r = mix[:, 1] - nocho[:, 1]
dry_l = nocho[:, 0]


def onset(x, tg):
    a = int((tg - 0.15) * SR)
    seg = x[a:a + int(0.3 * SR)]
    h = int(0.0005 * SR)
    e = 10 * np.log10(np.array([np.mean(seg[i:i + h] ** 2) for i in range(0, len(seg) - h, h)]) + 1e-14)
    return (a + int(np.argmax(e > e.max() - 20)) * h) / SR


def db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-14)


def levels(left, ret, t):
    a = int(t * SR)
    d = db(left[a:a + int(0.030 * SR)])
    echo, gap = [], []
    for n in range(NE):
        s = a + int((0.024 + 0.064 * n) * SR)
        echo.append(db(ret[s:s + int(0.040 * SR)]) - d)
        gap.append(db(ret[s + int(0.042 * SR):s + int(0.062 * SR)]) - d)
    return np.array(echo), np.array(gap)


noise = db(card[int(10 * SR):int(10.5 * SR), 1])
groups = {}
for e in ev:
    if e.block != 42 or 'tick' not in e.text:
        continue
    tr = onset(dry_l, rtime(e))
    tc = onset(card[:, 0], ctime(e))
    groups.setdefault(e.text, []).append((levels(card[:, 0], card[:, 1], tc), levels(dry_l, wet_r, tr),
                                          db(card[int(tc * SR):int(tc * SR) + int(0.03 * SR), 0])))


def pmean(v):
    return 10 * np.log10(np.mean([10 ** (x / 10) for x in v], axis=0))


print('card noise R (0.5 s at 10 s): %.1f dBFS' % noise)
for text, g in groups.items():
    ce, cg = pmean([x[0][0] for x in g]), pmean([x[0][1] for x in g])
    re, rg = pmean([x[1][0] for x in g]), pmean([x[1][1] for x in g])
    floor = noise - np.mean([x[2] for x in g])
    print('%s   (card noise floor rel. %.0f dB)' % (text, floor))
    print('  card  echo ' + ' '.join('%4.0f' % v for v in ce))
    print('  rend  echo ' + ' '.join('%4.0f' % v for v in re))
    print('  card  gap  ' + ' '.join('%4.0f' % v for v in cg))
    print('  rend  gap  ' + ' '.join('%4.0f' % max(v, -99) for v in rg))

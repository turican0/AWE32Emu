# -*- coding: utf-8 -*-
"""Block 22 ticks: internal capture (22234001-004.wav) against the line out
of the same run, same definitions as rev27.py (windows vs the dry tick).
Each internal file is aligned to the render by the envelope correlation
of the left channel over the whole file."""
import glob
import os

import numpy as np
import soundfile as sf

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rev27.py'), encoding='utf-8').read().split("print('windows ms")[0])

files = sorted(glob.glob(os.path.join(TD, '22234*.wav')))
files = [f for f in files if not f.endswith('_ext.wav')]
ints = []
for f in files:
    x = sf.read(f, dtype='float64')[0]
    ex = logenv(x[:, 0], HOP)
    tpl = ex[int(5 * SR / HOP):int(65 * SR / HOP)]
    tpl = tpl - tpl.mean()
    best, bo = -1e30, 0
    for o in range(0, len(_er) - len(tpl)):
        seg = _er[o:o + len(tpl)]
        c = np.dot(tpl, seg - seg.mean())
        if c > best:
            best, bo = c, o
    off = bo * HOP / SR - 5.0          # render time of the file's sample 0
    ints.append((off, off + len(x) / SR, x))
    print('%s: render %.2f .. %.2f s' % (os.path.basename(f), off, off + len(x) / SR))


def internal_at(t):
    for a, b, x in ints:
        if a + 0.2 < t < b - 1.5:
            return x, t - a
    return None, None


print('windows ms: ' + '  '.join('%d-%d' % w for w in WIN))
for e in ev:
    if e.block != 22 or 'tick' not in e.text:
        continue
    p = int(e.text.split()[1].rstrip(','))
    tr = onset(norev[:, 0], rtime(e))
    x, ti = internal_at(tr)
    tc = onset(card[:, 0], ctime(e))
    n = int(1.3 * SR)
    c = hp(card[int(tc * SR):int(tc * SR) + n, 1])
    cw = wins(c, dref(hp(card[int(tc * SR):int(tc * SR) + n, 0])))
    print('preset %d ext   ' % p + ' '.join('%6.1f' % v for v in cw))
    if x is not None:
        ti = onset(x[:, 0], ti)
        s = x[int(ti * SR):int(ti * SR) + n]
        iw = wins(hp(s[:, 1]) * 10 ** (6.5 / 20), dref(hp(s[:, 0])))
        print('         int+6.5' + ' '.join('%6.1f' % v for v in iw) + '   int-ext ' + ' '.join('%+5.1f' % (a - b) for a, b in zip(iw, cw)))

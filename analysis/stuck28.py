# -*- coding: utf-8 -*-
"""Find AWETST28 notes the card did not stop (or start): card L envelope
against the render's dry L after the render's note end. A note is 'stuck'
when the card stays within 12 dB of its own note level for more than 0.3 s
after the render has gone 30 dB down. Also flags notes that are 4 dB+ off
the render level while sounding (missing voice)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align28.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

H = int(0.02 * SR)


def env(x, t, dur):
    a = int(t * SR)
    s = x[a:a + int(dur * SR) // H * H]
    return 10 * np.log10((s ** 2).reshape(-1, H).mean(axis=1) + 1e-14)


evs = [e for e in ev if e.block > 0 and not e.text.startswith('----') and not e.text.startswith('MINUTE')]
for i, e in enumerate(evs):
    nxt = evs[i + 1] if i + 1 < len(evs) else None
    dur = (rtime(nxt) - rtime(e)) if nxt else 3.0
    er = env(nofx[:, 0], rtime(e), dur)
    ec = env(card[:, 0], ctime(e), dur)
    lev_r = er[2:6].max()
    lev_c = ec[2:6].max()
    off = int(np.argmax(er < lev_r - 30)) if (er < lev_r - 30).any() else len(er)
    stuck = (ec[off:] > lev_c - 12).sum() * 0.02 if off < len(er) else 0.0
    flag = []
    if stuck > 0.3:
        flag.append('STUCK %.1f s' % stuck)
    print('%2d %-58s %s' % (e.block, e.text[:58], ' '.join(flag))) if flag else None
print('done')

# -*- coding: utf-8 -*-
"""AWETST27 block 23 (chorus, 8 presets) on the LINE OUT: card R = chorus
return alone, render return = mix - no chorus sends (right channel).

  short   700 ms tone (decay 0x60, sustain 0) with send 48/96/160/255:
          return energy 0-1.2 s against the dry L energy of the same note
  sus     2.5 s sustained tone, send 255: return level against the dry L of
          the send-0 twin (0.5-2.3 s), and the modulation (p97-p3 of 20 ms
          frames, dB)
"""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align27.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

WET = mix[:, 1] - nocho[:, 1]
DRY = nocho[:, 0]


def hp(x, fc=100.0):
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[f < fc] = 0
    return np.fft.irfft(X, len(x), axis=0)


def onset(x, tg):
    a = int((tg - 0.15) * SR)
    seg = x[a:a + int(0.3 * SR)]
    h = int(0.0005 * SR)
    e = 10 * np.log10(np.array([np.mean(seg[i:i + h] ** 2) for i in range(0, len(seg) - h, h)]) + 1e-14)
    return (a + int(np.argmax(e > e.max() - 20)) * h) / SR


def db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-14)


def am(x):
    h = int(0.02 * SR)
    n = len(x) // h
    f = 10 * np.log10((x[:n * h] ** 2).reshape(n, h).mean(axis=1) + 1e-14)
    return np.percentile(f, 97) - np.percentile(f, 3)


rows = {}
susl = {}
dref0 = {}
for e in ev:
    if e.block != 23:
        continue
    t = e.text
    if not t.startswith('preset'):
        continue
    p = int(t.split()[1].rstrip(','))
    tr = onset(DRY, rtime(e))
    tc = onset(card[:, 0], ctime(e))
    if 'sustained' in t:
        a, b = 0.5, 2.3
        c = hp(card[int((tc + a) * SR):int((tc + b) * SR)])
        r = np.stack([hp(DRY[int((tr + a) * SR):int((tr + b) * SR)]), hp(WET[int((tr + a) * SR):int((tr + b) * SR)])], axis=1)
        if t.endswith('send 0'):
            susl[p] = (db(c[:, 0]), db(r[:, 0]))
        else:
            cl, rl = susl[p]
            rows.setdefault(p, {})['sus'] = (db(c[:, 1]) - cl, db(r[:, 1]) - rl, am(c[:, 1]), am(r[:, 1]))
    else:
        s = int(t.split()[-1])
        c = hp(card[int(tc * SR):int((tc + 1.2) * SR)])
        rl = hp(DRY[int(tr * SR):int((tr + 1.2) * SR)])
        rr = hp(WET[int(tr * SR):int((tr + 1.2) * SR)])
        if s == 0:            # dry reference: the send-0 twin (card L has no return then)
            dref0[p] = (db(c[:, 0]), db(rl))
            continue
        rows.setdefault(p, {})[s] = (db(c[:, 1]) - dref0[p][0], db(rr) - dref0[p][1])

print('return vs dry, dB (card / render / diff)')
for p in sorted(rows):
    r = rows[p]
    sh = '  '.join('s%-3d %+5.1f/%+5.1f %+4.1f' % (s, r[s][0], r[s][1], r[s][0] - r[s][1]) for s in (48, 96, 160, 255) if s in r)
    su = r.get('sus')
    print('p%d %s' % (p, sh))
    if su:
        print('   sustained: level %+5.1f/%+5.1f %+4.1f   modulation card %.1f dB, render %.1f dB' % (su[0], su[1], su[0] - su[1], su[2], su[3]))

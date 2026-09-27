# -*- coding: utf-8 -*-
"""Note-on of attack 0x7F (every AWETEST tone) on the card's line out:
AWETST28 block 44 dry noise notes (interpolation noise at all pitches,
filter anchors and open cutoffs). The card and the render play the same
ROM noise, so the card is aligned to the render by the cross-correlation
of the waveform (sample exact, no threshold). Power in 0.25 ms bins from
the render's first sample, relative to each note's steady power (50-150
ms), averaged over the notes (power), card vs render, and the amplitude
ratio card / render."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align28.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

H = int(0.00025 * SR)
NB = 60
sel = []
for e in ev:
    if e.block != 44:
        continue
    t = e.text
    if t.startswith('interpolation: noise') or 'anchor' in t or ('filter: noise, cutoff 224, Q 0' in t) or ('filter: noise, cutoff 192, Q 0' in t):
        sel.append(e)
accc, accr, used = np.zeros(NB), np.zeros(NB), 0
for e in sel:
    tr = rtime(e)
    a = int((tr - 0.05) * SR)
    r = nofx[a:a + int(0.25 * SR), 0]
    nz = np.nonzero(np.abs(r) > 1e-6)[0]
    if not len(nz):
        continue
    r0 = a + nz[0]                                   # render note start (sample exact)
    tc = ctime(e)
    c_a = int((tc - 0.05) * SR)
    # align: correlate the steady part (20-150 ms after the start)
    rs = nofx[r0 + int(0.02 * SR):r0 + int(0.15 * SR), 0]
    best, lag = -1, 0
    base = c_a + (r0 - a)
    for L in range(-int(0.004 * SR), int(0.004 * SR)):
        cs = card[base + L + int(0.02 * SR):base + L + int(0.15 * SR), 0]
        v = np.dot(rs, cs) / (np.linalg.norm(rs) * np.linalg.norm(cs) + 1e-12)
        if v > best:
            best, lag = v, L
    if best < 0.8:
        continue
    c0 = base + lag
    cst = np.mean(card[c0 + int(0.05 * SR):c0 + int(0.15 * SR), 0] ** 2)
    rst = np.mean(nofx[r0 + int(0.05 * SR):r0 + int(0.15 * SR), 0] ** 2)
    ce = (card[c0:c0 + NB * H, 0] ** 2).reshape(NB, H).mean(axis=1) / cst
    re = (nofx[r0:r0 + NB * H, 0] ** 2).reshape(NB, H).mean(axis=1) / rst
    accc += ce
    accr += re
    used += 1
print('notes used %d of %d (waveform correlation >= 0.8)' % (used, len(sel)))
cdb = 10 * np.log10(accc / used + 1e-12)
rdb = 10 * np.log10(accr / used + 1e-12)
print('ms      ' + ' '.join('%5.2f' % (i * 0.25) for i in range(0, NB, 2)))
print('card    ' + ' '.join('%5.1f' % v for v in cdb[::2]))
print('render  ' + ' '.join('%5.1f' % v for v in rdb[::2]))
print('amp c/r ' + ' '.join('%5.2f' % v for v in 10 ** ((cdb - rdb) / 20)[::2]))

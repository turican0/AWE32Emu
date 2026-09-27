# -*- coding: utf-8 -*-
"""Attack shape: card vs render on identical register writes.

Block 8: attack rate r, sine at full level, no hold/decay. Both envelopes
(5 ms RMS, amplitude) are normalised to their own plateau and sampled at
fractions of the formula time T = 11.878 s / RateDivisor(r - 1). Printed:
amplitude at t/T = 0.1 .. 1.2 for card and render, and the card's
"effective exponent" p from amp = (t/T)^p fitted on 0.1 .. 0.9.
Block 31: note off during a slow attack (0x58) - level at release time.

    python attack25.py [--ours replay_cham.wav]
"""
import argparse
import sys

import numpy as np
import soundfile as sf

import replay25 as R
import awe25

ap = argparse.ArgumentParser()
ap.add_argument('--ours', default='replay_cham.wav')
a = ap.parse_args()

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)
SR = 44100
H = 0.005


def rd(i):
    g, m = (i >> 4) & 7, i & 15
    return (m + 1) if g == 0 else ((m + 17) << (g - 1))


def amp(x):
    h = int(H * SR)
    n = len(x) // h * h
    return np.sqrt((x[:n].astype(np.float64) ** 2).reshape(-1, h).mean(axis=1))


def pair(blk, t0, t1):
    cnt = {}
    for ev in awe25.events(blk):
        i = cnt.get(ev.text, 0)
        cnt[ev.text] = i + 1
        m = vmk.get((blk, ev.text, i))
        if m is None or m.rt is None:
            continue
        fr = k * m.rt + q
        c = awe25.seg(ev, t0, t1)
        a0 = int(round(fr + t0 * SR))
        o = ours[max(a0, 0):max(a0, 0) + len(c)]
        if len(o) == len(c):
            yield ev.text, c, o


FR = np.array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2])
print('block 8: amplitude / plateau at t/T (card | render)')
print('%-12s %s' % ('rate', ' '.join('%5.1f' % f for f in FR)))
ps = []
for text, c, o in pair(8, 0.0, 6.5):
    if not text.startswith('attack'):
        continue
    r = int(text.split('0x')[1], 16)
    T = 11.878 / rd(r - 1)
    if T < 0.08 or T > 3.5:
        continue
    ac, ao = amp(c), amp(o)
    end = int((T * 1.4 + 0.3) / H)
    if end + 20 > len(ac):
        continue
    pc = np.median(ac[end:end + 20])
    po = np.median(ao[end:end + 20])
    idx = np.clip((FR * T / H).astype(int), 0, len(ac) - 1)
    vc, vo = ac[idx] / pc, ao[idx] / po
    print('0x%02X card   %s' % (r, ' '.join('%5.2f' % v for v in vc)))
    print('     render %s' % ' '.join('%5.2f' % v for v in vo))
    m = (FR <= 0.9) & (vc > 0.02)
    p = np.polyfit(np.log(FR[m]), np.log(vc[m]), 1)[0]
    ps.append(p)
print('card effective exponent p (amp = (t/T)^p): median %.3f, IQR %.3f..%.3f' % (
    np.median(ps), np.percentile(ps, 25), np.percentile(ps, 75)))

print('\nblock 31: note off during attack 0x58 / decay 0x58 / sustain 0x50')
for text, c, o in pair(31, 0.0, 3.5):
    if not text.startswith('note off'):
        continue
    ms = int(text.split('after')[1].split()[0])
    ac, ao = amp(c), amp(o)
    pc, po = np.max(ac), np.max(ao)
    i = int(ms / 1000.0 / H)
    print('  off after %4d ms: level at off card %.3f render %.3f (of own max); max card %.1f dB vs render %.1f dB'
          % (ms, ac[min(i, len(ac) - 1)] / pc, ao[min(i, len(ao) - 1)] / po,
             20 * np.log10(pc + 1e-9), 20 * np.log10(po + 1e-9)))

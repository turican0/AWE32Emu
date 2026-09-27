# -*- coding: utf-8 -*-
"""Mod envelope and filter modulation, card internal capture vs render.

(a) Block 30, mod attack notes: the render's brightness (3-12 kHz vs
    0.2-1.5 kHz, 256-sample frames, 5 ms hop) is a known function of its
    mod level m(t) = t/T. That relation, taken from the slowest note, is
    inverted to read the card's mod level from the card's brightness. Both
    are referenced to their own final brightness, so the capture tilt
    cancels. Printed: card mod level at t/T.
(b) Blocks 15 (PEFE -> filter) and 19 (LFO1 -> filter, FMMOD low byte):
    1/3-octave spectrum of every note relative to the depth-0 note of the
    same path (exactly tilt-free), then card - render per band.

    python modfilt.py [--ours replay_rev.wav]
"""
import argparse

import numpy as np
import soundfile as sf

import replay25 as R
import ext25

ap = argparse.ArgumentParser()
ap.add_argument('--ours', default='replay_rev.wav')
a = ap.parse_args()

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)
SR = 44100


def pairs(blk, t0, t1):
    cnt = {}
    for ev in ext25.events(blk):
        i = cnt.get(ev.text, 0)
        cnt[ev.text] = i + 1
        m = vmk.get((blk, ev.text, i))
        if m is None or m.rt is None:
            continue
        c = ext25.seg(ev, t0, t1).astype(np.float64)
        fr = int(round(k * m.rt + q + t0 * SR))
        o = ours[fr:fr + len(c)].astype(np.float64)
        if len(o) == len(c):
            yield ev.text, c, o


def rd(i):
    g, m = (i >> 4) & 7, i & 15
    return (m + 1) if g == 0 else ((m + 17) << (g - 1))


# ------------------------------------------------------------------ (a)
N, HOP = 256, int(0.005 * SR)
fq = np.fft.rfftfreq(N, 1.0 / SR)
HI, LO = (fq >= 3000) & (fq < 12000), (fq >= 200) & (fq < 1500)
W = np.hanning(N)


def bright(x):
    out = []
    for i in range(0, len(x) - N, HOP):
        s = np.abs(np.fft.rfft(x[i:i + N] * W)) ** 2
        out.append(10 * np.log10((s[HI].sum() + 1e-12) / (s[LO].sum() + 1e-12)))
    b = np.convolve(np.array(out), np.ones(5) / 5.0, 'same')      # 25 ms smoothing
    return b - np.median(b[int(1.8 / 0.005):int(2.2 / 0.005)])


notes = []
for text, c, o in pairs(30, 0.0, 2.3):
    if text.startswith('mod attack'):
        r = int(text.split('0x')[1][:2], 16)
        notes.append((r, 11.878 / rd(r - 1), bright(c), bright(o)))
notes.sort()
r0, T0, _, bo0 = notes[0]
t = np.arange(len(bo0)) * 0.005 + N / 2.0 / SR
m_r = np.clip(t / T0, 0, 1)
sel = t < T0 * 0.98
order = np.argsort(bo0[sel])
bmap, mmap = bo0[sel][order], m_r[sel][order]
bmap = np.maximum.accumulate(bmap)                    # monotonic for the inversion
FR = [0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8]
print('(a) block 30: mod level read from brightness (map from render 0x%02X, T %.2f s)' % (r0, T0))
print('    brightness map: m 0.1 %+.0f dB, 0.3 %+.0f, 0.5 %+.0f, 0.7 %+.0f, 0.9 %+.0f' % tuple(
    np.interp(v, mmap, bmap) for v in (0.1, 0.3, 0.5, 0.7, 0.9)))
print('    %-12s %s' % ('rate', ' '.join('%5.2f' % f for f in FR)))
for r, T, bc, bo in notes:
    tt = np.arange(len(bc)) * 0.005 + N / 2.0 / SR
    mc = [np.interp(bc[min(int((f * T - N / 2.0 / SR) / 0.005), len(bc) - 1)], bmap, mmap) for f in FR]
    mo = [np.interp(bo[min(int((f * T - N / 2.0 / SR) / 0.005), len(bo) - 1)], bmap, mmap) for f in FR]
    print('    0x%02X card   %s   (T %.2f s)' % (r, ' '.join('%5.2f' % v for v in mc), T))
    print('         render %s' % ' '.join('%5.2f' % v for v in mo))

# ------------------------------------------------------------------ (b)
BANDS = 1000.0 * 2 ** (np.arange(-9, 13) / 3.0)
BANDS = BANDS[BANDS <= 17000]


def spec(x):
    n = 4096
    acc = np.zeros(n // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - n + 1, n // 2):
        acc += np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
        cnt += 1
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.array([10 * np.log10(acc[(f >= b / 2 ** (1 / 6.0)) & (f < b * 2 ** (1 / 6.0))].mean() / max(cnt, 1) + 1e-20)
                     for b in BANDS])


SHOW = [250, 1000, 2000, 4000, 6300, 8000, 10000, 12500, 16000]
idx = [int(np.argmin(abs(BANDS - s))) for s in SHOW]
for blk, zero_key, label in ((15, 'depth 0,', 'PEFE -> filter'), (19, 'FMMOD 0x0000', 'LFO1 -> filter')):
    rows = [(text, spec(c[int(0.1 * SR):int(1.7 * SR)]), spec(o[int(0.1 * SR):int(1.7 * SR)]))
            for text, c, o in pairs(blk, 0.0, 1.8) if not text.startswith('reference') and not text.startswith('MINUTE')]
    z = [r for r in rows if zero_key in r[0]]
    if not z:
        print('(b) block %d: no depth-0 note' % blk)
        continue
    _, zc, zo = z[0]
    print('\n(b) block %d %s: (card - card@0) - (render - render@0) per band [dB]; render - render@0 in brackets' % (blk, label))
    print('    %-44s %s' % ('note', ' '.join('%6.0f' % s for s in SHOW)))
    for text, sc, so in rows:
        d = (sc - zc) - (so - zo)
        print('    %-44s %s  [%s]' % (text[:44], ' '.join('%+6.1f' % d[i] for i in idx),
                                     ' '.join('%+.0f' % (so - zo)[i] for i in idx)))

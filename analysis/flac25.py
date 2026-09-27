# -*- coding: utf-8 -*-
"""The card's internal capture (reca) against the external line recording (awetst25.flac).

The same note in both: the only difference is the path - the internal loop
(MIDI.L -> SB16 ADC) against the line output and the tester's recording
card. What differs here is not a chip property.

Mapping: FLAC time = a * rt + b. The anchors are the reference tones of
blocks 1..11 (line "reference tone"), predicted from the first anchor
(reca001 lies in the FLAC from 21.76 s), then refined by an envelope
correlation +-3 s and a line fit.

    python flac25.py
"""
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, r'C:\prenos\AWE32Emu\tools\awetest')
import awe25  # noqa: E402
from awe25 import SR, events, seg  # noqa: E402

awe25.LATENCY = 0.003
FLAC = r'C:\Users\vesely\Downloads\awetst25.flac'
_flac = sf.SoundFile(FLAC)


def flac_seg(t0, t1):
    a = max(int(t0 * SR), 0)
    _flac.seek(a)
    x = _flac.read(int((t1 - t0) * SR), dtype='float32')
    return x[:, 0].astype(np.float64)


def env10(x):
    h = 441
    n = len(x) // h * h
    v = np.log((x[:n] ** 2).reshape(-1, h).mean(axis=1) + 1e-10)
    return v - v.mean()


# ---- anchors: reference tones of blocks 1..11 in the reca run
anchors = []
first = None
for blk in range(1, 12):
    evs = [e for e in events(blk, 'reference tone', run='reca')]
    if not evs:
        continue
    e = evs[0]
    if first is None:
        # reca001 starts in the FLAC at 21.76 s (envelope correlation 0.86)
        first = (e.rt, 21.76 + e.t) if 'reca001' in e.path else None
        if first is None:
            continue
    pred = first[1] + (e.rt - first[0])
    card = env10(seg(e, -1.0, 4.0).astype(np.float64))
    fl = env10(flac_seg(pred - 4.0, pred + 7.0))
    c = np.correlate(fl, card, 'valid')
    k = int(np.argmax(c))
    t_found = pred - 4.0 + k / 100.0 + 1.0
    r = np.corrcoef(fl[k:k + len(card)], card)[0, 1]
    anchors.append((e.rt, t_found, r))
    print('block %2d: predicted %.2f s, found %.2f s, correlation %.2f' % (blk, pred, t_found, r))
good = [(rt, t) for rt, t, r in anchors if r > 0.6]
x = np.array([g[0] for g in good])
y = np.array([g[1] for g in good])
A, B = np.polyfit(x, y, 1)
res = y - (A * x + B)
print('FLAC = %.6f * rt + %.3f, anchors %d, residual max %.1f ms' % (A, B, len(good), 1000 * np.max(np.abs(res))))


def fseg(ev, t0, t1):
    t = A * ev.rt + B
    # fine refinement +-20 ms by correlation with the internal capture
    c = seg(ev, t0, t1).astype(np.float64)
    f = flac_seg(t + t0 - 0.02, t + t1 + 0.02)
    ce = np.abs(c)
    fe = np.abs(f)
    cc = np.correlate(fe[:len(fe)], ce, 'valid')
    k = int(np.argmax(cc))
    return c, f[k:k + len(c)]


def band_db(x, bands):
    n = 4096
    win = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - n + 1, n // 2):
        acc += np.abs(np.fft.rfft(x[i:i + n] * win)) ** 2
        cnt += 1
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.array([10 * np.log10(acc[(f >= b / 2 ** (1 / 6.0)) & (f < b * 2 ** (1 / 6.0))].mean() / max(cnt, 1) + 1e-20)
                     for b in bands])


def lev(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-14)


# ---- block 4: sine level by pitch, internal - line
print('\n=== block 4 sine: internal - line (dB), per semitone')
out = []
for e in events(4, 'semitone', run='reca'):
    s = int(e.text.split()[1].rstrip(','))
    c, f = fseg(e, 0.08, 0.38)
    out.append((s, lev(c) - lev(f)))
ref = [d for s, d in out if s == 0][0]
print('  ' + ' '.join('%d:%+.1f' % (s, d - ref) for s, d in out))

# ---- block 5: noise spectrum, internal - line
BANDS = 1000.0 * 2 ** (np.arange(-12, 13) / 3.0)
BANDS = BANDS[BANDS <= 17000]
print('\n=== block 5 noise: spectrum internal - line (median over notes, normalised to 1 kHz)')
mats = []
for e in events(5, 'semitone', run='reca'):
    c, f = fseg(e, 0.05, 0.35)
    mats.append(band_db(c, BANDS) - band_db(f, BANDS))
med = np.median(np.array(mats), axis=0)
med -= med[np.argmin(abs(BANDS - 1000))]
print('  Hz  ' + ' '.join('%6.0f' % b for b in BANDS))
print('  dB  ' + ' '.join('%+6.1f' % v for v in med))

# ---- block 3: pan in both paths
print('\n=== block 3 pan: level against pan 128 - internal / line')
rows = []
for e in events(3, 'pan', run='reca'):
    if 'other' in e.text:
        continue
    p = int(e.text.split()[-1])
    c, f = fseg(e, 0.08, 0.38)
    rows.append((p, lev(c), lev(f)))
c128 = [r for r in rows if r[0] == 128][0]
print('  ' + ' '.join('%d:%+.1f/%+.1f' % (p, lc - c128[1], lf - c128[2]) for p, lc, lf in rows[::2]))

# ---- block 6: highs by cutoff in both paths
print('\n=== block 6 noise through the filter: bands 4k/8k/12.7k against 1 kHz - internal / line')
for e in events(6, 'cutoff', run='reca'):
    cut = int(e.text.split()[1])
    if cut % 32:
        continue
    c, f = fseg(e, 0.05, 0.45)
    bc = band_db(c, BANDS)
    bf = band_db(f, BANDS)
    i1 = np.argmin(abs(BANDS - 1000))
    vals = []
    for hz in (4000, 8000, 12700):
        j = np.argmin(abs(BANDS - hz))
        vals.append('%+5.1f/%+5.1f' % (bc[j] - bc[i1], bf[j] - bf[i1]))
    print('  cutoff %3d: %s' % (cut, '  '.join(vals)))

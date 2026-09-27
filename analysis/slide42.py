# -*- coding: utf-8 -*-
"""Volume slide after the note-off (DCYSUSV 0x0080 + VTFT volume target 0)
on the card: block 42, feedback 0x00, card R = the chorus R tap (24 ms copy
of the voice). Hilbert envelope (band-passed around the tick pitch),
0.1 ms steps, -2..+12 ms around the note-off, dB against the level before.
Card vs render (render R return)."""
import os

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loop42c.py'), encoding='utf-8').read().split('noise = db(')[0])

F = {'IP 49152': 183.75, 'IP 57344': 735.0, 'IP 65535': 2939.5}


def henv(x, f0):
    n = len(x)
    X = np.fft.rfft(x * np.hanning(n) ** 0 )
    fr = np.fft.rfftfreq(n, 1.0 / SR)
    X[(fr < f0 / 1.6) | (fr > f0 * 1.6)] = 0
    full = np.zeros(n, complex)
    full[:len(X)] = X
    full[1:len(X)] *= 2
    return np.abs(np.fft.ifft(full))


out = {}
for e in ev:
    if e.block != 42 or not e.text.startswith('feedback 0x00'):
        continue
    ip = e.text.split(',')[1].replace('tick', '').strip()
    for k, x, t in (('card', card[:, 1], onset(card[:, 0], ctime(e))), ('rend', wet_r, onset(dry_l, rtime(e)))):
        a = int((t + 0.024 + 0.030 - 0.020) * SR)
        seg = x[a:a + int(0.060 * SR)]
        h = henv(seg, F[ip])
        # locate the drop: the steepest fall of the smoothed envelope
        out.setdefault((ip, k), []).append(h)
for (ip, k), v in sorted(out.items()):
    h = np.mean(v, axis=0)
    d = 20 * np.log10(h + 1e-9)
    pre = np.median(d[int(0.005 * SR):int(0.015 * SR)])
    i0 = int(0.010 * SR) + int(np.argmax(d[int(0.010 * SR):int(0.030 * SR)] < pre - 3))
    idx = [i0 + int(ms * SR / 1000) for ms in np.arange(-1, 12, 0.5)]
    print('%-9s %-5s ' % (ip, k) + ' '.join('%4.0f' % (d[i] - pre) for i in idx))
print('columns: -1.0 .. +11.5 ms in 0.5 ms steps from the -3 dB point')

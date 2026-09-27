# -*- coding: utf-8 -*-
"""AWETST25 at the tester: capture delay and probes of the chip's control clock.

Block 1 (reca) and block 39 (recc) play the same:
  reference sine 3 s          -> frequency (sample clock), level
  tremolo TREMFRQ 0x7F40 3 s  -> LFO1 0x40, [PG] 2.698 Hz
  hold 0x6F decay 0x50 sus 0x20 2.6 s -> hold 16 steps = 1472 ms [PG]
  vibrato FM2FRQ2 0x7F40 3 s  -> LFO2 0x40, [PG] 2.698 Hz
  envvol 0x8000-1200 (tick before the note) -> delay 870 ms [PG]
In run5 LFO1 came out 0.907x [PG] (a slow clock). Here it is decided.
The capture delay is measured on the notes of block 2 (onset from silence,
400/600 ms).
"""
import sys

import numpy as np

sys.path.insert(0, r'C:\prenos\AWE32Emu\tools\awetest')
import awe25  # noqa: E402
from awe25 import SR, env_db, events, freq_zc, seg  # noqa: E402

# ---- delay: onset of the block 2 notes (attenuation 0..40, well above the noise) ----
lat = []
for ev in events(2, 'atten'):
    a = int(ev.text.split()[1])
    if a > 40:
        continue
    x = seg(ev, -0.2, 0.3)
    e = env_db(x, 0.001)
    fl = np.median(e[:150])
    pk = np.max(e[200:450])
    thr = fl + 0.5 * (pk - fl)
    idx = np.nonzero(e[150:] > thr)[0]
    if len(idx):
        lat.append((150 + idx[0]) * 0.001 - 0.2)
lat = np.array(lat)
print('onset delay after the stamp (block 2, %d notes): median %.1f ms, IQR %.1f..%.1f ms'
      % (len(lat), 1000 * np.median(lat), 1000 * np.percentile(lat, 25), 1000 * np.percentile(lat, 75)))
awe25.LATENCY = float(np.median(lat))


def lfo_rate(track, fs, fmin=0.5, fmax=12.0):
    """Frequency of the dominant periodicity of a course (zero-padded FFT + parabola)."""
    t = track - np.mean(track)
    t = t * np.hanning(len(t))
    n = 1 << 18
    s = np.abs(np.fft.rfft(t, n))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    m = (f >= fmin) & (f <= fmax)
    k = np.nonzero(m)[0][np.argmax(s[m])]
    a, b, c = np.log(s[k - 1:k + 2] + 1e-12)
    p = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + p) * (f[1] - f[0])


for blk in (1, 39):
    ev = events(blk)
    print('\n=== block %d (%s)' % (blk, ev[0].run if ev else '-'))
    for e in ev:
        print('  %s' % e.text)
        if e.text.startswith('reference sine'):
            x = seg(e, 0.5, 2.5)
            print('     frequency %.3f Hz (ROM sine 44100/65 = 678.46), RMS %.1f dBFS'
                  % (freq_zc(x), 10 * np.log10(np.mean(x.astype(float) ** 2))))
        elif e.text.startswith('clock probe: tremolo'):
            x = seg(e, 0.3, 2.9)
            env = env_db(x, 0.005)
            r = lfo_rate(env, 200.0)
            print('     LFO1 %.4f Hz -> %.4f x [PG] 2.698; depth %.1f dB peak to peak'
                  % (r, r / 2.698, np.percentile(env, 97) - np.percentile(env, 3)))
        elif e.text.startswith('clock probe: vibrato'):
            x = seg(e, 0.3, 2.9)
            w = int(0.02 * SR)
            fr = np.array([freq_zc(x[i:i + w]) or np.nan for i in range(0, len(x) - w, int(0.005 * SR))])
            fr = fr[np.isfinite(fr)]
            r = lfo_rate(fr, 200.0)
            print('     LFO2 %.4f Hz -> %.4f x [PG]; pitch %.1f..%.1f Hz (%.0f cents)'
                  % (r, r / 2.698, np.percentile(fr, 3), np.percentile(fr, 97),
                     1200 * np.log2(np.percentile(fr, 97) / np.percentile(fr, 3))))
        elif e.text.startswith('clock probe: hold'):
            x = seg(e, 0.0, 2.6)
            env = env_db(x, 0.005)
            plateau = np.median(env[10:60])
            inside = np.nonzero(env > plateau - 3.0)[0]
            end = inside[-1] * 0.005 if len(inside) else float('nan')
            # slope of the drop after the hold
            after = np.nonzero(env < plateau - 6.0)[0]
            print('     hold (last point within 3 dB of the plateau) %.0f ms -> %.4f x [PG] 1472 ms'
                  % (1000 * end, end / 1.472))
        elif e.text.startswith('clock probe: envelope delay'):
            x = seg(e, -0.3, 1.6)
            en = env_db(x, 0.002)
            fl = np.median(en[:100])
            pk = np.max(en)
            idx = np.nonzero(en[160:] > fl + 0.5 * (pk - fl))[0]
            d = (160 + idx[0]) * 0.002 - 0.3 if len(idx) else float('nan')
            print('     onset delay %.0f ms -> %.4f x [PG] 870 ms' % (1000 * d, d / 0.870))

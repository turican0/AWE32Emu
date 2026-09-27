# -*- coding: utf-8 -*-
"""Fit our chorus (Emu8000Fx::Chorus: two modulated delay lines with
feedback, left = 0.7 a + 0.3 b) to the card, AWETST25 block 23 (sustained
2.5 s tone, send 0 and 255 per preset; internal capture only).

The preset data are the driver's (kChorusPresets); fitted are GLOBAL scales:
  Kd  depth in ms for depth byte 255 (now 6.0)
  Kf  feedback multiplier (now 1.0)
  Kr  LFO rate multiplier (now 1.0)
  G   wet gain (absorbs return 0.7, pan and EQ; reported as x current)
Model: y = dry + G * wet(dry), dry = our render of the send 0 tone. Metrics
per preset as in fx_ext.py: level send 255 vs 0 over 0.5-2.3 s and
modulation = (p95 - p5 of the 20 ms level) minus the dry's.

The delay line with feedback is computed in vector blocks shorter than the
minimum delay (every read then hits already written samples).

    python fit_cho.py [--ours replay_fx.wav]
"""
import argparse

import numpy as np
import soundfile as sf

import replay25 as R
import ext25

ap = argparse.ArgumentParser()
ap.add_argument('--ours', default='replay_fx.wav')
a = ap.parse_args()

SR = 44100
PRE = 0.3
T0, T1 = 0.5, 2.3
PRESETS = [(0xE600, 0x03F6, 0xBC2C, 0x006D), (0xE608, 0x031A, 0xBC6E, 0x017C),
           (0xE610, 0x031A, 0xBC84, 0x0083), (0xE620, 0x0269, 0xBC6E, 0x017C),
           (0xE680, 0x04D3, 0xBCA6, 0x005B), (0xE6E0, 0x044E, 0xBC37, 0x0026),
           (0xE600, 0x0B06, 0xBC00, 0x0083), (0xE6C0, 0x0B06, 0xBC00, 0x0083)]

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)


def collect():
    cnt, out = {}, {}
    for ev in ext25.events(23):
        i = cnt.get(ev.text, 0)
        cnt[ev.text] = i + 1
        if 'sustained' not in ev.text:
            continue
        m = vmk.get((23, ev.text, i))
        if m is None or m.rt is None:
            continue
        c = ext25.seg(ev, -PRE, T1 + 0.1).astype(np.float64)
        fr = int(round(k * m.rt + q - PRE * SR))
        o = ours[fr:fr + len(c)].astype(np.float64)
        nz = np.mean(c[int(0.02 * SR):int((PRE - 0.02) * SR)] ** 2)
        p = int(ev.text.split()[1].rstrip(','))
        s = int(ev.text.split('send')[1])
        out[(p, s)] = (c[int(PRE * SR):], nz, o[int(PRE * SR):])
    return out


def voice(x, base, depth, rate, phase, fb):
    n = len(x)
    t = np.arange(n)
    rd = t - (base + depth * np.sin(phase + 2 * np.pi * rate / SR * t))
    line = np.zeros(n)
    out = np.zeros(n)
    step = max(int(base - depth) - 2, 1)
    for s in range(0, n, step):
        e = min(s + step, n)
        r = rd[s:e]
        i0 = np.floor(r).astype(int)
        f = r - i0
        ok = i0 >= 0
        i0c = np.clip(i0, 0, n - 2)
        v = line[i0c] + (line[i0c + 1] - line[i0c]) * f
        out[s:e] = np.where(ok, v, 0.0)
        line[s:e] = x[s:e] + out[s:e] * fb
    return out


def wet(x, p, Kd, Kf, Kr):
    fbw, dly, dep, lfo = PRESETS[p]
    base = float(dly)
    depth = (dep & 0xFF) / 255.0 * Kd * SR / 1000.0
    rate = lfo * 0.0073 * Kr
    fb = min((fbw & 0xFF) / 255.0 * Kf, 0.98)
    va = voice(x, base, depth, rate, 0.0, fb)
    vb = voice(x, base * 1.4, depth, rate * 0.8, np.pi, fb)
    return 0.7 * va + 0.3 * vb


def blocks(x):
    h = int(0.02 * SR)
    n = len(x) // h * h
    return x[:n].reshape(-1, h)


GS = np.geomspace(0.02, 5.0, 300)


def model(dry, w):
    """level and modulation for every G"""
    sl = slice(int(T0 * SR), int(T1 * SR))
    d, ww = dry[sl], w[sl]
    Edd, Edw, Eww = np.sum(d * d), np.sum(d * ww), np.sum(ww * ww)
    lev = 10 * np.log10(np.maximum(Edd + 2 * GS * Edw + GS ** 2 * Eww, 1e-20) / Edd)
    bd, bw = blocks(d), blocks(ww)
    edd, edw, eww = (bd * bd).sum(1), (bd * bw).sum(1), (bw * bw).sum(1)
    e = 10 * np.log10(np.maximum(edd[None] + 2 * GS[:, None] * edw[None] + GS[:, None] ** 2 * eww[None], 1e-20))
    ed = 10 * np.log10(edd + 1e-20)
    mod = (np.percentile(e, 95, axis=1) - np.percentile(e, 5, axis=1)) - (np.percentile(ed, 95) - np.percentile(ed, 5))
    return lev, mod


data = collect()
tg = {}
dry = {}
for p in range(8):
    if (p, 0) not in data or (p, 255) not in data:
        continue
    (c0, n0, o0), (c1, n1, o1) = data[(p, 0)], data[(p, 255)]
    sl = slice(int(T0 * SR), int(T1 * SR))
    lev = 10 * np.log10((np.mean(c1[sl] ** 2) - n1) / (np.mean(c0[sl] ** 2) - n0))
    e1 = 10 * np.log10(blocks(c1[sl]).__pow__(2).mean(1) + 1e-14)
    e0 = 10 * np.log10(blocks(c0[sl]).__pow__(2).mean(1) + 1e-14)
    mod = (np.percentile(e1, 95) - np.percentile(e1, 5)) - (np.percentile(e0, 95) - np.percentile(e0, 5))
    tg[p] = (lev, mod)
    dry[p] = o0

# calibration: the current exe render against the simulation with current scales
print('calibration (Kd 6, Kf 1, Kr 1): G0 and residual of the exe wet')
G0 = []
for p in tg:
    w = wet(dry[p], p, 6.0, 1.0, 1.0)
    wr = data[(p, 255)][2] - dry[p]
    sl = slice(int(T0 * SR), int(T1 * SR))
    g0 = np.dot(wr[sl], w[sl]) / np.dot(w[sl], w[sl])
    res = wr[sl] - g0 * w[sl]
    G0.append(g0)
    print('  preset %d: G0 %.3f, residual %.1f dB' % (p, g0, 10 * np.log10(np.dot(res, res) / np.dot(wr[sl], wr[sl]))))
G0 = float(np.median(G0))

best = (1e30, None)
for Kd in (0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 6.0, 10.0):
    for Kf in (0.0, 0.5, 1.0, 1.5):
        for Kr in (0.5, 1.0, 2.0):
            L = np.zeros(len(GS))
            per = {}
            for p in tg:
                lev, mod = model(dry[p], wet(dry[p], p, Kd, Kf, Kr))
                per[p] = (lev, mod)
                L += (lev - tg[p][0]) ** 2 + ((mod - tg[p][1]) / 2.0) ** 2
            i = int(np.argmin(L))
            if L[i] < best[0]:
                best = (L[i], (Kd, Kf, Kr, GS[i]), {p: (per[p][0][i], per[p][1][i]) for p in per})
L, (Kd, Kf, Kr, G), per = best
print('\nbest: Kd %.2f ms, Kf %.2f, Kr %.2f, gain x%.2f of current, loss %.1f' % (Kd, Kf, Kr, G / G0, L))
for p in sorted(tg):
    print('  preset %d: level card %+5.1f model %+5.1f | modulation card %5.1f model %5.1f'
          % (p, tg[p][0], per[p][0], tg[p][1], per[p][1]))

# -*- coding: utf-8 -*-
"""Fit our reverb (Emu8000Fx::Reverb: 8 damped combs -> 4 allpasses, input
scaled by 1 - feedback, left channel = no spread) per preset to the card
(AWETST25 block 22, internal capture - the line-out does not contain it).

Model of one note with send s:  y = dry + G * (s / 255) * FV(dry, f, d) delayed
by a pre-delay D. `dry` is our render of the same note with send 0 (it
already carries pan and EQ, which are linear and commute with the reverb,
so they end up in G). Metrics, all relative to the same path's dry note:
  note 0-0.1 s vs send 0, tails 0.15-0.6 s and 0.6-1.5 s vs dry 0-0.1 s,
for sends 96 and 255 (card windows below noise + 3 dB are skipped).
Grid: size 0..1 (feedback 0.70..0.98 as SetRoom), damp, pre-delay; G is
scanned on a log grid. G0 = the gain that reproduces the CURRENT exe render
from its own preset parameters (checks the simulation against the exe);
the printed gain multiplier is G / G0.

No scipy here: the combs run sample by sample exactly like Comb::Process,
the allpasses in vector blocks of their delay length.

    python fit_rev.py [--ours replay_fx.wav]
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
DUR = 1.6
COMB = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
ALLP = [556, 441, 341, 225]
CUR_ROOMS = [(0.30, 0.55), (0.42, 0.50), (0.54, 0.45), (0.70, 0.35),
             (0.82, 0.30), (0.66, 0.25), (0.50, 0.60), (0.50, 0.60)]

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)


def collect():
    cnt, out = {}, {}
    for ev in ext25.events(22):
        i = cnt.get(ev.text, 0)
        cnt[ev.text] = i + 1
        if not ev.text.startswith('preset') or 'tick' in ev.text:
            continue
        m = vmk.get((22, ev.text, i))
        if m is None or m.rt is None:
            continue
        c = ext25.seg(ev, -PRE, DUR).astype(np.float64)
        fr = int(round(k * m.rt + q - PRE * SR))
        o = ours[fr:fr + len(c)].astype(np.float64)
        nz = np.mean(c[int(0.02 * SR):int((PRE - 0.02) * SR)] ** 2)
        out[ev.text] = (c[int(PRE * SR):], nz, o[int(PRE * SR):])
    return out


def comb(u, n, f, damp):
    line = [0.0] * n
    out = [0.0] * len(u)
    store, pos, fd = 0.0, 0, 1.0 - damp
    for i, x in enumerate(u):
        o = line[pos]
        store = o * fd + store * damp
        line[pos] = x + store * f
        pos += 1
        if pos == n:
            pos = 0
        out[i] = o
    return np.array(out)


def allpass(x, n, g=0.5):
    L = len(x)
    out = np.empty(L)
    w = np.zeros(L)
    for s in range(0, L, n):
        e = min(s + n, L)
        b = w[s - n:e - n] if s >= n else np.zeros(e - s)
        out[s:e] = -x[s:e] + b
        w[s:e] = x[s:e] + b * g
    return out


def fv(x, size, damp):
    f = 0.7 + size * 0.28
    u = (x * (1.0 - f)).tolist()
    acc = np.zeros(len(x))
    for n in COMB:
        acc += comb(u, n, f, damp)
    acc *= 0.125
    for n in ALLP:
        acc = allpass(acc, n)
    return acc


WIN = [(0.0, 0.1), (0.15, 0.6), (0.6, 1.5)]


def wsl(w):
    return slice(int(w[0] * SR), int(w[1] * SR))


data = collect()
dry = data['preset 4, send 0'][2]                 # render dry note (same for all presets)
Ed = [np.sum(dry[wsl(w)] ** 2) for w in WIN]

# card targets
targets = {}
for p in range(8):
    d0 = data.get('preset %d, send 0' % p)
    if d0 is None:
        continue
    c0, n0, _ = d0
    cd = np.sum(c0[wsl(WIN[0])] ** 2) - n0 * (WIN[0][1] - WIN[0][0]) * SR
    t = []
    for s in (96, 255):
        e = data.get('preset %d, send %d' % (p, s))
        if e is None:
            continue
        c, nz, _ = e
        for j, w in enumerate(WIN):
            n = (w[1] - w[0]) * SR
            ew = np.sum(c[wsl(w)] ** 2)
            if ew < 2 * nz * n:
                continue
            t.append((s, j, 10 * np.log10((ew - nz * n) / cd)))
    targets[p] = t

GS = np.geomspace(0.02, 50.0, 400)
DELAYS = [0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3]
SIZES = np.linspace(0.0, 1.0, 11)
DAMPS = [0.0, 0.2, 0.4, 0.6, 0.8]


def metrics(w, G, s):
    """model metrics per window for wet w (unit gain) and gains G"""
    g = G * (s / 255.0)
    out = []
    for j, win in enumerate(WIN):
        d_, w_ = dry[wsl(win)], w[wsl(win)]
        e = Ed[j] + 2 * g * np.dot(d_, w_) + g ** 2 * np.dot(w_, w_)
        out.append(10 * np.log10(np.maximum(e, 1e-20) / Ed[0]))
    return out


def loss_for(w, p):
    L = np.zeros(len(GS))
    for s, j, v in targets[p]:
        L += (metrics(w, GS, s)[j] - v) ** 2
    return L


print('calibration: current exe render vs simulation with the current rooms')
G0 = {}
for p in range(8):
    size, damp = CUR_ROOMS[p]
    w = fv(dry, size, damp)
    wet_ren = data['preset %d, send 255' % p][2] - dry
    g0 = np.dot(wet_ren, w) / np.dot(w, w)
    res = wet_ren - g0 * w
    G0[p] = g0
    print('  preset %d: G0 %.4f, residual %.1f dB below the exe wet' % (
        p, g0, 10 * np.log10(np.dot(res, res) / np.dot(wet_ren, wet_ren) + 1e-20)))

print('\nfit per preset (size, damp, pre-delay, gain x current) and metrics card / model')
best = {p: (1e30, None, None) for p in targets}
for size in SIZES:
    for damp in DAMPS:
        w0 = fv(dry, size, damp)
        for D in DELAYS:
            n = int(D * SR)
            w = np.r_[np.zeros(n), w0[:len(w0) - n]] if n else w0
            for p in targets:
                L = loss_for(w, p)
                i = int(np.argmin(L))
                if L[i] < best[p][0]:
                    best[p] = (L[i], (size, damp, D, GS[i]), w)
for p in sorted(best):
    L, (size, damp, D, G), w = best[p]
    rows = []
    for s, j, v in targets[p]:
        mv = metrics(w, np.array([G]), s)[j][0]
        rows.append('s%d w%d %+.1f/%+.1f' % (s, j, v, mv))
    print('  preset %d: size %.1f damp %.1f pre-delay %.2f s gain x%.2f rms %.2f dB | %s' % (
        p, size, damp, D, G / G0[p] if G0[p] else float('nan'),
        np.sqrt(L / max(len(targets[p]), 1)), ' '.join(rows)))

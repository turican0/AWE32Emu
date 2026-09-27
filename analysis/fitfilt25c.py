# -*- coding: utf-8 -*-
"""An exact filter fit without a grid: the response is computed directly for
every note, the parameters are refined by coordinate descent.

  variants:
    render-bil        method check - must return 101.81 Hz / 29.38 c / Q0 1.0 / 1.6 dB
    card-cham        Chamberlin 44.1 kHz, free map and Q
    card-cham-ours   Chamberlin 44.1 kHz, our map (101.81 Hz, 29.38 c), only Q free
    card-ana         analog prototype, free map and Q (for comparison)
The level offset G is free for every note (input attenuation by Q).

    python fitfilt25c.py
"""
import numpy as np

D = np.load('filt25.npz')
B = D['bands']
FS = 44100.0
GATE = 8.0
NS = 16
SUBF = np.array([np.geomspace(b / 2 ** (1 / 6.0), b * 2 ** (1 / 6.0), NS) for b in B])   # [bands, NS]


def resp_db(kind, fc, Q):
    """fc, Q: [notes] -> dB [notes, bands] (power mean over the band)."""
    f = SUBF[None, :, :]
    fc = fc[:, None, None]
    Q = Q[:, None, None]
    if kind == 'ana':
        x = f / fc
        h = 1.0 / ((1 - x * x) ** 2 + (x / Q) ** 2)
    elif kind == 'bil':
        x = np.tan(np.pi * np.minimum(f, FS * 0.4999) / FS) / np.tan(np.pi * np.minimum(fc, FS * 0.49) / FS)
        h = 1.0 / ((1 - x * x) ** 2 + (x / Q) ** 2)
    else:
        F = 2 * np.sin(np.pi * np.minimum(fc, FS / 6.0) / FS)
        qd = 1.0 / Q
        z1 = np.exp(-2j * np.pi * f / FS)
        den = 1 - (2 - F * qd - F * F) * z1 + (1 - F * qd) * z1 * z1
        h = np.abs(F * F * z1 / den) ** 2
        bad = (F * qd >= 2) | (F * F + 2 * F * qd >= 4)
        h = np.where(bad, np.nan, h)
    return 10 * np.log10(np.nanmean(h, axis=2) + 1e-30)


cut, qv = D['cut'].astype(float), D['q'].astype(float)
Hc = D['bc'] - D['ref_c']
Vc = (D['bc'] > D['bn'] + GATE) & (B[None, :] <= 16000)
Ho = D['bo'] - D['ref_o']
Vo = (Ho > -55) & (B[None, :] <= 16000)
keep = ~((cut == 255) & (qv == 0))


def score(kind, H, V, p):
    base, cents, q0, dbs = p
    fc = base * 2 ** (cut[keep] * cents / 1200.0)
    Q = q0 * 10 ** (qv[keep] * dbs / 20.0)
    M = resp_db(kind, fc, Q)
    d = np.where(V[keep] & np.isfinite(M), H[keep] - M, np.nan)
    G = np.nanmean(d, axis=1)
    return float(np.sqrt(np.nanmean((d - G[:, None]) ** 2))), G


def descend(kind, H, V, p, free, steps):
    p = list(p)
    best, _ = score(kind, H, V, p)
    for _ in range(40):
        improved = False
        for i in free:
            for sgn in (+1, -1):
                q = list(p)
                q[i] = p[i] * (1 + sgn * steps[i]) if i in (0, 2) else p[i] + sgn * steps[i]
                e, _ = score(kind, H, V, q)
                if e < best - 1e-5:
                    best, p, improved = e, q, True
        if not improved:
            steps = [s * 0.5 for s in steps]
            if max(steps) < 1e-3:
                break
    return best, p


def report(label, kind, H, V, p):
    e, G = score(kind, H, V, p)
    base, cents, q0, dbs = p
    gq = {}
    for g, q in zip(G, qv[keep]):
        gq.setdefault(int(q), []).append(g)
    print('%-16s residual %.3f dB | base %.2f Hz, %.3f c/step (255 -> %.0f Hz), Q0 %.3f, %.3f dB/step Q (Q15 %.1f dB)'
          % (label, e, base, cents, base * 2 ** (255 * cents / 1200.0), q0, dbs, 15 * dbs))
    print('                 input attenuation by Q: ' + ' '.join('%d:%+.2f' % (q, np.median(v)) for q, v in sorted(gq.items())))


steps = [0.05, 0.5, 0.08, 0.1]
e, p = descend('bil', Ho, Vo, (93.2, 30.5, 0.95, 1.5), [0, 1, 2, 3], steps)
report('render-bil', 'bil', Ho, Vo, p)
report('render-bil@ours', 'bil', Ho, Vo, (101.81, 29.3843, 1.0, 24.0 / 15))

e, p = descend('cham', Hc, Vc, (105.0, 28.8, 0.90, 1.2), [0, 1, 2, 3], steps)
report('card-cham', 'cham', Hc, Vc, p)
e, p2 = descend('cham', Hc, Vc, (101.81, 29.3843, 0.90, 1.2), [2, 3], steps)
report('card-cham-ours', 'cham', Hc, Vc, p2)
e, p3 = descend('ana', Hc, Vc, (97.7, 30.7, 0.945, 1.0), [0, 1, 2, 3], steps)
report('card-ana', 'ana', Hc, Vc, p3)

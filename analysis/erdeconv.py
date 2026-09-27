# -*- coding: utf-8 -*-
"""Early-reflection power profile from the 10 ms noise click (AWETST28
block 45, presets 0-5, line out R).

Card return power envelope E(t) (2 ms bins, relative to the dry click
power) minus the render's comb-only return C(t) (ER gain 0) = target T(t).
With a 10 ms click (5 bins) of unit power, T = A w, A[t, k] = 1 for
k <= t < k + 5: w_k = power of the reflections in the 2 ms bin k.
Non-negative least squares (projected gradient), bins 0-29 (0-58 ms).
Prints w per preset in dB and the common shape (mean over presets,
normalised to the strongest bin)."""
import os
import sys

import numpy as np

sys.argv = sys.argv[:1]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'clickfit.py'), encoding='utf-8').read().split('target = {p:')[0])

BASE = {0: [0.613, 0.045, 0.347], 1: [0.583, 0.08, 0.587], 2: [0.647, 0.03, 0.620],
        3: [0.692, 0.09, 0.849], 4: [0.672, 0.06, 0.988], 5: [0.694, 0.10, 0.912]}
env = {'EMU8K_RV_P%d' % p: '%.4f,%.3f,%.4f,0' % tuple(v) for p, v in BASE.items()}
mix = render(B45, os.path.join(D28, 'b45_comb.wav'), env)
wet = mix[:, 1] - dry[:, 1]
NB = 30
A = np.zeros((NB, NB))
for k in range(NB):
    A[k:min(k + 5, NB), k] = 1.0


def nnls(A, b, it=20000):
    x = np.zeros(A.shape[1])
    L = np.linalg.norm(A, 2) ** 2
    for _ in range(it):
        x = np.maximum(0.0, x - (A.T @ (A @ x - b)) / L)
    return x


shapes = []
for p, tc, tr in clicks:
    E = 10 ** (envc(card[:, 0], card[:, 1], tc) / 10)
    C = 10 ** (envc(dry[:, 0], wet, tr) / 10)
    T = np.maximum(E - C, 0.0)
    w = nnls(A, T)
    fitE = A @ w + C
    shapes.append(w)
    print('p%d  w dB ' % p + ' '.join('%4.0f' % (10 * np.log10(v + 1e-6)) for v in w))
    print('     card ' + ' '.join('%4.0f' % v for v in 10 * np.log10(E)) )
    print('     comb ' + ' '.join('%4.0f' % v for v in 10 * np.log10(C + 1e-9)))
    print('     fit  ' + ' '.join('%4.0f' % v for v in 10 * np.log10(fitE)))
    print('     total ER power %.3f (%.1f dB)' % (w.sum(), 10 * np.log10(w.sum())))
m = np.mean([w / w.sum() for w in shapes], axis=0)
print('common shape (fraction of the ER power per 2 ms bin):')
print('  ' + ' '.join('%.3f' % v for v in m))
np.save(os.path.join(HERE, 'er_shape.npy'), np.array(shapes))

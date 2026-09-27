# -*- coding: utf-8 -*-
"""AWETST28 block 44: which interpolation matches the card (line out)?
For each kernel (EMU8K_INTERP) the trace is rendered and card - render is
taken per third-octave band for every noise IP offset, WITHOUT normalising
to IP 0 (a kernel that smooths even at unity pitch shows there); only a
common level offset (mean of 250 Hz - 2 kHz at IP 0) is removed. Score =
rms over the bands 1-16 kHz and all offsets. Sine spurs as a second check.

    python interp28.py linear point3 bspline3 catmull sinc
"""
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

KS = [a for a in sys.argv[1:] if not a.startswith('--')] or ['linear', 'point3', 'bspline3', 'catmull', 'sinc']
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align28.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

nz = [(e, onset(card[:, 0], ctime(e))) for e in ev if e.block == 44 and e.text.startswith('interpolation: noise')]
sn = [(e, onset(card[:, 0], ctime(e))) for e in ev if e.block == 44 and e.text.startswith('interpolation: sine')]
cb = {int(e.text.split()[-1]): bands(card[int((t + 0.2) * SR):int((t + 1.4) * SR), 0]) for e, t in nz}
lo = [i for i, c in enumerate(BANDS) if 240 <= c <= 2100]
hi = [i for i, c in enumerate(BANDS) if c >= 900]


def spur(x):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    i1 = int(np.argmax(X))
    m = X.copy()
    m[max(i1 - 6, 0):i1 + 7] = 0
    m[:int(150 * len(x) / SR)] = 0
    return 20 * np.log10(m.max() / X[i1])


cs = [spur(card[int((t + 0.2) * SR):int((t + 1.1) * SR), 0]) for e, t in sn]
print('card sine spurs: ' + ' '.join('%6.1f' % v for v in cs))
print('bands ' + ' '.join('%6d' % BANDS[i] for i in hi))
for k in KS:
    out = os.path.join(D28, 'interp_%s.wav' % k)
    if not os.path.exists(out):
        subprocess.run([EXE, '--replay', TRACE, '--rom', ROM, '--wav', out],
                       env=dict(os.environ, EMU8K_INTERP=k), capture_output=True, check=True)
    m = sf.read(out, dtype='float64')[0]
    rb = {}
    for e, t in nz:
        tr = onset(m[:, 0], rtime(e))
        rb[int(e.text.split()[-1])] = bands(m[int((tr + 0.2) * SR):int((tr + 1.4) * SR), 0])
    off = np.mean((cb[0] - rb[0])[lo])
    allv = []
    print('\n%s' % k)
    for ip in sorted(cb):
        d = (cb[ip] - rb[ip] - off)[hi]
        allv.extend(d)
        print('  IP %+6d ' % ip + ' '.join('%+6.1f' % v for v in d))
    rs = [spur(m[int((onset(m[:, 0], rtime(e)) + 0.2) * SR):int((onset(m[:, 0], rtime(e)) + 1.1) * SR), 0]) for e, t in sn]
    print('  score rms %.2f dB | sine spurs ' % np.sqrt(np.mean(np.square(allv))) + ' '.join('%6.1f' % v for v in rs))

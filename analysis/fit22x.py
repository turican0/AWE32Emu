# -*- coding: utf-8 -*-
"""Refit of the room presets 0-5 on the LINE OUT (AWETST27 block 22 tick,
card R = reverb return alone), with the definitions of rev27.py.

Short trace vmdos27/b22.trace (up to the end of block 22), rendered with
EMU8K_RV_P<n> = "size,damp,gain,er" (temporary knob). Preset 1: its line
tick is spoilt by a foreign sound, the target is the internal capture of the
same run minus the mean internal-line difference of the other presets.

Each iteration: gain from the 150-600 ms level, ER from 0-25 ms, size from
the slope 150-300 -> 600-1200 ms."""
import os
import subprocess

import numpy as np
import soundfile as sf

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'intext27.py'), encoding='utf-8').read().split("\nprint('windows ms")[0])

EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
B22 = os.path.join(HERE, 'vmdos27', 'b22.trace')
B22D = os.path.join(HERE, 'vmdos27', 'b22_norev.trace')


def render(trace, out, env=None):
    subprocess.run([EXE, '--replay', trace, '--rom', ROM, '--wav', out],
                   env=dict(os.environ, **(env or {})), capture_output=True, check=True)
    return sf.read(out, dtype='float64')[0]


dry22 = render(B22D, os.path.join(HERE, 'vmdos27', 'b22_dry.wav'))
ticks = {}
card_w, int_w = {}, {}
for e in ev:
    if e.block != 22 or 'tick' not in e.text:
        continue
    p = int(e.text.split()[1].rstrip(','))
    if p > 5:
        continue
    tr = onset(norev[:, 0], rtime(e))
    ticks[p] = tr
    n = int(1.3 * SR)
    tc = onset(card[:, 0], ctime(e))
    card_w[p] = np.array(wins(hp(card[int(tc * SR):int(tc * SR) + n, 1]), dref(hp(card[int(tc * SR):int(tc * SR) + n, 0]))))
    x, ti = internal_at(tr)
    ti = onset(x[:, 0], ti)
    s = x[int(ti * SR):int(ti * SR) + n]
    int_w[p] = np.array(wins(hp(s[:, 1]) * 10 ** (6.5 / 20), dref(hp(s[:, 0]))))
off = np.mean([int_w[p] - card_w[p] for p in (0, 2, 3, 4, 5)], axis=0)
card_w[1] = int_w[1] - off
print('internal - line mean per window: ' + ' '.join('%+.1f' % v for v in off))

params = {0: [0.2, 0.6, 0.448, 0.189], 1: [0.49, 0.6, 1.21, 0.152], 2: [0.64, 0.6, 1.46, 0.161],
          3: [0.61, 0.0, 1.49, 0.157], 4: [0.60, 0.2, 1.49, 0.129], 5: [0.68, 0.0, 1.68, 0.164]}


def evaluate():
    env = {'EMU8K_RV_P%d' % p: '%.4f,%.3f,%.4f,%.4f' % tuple(v) for p, v in params.items()}
    mix22 = render(B22, os.path.join(HERE, 'vmdos27', 'b22_mix.wav'), env)
    wet = mix22[:, 1] - dry22[:, 1]
    out = {}
    for p, tr in ticks.items():
        n = int(1.3 * SR)
        out[p] = np.array(wins(hp(wet[int(tr * SR):int(tr * SR) + n]), dref(hp(dry22[int(tr * SR):int(tr * SR) + n, 0]))))
    return out


for it in range(8):
    rw = evaluate()
    tot = 0.0
    for p in sorted(params):
        d = rw[p] - card_w[p]
        ok = card_w[p] > -45
        err = np.abs(d[ok]).mean()
        tot += err
        print('it %d p%d  %s  diff %s  err %.2f' % (it, p, ' '.join('%.3f' % v for v in params[p]),
                                                   ' '.join('%+5.1f' % v for v in d), err))
        sz, dp, gn, er = params[p]
        late = [i for i in (3, 4) if ok[i]]
        if late:
            gn *= 10 ** (-np.mean(d[late]) / 20 * 0.8)
        er *= 10 ** (-d[0] / 20 * 0.8)
        if ok[5] and ok[3]:
            slope = (d[5] - d[3])        # render tail too high at the end -> smaller size
            sz = float(np.clip(sz - 0.012 * slope, 0.0, 0.95))
        params[p] = [sz, dp, gn, er]
    print('it %d total %.2f' % (it, tot), flush=True)

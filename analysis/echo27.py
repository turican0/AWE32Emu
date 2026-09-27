# -*- coding: utf-8 -*-
"""Echo levels of reverb presets 6/7 on the LINE OUT (AWETST27 block 22):
energy in 40 ms windows at the echo times (echolag_fine.py), relative to
the dry tick (max 40 ms of the left channel in the first 100 ms); card L/R
vs render return L/R (mix - no reverb sends).

    python echo27.py [E6:E7 ...]   renders b22.trace with EMU8K_RV_E6/E7 =
                                   "c,gL,gR,fb" (temporary knob) when given
"""
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

ARGS = sys.argv[1:]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rev27.py'), encoding='utf-8').read().split("\nprint('windows ms")[0])

TIMES = {6: ([114.75 + k * 116.75 for k in range(5)], [111.25 + k * 116.75 for k in range(5)]),
         7: ([115.5 + k * 236.0 for k in range(3)], [231.0 + k * 236.0 for k in range(3)])}


def levels(p, l, r, ref):
    out = []
    for ch, x in ((0, l), (1, r)):
        out.append([10 * np.log10(np.mean(x[int((t - 2) / 1000 * SR):int((t + 38) / 1000 * SR)] ** 2) + 1e-14) - ref
                    for t in TIMES[p][ch]])
    return out


def show(lv):
    return 'L ' + ' '.join('%+5.1f' % v for v in lv[0]) + ' | R ' + ' '.join('%+5.1f' % v for v in lv[1])


tk = {}
for e in ev:
    if e.block == 22 and 'tick' in e.text:
        p = int(e.text.split()[1].rstrip(','))
        if p in (6, 7):
            tk[p] = (onset(card[:, 0], ctime(e)), onset(norev[:, 0], rtime(e)))
n = int(1.2 * SR)
cardlv = {}
for p, (tc, tr) in sorted(tk.items()):
    c = card[int(tc * SR):int(tc * SR) + n]
    cardlv[p] = levels(p, c[:, 0], c[:, 1], dref(c[:, 0]))
    print('p%d card   %s' % (p, show(cardlv[p])))


def run(mixx, dryx, trs, tag):
    err = 0.0
    for p, (tc, tr) in sorted(tk.items()):
        tr = trs[p]
        w = mixx[int(tr * SR):int(tr * SR) + n] - dryx[int(tr * SR):int(tr * SR) + n]
        lv = levels(p, w[:, 0], w[:, 1], dref(dryx[int(tr * SR):int(tr * SR) + n, 0]))
        err += sum(abs(a - b) for ch in (0, 1) for a, b in zip(lv[ch], cardlv[p][ch]) if cardlv[p][ch] and b > -45)
        print('p%d render %s   %s' % (p, show(lv), tag))
    print('err %.1f' % err, flush=True)


if not ARGS:
    run(mix, norev, {p: v[1] for p, v in tk.items()}, 'full render')
else:
    EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
    ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
    B22 = os.path.join(HERE, 'vmdos27', 'b22.trace')
    dry22 = sf.read(os.path.join(HERE, 'vmdos27', 'b22_dry.wav'), dtype='float64')[0]
    trs = {p: onset(dry22[:, 0], v[1]) for p, v in tk.items()}
    for spec in ARGS:
        e6, e7 = (spec.split(':') + [''])[:2]
        env = dict(os.environ)
        if e6:
            env['EMU8K_RV_E6'] = e6
        if e7:
            env['EMU8K_RV_E7'] = e7
        out = os.path.join(HERE, 'vmdos27', 'b22_mix.wav')
        subprocess.run([EXE, '--replay', B22, '--rom', ROM, '--wav', out], env=env, capture_output=True, check=True)
        run(sf.read(out, dtype='float64')[0], dry22, trs, spec)

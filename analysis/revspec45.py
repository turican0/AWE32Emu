# -*- coding: utf-8 -*-
"""Reverb transfer shape on the line out (AWETST28 block 45, noise burst):
return spectrum (R, 550-900 ms and 900-1700 ms) minus the dry burst
spectrum (L, 20-190 ms), third octaves 100 Hz - 16 kHz, card and render
(render with damping 0 for presets 0-5, knob EMU8K_RV_P). Printed relative
to each row's mean over 200 Hz - 4 kHz, so only the colour remains."""
import os
import subprocess

import numpy as np
import soundfile as sf

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'damp45.py'), encoding='utf-8').read().split('cardw = {p:')[0])

env = {'EMU8K_RV_P%d' % p: '%.4f,%.3f,%.4f,%.4f' % (v[0], 0.0, v[2], v[3]) for p, v in P.items()}
mix = render(B45, os.path.join(D28, 'b45_mix0.wav'), env)
wet = mix[:, 1] - dry[:, 1]
sel = [i for i, c in enumerate(BANDS)]
mid = [i for i, c in enumerate(BANDS) if 190 <= c <= 4200]
print('band    ' + ' '.join('%5d' % BANDS[i] for i in sel))
avg = {}
for p, tc, tr in bursts:
    for name, l, r, t in (('card', card[:, 0], card[:, 1], tc), ('rend', dry[:, 0], wet, tr)):
        d = bands(l[int((t + 0.02) * SR):int((t + 0.19) * SR)])
        for (a, b), lab in (((0.55, 0.90), 'early'), ((0.90, 1.70), 'late ')):
            w = bands(r[int((t + a) * SR):int((t + b) * SR)]) - d
            w = w - w[mid].mean()
            avg.setdefault((name, lab), []).append(w)
            print('p%d %s %s ' % (p, name, lab) + ' '.join('%+5.1f' % w[i] for i in sel))
print('mean card - render over presets 0-5:')
for lab in ('early', 'late '):
    dd = np.mean(avg[('card', lab)], axis=0) - np.mean(avg[('rend', lab)], axis=0)
    print('   %s ' % lab + ' '.join('%+5.1f' % dd[i] for i in sel))

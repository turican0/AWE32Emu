# -*- coding: utf-8 -*-
"""Effects: card vs render on identical register writes (blocks 22, 23).

Block 22 (reverb): per preset 5 sends (0/48/96/160/255) of a 700 ms tone
with decay 0x60 / sustain 0 (silent after ~90 ms), 2.5 s gap; then a tick
impulse with send 255 and 3 s gap.
Block 23 (chorus): per preset 5 sends of the same tone (1.3 s gap); then a
sustained 2.5 s tone with send 0 and with send 255.

The capture tilt cancels when the same note with send 0 is used as the
reference, so everything is reported relative to send 0 of the same preset:
  reverb: tail energy 0.15-0.6 s and 0.6-1.5 s after note start, and the
          -20 / -40 dB decay times of the tick response
  chorus: steady-state level of the sustained tone (send 255 vs send 0)
          and the modulation depth (level p95-p5 over 0.5-2.3 s)

    python fx25.py [--ours replay_cham.wav]
"""
import argparse

import numpy as np
import soundfile as sf

import replay25 as R
import awe25

ap = argparse.ArgumentParser()
ap.add_argument('--ours', default='replay_cham.wav')
a = ap.parse_args()

vm, k, q = R.rt_to_frame()
ours = sf.read(R.os.path.join(R.HERE, a.ours), dtype='float32')[0][:, 0]
vmk = R.keyed(vm.events)
SR = 44100


def pairs(blk, t0, t1):
    cnt = {}
    for ev in awe25.events(blk):
        i = cnt.get(ev.text, 0)
        cnt[ev.text] = i + 1
        m = vmk.get((blk, ev.text, i))
        if m is None or m.rt is None:
            continue
        fr = k * m.rt + q
        c = awe25.seg(ev, t0, t1).astype(np.float64)
        n = awe25.seg(ev, -0.3, -0.05).astype(np.float64)
        a0 = int(round(fr + t0 * SR))
        o = ours[max(a0, 0):max(a0, 0) + len(c)].astype(np.float64)
        if len(o) == len(c) and len(c):
            yield ev.text, c, o, np.mean(n ** 2) if len(n) else 0.0


def energy_db(x, a0, a1, noise=0.0):
    s = x[int(a0 * SR):int(a1 * SR)]
    return 10 * np.log10(max(np.mean(s ** 2) - noise, 1e-14))


def env_db(x, hop=0.005):
    h = int(hop * SR)
    n = len(x) // h * h
    return 10 * np.log10((x[:n] ** 2).reshape(-1, h).mean(axis=1) + 1e-14)


# ---------------------------------------------------------------- reverb
print('=== block 22 reverb: level relative to send 0 of the same preset [dB]  card / render')
print('%-10s %-24s %-24s %-24s' % ('preset/send', 'note 0-0.1 s', 'tail 0.15-0.6 s', 'tail 0.6-1.5 s'))
data = {}
for text, c, o, nz in pairs(22, 0.0, 3.2):
    if not text.startswith('preset'):
        continue
    p = int(text.split()[1].rstrip(','))
    if 'tick' in text:
        data[(p, 'tick')] = (c, o, nz)
    else:
        s = int(text.split('send')[1])
        data[(p, s)] = (c, o, nz)
for p in range(8):
    if (p, 0) not in data:
        continue
    c0, o0, n0 = data[(p, 0)]
    for s in (48, 96, 160, 255):
        if (p, s) not in data:
            continue
        c, o, nz = data[(p, s)]
        cells = []
        # note window: relative to send 0 (dry reference of the same preset)
        dc = energy_db(c, 0.0, 0.1, nz) - energy_db(c0, 0.0, 0.1, n0)
        do = energy_db(o, 0.0, 0.1) - energy_db(o0, 0.0, 0.1)
        cells.append('%+7.1f / %+7.1f' % (dc, do))
        # tails: relative to the dry note level (send 0 tail is silence);
        # card windows below its noise floor + 3 dB are shown as '.'
        dry_c = energy_db(c0, 0.0, 0.1, n0)
        dry_o = energy_db(o0, 0.0, 0.1)
        fl_c = 10 * np.log10(nz + 1e-14)
        for w0, w1 in ((0.15, 0.6), (0.6, 1.5)):
            tc = energy_db(c, w0, w1, nz)
            to = energy_db(o, w0, w1)
            tc_s = '%+7.1f' % (tc - dry_c) if tc > fl_c + 3 else '      .'
            cells.append('%s / %+7.1f' % (tc_s, to - dry_o))
        print('%d/%-8d %-24s %-24s %-24s' % (p, s, cells[0], cells[1], cells[2]))
print('\n  tick impulse, send 255: time to -20 / -40 dB after the peak [ms] and tail 0.2-1.0 s vs peak [dB]')
for p in range(8):
    if (p, 'tick') not in data:
        continue
    c, o, nz = data[(p, 'tick')]
    res = []
    for x, noise in ((c, nz), (o, 0.0)):
        e = env_db(x)
        fl = 10 * np.log10(noise + 1e-14)
        kpk = int(np.argmax(e[:40]))
        pk = e[kpk]
        after = e[kpk:]
        t20 = np.argmax(after < pk - 20) * 5 if (after < pk - 20).any() else -1
        t40 = np.argmax(after < pk - 40) * 5 if (pk - 40 > fl + 3 and (after < pk - 40).any()) else -1
        tail = energy_db(x, 0.2, 1.0, noise) - pk
        res.append('%5d / %5d ms, tail %+6.1f' % (t20, t40, tail))
    print('  preset %d: card %s | render %s' % (p, res[0], res[1]))

# ---------------------------------------------------------------- chorus
print('\n=== block 23 chorus: sustained tone, send 255 vs send 0 of the same preset')
chd = {}
for text, c, o, nz in pairs(23, 0.0, 2.6):
    if 'sustained' not in text:
        continue
    p = int(text.split()[1].rstrip(','))
    s = int(text.split('send')[1])
    chd[(p, s)] = (c, o, nz)
for p in range(8):
    if (p, 0) not in chd or (p, 255) not in chd:
        continue
    c0, o0, n0 = chd[(p, 0)]
    c1, o1, n1 = chd[(p, 255)]
    lc = energy_db(c1, 0.5, 2.3, n1) - energy_db(c0, 0.5, 2.3, n0)
    lo = energy_db(o1, 0.5, 2.3) - energy_db(o0, 0.5, 2.3)
    ec, eo = env_db(c1[int(0.5 * SR):int(2.3 * SR)], 0.02), env_db(o1[int(0.5 * SR):int(2.3 * SR)], 0.02)
    print('  preset %d: level +%.2f dB card / %+.2f dB render | modulation p95-p5 %.1f / %.1f dB'
          % (p, lc, lo, np.percentile(ec, 95) - np.percentile(ec, 5), np.percentile(eo, 95) - np.percentile(eo, 5)))
print('\n=== block 23 chorus: short tone, level relative to send 0 [dB] card / render (0-0.1 s | 0.1-0.5 s)')
cs = {}
for text, c, o, nz in pairs(23, 0.0, 1.0):
    if 'sustained' in text or not text.startswith('preset'):
        continue
    p = int(text.split()[1].rstrip(','))
    s = int(text.split('send')[1])
    cs[(p, s)] = (c, o, nz)
for p in range(8):
    if (p, 0) not in cs:
        continue
    c0, o0, n0 = cs[(p, 0)]
    row = []
    for s in (48, 96, 160, 255):
        if (p, s) not in cs:
            continue
        c, o, nz = cs[(p, s)]
        row.append('%d: %+.1f/%+.1f | %+.1f/%+.1f' % (
            s, energy_db(c, 0, 0.1, nz) - energy_db(c0, 0, 0.1, n0), energy_db(o, 0, 0.1) - energy_db(o0, 0, 0.1),
            energy_db(c, 0.1, 0.5, nz) - energy_db(c0, 0.1, 0.5, n0), energy_db(o, 0.1, 0.5) - energy_db(o0, 0.1, 0.5)))
    print('  preset %d: %s' % (p, '   '.join(row)))

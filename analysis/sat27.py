# -*- coding: utf-8 -*-
"""Chorus saturation threshold against the line out (AWETST27 blocks 42/23).

For each C (EMU8K_CHO_C, temporary knob) the full trace is rendered and
card - render is printed for:
  b42  first R echo of the tick (feedback 00/40/80/C0, send 255; 80 also
       send 64) and the 4th echo of 80/C0, all against the dry tick
  b23  short-note return energy at send 160/255 (against the send-0 twin's
       dry left) for presets 0-4, 6, 7, and the sustained tone's left level
       against its send-0 twin

    python sat27.py 36000 40000 ...
"""
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

CS = sys.argv[1:] or ['36000']
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align27.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
TRACE = r'C:\prenos\AWE32EmuData\tests\out\vm27.trace'
DRY = nocho[:, 0]


def hp(x, fc=100.0):
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[f < fc] = 0
    return np.fft.irfft(X, len(x), axis=0)


def onset(x, tg):
    a = int((tg - 0.15) * SR)
    seg = x[a:a + int(0.3 * SR)]
    h = int(0.0005 * SR)
    e = 10 * np.log10(np.array([np.mean(seg[i:i + h] ** 2) for i in range(0, len(seg) - h, h)]) + 1e-14)
    return (a + int(np.argmax(e > e.max() - 20)) * h) / SR


def db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-14)


# event times, once
T = []
for e in ev:
    if e.block in (23, 42) and (e.text.startswith('preset') or 'tick' in e.text):
        T.append((e, onset(card[:, 0], ctime(e)), onset(DRY, rtime(e))))


def metrics(left, ret, mixl, is_card):
    """left = dry-ish left (reference), ret = chorus return (R), mixl = full left."""
    out = {}
    b23ref = {}
    for e, tc, tr in T:
        t = tc if is_card else tr
        a = int(t * SR)
        if e.block == 42:
            fb = e.text.split(',')[0].split()[-1]
            snd = e.text.split()[-1]
            if 'IP 57344' not in e.text:
                continue
            d = db(left[a:a + int(0.030 * SR)])
            key = 'b42 fb %s s%s' % (fb[2:], snd)
            # left tap echo at 64 ms: the dry tick is over on both sides, no
            # 16 bit clipping of dry + return in the render's left channel
            seg = hp(mixl[a:a + int(0.40 * SR)])
            e1 = db(seg[int(0.066 * SR):int(0.096 * SR)]) - d
            out.setdefault(key + ' e1L', []).append(e1)
            if fb in ('0x80', '0xC0') and snd == '255':
                e4 = db(seg[int(0.258 * SR):int(0.288 * SR)]) - d
                out.setdefault(key + ' e4L', []).append(e4)
        else:
            p = int(e.text.split()[1].rstrip(','))
            s = int(e.text.split()[-1])
            if 'sustained' in e.text:
                lv = db(hp(mixl[a + int(0.5 * SR):a + int(2.3 * SR)]))
                if s == 0:
                    b23ref[(p, 'sus')] = lv
                elif p != 5:
                    out.setdefault('b23 sus L p%d' % p, []).append(lv - b23ref[(p, 'sus')])
            else:
                if s == 0:
                    b23ref[p] = db(hp(left[a:a + int(1.2 * SR)]))
                elif s in (160, 255) and p != 5:
                    out.setdefault('b23 short s%d' % s, []).append(db(hp(ret[a:a + int(1.2 * SR)])) - b23ref[p])
    return {k: float(np.mean(v)) for k, v in out.items()}


cm = metrics(card[:, 0], card[:, 1], card[:, 0], True)
for spec in CS:
    C, sh = (spec.split(':') + ['0'])[:2]
    env = dict(os.environ, EMU8K_CHO_C=C, EMU8K_OUT_SHIFT=sh)
    out = os.path.join(HERE, 'vmdos27', 'sat_mix.wav')
    subprocess.run([EXE, '--replay', TRACE, '--rom', ROM, '--wav', out], env=env, capture_output=True, check=True)
    m = sf.read(out, dtype='float64')[0]
    outd = os.path.join(HERE, 'vmdos27', 'sat_nocho.wav')
    subprocess.run([EXE, '--replay', os.path.join(HERE, 'vmdos27', 'c_nocho.trace'), '--rom', ROM, '--wav', outd],
                   env=env, capture_output=True, check=True)
    nd = sf.read(outd, dtype='float64')[0]
    rm = metrics(nd[:, 0], m[:, 1] - nd[:, 1], m[:, 0], False)
    C = spec
    err = 0.0
    print('C = %s' % C)
    for k in sorted(cm):
        d = cm[k] - rm[k]
        err += abs(d)
        print('   %-24s card %+6.1f render %+6.1f  diff %+5.2f' % (k, cm[k], rm[k], d))
    print('   total |diff| %.2f' % err, flush=True)

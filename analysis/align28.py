# -*- coding: utf-8 -*-
"""Common setup for AWETST28 blocks 43-46 (the last line-out test).

Render of the VM trace tests/out/vm28.trace (vmdos28/r28_*.wav):
  mix    everything
  nocho  chorus sends 0,  norev  reverb sends 0,  nofx  both 0
rt -> render frame from replay25.rt_to_frame.

CARD: env AWE28_CARD = stereo line-out WAV. Without it a pseudo-card is made
from the render, behaving like the tester's card: L = mix, R = effect
returns only (mix - nofx), plus noise 59 dB under one voice (the level of
the line noise in the earlier recordings) - the dress rehearsal.

Card time = render time + offset per block (5 ms log envelopes of L).
"""
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

HERE = r'C:\Users\vesely\AppData\Local\Temp\claude\C--prenos-NeuralFin2\92fb4258-e9b2-4a10-8c5a-7fe50611e045\scratchpad'
OLD = r'C:\Users\vesely\AppData\Local\Temp\claude\C--prenos-NeuralFin2\04252be9-b795-4740-a055-4fe3a91f9f9e\scratchpad'
D28 = os.path.join(HERE, 'vmdos28')
sys.path.insert(0, OLD)
import replay25 as R  # noqa: E402

SR = 44100
EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
TRACE = r'C:\prenos\AWE32EmuData\tests\out\vm28.trace'
R.TRACE = TRACE
R.VMLOG = os.path.join(D28, 'AWE28.LOG')


def _render_all(force=False):
    want = {'mix': TRACE}
    for k, what in (('nocho', ['chorus']), ('norev', ['reverb']), ('nofx', ['chorus', 'reverb'])):
        src = TRACE
        for i, w in enumerate(what):
            dst = os.path.join(D28, 't_%s_%d.trace' % (k, i))
            if force or not os.path.exists(dst):
                subprocess.run([sys.executable, os.path.join(HERE, 'drytrace.py'), src, dst, w], check=True,
                               capture_output=True)
            src = dst
        want[k] = src
    out = {}
    for k, tr in want.items():
        wav = os.path.join(D28, 'r28_%s.wav' % k)
        if force or not os.path.exists(wav):
            subprocess.run([EXE, '--replay', tr, '--rom', ROM, '--wav', wav], check=True, capture_output=True)
        out[k] = sf.read(wav, dtype='float64')[0]
    return out


_r = _render_all('--render' in sys.argv)
mix, nocho, norev, nofx = _r['mix'], _r['nocho'], _r['norev'], _r['nofx']
_vm, _k, _q = R.rt_to_frame()
ev = [e for e in _vm.events if e.rt is not None]


def rtime(e):
    return (_k * e.rt + _q) / SR


if os.environ.get('AWE28_CARD'):
    card = sf.read(os.environ['AWE28_CARD'], dtype='float64')[0]
    SELF = False
else:
    card = np.stack([mix[:, 0], mix[:, 1] - nofx[:, 1]], axis=1)
    ref = [e for e in ev if e.text == 'reference tone'][0]
    a = int((rtime(ref) + 0.3) * SR)
    one = np.sqrt(np.mean(nofx[a:a + int(0.5 * SR), 0] ** 2))
    card = card + np.random.default_rng(1).normal(0, one * 10 ** (-59 / 20), card.shape)
    card = np.concatenate([np.zeros((int(7.3 * SR), 2)), card])   # some offset
    SELF = True


def logenv(x, hop):
    n = len(x) // hop
    e = 10 * np.log10((x[:n * hop] ** 2).reshape(n, hop).mean(axis=1) + 1e-14)
    return np.maximum(e, e.max() - 50) - (e.max() - 50)


HOP = int(0.005 * SR)
_er = logenv(mix[:, 0], HOP)
_ec = logenv(card[:, 0], HOP)


def _offset(t0, t1, guess=None, span=600.0):
    a, b = int(t0 * SR / HOP), int(t1 * SR / HOP)
    tpl = _er[a:b] - _er[a:b].mean()
    if guess is None:
        # coarse FFT correlation over everything
        n = 1 << int(np.ceil(np.log2(len(_ec) + len(tpl))))
        c = np.fft.irfft(np.fft.rfft(_ec - _ec.mean(), n) * np.conj(np.fft.rfft(tpl, n)), n)
        o = int(np.argmax(c[:len(_ec)])) - a
        return o * HOP / SR
    best, bo = -1e30, 0
    for o in range(int((guess - 0.5) * SR / HOP), int((guess + 0.5) * SR / HOP)):
        s = a + o
        if s < 0 or s + len(tpl) > len(_ec):
            continue
        seg = _ec[s:s + len(tpl)]
        c = np.dot(tpl, seg - seg.mean())
        if c > best:
            best, bo = c, o
    return bo * HOP / SR


_marks = [(e.block, rtime(e)) for e in ev if e.text.startswith('---- planned')]
_end = max(rtime(e) for e in ev) + 6
_spans = [(b, t, (_marks[i + 1][1] if i + 1 < len(_marks) else _end)) for i, (b, t) in enumerate(_marks)]
_g = _offset(_spans[0][1], _spans[0][2])
OFFS = {b: _offset(t0, min(t1, len(card) / SR - _g - 1.0), _g) for b, t0, t1 in _spans}


_LOC = {}


def _local(e):
    """Card - render offset around one event: the real machine pauses inside
    blocks (capture files are written), so each event is aligned on its own
    (5 ms log envelopes, render -0.3..+2.5 s, search +-3 s around the block
    offset)."""
    key = (e.block, e.text, e.rt)
    if key not in _LOC:
        t = rtime(e)
        a, b = int((t - 0.3) * SR / HOP), int((t + 2.5) * SR / HOP)
        tpl = _er[a:b] - _er[a:b].mean()
        g = OFFS.get(e.block, _g)
        best, bo = -1e30, 0
        for o in range(int((g - 0.05) * SR / HOP), int((g + 0.05) * SR / HOP)):
            s = a + o
            if s < 0 or s + len(tpl) > len(_ec):
                continue
            seg = _ec[s:s + len(tpl)]
            c = np.dot(tpl, seg - seg.mean())
            if c > best:
                best, bo = c, o
        _LOC[key] = bo * HOP / SR
    return _LOC[key]


def ctime(e):
    return rtime(e) + _local(e)


def onset(x, tg, thr=20):
    a = int((tg - 0.15) * SR)
    seg = x[a:a + int(0.3 * SR)]
    h = int(0.0005 * SR)
    e = 10 * np.log10(np.array([np.mean(seg[i:i + h] ** 2) for i in range(0, len(seg) - h, h)]) + 1e-14)
    return (a + int(np.argmax(e > e.max() - thr)) * h) / SR


def hp(x, fc=60.0):
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[f < fc] = 0
    return np.fft.irfft(X, len(x), axis=0)


def db(x):
    return 10 * np.log10(np.mean(np.asarray(x) ** 2) + 1e-20)


BANDS = [100 * 2 ** (i / 3) for i in range(0, 23)]     # 100 Hz .. 16 kHz


def bands(x, n=8192):
    if len(x) < n:
        n = 1 << int(np.log2(len(x)))
    acc = 0
    cnt = 0
    for i in range(0, len(x) - n + 1, n // 2):
        acc = acc + np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
        cnt += 1
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.array([10 * np.log10(acc[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].mean() / cnt + 1e-20)
                     for c in BANDS])


def events(block, prefix=''):
    """(event, card time, render time) with onsets refined on the left channel."""
    out = []
    for e in ev:
        if e.block == block and e.text.startswith(prefix) and not e.text.startswith('----'):
            out.append((e, onset(card[:, 0], ctime(e)), onset(nofx[:, 0], rtime(e))))
    return out


if __name__ == '__main__':
    print('pseudo-card (dress rehearsal)' if SELF else 'card: ' + os.environ['AWE28_CARD'])
    for b, t0, t1 in _spans:
        print('block %d: render %.2f..%.2f s, card offset %+.3f s' % (b, t0, t1, OFFS[b]))

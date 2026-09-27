# -*- coding: utf-8 -*-
"""Common setup for the AWETST27 /ONLY:22,23,42 recordings (2026-09-26).

Render (VM trace tests/out/vm27.trace): c_mix / c_nocho / c_norev.wav.
rt -> render frame from replay25.rt_to_frame (block marks + reference tones).
Card: line out 222342_ext.wav (R = effect returns only). Card time = render
time + offset, the offset per block from the cross-correlation of the 5 ms
log envelopes of the left channel.

    exec(open('align27.py').read())  ->  ev, rtime(e), ctime(e), card, mix,
                                         nocho, norev, SR
    python align27.py                    (prints the offsets)
"""
import os
import sys

import numpy as np
import soundfile as sf

HERE = r'C:\Users\vesely\AppData\Local\Temp\claude\C--prenos-NeuralFin2\92fb4258-e9b2-4a10-8c5a-7fe50611e045\scratchpad'
OLD = r'C:\Users\vesely\AppData\Local\Temp\claude\C--prenos-NeuralFin2\04252be9-b795-4740-a055-4fe3a91f9f9e\scratchpad'
TD = r'C:\prenos\AWE32EmuData\tester\2026-09-26\22-23-42'
sys.path.insert(0, OLD)
import replay25 as R  # noqa: E402

SR = 44100
R.TRACE = r'C:\prenos\AWE32EmuData\tests\out\vm27.trace'
R.VMLOG = os.path.join(HERE, 'vmdos27', 'AWE27.LOG')
_vm, _k, _q = R.rt_to_frame()
ev = [e for e in _vm.events if e.rt is not None]

mix = sf.read(os.path.join(HERE, 'vmdos27', 'c_mix.wav'), dtype='float64')[0]
nocho = sf.read(os.path.join(HERE, 'vmdos27', 'c_nocho.wav'), dtype='float64')[0]
norev = sf.read(os.path.join(HERE, 'vmdos27', 'c_norev.wav'), dtype='float64')[0]
card = sf.read(os.path.join(TD, '222342_ext.wav'), dtype='float64')[0]


def rtime(e):
    return (_k * e.rt + _q) / SR


def logenv(x, hop):
    n = len(x) // hop
    e = 10 * np.log10((x[:n * hop] ** 2).reshape(n, hop).mean(axis=1) + 1e-14)
    return np.maximum(e, e.max() - 50) - (e.max() - 50)


HOP = int(0.005 * SR)
_er = logenv(mix[:, 0], HOP)
_ec = logenv(card[:, 0], HOP)


def _offset(t0, t1, guess=None, span=400.0):
    """Card - render offset (s) for the render span t0..t1."""
    a, b = int(t0 * SR / HOP), int(t1 * SR / HOP)
    tpl = _er[a:b] - _er[a:b].mean()
    best, bo = -1e30, 0
    lo = int(((guess - 5) if guess is not None else -span) * SR / HOP)
    hi = int(((guess + 5) if guess is not None else span) * SR / HOP)
    for o in range(lo, hi):
        s = a + o
        if s < 0 or s + len(tpl) > len(_ec):
            continue
        seg = _ec[s:s + len(tpl)]
        c = np.dot(tpl, seg - seg.mean())
        if c > best:
            best, bo = c, o
    return bo * HOP / SR


# spans of the blocks in render time
_marks = [(e.block, rtime(e)) for e in ev if e.text.startswith('---- planned')]
_end = max(rtime(e) for e in ev) + 6
_spans = [(b, t, (_marks[i + 1][1] if i + 1 < len(_marks) else _end)) for i, (b, t) in enumerate(_marks)]
_g = _offset(_spans[0][1], _spans[0][1] + 60)
OFFS = {b: _offset(t0, t1, _g) for b, t0, t1 in _spans}


def ctime(e):
    return rtime(e) + OFFS[e.block]


if __name__ == '__main__':
    print('global guess %+.3f s' % _g)
    for b, t0, t1 in _spans:
        print('block %d: render %.2f..%.2f s, card offset %+.3f s' % (b, t0, t1, OFFS[b]))

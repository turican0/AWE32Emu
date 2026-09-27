# -*- coding: utf-8 -*-
"""Note-level access to the AWETST25 recordings of the tester (2026-09-12).

Run `reca` has blocks 1..11, run `recc` blocks 11..39 (both internal capture,
left channel only - the card's right channel does not come out even on the
line output). Every log line carries the stamp `cap <file from ms>:<frame>`
of the onset moment, so notes are not searched for in a grid but taken
directly.

The recordings are in the `sources` branch of the repository
(recordings/2026-09-12); point AWE32EMU_TESTER25 at that folder.

    from awe25 import events, seg, env_db
    for ev in events(10, 'decay'):
        x = seg(ev, -0.05, 3.0)          # samples from 50 ms before the note
"""
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from awelog import parse  # noqa: E402

BASE = os.environ.get('AWE32EMU_TESTER25', os.path.join('recordings', '2026-09-12'))
SR = 44100
# Delay between the stamp (right before the write that starts the note) and
# the onset in the WAV. Measured in block 1 (latency()) and then subtracted.
LATENCY = 0.0

_logs = {}
_audio = {}


def _log(run):
    if run not in _logs:
        _logs[run] = parse(os.path.join(BASE, run, 'awetest.log'))
    return _logs[run]


def run_for(block):
    return 'reca' if block <= 10 else 'recc'


class Ev(object):
    __slots__ = ('run', 'block', 'text', 'path', 't', 'ms', 'rt')

    def __init__(self, run, e):
        self.run, self.block, self.text, self.ms, self.rt = run, e.block, e.text, e.ms, e.rt
        self.path = os.path.join(BASE, run, os.path.basename(e.file.replace('\\', '/'))) if e.file else None
        self.t = e.frame / float(SR) if e.frame is not None else None

    def __repr__(self):
        return '<%s b%d %.3fs %s | %s>' % (self.run, self.block, self.t or -1, os.path.basename(self.path or '-'), self.text)


def events(block, prefix=None, run=None):
    run = run or run_for(block)
    out = []
    for e in _log(run).events:
        if e.block != block or e.text.startswith('----') or e.frame is None or e.file is None:
            continue
        if prefix and not e.text.startswith(prefix):
            continue
        out.append(Ev(run, e))
    return out


def audio(path):
    if path not in _audio:
        x, sr = sf.read(path, dtype='float32')
        assert sr == SR
        _audio[path] = x[:, 0] if x.ndim > 1 else x
        if len(_audio) > 24:                      # recc has 195 files
            _audio.pop(next(iter(_audio)))
    return _audio[path]


def seg(ev, t0, t1):
    """Left channel from ev.t+t0 to ev.t+t1 (seconds), clipped to the file."""
    x = audio(ev.path)
    a = int(round((ev.t - LATENCY + t0) * SR))
    b = int(round((ev.t - LATENCY + t1) * SR))
    return x[max(a, 0):max(min(b, len(x)), 0)]


def env_db(x, hop=0.005):
    h = max(1, int(hop * SR))
    n = len(x) // h * h
    if n == 0:
        return np.zeros(0)
    return 10 * np.log10((x[:n].astype(np.float64) ** 2).reshape(-1, h).mean(axis=1) + 1e-14)


def onset(x, hop=0.002, rise_db=12.0):
    """Time of the first rise by rise_db above the floor (s from the start of x), or None."""
    e = env_db(x, hop)
    if len(e) < 10:
        return None
    floor = np.percentile(e[:max(5, len(e) // 10)], 50)
    idx = np.nonzero(e > floor + rise_db)[0]
    return None if len(idx) == 0 else idx[0] * hop


def freq_zc(x, sr=SR):
    """Frequency from zero crossings with linear interpolation (sine, no noise)."""
    s = np.signbit(x)
    i = np.nonzero(s[1:] != s[:-1])[0]
    if len(i) < 4:
        return None
    frac = x[i] / (x[i] - x[i + 1])
    tz = (i + frac) / sr
    return (len(tz) - 1) / (2.0 * (tz[-1] - tz[0]))

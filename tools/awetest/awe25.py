# -*- coding: utf-8 -*-
"""Pristup k nahravkam AWETST25 od testera (2026-09-12) po notach.

Beh `reca` ma bloky 1..11, beh `recc` bloky 11..39 (oba vnitrni zaznam,
jen levy kanal - pravy kanal karty nevychazi ani na linkovy vystup).
Kazdy radek logu ma razitko `cap <soubor od ms>:<ramec>` z okamziku nastupu,
takze se noty nehledaji v mrizce, ale berou se primo.

    from awe25 import events, seg, env_db
    for ev in events(10, 'decay'):
        x = seg(ev, -0.05, 3.0)          # vzorky od 50 ms pred notou
"""
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from awelog import parse  # noqa: E402

BASE = r'C:\prenos\AWE32EmuData\tester\2026-09-12'
SR = 44100
# Zpozdeni mezi razitkem (tesne pred zapisem, ktery notu spousti) a nastupem
# ve WAV. Zmeri se v bloku 1 (latency()) a pak se odecita.
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
        if len(_audio) > 24:                      # recc ma 195 souboru
            _audio.pop(next(iter(_audio)))
    return _audio[path]


def seg(ev, t0, t1):
    """Levy kanal od ev.t+t0 do ev.t+t1 (sekundy), orezane na soubor."""
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
    """Cas prvniho vzestupu o rise_db nad dno (s od zacatku x), nebo None."""
    e = env_db(x, hop)
    if len(e) < 10:
        return None
    floor = np.percentile(e[:max(5, len(e) // 10)], 50)
    idx = np.nonzero(e > floor + rise_db)[0]
    return None if len(idx) == 0 else idx[0] * hop


def freq_zc(x, sr=SR):
    """Kmitocet z pruchodu nulou s linearni interpolaci (sinus, bez sumu)."""
    s = np.signbit(x)
    i = np.nonzero(s[1:] != s[:-1])[0]
    if len(i) < 4:
        return None
    frac = x[i] / (x[i] - x[i + 1])
    tz = (i + frac) / sr
    return (len(tz) - 1) / (2.0 * (tz[-1] - tz[0]))

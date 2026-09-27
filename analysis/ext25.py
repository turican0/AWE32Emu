# -*- coding: utf-8 -*-
"""External (line-out) capture awetst25.flac aligned to the AWETST25 capture.

Findings that shaped this module:
  - the `ms` field of the cap stamp counts captured samples, not real time
    (the capture pauses between files);
  - the internal capture files are consistent with the DOS tick clock rt
    (rt - t constant within 1-2 ms per file);
  - the FLAC is NOT one continuous take of the same timeline: a global
    rt -> FLAC line holds for reca001 only, later there is loud content
    where the card was silent (edited or separately recorded passages).

So every block is located on its own: a template of the block (internal
capture placed by rt, 100 ms linear RMS envelope, up to 120 s) is searched
in the FLAC in block order with a sliding Pearson correlation, then refined
on 10 ms envelopes +-0.3 s. Inside a block rt is used (rate 0.99989), and
each note is refined by +-20 ms on a 0.5 ms envelope. Blocks with r < 0.6
are treated as absent. Cached in ext25_map.json and flac_env10.npy.

    from ext25 import fseg
    c, f = fseg(ev, 0.0, 1.0)       # internal capture, line-out (or None)
"""
import json
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, r'C:\prenos\AWE32Emu\tools\awetest')
import awe25  # noqa: E402
from awe25 import SR, events, seg  # noqa: E402

awe25.LATENCY = 0.003
HERE = os.path.dirname(os.path.abspath(__file__))
FLAC = r'C:\Users\vesely\Downloads\awetst25.flac'
CACHE = os.path.join(HERE, 'ext25_map.json')
ENVC = os.path.join(HERE, 'flac_env10.npy')
_flac = sf.SoundFile(FLAC)
assert _flac.samplerate == SR
FLAC_LEN = _flac.frames / float(SR)
RATE = 0.99989
MIN_R = 0.6


def flac_seg(t0, t1):
    a = max(int(round(t0 * SR)), 0)
    b = min(int(round(t1 * SR)), _flac.frames)
    if b <= a:
        return np.zeros(0)
    _flac.seek(a)
    x = _flac.read(b - a, dtype='float32')
    return (x[:, 0] if x.ndim > 1 else x).astype(np.float64)


def envlin(x, h=441):
    n = len(x) // h * h
    return np.sqrt((np.asarray(x[:n], dtype=np.float64) ** 2).reshape(-1, h).mean(axis=1))


def flac_env10():
    if os.path.exists(ENVC):
        return np.load(ENVC)
    out = []
    _flac.seek(0)
    while True:
        x = _flac.read(441 * 6000, dtype='float32')
        if len(x) == 0:
            break
        out.append(envlin(x[:, 0] if x.ndim > 1 else x))
    e = np.concatenate(out)
    np.save(ENVC, e)
    return e


def slide_pearson(f, t):
    n = len(t)
    if len(f) < n or n < 3:
        return np.zeros(0)
    tn = (t - t.mean()) / (t.std() + 1e-12)
    c = np.correlate(f, tn, 'valid') / n
    cs = np.cumsum(np.r_[0.0, f])
    cs2 = np.cumsum(np.r_[0.0, f * f])
    m = (cs[n:] - cs[:-n]) / n
    v = (cs2[n:] - cs2[:-n]) / n - m * m
    return c / np.sqrt(np.maximum(v, 1e-18))


def best_lag(fe, ce):
    ce = ce - ce.mean()
    if len(fe) < len(ce) or len(ce) < 2:
        return None, 0.0
    c = np.correlate(fe, ce, 'valid')
    k = int(np.argmax(c))
    return k, float(np.corrcoef(fe[k:k + len(ce)], ce)[0, 1])


_files_cache = {}


def _files(run):
    if run not in _files_cache:
        d = {}
        for e in awe25._log(run).events:
            if e.frame is None or e.file is None or e.rt is None:
                continue
            ev = awe25.Ev(run, e)
            d.setdefault(ev.path, []).append(ev.rt - ev.t)
        _files_cache[run] = sorted(((p, float(np.median(v))) for p, v in d.items()), key=lambda r: r[1])
    return _files_cache[run]


def _template(run, rt_a, rt_b):
    """internal 10 ms envelope placed by rt over [rt_a, rt_b)"""
    n = int((rt_b - rt_a) * 100)
    tpl = np.zeros(n)
    for path, rt0 in _files(run):
        x = awe25.audio(path)
        if rt0 > rt_b or rt0 + len(x) / float(SR) < rt_a:
            continue
        e = envlin(x)
        off = int(round((rt0 - rt_a) * 100))
        a = max(off, 0)
        b = min(off + len(e), n)
        if b > a:
            tpl[a:b] = e[a - off:b - off]
    return tpl


def _build():
    fenv = flac_env10()
    f100 = fenv[:len(fenv) // 10 * 10].reshape(-1, 10).mean(axis=1)
    out = {}
    prev = None                                   # (rt_a, flac start) of the last found block
    for blk in range(1, 40):
        run = awe25.run_for(blk)
        evs = [e for e in events(blk) if e.rt is not None]
        if not evs:
            continue
        rt_a = min(e.rt for e in evs) - 1.0
        rt_b = min(max(e.rt for e in evs) + 3.0, rt_a + 120.0)
        tpl = _template(run, rt_a, rt_b)
        t100 = tpl[:len(tpl) // 10 * 10].reshape(-1, 10).mean(axis=1)
        if prev is None:
            lo, hi = 0.0, 120.0
        else:
            guess = prev[1] + (rt_a - prev[0])
            lo, hi = max(prev[1], guess - 120.0), guess + 900.0
        a, b = int(lo * 10), min(int((hi + len(t100) / 10.0) * 10), len(f100))
        pr = slide_pearson(f100[a:b], t100)
        if len(pr) == 0:
            print('  block %2d: beyond the FLAC' % blk)
            break
        k = int(np.argmax(pr))
        coarse = (a + k) / 10.0
        # refine on 10 ms
        a2 = max(int((coarse - 0.3) * 100), 0)
        seg_f = fenv[a2:a2 + len(tpl) + 60]
        pr2 = slide_pearson(seg_f, tpl)
        k2 = int(np.argmax(pr2)) if len(pr2) else 0
        start = (a2 + k2) / 100.0
        r = float(pr2[k2]) if len(pr2) else float(pr[k])
        dev = None if prev is None else start - (prev[1] + (rt_a - prev[0]) * RATE)
        print('  block %2d (%s): template %5.1f s, FLAC %8.2f s, r %.3f, vs previous block by rt %s'
              % (blk, run, len(tpl) / 100.0, start, r, '-' if dev is None else '%+.2f s' % dev))
        out[str(blk)] = {'rt_a': rt_a, 'start': start, 'r': r}
        if r >= MIN_R:
            prev = (rt_a, start)
    json.dump(out, open(CACHE, 'w'), indent=0)
    return out


MAP = json.load(open(CACHE)) if os.path.exists(CACHE) else _build()


def _env05(x):
    h = 22
    n = len(x) // h * h
    return np.sqrt((x[:n] ** 2).reshape(-1, h).mean(axis=1))


def flac_time(ev):
    b = MAP.get(str(ev.block))
    if b is None or b['r'] < MIN_R:
        return None
    return b['start'] + RATE * (ev.rt - awe25.LATENCY - b['rt_a'])


def fseg(ev, t0, t1, refine=0.02):
    """(internal, line-out) segments of the same note; line-out is None when
    the note's block was not found in the FLAC."""
    c = seg(ev, t0, t1).astype(np.float64)
    t = flac_time(ev)
    if t is None or t + t1 + refine > FLAC_LEN or t + t0 - refine < 0:
        return c, None
    f = flac_seg(t + t0 - refine, t + t1 + refine)
    k, _ = best_lag(_env05(f), _env05(c))
    if k is None:
        return c, None
    f = f[k * 22:k * 22 + len(c)]
    return (c, f) if len(f) == len(c) else (c, None)


if __name__ == '__main__':
    for kk in sorted(MAP, key=int):
        print(kk, MAP[kk])

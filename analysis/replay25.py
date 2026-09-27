# -*- coding: utf-8 -*-
"""The card against our core NOTE BY NOTE on the same registers.

  1) the VM trace (AWETST25 without /REC, EMU8K_TRACE) -> --replay through
     our core (and --chip 86box) into a WAV
  2) the VM log has rt stamps; the trace has frames. The rt -> frame
     conversion is a line fit through the reference tones of the blocks (the
     stamp is on the note-on of voice 29; in the trace the first note-on of
     voice 29 after a group of N high ticks = block N).
  3) every event of the tester's log (cap stamp) is paired with the same
     event of the VM log (same block, text and order) and the same window is
     taken from both recordings.
  4) level and third-octave bands card - render. Notes that are less than
     GATE dB above the noise on the card (the window before the note) are
     skipped. Bands below the card noise + 6 dB are not averaged.

    python replay25.py                              # summary per block
    python replay25.py --ours replay_box.wav        # the same for the 86Box core
    python replay25.py --blocks 5,35 --spec         # bands per block
    python replay25.py --blocks 3 --list            # single notes
"""
import argparse
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, r'C:\prenos\AWE32Emu\tools\awetest')
import awe25  # noqa: E402
from awelog import parse  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.join(HERE, 'trace', 'full25.trace')
VMLOG = os.path.join(HERE, 'vmfull25', 'AWETEST.LOG')
EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
SR = 44100
P_PTR, P_D1, P_D3 = 0xE22, 0xA20, 0xE20
IP_HIGH_TICK = 0xF000
awe25.LATENCY = 0.003
BANDS = 1000.0 * 2 ** (np.arange(-12, 13) / 3.0)          # 62 Hz .. 16 kHz
BANDS = BANDS[BANDS <= 17000]


def trace_ticks():
    """Note-on frames: high ticks on voice 28 (block marks) and voice 29."""
    ptr = 0
    ip28 = 0
    marks, tests = [], []
    with open(TRACE) as f:
        for line in f:
            if not line or line[0] in '#R':
                continue
            p = line.split()
            if len(p) < 3 or (len(p) > 3 and p[3] == 'b'):
                continue
            port = int(p[1], 16)
            if port == P_PTR:
                ptr = int(p[2], 16)
            elif port == P_D3 and ptr == 0x1C:
                ip28 = int(p[2], 16)
            elif port == P_D1 and ptr in (0xBC, 0xBD):
                v = int(p[2], 16)
                if v != 0x0080 and not (v & 0x8000):
                    if ptr == 0xBC:
                        if ip28 == IP_HIGH_TICK:
                            marks.append(int(p[0]))
                    else:
                        tests.append(int(p[0]))
    return np.array(marks), np.array(tests)


def block_starts_trace(marks):
    groups = []
    start, n = marks[0], 1
    for a, b in zip(marks, marks[1:]):
        if b - a > 0.3 * SR:
            groups.append((n, start))
            start, n = b, 1
        else:
            n += 1
    groups.append((n, start))
    return groups


def pair_blocks(groups, starts, tests=None):
    """Block anchor = the reference tone (stamp on the note-on of voice 29);
    BlockMark writes the "planned" line before the minute mark, which is
    not suitable."""
    pairs = []
    gi = 0
    for blk in sorted(starts):
        j = gi
        while j < len(groups) and groups[j][0] != blk:
            j += 1
        if j < len(groups):
            fr = groups[j][1]
            if tests is not None and len(tests):
                k = np.searchsorted(tests, fr)
                if k < len(tests):
                    fr = tests[k]
            pairs.append((blk, starts[blk], fr))
            gi = j + 1
    return pairs


def rt_to_frame():
    marks, tests = trace_ticks()
    groups = block_starts_trace(marks)
    vm = parse(VMLOG)
    starts = {}
    for e in vm.events:
        if e.text == 'reference tone' and e.rt is not None and e.block >= 1:
            starts.setdefault(e.block, e.rt)
    pairs = pair_blocks(groups, starts, tests)
    x = np.array([p[1] for p in pairs])
    y = np.array([p[2] for p in pairs], float)
    k, q = np.polyfit(x, y, 1)
    res = y - (k * x + q)
    print('rt -> frame conversion: %.3f frames/s, blocks %d, residual max %.2f ms'
          % (k, len(pairs), 1000 * np.max(np.abs(res)) / SR))
    return vm, k, q


def band_db(x):
    n = 4096
    if len(x) < n:
        x = np.concatenate([x, np.zeros(n - len(x))])
    win = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - n + 1, n // 2):
        acc += np.abs(np.fft.rfft(x[i:i + n] * win)) ** 2
        cnt += 1
    f = np.fft.rfftfreq(n, 1.0 / SR)
    out = np.empty(len(BANDS))
    for j, c in enumerate(BANDS):
        m = (f >= c / 2 ** (1 / 6.0)) & (f < c * 2 ** (1 / 6.0))
        out[j] = 10 * np.log10(acc[m].mean() / max(cnt, 1) + 1e-20)
    return out


def keyed(events):
    out, cnt = {}, {}
    for e in events:
        if e.text.startswith('----') or e.text.startswith('MINUTE') or e.text.startswith('LEVEL'):
            continue
        i = cnt.get((e.block, e.text), 0)
        cnt[(e.block, e.text)] = i + 1
        out[(e.block, e.text, i)] = e
    return out


def collect(vm, k, q, ours, want, t0, t1, gate):
    vmk = keyed(vm.events)
    rows = []
    for blk in range(1, 40):
        if want and blk not in want:
            continue
        cnt = {}
        for ev in awe25.events(blk):
            i = cnt.get(ev.text, 0)
            cnt[ev.text] = i + 1
            m = vmk.get((blk, ev.text, i))
            if m is None or m.rt is None:
                continue
            fr = k * m.rt + q
            c = awe25.seg(ev, t0, t1).astype(np.float64)
            nz = awe25.seg(ev, -0.25, -0.03).astype(np.float64)
            a0 = int(round(fr + t0 * SR))
            o = ours[max(a0, 0):max(a0, 0) + len(c)].astype(np.float64)
            if len(o) < len(c) or len(c) < 1024:
                continue
            lc = 10 * np.log10(np.mean(c ** 2) + 1e-14)
            ln = 10 * np.log10(np.mean(nz ** 2) + 1e-14) if len(nz) else -140.0
            lo = 10 * np.log10(np.mean(o ** 2) + 1e-14)
            if lc - ln < gate:
                continue
            rows.append(dict(blk=blk, text=ev.text, lc=lc, lo=lo, ln=ln,
                             bc=band_db(c), bo=band_db(o), bn=band_db(nz) if len(nz) >= 1024 else None))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--render', action='store_true')
    ap.add_argument('--blocks', default='')
    ap.add_argument('--win', default='0.05:0.35')
    ap.add_argument('--ours', default=os.path.join(HERE, 'replay_nas.wav'))
    ap.add_argument('--gate', type=float, default=10.0)
    ap.add_argument('--spec', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--ref', type=float, default=None,
                    help='level offset to subtract; default = median of the sine blocks 1,2,8-12')
    a = ap.parse_args()
    if a.render:
        subprocess.run([EXE, '--replay', TRACE, '--rom', ROM, '--wav', a.ours], check=True)
        return

    vm, k, q = rt_to_frame()
    ours, sr = sf.read(a.ours, dtype='float32')
    ours = ours[:, 0]
    t0, t1 = [float(v) for v in a.win.split(':')]
    want = {int(b) for b in a.blocks.split(',') if b}

    ref = a.ref
    if ref is None:
        base = collect(vm, k, q, ours, {1, 2, 8, 9, 10, 11, 12}, 0.05, 0.35, 20.0)
        ref = float(np.median([r['lc'] - r['lo'] for r in base])) if base else 0.0
    print('reference offset card - render (sine, blocks 1,2,8-12): %+.2f dB - subtracted' % ref)

    rows = collect(vm, k, q, ours, want, t0, t1, a.gate)
    byblk = {}
    for r in rows:
        byblk.setdefault(r['blk'], []).append(r)
    for blk, rs in sorted(byblk.items()):
        d = np.array([r['lc'] - r['lo'] - ref for r in rs])
        print('block %2d: notes %3d, level card-render %+6.2f dB, IQR %+6.2f..%+6.2f'
              % (blk, len(d), np.median(d), np.percentile(d, 25), np.percentile(d, 75)))
        if a.spec:
            mats = []
            for r in rs:
                v = r['bc'] - r['bo'] - ref
                if r['bn'] is not None:
                    v = np.where(r['bc'] < r['bn'] + 6.0, np.nan, v)
                mats.append(v)
            mats = np.array(mats)
            med = np.nanmedian(mats, axis=0)
            print('   bands Hz: ' + ' '.join('%6.0f' % b for b in BANDS))
            print('   card-r  : ' + ' '.join('%+6.1f' % v if np.isfinite(v) else '     .' for v in med))
        if a.list:
            for r in rs:
                v = r['bc'] - r['bo'] - ref
                print('   %-58s %+6.2f dB | 250 %+5.1f 1k %+5.1f 4k %+5.1f 8k %+5.1f 12k %+5.1f'
                      % (r['text'][:58], r['lc'] - r['lo'] - ref,
                         v[np.argmin(abs(BANDS - 250))], v[np.argmin(abs(BANDS - 1000))],
                         v[np.argmin(abs(BANDS - 4000))], v[np.argmin(abs(BANDS - 8000))],
                         v[np.argmin(abs(BANDS - 12700))]))


if __name__ == '__main__':
    main()

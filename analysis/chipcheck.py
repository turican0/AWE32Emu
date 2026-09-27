# -*- coding: utf-8 -*-
"""Economical check that AWE32Emu and 86Box produce the same chip output.

Both use the same snd_emu8k.c; the VM was shown to match a replay of its own
trace frame by frame (2026-09-16), so the 86Box side here is emu8k_ref.exe,
the plain 86Box chip driven by the trace. A short trace is built from the
AWETST25 VM trace: everything before the first block mark (initialisation
and sample upload) followed by the chosen block(s), moved to start 1 s after
the initialisation. Both programs render it with the layered state dump;
dumps (registers, envelopes, oscillator, filter, effects) and audio are
compared.

    python chipcheck.py --blocks 22            # default trace: trace/vm25u.trace (8 MB DRAM)
    python chipcheck.py --blocks 8,17 --step 2205
"""
import argparse
import os
import subprocess

import numpy as np
import soundfile as sf

import replay25 as R

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
REF = r'C:\prenos\AWE32EmuData\ref86box\build\emu8k_ref.exe'
ROM = r'C:\prenos\AWE32EmuData\rom\awe32.raw'
SR = 44100


def build_trace(src, blocks, out_path):
    R.TRACE = src
    marks, _ = R.trace_ticks()
    groups = R.block_starts_trace(marks)          # (ticks, start frame), ticks = block number
    starts = {n: fr for n, fr in groups}
    order = [fr for _, fr in groups]
    init_end = order[0]
    ranges = []
    for b in blocks:
        s = starts[b]
        later = [fr for fr in order if fr > s]
        ranges.append((s, later[0] if later else None))
    lines = open(src).read().splitlines()
    out, t = [], None
    shift_base = init_end + SR
    for ln in lines:
        p = ln.split()
        if not p or ln[0] == '#' or p[0] == 'R':
            continue
        if int(p[0]) < init_end:
            out.append(ln)
    cursor = shift_base
    for s, e in ranges:
        last = s
        for ln in lines:
            p = ln.split()
            if not p or ln[0] == '#' or p[0] == 'R':
                continue
            fr = int(p[0])
            if fr < s or (e is not None and fr >= e):
                continue
            nf = cursor + (fr - s)
            out.append('%d %s' % (nf, ' '.join(p[1:])))
            last = fr
        cursor += (last - s) + 4 * SR
    open(out_path, 'w').write('\n'.join(out) + '\n')
    return cursor


def run(cmd, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise SystemExit('failed: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--blocks', required=True)
    ap.add_argument('--trace', default=os.path.join(HERE, 'trace', 'vm25u.trace'))
    ap.add_argument('--ram', default='8192')
    ap.add_argument('--step', default='4410')
    args = ap.parse_args()
    blocks = [int(b) for b in args.blocks.split(',')]
    tag = 'cc_' + '_'.join(str(b) for b in blocks)
    mini = os.path.join(HERE, tag + '.trace')
    end = build_trace(args.trace, blocks, mini)

    a_wav, a_dump = os.path.join(HERE, tag + '_emu.wav'), os.path.join(HERE, tag + '_emu.dump')
    b_wav, b_dump = os.path.join(HERE, tag + '_86box.wav'), os.path.join(HERE, tag + '_86box.dump')
    run([EXE, '--replay', mini, '--rom', ROM, '--chip', '86box', '--ram', args.ram, '--wav', a_wav],
        {'EMU8K_STATE_DUMP': a_dump, 'EMU8K_STATE_STEP': args.step})
    run([REF, '--rom', ROM, '--trace', mini, '--ram', args.ram, '--frames', str(end), '--wav', b_wav],
        {'EMU8K_STATE_DUMP': b_dump, 'EMU8K_STATE_STEP': args.step})

    # both renders must cover the compared range; AWE32Emu stops shortly
    # after the last event, emu8k_ref at --frames
    a = sf.read(a_wav, dtype='int16')[0]
    b = sf.read(b_wav, dtype='int16')[0]
    n = min(len(a), len(b), end - SR)

    # state dumps, layer by layer
    import statecmp
    A = statecmp.load(a_dump, 0, n)
    B = statecmp.load(b_dump, 0, n)
    bad = {}
    for k in set(A) | set(B):
        if A.get(k) != B.get(k):
            bad.setdefault(k[0], []).append(k)
    print('blocks %s: %d dump points' % (args.blocks, len({k[1] for k in A})))
    for layer in 'REOFS':
        ks = sorted(bad.get(layer, []))
        print('  layer %s: %s' % (layer, 'same' if not ks else '%d differ, first frame %d voice %d'
                                                    % (len(ks), ks[0][1], ks[0][2])))
    # audio
    a = sf.read(a_wav, dtype='int16')[0]
    b = sf.read(b_wav, dtype='int16')[0]
    n = min(len(a), len(b), end - SR)
    d = np.any(a[:n] != b[:n], axis=1)
    print('  audio: %d frames compared, %d differ%s'
          % (n, int(d.sum()), '' if not d.any() else ', first %d' % int(np.argmax(d))))
    print('RESULT: %s' % ('SAME' if not bad and not d.any() else 'DIFFERENT'))


if __name__ == '__main__':
    main()

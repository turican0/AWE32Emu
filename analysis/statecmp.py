# -*- coding: utf-8 -*-
"""Compare two EMU8K_STATE_DUMP files layer by layer.

Layers: S (effects/output), R (registers), E (envelopes), O (oscillator/LFO),
F (filter). For each layer: first frame and voice where A and B differ, which
fields and their values; lines present in only one file are reported too.
Registers are compared first because a register difference explains the
rest.

    python statecmp.py vm.dump replay.dump [--from FRAME] [--to FRAME] [--max 5]
"""
import argparse

FIELDS = {
    'S': ['out_l', 'out_r', '|', 'cho', 'cho_write', 'cho_lfo', '|', 'rev', 'rev_preset', 'rev_pre_pos',
          'rev_comb_pos', '|', 'eq', 'eq_bass', 'eq_treble'],
    'R': ['ccca', 'cpf', 'ptrx', 'cvcf', 'vtft', 'psst', 'csl', 'ip', 'ifatn', 'pefe', 'fmmod', 'tremfrq',
          'fm2frq2', 'envvol', 'dcysusv', 'atkhldv', 'envval', 'dcysus', 'atkhld', 'lfo1val', 'lfo2val'],
    'E': ['engine', 'vstate', 'vamp', 'vdb', 'vfrac', 'mstate', 'mamp', 'mdb', 'mfrac', 'initial_att',
          'initial_filter'],
    'O': ['addr', 'loop_start', 'loop_end', 'cpf_pitch', 'lfo1', 'lfo2', 'lfo1_delay', 'lfo2_delay',
          'vol_l', 'vol_r'],
    'F': ['q', 'att', 'oct_target', 'oct_curr', 'lp', 'bp'],
}


def load(path, lo, hi):
    out = {}
    for line in open(path):
        p = line.split()
        if len(p) < 3 or p[0] not in FIELDS or not p[1].isdigit() or (p[0] != "S" and not p[2].isdigit()):
            continue
        fr = int(p[1])
        if fr < lo or fr > hi:
            continue
        if p[0] == 'S':
            out[('S', fr, -1)] = p[2:]
        else:
            out[(p[0], fr, int(p[2]))] = p[3:]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--from', dest='lo', type=int, default=0)
    ap.add_argument('--to', dest='hi', type=int, default=1 << 62)
    ap.add_argument('--max', type=int, default=5)
    args = ap.parse_args()
    A = load(args.a, args.lo, args.hi)
    B = load(args.b, args.lo, args.hi)
    framesA = {k[1] for k in A}
    framesB = {k[1] for k in B}
    common = framesA & framesB
    print('dump frames: A %d, B %d, common %d' % (len(framesA), len(framesB), len(common)))
    for layer in ['R', 'E', 'O', 'F', 'S']:
        keys = sorted(k for k in set(A) | set(B) if k[0] == layer and k[1] in common)
        diffs, shown = 0, 0
        for k in keys:
            a, b = A.get(k), B.get(k)
            if a == b:
                continue
            diffs += 1
            if shown < args.max:
                shown += 1
                if a is None or b is None:
                    print('  %s frame %d voice %d: only in %s' % (layer, k[1], k[2], 'B' if a is None else 'A'))
                else:
                    names = FIELDS[layer]
                    fd = ['%s %s/%s' % (names[i] if i < len(names) else i, a[i], b[i])
                          for i in range(min(len(a), len(b))) if a[i] != b[i]]
                    print('  %s frame %d voice %d: %s' % (layer, k[1], k[2], ', '.join(fd)))
        print('layer %s: %d differing lines of %d' % (layer, diffs, len(keys)))


if __name__ == '__main__':
    main()

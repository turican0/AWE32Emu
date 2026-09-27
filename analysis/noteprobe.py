# -*- coding: utf-8 -*-
"""Data probe: the whole life of every note in two port traces.

A note starts with a DCYSUSV write with bits 15 and 7 clear (as notes_cmp)
after the sample upload. Its life are all writes to the same voice until the
next note start on that voice. Notes are paired by order of their starts
(voice numbers are ignored). For every pair the writes AFTER the start are
compared as (register, value) sequences, and the time of the first release
(DCYSUSV / DCYSUS with bit 15) relative to the start.

Report: per register, how many notes have the same count and values; the
distribution of release-time differences; first differing notes in detail.

    python noteprobe.py ours.trace driver.trace [--from 0 --to 1e12] [--show 5]
"""
import argparse
from collections import Counter, defaultdict

ap = argparse.ArgumentParser()
ap.add_argument('a')
ap.add_argument('b')
ap.add_argument('--show', type=int, default=5)
ap.add_argument('--dfrom', type=int, default=0, help='driver trace frame window start')
ap.add_argument('--dto', type=int, default=1 << 62)
ap.add_argument('--max', type=int, default=1 << 30, help='compare at most N notes')
a = ap.parse_args()

NAMES = {
    0x620: ['CPF', 'PTRX', 'CVCF', 'VTFT', 'Z2', 'Z1', 'PSST', 'CSL'],
    0x622: ['CPF^', 'PTRX^', 'CVCF^', 'VTFT^', 'Z2^', 'Z1^', 'PSST^', 'CSL^'],
    0xA20: ['CCCA', 'HW', 'INIT1', 'INIT3', 'ENVVOL', 'DCYSUSV', 'ENVVAL', 'DCYSUS'],
    0xA22: ['CCCA^', 'HW^', 'INIT2', 'INIT4', 'ATKHLDV', 'LFO1VAL', 'ATKHLD', 'LFO2VAL'],
    0xE20: ['IP', 'IFATN', 'PEFE', 'FMMOD', 'TREMFRQ', 'FM2FRQ2', 'R6', 'ID'],
}


def notes(path, lo, hi):
    ptr = 0
    started = False
    out = []           # list of dict(start, voice, writes=[(dt, reg, val)])
    cur = {}           # voice -> index into out
    for line in open(path):
        if not line or line[0] in '#R':
            continue
        p = line.split()
        if len(p) < 3:
            continue
        fr, port, val = int(p[0]), int(p[1], 16), int(p[2], 16)
        if port == 0xE22:
            ptr = val
            continue
        if port not in NAMES:
            continue
        reg = NAMES[port][(ptr >> 5) & 7]
        v = ptr & 0x1F
        if reg == 'ATKHLDV' and val != 0:
            started = True
        if not started or fr < lo or fr > hi:
            continue
        if reg == 'DCYSUSV' and not (val & 0x8000) and not (val & 0x0080):
            cur[v] = len(out)
            out.append({'start': fr, 'voice': v, 'writes': [], 'key': None})
            continue
        if v in cur:
            # the voice is taken for the next note (allocation writes DCYSUSV
            # 0x00FF / 0x807F): the life of this note ends here
            if reg == 'DCYSUSV' and val in (0x00FF, 0x807F, 0x0080):
                n = out[cur[v]]
                n['writes'].append((fr - n['start'], 'TAKEN', val))
                del cur[v]
                continue
            n = out[cur[v]]
            n['writes'].append((fr - n['start'], reg, val))
    return out


A = notes(a.a, 0, 1 << 62)
B = notes(a.b, a.dfrom, a.dto)
n = min(len(A), len(B), a.max)
print('notes: ours %d, driver %d, compared %d' % (len(A), len(B), n))

same_cnt = Counter()
same_val = Counter()
seen = Counter()
rel_diff = []
extra = Counter()
missing = Counter()
examples = defaultdict(list)
for i in range(n):
    wa = defaultdict(list)
    wb = defaultdict(list)
    for dt, reg, val in A[i]['writes']:
        wa[reg].append((dt, val))
    for dt, reg, val in B[i]['writes']:
        wb[reg].append((dt, val))
    for reg in set(wa) | set(wb):
        seen[reg] += 1
        va = [v for _, v in wa.get(reg, [])]
        vb = [v for _, v in wb.get(reg, [])]
        if len(va) == len(vb):
            same_cnt[reg] += 1
            if va == vb:
                same_val[reg] += 1
                continue
        if len(va) > len(vb):
            extra[reg] += 1
        elif len(va) < len(vb):
            missing[reg] += 1
        if len(examples[reg]) < a.show:
            examples[reg].append((i, va[:6], vb[:6]))

    def first_rel(w):
        for dt, reg, val in w:
            if reg in ('DCYSUSV', 'DCYSUS') and (val & 0x8000):
                return dt
        return None
    ra, rb = first_rel(A[i]['writes']), first_rel(B[i]['writes'])
    if ra is not None and rb is not None:
        rel_diff.append(ra - rb)

print('\nregister   notes  same-count  same-values   ours-more  ours-fewer')
for reg in sorted(seen, key=lambda r: -seen[r]):
    print('%-9s %6d %10d %12d %10d %10d' % (reg, seen[reg], same_cnt[reg], same_val[reg], extra[reg], missing[reg]))

if rel_diff:
    import statistics
    rd = sorted(rel_diff)
    print('\nrelease time ours - driver [frames]: median %d, p5 %d, p95 %d, |d|>441 (10 ms): %d of %d'
          % (statistics.median(rd), rd[len(rd) // 20], rd[len(rd) * 19 // 20],
             sum(1 for d in rd if abs(d) > 441), len(rd)))

print('\nexamples (note index: ours values | driver values):')
for reg in sorted(examples, key=lambda r: -(seen[r] - same_val[r])):
    if seen[reg] == same_val[reg]:
        continue
    print(' %s:' % reg)
    for i, va, vb in examples[reg]:
        print('   #%d  %s | %s' % (i, ' '.join('%04X' % v for v in va), ' '.join('%04X' % v for v in vb)))

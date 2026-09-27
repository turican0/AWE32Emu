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
import argparse  # noqa
from collections import Counter, defaultdict

ap = argparse.ArgumentParser()
ap.add_argument('a')
ap.add_argument('b')
ap.add_argument('--show', type=int, default=5)
ap.add_argument('--dfrom', type=int, default=0, help='driver trace frame window start')
ap.add_argument('--dto', type=int, default=1 << 62)
ap.add_argument('--max', type=int, default=1 << 30, help='compare at most N notes')
a = None

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



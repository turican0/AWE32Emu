"""Extracts all envelope delay generator values from an SF1 bank.

SF1 stores times directly in **milliseconds**, so the input of the conversion
is an integer and the constant the driver uses can be verified against it.
"""
import struct
from collections import Counter

SBK = 'C:/prenos/AWE32EmuData/cdrom/2/WIN95/DRIVERS/SYNTHGM.SBK'
DELAY_MOD, DELAY_VOL = 25, 33

d = open(SBK, 'rb').read()


def chunks(start, end):
    p = start
    while p + 8 <= end:
        cid = d[p:p + 4]
        ln = struct.unpack_from('<I', d, p + 4)[0]
        yield cid, p + 8, ln
        p += 8 + ln + (ln & 1)


def find(path):
    # walk RIFF/LIST recursively and find the sub-chunk named path[-1]
    def walk(start, end, want):
        for cid, off, ln in chunks(start, end):
            if cid == b'LIST':
                sub = d[off:off + 4]
                r = walk(off + 4, off + ln, want)
                if r:
                    return r
            elif cid == want:
                return off, ln
        return None
    return walk(12, len(d), path)


vals = Counter()
for name in (b'pgen', b'igen'):
    r = find(name)
    if not r:
        print('%s not found' % name.decode())
        continue
    off, ln = r
    n = ln // 4
    for i in range(n):
        op, amt = struct.unpack_from('<Hh', d, off + i * 4)
        if op in (DELAY_MOD, DELAY_VOL):
            vals[(name.decode(), op, amt)] += 1

print('%-6s %-12s %-8s %-7s %s' % ('chunk', 'generator', 'ms', 'count', 'steps: ms*2^(12516/1200)/1000 | ms*1000/725'))
for (blk, op, amt), n in sorted(vals.items()):
    a = int(amt * (2.0 ** (12516.0 / 1200.0)) / 1000.0)
    b = int(amt * 1000 / 725) if amt >= 0 else 0
    gen = 'delayModEnv' if op == DELAY_MOD else 'delayVolEnv'
    mark = '   <-- differs' if a != b else ''
    print('%-6s %-12s %-8d %-7d %-6d | %-6d%s' % (blk, gen, amt, n, a, b, mark))

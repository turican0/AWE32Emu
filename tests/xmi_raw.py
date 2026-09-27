"""Raw analysis of an XMI: IFF chunks and all control messages including XMIDI.

XMIDI control messages (Miles AIL):
  110 channel lock        111 lock protect       112 voice protect
  113 timbre protect      114 bank select        115 indirect CC prefix
  116 loop start          117 loop next/break    118 clear beat count
  119 callback trigger                           120 sequence branch index
"""
import struct
import sys
from collections import Counter

path = sys.argv[1]
d = open(path, 'rb').read()

XMIDI = {110: 'channel lock', 111: 'lock protect', 112: 'voice protect',
         113: 'timbre protect', 114: 'bank select', 115: 'indirect CC prefix',
         116: 'ZACATEK SMYCKY', 117: 'NEXT/BREAK SMYCKY', 118: 'clear beats',
         119: 'callback', 120: 'branch index'}


def walk(off, end, depth=0):
    while off + 8 <= end:
        cid = d[off:off + 4]
        ln = struct.unpack_from('>I', d, off + 4)[0]
        body = off + 8
        print('%s%s  %d B  @0x%X' % ('  ' * depth, cid.decode('latin1'), ln, off))
        if cid in (b'FORM', b'CAT '):
            print('%s  typ %s' % ('  ' * depth, d[body:body + 4].decode('latin1')))
            walk(body + 4, body + ln, depth + 1)
        elif cid == b'RBRN':
            n = struct.unpack_from('<H', d, body)[0]
            print('%s  ** RBRN: %d branches **' % ('  ' * depth, n))
            for i in range(min(n, 12)):
                bid, boff = struct.unpack_from('<HI', d, body + 2 + i * 6)
                print('%s     branch %d -> offset %d' % ('  ' * depth, bid, boff))
        elif cid == b'EVNT':
            scan_events(body, body + ln, '  ' * depth)
        off = body + ln + (ln & 1)


def scan_events(off, end, ind):
    ccs = Counter()
    xm = []
    notes = 0
    p = off
    run = None
    while p < end:
        # delay: bytes < 0x80 are summed
        while p < end and d[p] < 0x80:
            p += 1
        if p >= end:
            break
        st = d[p]
        if st & 0x80:
            run = st
            p += 1
        else:
            st = run
        hi = st & 0xF0
        ch = st & 0x0F
        if st == 0xFF:
            mt = d[p]; p += 1
            ln = 0
            while True:
                b = d[p]; p += 1
                ln = (ln << 7) | (b & 127)
                if not b & 128:
                    break
            p += ln
        elif hi == 0x90:                      # XMIDI: note + length
            note, vel = d[p], d[p + 1]
            p += 2
            notes += 1
            ln = 0
            while True:
                b = d[p]; p += 1
                ln = (ln << 7) | (b & 127)
                if not b & 128:
                    break
        elif hi == 0xB0:
            cc, val = d[p], d[p + 1]
            p += 2
            ccs[cc] += 1
            if cc in XMIDI:
                xm.append((ch, cc, val))
        elif hi in (0xC0, 0xD0):
            p += 1
        else:
            p += 2

    print('%s  not %d' % (ind, notes))
    print('%s  control messages: %s' % (ind, ', '.join(
        '%d x%d' % (c, n) for c, n in sorted(ccs.items()))))
    if xm:
        print('%s  XMIDI messages:' % ind)
        for ch, cc, val in xm:
            print('%s     kanal %-3d CC%-4d %-3d  %s' % (ind, ch, cc, val, XMIDI[cc]))
    else:
        print('%s  XMIDI messages: none' % ind)


walk(0, len(d))

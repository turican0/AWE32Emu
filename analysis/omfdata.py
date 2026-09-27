# -*- coding: utf-8 -*-
"""Dump the data segments of an OMF object into one flat file.

    python omfdata.py embed.obj embed.bin
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omfdis import parse  # noqa: E402

segs, pubs, seg_data, fix = parse(sys.argv[1])
out = bytearray()
for si, seg in enumerate(segs):
    if seg is None or 'CODE' in seg[1].upper():
        continue
    print('segment %-16s class %-8s length %6d at %6d' % (seg[0], seg[1], seg[2], len(out)))
    out += seg_data[si]
for (s, off), n in sorted(pubs.items()):
    if segs[s] and 'CODE' not in segs[s][1].upper():
        print('  public %s %s:%04X' % (n, segs[s][0], off))
open(sys.argv[2], 'wb').write(out)
print('written', len(out), 'bytes')

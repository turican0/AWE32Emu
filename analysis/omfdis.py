# -*- coding: utf-8 -*-
"""Disassembler for 16-bit Microsoft OMF object files (the AWE32 SDK libs).

Reads LNAMES/SEGDEF/EXTDEF/PUBDEF/LEDATA/FIXUPP, disassembles every code
segment with capstone (16-bit), labels publics and names the targets of
fixups (calls to externs, data references to segments), so the listing
reads like source-level assembly.

    python omfdis.py midieng.obj > midieng.asm
    python omfdis.py midieng.obj --func AWE32NOTEON
"""
import struct
import sys

import capstone


def records(data):
    i = 0
    while i + 3 <= len(data):
        typ = data[i]
        ln = struct.unpack_from('<H', data, i + 1)[0]
        body = data[i + 3:i + 3 + ln - 1]      # without checksum
        yield typ, body
        i += 3 + ln
        if typ in (0x8A, 0x8B):                  # MODEND
            break


def idx(b, p):
    """OMF index: 1 or 2 bytes."""
    v = b[p]
    if v & 0x80:
        return ((v & 0x7F) << 8) | b[p + 1], p + 2
    return v, p + 1


def name(b, p):
    n = b[p]
    return b[p + 1:p + 1 + n].decode('latin-1'), p + 1 + n


def parse(path):
    data = open(path, 'rb').read()
    lnames = ['']
    segs = [None]             # (name, class, length)
    exts = ['']
    pubs = {}                 # (seg, off) -> name
    seg_data = {}             # seg -> bytearray
    fix = {}                  # (seg, off) -> target text
    last = None               # (seg, base offset) of last LEDATA
    for typ, b in records(data):
        if typ == 0x96:                              # LNAMES
            p = 0
            while p < len(b):
                s, p = name(b, p)
                lnames.append(s)
        elif typ in (0x98, 0x99):                    # SEGDEF
            p = 1
            ln = struct.unpack_from('<H' if typ == 0x98 else '<I', b, p)[0]
            p += 2 if typ == 0x98 else 4
            ni, p = idx(b, p)
            ci, p = idx(b, p)
            segs.append((lnames[ni], lnames[ci], ln))
            seg_data[len(segs) - 1] = bytearray(ln)
        elif typ in (0x8C, 0xB4):                    # EXTDEF / LEXTDEF
            p = 0
            while p < len(b):
                s, p = name(b, p)
                _, p = idx(b, p)
                exts.append(s)
        elif typ in (0x90, 0x91, 0xB6, 0xB7):        # PUBDEF / LPUBDEF
            p = 0
            _, p = idx(b, p)                         # group
            si, p = idx(b, p)
            if si == 0:
                p += 2
            w = 2 if typ in (0x90, 0xB6) else 4
            while p < len(b):
                s, p = name(b, p)
                off = struct.unpack_from('<H' if w == 2 else '<I', b, p)[0]
                p += w
                _, p = idx(b, p)
                pubs[(si, off)] = s
        elif typ in (0xA0, 0xA1):                    # LEDATA
            p = 0
            si, p = idx(b, p)
            w = 2 if typ == 0xA0 else 4
            off = struct.unpack_from('<H' if w == 2 else '<I', b, p)[0]
            p += w
            chunk = b[p:]
            seg_data[si][off:off + len(chunk)] = chunk
            last = (si, off)
        elif typ in (0x9C, 0x9D) and last:           # FIXUPP
            p = 0
            while p < len(b):
                c = b[p]
                if not c & 0x80:                     # THREAD - skip
                    p += 1
                    if (c >> 6) & 1 == 0 and (c >> 2) & 7 < 3:
                        _, p = idx(b, p)
                    continue
                loc = ((c << 8) | b[p + 1]) & 0x3FF
                p += 2
                fd = b[p]
                p += 1
                frame_thread = fd & 0x80
                fmethod = (fd >> 4) & 7
                target_thread = fd & 0x08
                tmethod = fd & 3
                if not frame_thread and fmethod < 3:
                    _, p = idx(b, p)
                tname = '?'
                if not target_thread:
                    ti, p = idx(b, p)
                    if tmethod == 0:
                        tname = segs[ti][0] if ti < len(segs) and segs[ti] else 'seg%d' % ti
                    elif tmethod == 2:
                        tname = exts[ti] if ti < len(exts) else 'ext%d' % ti
                    else:
                        tname = 'grp%d' % ti
                if not (fd & 0x04):
                    disp = struct.unpack_from('<H', b, p)[0]
                    p += 2
                    if tmethod == 0 and disp:
                        tname += '+%X' % disp
                fix[(last[0], last[1] + loc)] = tname
    return segs, pubs, seg_data, fix


def main():
    segs, pubs, seg_data, fix = parse(sys.argv[1])
    only = sys.argv[sys.argv.index('--func') + 1].upper() if '--func' in sys.argv else None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.skipdata = True
    for si, seg in enumerate(segs):
        if seg is None or 'CODE' not in seg[1].upper():
            continue
        sname, cls, ln = seg
        code = bytes(seg_data[si])
        starts = sorted(off for (s, off) in pubs if s == si)
        printing = only is None
        print('; segment %s class %s length %X' % (sname, cls, ln))
        for ins in md.disasm(code, 0):
            if (si, ins.address) in pubs:
                label = pubs[(si, ins.address)]
                if only is not None:
                    printing = label.upper() == only
                if printing:
                    print('\n%s:' % label)
            if not printing:
                continue
            ref = ''
            for k in range(ins.address, ins.address + ins.size):
                if (si, k) in fix:
                    ref = '    ; -> ' + fix[(si, k)]
                    break
            print('  %04X  %-8s %s%s' % (ins.address, ins.mnemonic, ins.op_str, ref))
    # data segments: publics only
    if only is None:
        for (s, off), n in sorted(pubs.items()):
            if segs[s] and 'CODE' not in segs[s][1].upper():
                print('; data %s:%04X %s' % (segs[s][0], off, n))


if __name__ == '__main__':
    main()

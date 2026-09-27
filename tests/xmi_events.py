#!/usr/bin/env python
"""Vypis MIDI udalosti z XMI (format IFF FORM XMID, chunk EVNT).

    python ../AWE32EmuData/tests/xmi_events.py ../AWE32EmuData/midi/004_C2INTRO_w.xmi --cc 10,11,91,7
"""
import argparse
import collections
import struct


def chunks(buf, off, end):
    while off + 8 <= end:
        cid = buf[off:off + 4]
        size = struct.unpack(">I", buf[off + 4:off + 8])[0]
        body = off + 8
        if cid in (b"FORM", b"CAT ", b"LIST"):
            yield cid, buf[body:body + 4], body + 4, body + size
            off = body + size + (size & 1)
        else:
            yield cid, None, body, body + size
            off = body + size + (size & 1)


def find_evnt(buf):
    out = []
    stack = [(0, len(buf))]
    while stack:
        s, e = stack.pop()
        for cid, sub, b, en in chunks(buf, s, e):
            if cid == b"EVNT":
                out.append((b, en))
            elif sub is not None:
                stack.append((b, en))
    return out


def events(buf, start, end):
    off = start
    t = 0
    running = 0
    while off < end:
        # delay: bytes < 0x80 are summed
        while off < end and buf[off] < 0x80:
            t += buf[off]
            off += 1
        if off >= end:
            break
        st = buf[off]
        if st < 0x80:
            st = running
        else:
            off += 1
        running = st
        hi = st & 0xF0
        if hi == 0x90:
            note, vel = buf[off], buf[off + 1]
            off += 2
            dur = 0
            while True:
                b = buf[off]; off += 1
                dur = (dur << 7) | (b & 0x7F)
                if b < 0x80:
                    break
            yield t, st, note, vel, dur
        elif hi in (0x80, 0xA0, 0xB0, 0xE0):
            yield t, st, buf[off], buf[off + 1], None
            off += 2
        elif hi in (0xC0, 0xD0):
            yield t, st, buf[off], None, None
            off += 1
        elif st == 0xFF:
            meta = buf[off]; off += 1
            ln = 0
            while True:
                b = buf[off]; off += 1
                ln = (ln << 7) | (b & 0x7F)
                if b < 0x80:
                    break
            off += ln
            if meta == 0x2F:
                break
        elif st in (0xF0, 0xF7):
            ln = 0
            while True:
                b = buf[off]; off += 1
                ln = (ln << 7) | (b & 0x7F)
                if b < 0x80:
                    break
            off += ln
        else:
            break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--cc", default="", help="print the course of these controllers")
    ap.add_argument("--track", type=int, default=0)
    ap.add_argument("--notes", action="store_true")
    args = ap.parse_args()

    buf = open(args.path, "rb").read()
    ev = find_evnt(buf)
    print(f"{len(ev)} stop(y) EVNT")
    s, e = ev[args.track]
    evs = list(events(buf, s, e))
    print(f"track {args.track}: {len(evs)} events\n")

    want = {int(x) for x in args.cc.split(",") if x.strip()}
    ccvals = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    prog = {}
    notes = collections.Counter()
    for t, st, a, b, dur in evs:
        ch = st & 0x0F
        if st & 0xF0 == 0xB0:
            ccvals[ch][a][b] += 1
        elif st & 0xF0 == 0xC0:
            prog.setdefault(ch, []).append((t, a))
        elif st & 0xF0 == 0x90:
            notes[ch] += 1

    print("channel  program           notes  CC (value x count)")
    for ch in sorted(set(list(ccvals) + list(prog) + list(notes))):
        pr = ",".join(f"{p}@{t}" for t, p in prog.get(ch, []))
        print(f"  {ch:2}   {pr:16} {notes.get(ch,0):5}")
        for cc in sorted(ccvals[ch]):
            if want and cc not in want:
                continue
            vals = ", ".join(f"{v}x{n}" for v, n in ccvals[ch][cc].most_common(6))
            print(f"          CC{cc:<3} {vals}")

    if args.notes:
        print("\nfirst 60 events:")
        for t, st, a, b, dur in evs[:60]:
            print(f"  t={t:6} {st:02X} {a:3} {b if b is not None else '':>4} "
                  f"{'dur=' + str(dur) if dur is not None else ''}")


if __name__ == "__main__":
    main()

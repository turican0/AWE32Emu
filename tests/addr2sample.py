#!/usr/bin/env python
"""Translates a sample address (what `tune.py --by sample` prints) into a name.

`note_probe.py` groups notes by the **sample address** in the CCCA register,
because that is unambiguous and independent of how the bank is named. To find
a cause, though, we need to know which instrument it is.

    python tests/addr2sample.py SYNTHGM.SBK 069091 0449B4
    python tests/addr2sample.py SYNTHGM.SBK --list
"""
import argparse
import struct


def riff(path):
    d = open(path, "rb").read()

    def chunks(pos, end):
        out = []
        while pos + 8 <= end:
            tag = d[pos:pos + 4].decode("latin1")
            sz = struct.unpack_from("<I", d, pos + 4)[0]
            out.append((tag, pos + 8, sz))
            pos += 8 + sz + (sz & 1)
        return out

    m = {}
    total = struct.unpack_from("<I", d, 4)[0]
    for tag, off, sz in chunks(12, 8 + total):
        if tag == "LIST":
            for t2, o2, s2 in chunks(off + 4, off + sz):
                m[t2] = (o2, s2)
        else:
            m[tag] = (off, sz)
    return d, m


def samples(path):
    d, m = riff(path)
    ho, hs = m["shdr"]
    sf1 = "snam" in m
    out = []
    if sf1:
        no, _ = m["snam"]
        for i in range(hs // 16):
            st, en, ls, le = struct.unpack_from("<IIII", d, ho + i * 16)
            nm = d[no + i * 20:no + i * 20 + 20].split(b"\0")[0].decode("latin1")
            out.append((st, en, ls, le, nm.strip()))
    else:
        for i in range(hs // 46 - 1):
            p = ho + i * 46
            nm = d[p:p + 20].split(b"\0")[0].decode("latin1")
            st, en, ls, le = struct.unpack_from("<IIII", d, p + 20)
            out.append((st, en, ls, le, nm.strip()))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bank")
    ap.add_argument("addr", nargs="*", help="hex addresses, several at once")
    ap.add_argument("--list", action="store_true", help="print the whole table")
    a = ap.parse_args()

    smp = samples(a.bank)
    if a.list:
        for st, en, ls, le, nm in smp:
            print(f"{st:8d} 0x{st:06X}..0x{en:06X} loop {ls}..{le}  {nm}")
        return

    # The address in CCCA lies 46 words before the sample start per shdr - it
    # is the mandatory padding SoundFont prescribes between samples, and the
    # driver moves the start by it. It holds for every sample checked so far.
    PAD = 46

    for t in a.addr:
        v = int(t, 16) + PAD
        hit = [s for s in smp if s[0] <= v < s[1]]
        if not hit:
            # The address may be a few words off (interpolator offset), so
            # offer the nearest start.
            near = min(smp, key=lambda s: abs(s[0] - v))
            print(f"0x{v - PAD:06X}  outside all samples; nearest start "
                  f"0x{near[0] - PAD:06X} ({near[4]}), difference {v - near[0]}")
            continue
        for st, en, ls, le, nm in hit:
            print(f"0x{v - PAD:06X}  {nm:24s} 0x{st:06X}..0x{en:06X} "
                  f"length {en - st}, offset {v - st}, "
                  f"loop {ls - st}..{le - st}")


main()

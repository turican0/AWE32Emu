#!/usr/bin/env python
"""Extracts pairs (SF1 cutoff, SF2 initialFilterFc) from the same bank in two formats.

Creative released `SYNTHGM` as `.SBK` (SoundFont 1.0) and as `.SF2`. They
describe the same presets, so for each zone the **raw SF1 value** and the
**absolute cents per SF2** can be put side by side - and from that one can
read which frequency Creative meant by which register. No estimate, no guide:
straight from them.

    python tests/cutoff_pairs.py SYNTHGM.SBK SYNTHGM.SF2
"""
import argparse
import collections
import struct

GEN_FILTER_FC = 8
GEN_FILTER_Q = 9


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


def inst_gens(path):
    """{instrument name: [{gen: value} for every zone]}"""
    d, m = riff(path)
    io_, is_ = m["inst"]
    bo, bs = m["ibag"]
    go, gs = m["igen"]

    n_inst = is_ // 22
    bags = [struct.unpack_from("<HH", d, bo + i * 4)[0] for i in range(bs // 4)]
    gens = [struct.unpack_from("<Hh", d, go + i * 4) for i in range(gs // 4)]

    out = {}
    for i in range(n_inst - 1):
        p = io_ + i * 22
        nm = d[p:p + 20].split(b"\0")[0].decode("latin1").strip()
        b0 = struct.unpack_from("<H", d, p + 20)[0]
        b1 = struct.unpack_from("<H", d, io_ + (i + 1) * 22 + 20)[0]
        zones = []
        for b in range(b0, b1):
            g0 = bags[b]
            g1 = bags[b + 1] if b + 1 < len(bags) else len(gens)
            zones.append({op: v for op, v in gens[g0:g1]})
        out.setdefault(nm, []).extend(zones)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sf1")
    ap.add_argument("sf2")
    a = ap.parse_args()

    A = inst_gens(a.sf1)
    B = inst_gens(a.sf2)

    spolecne = sorted(set(A) & set(B))
    print("instruments in both:", len(spolecne))

    dvojice = collections.defaultdict(collections.Counter)
    for nm in spolecne:
        za, zb = A[nm], B[nm]
        if len(za) != len(zb):
            continue
        for x, y in zip(za, zb):
            if GEN_FILTER_FC in x and GEN_FILTER_FC in y:
                dvojice[x[GEN_FILTER_FC]][y[GEN_FILTER_FC]] += 1

    print()
    print(" SF1   register  SF2 cents   Hz        count   (SF2 = what Creative")
    print("                                                means by that value)")
    for v in sorted(dvojice):
        for cents, n in dvojice[v].most_common():
            hz = 8.176 * 2.0 ** (cents / 1200.0)
            print(f"{v:5d}   {v * 2:5d}   {cents:9d}   {hz:8.1f}   {n:5d}")


main()

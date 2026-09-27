#!/usr/bin/env python
"""Splits the groups of `note_probe.py --by sample` by what that zone has in the bank.

`--by q` says notes with resonance match worse. But in `SYNTHGM.SBK` 34 of 54
zones with `Q > 0` **also** have an envelope on the filter cutoff, so the two
properties are confounded and `--by q` cannot tell which is to blame. This
script takes the per-sample deviations and sums them separately for the four
combinations.

    python tests/zone_class.py --bank SYNTHGM.SBK \\
        --base out/tune/dance-bp --ref "...mp3"
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict

GEN_Q = 9
GEN_MODENV_TO_FC = 11
GEN_SAMPLE = 53
PAD = 46


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


def sample_props(bank):
    """sample address (as CCCA writes it) -> (has Q, has an envelope on the filter)"""
    d, m = riff(bank)
    ho, hs = m["shdr"]
    starts = [struct.unpack_from("<I", d, ho + i * 16)[0] for i in range(hs // 16)]

    go, gs = m["igen"]
    bo, bs = m["ibag"]
    gens = [struct.unpack_from("<Hh", d, go + i * 4) for i in range(gs // 4)]
    bags = [struct.unpack_from("<HH", d, bo + i * 4)[0] for i in range(bs // 4)]

    out = {}
    # Zones combine: the global zone of the instrument carries Q, the key zone
    # the sample. They are walked in order, keeping the last seen Q / envelope.
    drzQ = drzF = False
    for b in range(len(bags) - 1):
        z = {op: v for op, v in gens[bags[b]:bags[b + 1]]}
        if GEN_SAMPLE not in z:
            drzQ = bool(z.get(GEN_Q, 0))
            drzF = bool(z.get(GEN_MODENV_TO_FC, 0))
            continue
        sid = z[GEN_SAMPLE]
        q = bool(z.get(GEN_Q, 0)) or drzQ
        f = bool(z.get(GEN_MODENV_TO_FC, 0)) or drzF
        if 0 <= sid < len(starts):
            adr = starts[sid] - PAD
            # When the same sample uses several zones, "at least one has it" counts.
            pq, pf = out.get(adr, (False, False))
            out[adr] = (pq or q, pf or f)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bank", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--min-notes", default="8")
    a = ap.parse_args()

    props = sample_props(a.bank)

    here = os.path.dirname(os.path.abspath(__file__))
    cmd = [sys.executable, os.path.join(here, "note_probe.py"),
           "--warp", a.base + "_warp.json", "--trace", a.base + ".trace",
           "--notes", a.base + ".csv", "--ours", a.base + ".wav",
           "--ref", a.ref, "--by", "sample", "--min-notes", a.min_notes]
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")

    koše = defaultdict(list)
    for line in r.stdout.splitlines():
        m = re.match(r"sample (\S+)\s+(\d+)\s+([\d.]+) dB", line.strip())
        if not m:
            continue
        adr = int(m.group(1), 16)
        n = int(m.group(2))
        dev = float(m.group(3))
        q, f = props.get(adr, (None, None))
        if q is None:
            koše[("?", "?")].append((dev, n))
        else:
            koše[("Q" if q else "-", "envelope" if f else "-")].append((dev, n))

    print("split of the deviations by what the zone has in the bank:")
    print("%-16s %6s %6s   %s" % ("combination", "groups", "not", "weighted deviation"))
    for k in sorted(koše):
        v = koše[k]
        celkem = sum(n for _, n in v)
        if not celkem:
            continue
        vaz = sum(d * n for d, n in v) / celkem
        print("%-16s %6d %6d   %6.2f dB"
              % ("resonance %s, filter env. %s" % k if k[0] != "?" else "unknown",
                 len(v), celkem, vaz))


main()

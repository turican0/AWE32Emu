#!/usr/bin/env python
"""Dump of the generators of an SBK/SF2 bank - a preset and its instruments.

    python tests/sbk_dump.py sbk/BULLFROG.SBK --preset 5
"""
import argparse
import struct

GEN = {
    0: "startAddrsOffset", 1: "endAddrsOffset", 2: "startloopAddrsOffset",
    3: "endloopAddrsOffset", 4: "startAddrsCoarse", 5: "modLfoToPitch",
    6: "vibLfoToPitch", 7: "modEnvToPitch", 8: "initialFilterFc",
    9: "initialFilterQ", 10: "modLfoToFilterFc", 11: "modEnvToFilterFc",
    12: "endAddrsCoarse", 13: "modLfoToVolume", 15: "chorusEffectsSend",
    16: "reverbEffectsSend", 17: "pan", 21: "delayModLFO", 22: "freqModLFO",
    23: "delayVibLFO", 24: "freqVibLFO", 25: "delayModEnv", 26: "attackModEnv",
    27: "holdModEnv", 28: "decayModEnv", 29: "sustainModEnv", 30: "releaseModEnv",
    31: "keynumToModEnvHold", 32: "keynumToModEnvDecay", 33: "delayVolEnv",
    34: "attackVolEnv", 35: "holdVolEnv", 36: "decayVolEnv", 37: "sustainVolEnv",
    38: "releaseVolEnv", 39: "keynumToVolEnvHold", 40: "keynumToVolEnvDecay",
    41: "instrument", 43: "keyRange", 44: "velRange", 45: "startloopAddrsCoarse",
    46: "keynum", 47: "velocity", 48: "initialAttenuation",
    50: "endloopAddrsCoarse", 51: "coarseTune", 52: "fineTune", 53: "sampleID",
    54: "sampleModes", 55: "sf1RootPitchCents", 56: "scaleTuning",
    57: "exclusiveClass", 58: "overridingRootKey",
}


def chunks(buf):
    out = {}
    assert buf[:4] == b"RIFF"
    off, end = 12, 8 + struct.unpack("<I", buf[4:8])[0]
    while off + 8 <= end:
        cid = buf[off:off + 4]
        sz = struct.unpack("<I", buf[off + 4:off + 8])[0]
        if cid == b"LIST":
            lend = off + 8 + sz
            o = off + 12
            while o + 8 <= lend:
                sid = buf[o:o + 4]
                ssz = struct.unpack("<I", buf[o + 4:o + 8])[0]
                out[sid.decode("latin1").strip()] = (o + 8, ssz)
                o += 8 + ssz + (ssz & 1)
        off += 8 + sz + (sz & 1)
    return out


def recs(buf, chunk, size):
    o, n = chunk
    return [buf[o + i * size:o + (i + 1) * size] for i in range(n // size)]


def gens(buf, ch):
    o, n = ch
    return [struct.unpack("<Hh", buf[o + i * 4:o + i * 4 + 4]) for i in range(n // 4)]


def bags(buf, ch):
    o, n = ch
    return [struct.unpack("<HH", buf[o + i * 4:o + i * 4 + 4]) for i in range(n // 4)]


def show(label, glist):
    for g, v in glist:
        name = GEN.get(g, f"gen{g}")
        extra = f" ({v & 0xFF}..{v >> 8})" if g in (43, 44) else ""
        print(f"      {name:22} {v:6}{extra}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--preset", type=int)
    ap.add_argument("--bank", type=int, default=0)
    args = ap.parse_args()
    buf = open(args.path, "rb").read()
    c = chunks(buf)
    phdr = recs(buf, c["phdr"], 38)
    pbag = bags(buf, c["pbag"])
    pgen = gens(buf, c["pgen"])
    inst = recs(buf, c["inst"], 22)
    ibag = bags(buf, c["ibag"])
    igen = gens(buf, c["igen"])

    for i in range(len(phdr) - 1):
        name = phdr[i][:20].split(b"\0")[0].decode("latin1")
        pno, bank, bagndx = struct.unpack("<HHH", phdr[i][20:26])
        nxt = struct.unpack("<H", phdr[i + 1][24:26])[0]
        if args.preset is not None and (pno != args.preset or bank != args.bank):
            continue
        print(f"preset {bank}:{pno} '{name}'  zones {bagndx}..{nxt - 1}")
        for z in range(bagndx, nxt):
            g0 = pbag[z][0]
            g1 = pbag[z + 1][0] if z + 1 < len(pbag) else len(pgen)
            zg = pgen[g0:g1]
            print(f"   preset zone {z}:")
            show("", zg)
            ino = next((v for g, v in zg if g == 41), None)
            if ino is None:
                continue
            iname = inst[ino][:20].split(b"\0")[0].decode("latin1")
            ib0 = struct.unpack("<H", inst[ino][20:22])[0]
            ib1 = struct.unpack("<H", inst[ino + 1][20:22])[0]
            print(f"      -> instrument {ino} '{iname}' zones {ib0}..{ib1 - 1}")
            for iz in range(ib0, ib1):
                h0 = ibag[iz][0]
                h1 = ibag[iz + 1][0] if iz + 1 < len(ibag) else len(igen)
                print(f"      inst zone {iz}:")
                show("", igen[h0:h1])


if __name__ == "__main__":
    main()

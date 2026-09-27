"""Prints the structure of a SoundFont bank (SF1.0 .SBK and SF2) - presets, zones, generators.

    python tests/dump_sbk.py sbk/BULLFROG.SBK
"""
import struct, sys, os

GEN = {
    0: 'startAddrsOffset', 1: 'endAddrsOffset', 2: 'startloopAddrsOffset',
    3: 'endloopAddrsOffset', 4: 'startAddrsCoarseOffset', 5: 'modLfoToPitch',
    6: 'vibLfoToPitch', 7: 'modEnvToPitch', 8: 'initialFilterFc',
    9: 'initialFilterQ', 10: 'modLfoToFilterFc', 11: 'modEnvToFilterFc',
    12: 'endAddrsCoarseOffset', 13: 'modLfoToVolume', 15: 'chorusEffectsSend',
    16: 'reverbEffectsSend', 17: 'pan', 21: 'delayModLFO', 22: 'freqModLFO',
    23: 'delayVibLFO', 24: 'freqVibLFO', 25: 'delayModEnv', 26: 'attackModEnv',
    27: 'holdModEnv', 28: 'decayModEnv', 29: 'sustainModEnv', 30: 'releaseModEnv',
    31: 'keynumToModEnvHold', 32: 'keynumToModEnvDecay', 33: 'delayVolEnv',
    34: 'attackVolEnv', 35: 'holdVolEnv', 36: 'decayVolEnv', 37: 'sustainVolEnv',
    38: 'releaseVolEnv', 39: 'keynumToVolEnvHold', 40: 'keynumToVolEnvDecay',
    41: 'instrument', 43: 'keyRange', 44: 'velRange', 45: 'startloopAddrsCoarse',
    46: 'keynum', 47: 'velocity', 48: 'initialAttenuation',
    50: 'endloopAddrsCoarse', 51: 'coarseTune', 52: 'fineTune', 53: 'sampleID',
    54: 'sampleModes', 56: 'scaleTuning', 57: 'exclusiveClass',
    58: 'overridingRootKey',
}
RANGE_GENS = {43, 44}


def walk(buf, start, end, out, depth=0):
    p = start
    while p + 8 <= end:
        cid = buf[p:p+4].decode('latin1')
        size = struct.unpack_from('<I', buf, p+4)[0]
        data = p + 8
        if cid == 'LIST':
            walk(buf, data+4, min(data+size, end), out, depth+1)
        else:
            out[cid] = (data, size)
        p = data + size + (size & 1)
    return out


def cstr(b):
    return b.split(b'\0')[0].decode('latin1', 'replace').strip()


def main(path):
    d = open(path, 'rb').read()
    c = walk(d, 12, 8 + struct.unpack_from('<I', d, 4)[0], {})
    ver = struct.unpack_from('<H', d, c['ifil'][0])[0]
    sf1 = ver < 2
    print("%s  SoundFont %d.x  (%s)" % (os.path.basename(path), ver,
                                        'SF1 - shdr 16 B, names in snam' if sf1 else 'SF2 - shdr 46 B'))
    for k in ('isng', 'irom', 'INAM', 'ICOP'):
        if k in c:
            print("  %-5s %s" % (k, cstr(d[c[k][0]:c[k][0]+c[k][1]])))
    print()

    # --- samples ---
    if sf1:
        snam, _ = c['snam']
        nsmp = c['shdr'][1] // 16
        names = [cstr(d[snam+i*20:snam+i*20+20]) for i in range(nsmp)]
        smp = []
        for i in range(nsmp):
            o = c['shdr'][0] + i*16
            s, e, ls, le = struct.unpack_from('<4I', d, o)
            smp.append((names[i], s, e, ls, le, 0, 0, 0))
    else:
        nsmp = c['shdr'][1] // 46
        smp = []
        for i in range(nsmp):
            o = c['shdr'][0] + i*46
            s, e, ls, le, sr = struct.unpack_from('<5I', d, o+20)
            key, corr = struct.unpack_from('<Bb', d, o+40)
            smp.append((cstr(d[o:o+20]), s, e, ls, le, sr, key, corr))

    print("=== samples (%d) ===" % nsmp)
    for i, s in enumerate(smp):
        rom = ' [ROM]' if s[0].startswith('*') else ''
        print("  [%3d] %-20s %8d..%-8d loop %8d..%-8d sr=%-6d key=%-3d%s"
              % (i, s[0], s[1], s[2], s[3], s[4], s[5], s[6], rom))
    print()

    def gens(off, size):
        return [struct.unpack_from('<HH', d, off + i*4) for i in range(size // 4)]

    pgen = gens(*c['pgen']); igen = gens(*c['igen'])
    pbag = [struct.unpack_from('<HH', d, c['pbag'][0]+i*4) for i in range(c['pbag'][1]//4)]
    ibag = [struct.unpack_from('<HH', d, c['ibag'][0]+i*4) for i in range(c['ibag'][1]//4)]

    def show(gs, indent):
        for op, val in gs:
            nm = GEN.get(op, 'gen%d' % op)
            if op in RANGE_GENS:
                print("%s%-22s %d..%d" % (indent, nm, val & 0xFF, val >> 8))
            else:
                print("%s%-22s %d (0x%04X)" % (indent, nm, struct.unpack('<h', struct.pack('<H', val))[0], val))

    ninst = c['inst'][1] // 22
    print("=== instruments (%d) ===" % (ninst - 1))
    for i in range(ninst - 1):
        o = c['inst'][0] + i*22
        nm = cstr(d[o:o+20]); bag = struct.unpack_from('<H', d, o+20)[0]
        nbag = struct.unpack_from('<H', d, o+22+20)[0]
        print("  [%2d] %s" % (i, nm))
        for b in range(bag, nbag):
            g0 = ibag[b][0]; g1 = ibag[b+1][0]
            print("       zone %d:" % (b - bag))
            show(igen[g0:g1], "         ")

    nphdr = c['phdr'][1] // 38
    print("\n=== presets (%d) ===" % (nphdr - 1))
    for i in range(nphdr - 1):
        o = c['phdr'][0] + i*38
        nm = cstr(d[o:o+20])
        pr, bk, bag = struct.unpack_from('<HHH', d, o+20)
        nbag = struct.unpack_from('<H', d, o+38+24)[0]
        print("  [%2d] bank=%-3d preset=%-3d %s" % (i, bk, pr, nm))
        for b in range(bag, nbag):
            g0 = pbag[b][0]; g1 = pbag[b+1][0]
            show(pgen[g0:g1], "         ")


if __name__ == '__main__':
    main(sys.argv[1])

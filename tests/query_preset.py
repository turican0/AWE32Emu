"""Prints one specific preset of a bank with its zones and samples laid out.

    python tests/query_preset.py rom/1mgm.sf2 0 52
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dump_sbk import GEN, RANGE_GENS, walk, cstr


def main(path, want_bank, want_preset):
    d = open(path, 'rb').read()
    c = walk(d, 12, 8 + struct.unpack_from('<I', d, 4)[0], {})
    sf1 = struct.unpack_from('<H', d, c['ifil'][0])[0] < 2

    if sf1:
        nsmp = c['shdr'][1] // 16
        snam = c['snam'][0]
        smp = []
        for i in range(nsmp):
            o = c['shdr'][0] + i * 16
            s, e, ls, le = struct.unpack_from('<4I', d, o)
            smp.append((cstr(d[snam+i*20:snam+i*20+20]), s, e, ls, le, 44100, 60))
    else:
        nsmp = c['shdr'][1] // 46
        smp = []
        for i in range(nsmp):
            o = c['shdr'][0] + i * 46
            s, e, ls, le, sr = struct.unpack_from('<5I', d, o + 20)
            key = d[o + 40]
            smp.append((cstr(d[o:o+20]), s, e, ls, le, sr, key))

    def bags(ch):
        return [struct.unpack_from('<HH', d, c[ch][0] + i*4) for i in range(c[ch][1]//4)]

    def gens(ch):
        return [struct.unpack_from('<HH', d, c[ch][0] + i*4) for i in range(c[ch][1]//4)]

    pbag, ibag = bags('pbag'), bags('ibag')
    pgen, igen = gens('pgen'), gens('igen')

    def show(gs, ind):
        for op, val in gs:
            nm = GEN.get(op, 'gen%d' % op)
            sv = struct.unpack('<h', struct.pack('<H', val))[0]
            if op in RANGE_GENS:
                print("%s%-22s %d..%d" % (ind, nm, val & 0xFF, val >> 8))
            else:
                print("%s%-22s %d" % (ind, nm, sv))

    nphdr = c['phdr'][1] // 38
    for i in range(nphdr - 1):
        o = c['phdr'][0] + i*38
        pr, bk, bag = struct.unpack_from('<HHH', d, o + 20)
        if bk != want_bank or pr != want_preset:
            continue
        nbag = struct.unpack_from('<H', d, o + 38 + 24)[0]
        print("preset bank=%d prog=%d  %s" % (bk, pr, cstr(d[o:o+20])))
        for b in range(bag, nbag):
            print("  preset zone %d:" % (b - bag))
            show(pgen[pbag[b][0]:pbag[b+1][0]], "    ")
            instr = None
            for op, val in pgen[pbag[b][0]:pbag[b+1][0]]:
                if op == 41:
                    instr = val
            if instr is None:
                continue
            io_ = c['inst'][0] + instr*22
            print("    -> instrument %d: %s" % (instr, cstr(d[io_:io_+20])))
            ib0 = struct.unpack_from('<H', d, io_ + 20)[0]
            ib1 = struct.unpack_from('<H', d, io_ + 22 + 20)[0]
            for ib in range(ib0, ib1):
                zg = igen[ibag[ib][0]:ibag[ib+1][0]]
                print("       zone %d:" % (ib - ib0))
                show(zg, "         ")
                for op, val in zg:
                    if op == 53 and val < len(smp):
                        s = smp[val]
                        print("         -> vzorek %-20s start=%d loop=%d..%d sr=%d rootkey=%d"
                              % (s[0], s[1], s[3], s[4], s[5], s[6]))


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))

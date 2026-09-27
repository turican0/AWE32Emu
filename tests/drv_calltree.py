"""The call tree within one note, chronologically, driver code only.

The input is the whole instruction line, so for every call we see **all**
registers at entry and return - those are the wanted input -> output pairs.
"""
import sys

TRACE = 'C:/prenos/AWE32EmuData/tests/out/insn_full.trace'
START = 381186
LO, HI = 0xC0FF7000, 0xC0FFF000
NOTE = int(sys.argv[1]) if len(sys.argv) > 1 else 0   # which note to print
ROOT = 0xC0FFA2AA                                     # the note set-up envelope

REG = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')


def main():
    stack = []
    n = 0
    prev = None
    seen = -1
    active = False
    out = []
    with open(TRACE, 'r', errors='replace') as f:
        for i, line in enumerate(f, 1):
            if i < START:
                continue
            c = line[0]
            if c == 'P':
                if active:
                    p = line.split()
                    out.append((len(stack), 'PORT %s %s val %s' % (p[1], p[2], p[3]), ''))
                continue
            if c != 'I':
                continue
            n += 1
            p = line.split()
            eip = int(p[1], 16)
            op = int(p[2], 16)
            rg = [int(x, 16) for x in p[3:11]]
            if prev is not None:
                pe, po, pr = prev
                if rg[7] == pr[7] - 4 and po in (0xE8, 0x9A, 0xFF):
                    if eip == ROOT:
                        seen += 1
                        active = (seen == NOTE)
                        if seen > NOTE:
                            break
                    if active and LO <= eip < HI:
                        args = '  '.join('%s=%08X' % (REG[k], pr[k]) for k in range(4))
                        out.append((len(stack), 'CALL %08X  z %08X' % (eip, pe), args))
                    stack.append((eip, n))
                elif po in (0xC3, 0xC2, 0xCB, 0xCA) and stack:
                    tgt, _ = stack.pop()
                    if active and LO <= tgt < HI:
                        args = '  '.join('%s=%08X' % (REG[k], rg[k]) for k in range(4))
                        out.append((len(stack), 'RET  %08X' % tgt, args))
            prev = (eip, op, rg)

    print('note %d, %d lines' % (NOTE, len(out)))
    for d, txt, args in out:
        print('%s%-34s %s' % ('  ' * min(d, 12), txt, args))


main()

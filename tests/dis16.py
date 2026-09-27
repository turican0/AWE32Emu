"""Disassembler of 16-bit code in a file (NE drivers).

    python dis16.py SBAWE32.DRV 0x57C0 0x5860
"""
import sys
import capstone

path = sys.argv[1]
lo = int(sys.argv[2], 16)
hi = int(sys.argv[3], 16)

d = open(path, 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
for ins in md.disasm(d[lo:hi], lo):
    print('%05X  %-20s %s %s' % (ins.address, ins.bytes.hex().upper(),
                                 ins.mnemonic, ins.op_str))

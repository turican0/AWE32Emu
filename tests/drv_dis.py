"""Disassembler over a dump of the driver code from the guest's memory.

The dump already has the LE file fixups applied, so the call targets are
right - unlike a static analysis of SBAWE.VXD itself.

    python dis.py C0FFB186 C0FFB2C0
"""
import sys
import capstone

MEM = 'C:/prenos/AWE32EmuData/SoundBlaster AWE32/runtime-dumps/SBAWE.VXD.obj1.noteon.mem'
BASE = 0xC0FF7BE0

lo = int(sys.argv[1], 16)
hi = int(sys.argv[2], 16)

data = open(MEM, 'rb').read()
off = lo - BASE
if off < 0 or off >= len(data):
    sys.exit('address %08X is outside the dump %08X..%08X' % (lo, BASE, BASE + len(data)))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False
for ins in md.disasm(data[off:hi - BASE], lo):
    print('%08X  %-22s %s %s' % (ins.address,
                                 ins.bytes.hex().upper(),
                                 ins.mnemonic, ins.op_str))

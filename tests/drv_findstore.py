"""Finds all places in the driver code that write to a given structure
offset, through **any** base register.

    python findstore.py 42 32 44 34
"""
import re
import sys
import capstone

MEM = 'C:/prenos/AWE32EmuData/SoundBlaster AWE32/runtime-dumps/SBAWE.VXD.obj1.noteon.mem'
BASE = 0xC0FF7BE0

offs = [int(a, 16) for a in sys.argv[1:]] or [0x42, 0x32]
data = open(MEM, 'rb').read()

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

# Disassemble from several alignments - in the byte stream an instruction
# can start being read elsewhere than where the main run goes.
seen = {}
for start in range(0, 8):
    for ins in md.disasm(data[start:], BASE + start):
        seen.setdefault(ins.address, (ins.mnemonic, ins.op_str))

pat = re.compile(r'^(?:word|dword|byte) ptr \[(e[a-z]{2})(?: \+ (?:e[a-z]{2}\*?\d?\s*\+\s*)?0x([0-9a-f]+))?\]$')

hits = []
for addr in sorted(seen):
    mn, ops = seen[addr]
    if not mn.startswith('mov'):
        continue
    dst = ops.split(',')[0].strip()
    m = pat.match(dst)
    if not m or m.group(2) is None:
        continue
    if int(m.group(2), 16) in offs:
        hits.append((addr, m.group(1), int(m.group(2), 16), mn, ops))

print('writes to offset %s:' % ', '.join('0x%02X' % o for o in offs))
for addr, reg, o, mn, ops in hits:
    print('  %08X  +0x%02X pres %s   %s %s' % (addr, o, reg, mn, ops))
print('total %d' % len(hits))

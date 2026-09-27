"""The jump table of the driver's conversion routine.

    C119C0F7  sub eax, 0x15                             ; generator - 21
    C119C0FA  cmp eax, 0x25                             ; range 21..58
    C119C0FF  movzx ecx, byte ptr [eax + 0xC119C362]    ; branch index
    C119C106  jmp  dword ptr [ecx*4 + 0xC119C2FA]       ; branch address
"""
import struct

MEM = 'C:/prenos/AWE32EmuData/SoundBlaster AWE32/runtime-dumps/SBAWE.VXD.obj1.mem'
BASE = 0xC1196C74
IDX = 0xC119C362          # byte table of indices
TAB = 0xC119C2FA          # table of branch addresses
FIRST, COUNT = 0x15, 0x26

d = open(MEM, 'rb').read()


def at(addr, n):
    off = addr - BASE
    return d[off:off + n]


NAMES = {
    21: 'delayModLFO', 22: 'freqModLFO', 23: 'delayVibLFO', 24: 'freqVibLFO',
    25: 'delayModEnv', 26: 'attackModEnv', 27: 'holdModEnv', 28: 'decayModEnv',
    29: 'sustainModEnv', 30: 'releaseModEnv',
    33: 'delayVolEnv', 34: 'attackVolEnv', 35: 'holdVolEnv', 36: 'decayVolEnv',
    37: 'sustainVolEnv', 38: 'releaseVolEnv',
}

idx = at(IDX, COUNT)
branches = {}
for i, b in enumerate(idx):
    gen = FIRST + i
    addr = struct.unpack_from('<I', at(TAB + 4 * b, 4))[0]
    branches.setdefault(addr, []).append(gen)
    print('gen %-3d %-14s index %-3d vetev %08X' % (gen, NAMES.get(gen, ''), b, addr))

print()
print('branches and the generators that use them:')
for addr in sorted(branches):
    gens = branches[addr]
    named = [NAMES.get(g, str(g)) for g in gens]
    print('  %08X  %s' % (addr, ', '.join(named)))

"""Field offsets of the _SFTYPE structure from the SDK header, against ours.

Every field is a `short`, so offset = index * 2. Compared with what we
derived from the instruction trace (tests/patch_struct.py).
"""
import re

H = ('C:/prenos/AWE32EmuData/docs/next docs/extracted/sdk/awe32-sdk'
     '/WINDOWS/INCLUDE/SFTYPE.H')

src = open(H, errors='replace').read()
m = re.search(r'typedef struct _SFTYPE \{(.*?)\}', src, re.S)
body = m.group(1)

fields = []
for line in body.splitlines():
    mm = re.match(r'\s*short\s+(\w+)\s*;', line)
    if mm:
        fields.append(mm.group(1))

NASE = {
    0x0E: 'f0E (when computing the target filter)',
    0x12: 'Q',
    0x20: 'reverb',
    0x24: 'panAux',
    0x26: 'atten',
    0x32: 'envvalDelay',
    0x34: 'modAttack',
    0x42: 'envvolDelay',
    0x44: 'volAttack',
    0x48: 'volHoldLo',
    0x4A: 'volHoldHi',
}

print('%-6s %-22s %s' % ('offset', 'name from the SDK', 'our label'))
for i, name in enumerate(fields):
    off = i * 2
    ours = NASE.get(off)
    if ours or off <= 0x4C:
        print('0x%02X   %-22s %s' % (off, name, ours or ''))
print()
print('fields in total %d, structure size 0x%X' % (len(fields), len(fields) * 2))

# -*- coding: utf-8 -*-
"""Compare an AWEDUMP dump with a reference ROM.

    python romcompare.py AWE32ROM.BIN [reference.raw]

The reference ROM is the awe32.raw used by 86Box/PCem (512 144 words plus
one leading word, little endian). Besides a plain match it looks for the
typical dumper faults:
  - a shift by N words (the first SMLD read not dropped, or dropped twice)
  - swapped bytes within a word (big endian)
  - every second word (the SMLD read advances the address twice)
  - repeating blocks (the 32 768 B buffer was not rewritten)
"""
import hashlib
import sys

REF = 'awe32.raw'

dump = open(sys.argv[1], 'rb').read()
ref = open(sys.argv[2] if len(sys.argv) > 2 else REF, 'rb').read()

print('dump %d B, md5 %s' % (len(dump), hashlib.md5(dump).hexdigest()))
print('ref  %d B, md5 %s' % (len(ref), hashlib.md5(ref).hexdigest()))
if dump == ref:
    print('MATCH - the dump is byte for byte the same as the reference ROM')
    sys.exit(0)

n = min(len(dump), len(ref))
diff = [i for i in range(0, n - 1, 2) if dump[i:i + 2] != ref[i:i + 2]]
print('differing words %d of %d; first difference at byte %s' % (len(diff), n // 2, diff[0] if diff else '-'))
if diff:
    i = diff[0]
    print('  dump %s' % dump[i:i + 16].hex(' '))
    print('  ref  %s' % ref[i:i + 16].hex(' '))


def words(b):
    return [int.from_bytes(b[k:k + 2], 'little') for k in range(0, len(b) - 1, 2)]


probe = slice(0x10000, 0x10000 + 4096)       # in the middle, away from the zeros at the start
rw = words(ref)
dw = words(dump)
for shift in range(-4, 5):
    if shift == 0:
        continue
    a = dw[4096 + 0x8000: 4096 + 0x8000 + 2048]
    b = rw[4096 + 0x8000 + shift: 4096 + 0x8000 + shift + 2048]
    if a == b:
        print('dump = ref shifted by %+d words' % shift)
swapped = bytes(dump[k + 1 - (k % 2) * 2] for k in range(len(dump) // 2 * 2))
if swapped[probe] == ref[probe]:
    print('the dump has the bytes within a word swapped')
if dw[8192:8192 + 1024] == rw[16384:16384 + 2048:2]:
    print('the dump contains every second word (SMLD advances the address twice)')
blocks = [dump[k:k + 32768] for k in range(0, len(dump), 32768)]
rep = sum(1 for k in range(1, len(blocks)) if blocks[k] == blocks[k - 1])
print('32768 B blocks equal to the previous one: %d of %d' % (rep, len(blocks) - 1))

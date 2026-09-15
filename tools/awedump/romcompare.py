# -*- coding: utf-8 -*-
"""Porovnani dumpu z AWEDUMP s referencni ROM.

    python romcompare.py AWE32ROM.BIN [referencni.raw]

Referencni ROM je 86Boxova awe32.raw (512 144 slov = 1 048 576 B, little
endian). Krome prosté shody hleda typicke vady dumperu:
  - posun o N slov (nezahozene nebo dvakrat zahozene prvni cteni SMLD)
  - prohozene bajty ve slove (big endian)
  - kazde druhe slovo (cteni SMLD posouva adresu dvakrat)
  - opakujici se bloky (32 768 B buffer se nezapsal znovu)
"""
import hashlib
import sys

REF = r'C:\prenos\AWE32EmuData\rom\awe32.raw'

dump = open(sys.argv[1], 'rb').read()
ref = open(sys.argv[2] if len(sys.argv) > 2 else REF, 'rb').read()

print('dump %d B, md5 %s' % (len(dump), hashlib.md5(dump).hexdigest()))
print('ref  %d B, md5 %s' % (len(ref), hashlib.md5(ref).hexdigest()))
if dump == ref:
    print('SHODA - dump je bajt po bajtu stejny jako referencni ROM')
    sys.exit(0)

n = min(len(dump), len(ref))
diff = [i for i in range(0, n - 1, 2) if dump[i:i + 2] != ref[i:i + 2]]
print('ruznych slov %d z %d; prvni rozdil na bajtu %s' % (len(diff), n // 2, diff[0] if diff else '-'))
if diff:
    i = diff[0]
    print('  dump %s' % dump[i:i + 16].hex(' '))
    print('  ref  %s' % ref[i:i + 16].hex(' '))


def words(b):
    return [int.from_bytes(b[k:k + 2], 'little') for k in range(0, len(b) - 1, 2)]


probe = slice(0x10000, 0x10000 + 4096)       # uprostred, mimo nuly na zacatku
rw = words(ref)
dw = words(dump)
for shift in range(-4, 5):
    if shift == 0:
        continue
    a = dw[4096 + 0x8000: 4096 + 0x8000 + 2048]
    b = rw[4096 + 0x8000 + shift: 4096 + 0x8000 + shift + 2048]
    if a == b:
        print('dump = ref posunuta o %+d slov' % shift)
swapped = bytes(dump[k + 1 - (k % 2) * 2] for k in range(len(dump) // 2 * 2))
if swapped[probe] == ref[probe]:
    print('dump ma prohozene bajty ve slove')
if dw[8192:8192 + 1024] == rw[16384:16384 + 2048:2]:
    print('dump obsahuje kazde druhe slovo (SMLD posouva adresu dvakrat)')
blocks = [dump[k:k + 32768] for k in range(0, len(dump), 32768)]
rep = sum(1 for k in range(1, len(blocks)) if blocks[k] == blocks[k - 1])
print('bloku 32768 B stejnych jako predchozi: %d z %d' % (rep, len(blocks) - 1))

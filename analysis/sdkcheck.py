# -*- coding: utf-8 -*-
"""Render Georgia with --driver sdk and the SDK GM bank, compare with the
DOSMid trace (register values at note-on, notes_diff.py) and voice numbers.

    python sdkcheck.py [--show]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
DATA = r'C:\prenos\AWE32EmuData'
ROM = DATA + r'\rom\awe32.raw'
BANK = os.path.join(HERE, 'sdk', 'embed_ail.mdi')
MID = DATA + r'\SAMPLES2\GEORG_BK.MID'
DRV = DATA + r'\tests\out\dosmid_awe.trace'
WIN = '1526205:8063958'
TR = os.path.join(HERE, 'sdk', 'sdk_georgia.trace')

subprocess.run([EXE, MID, '--rom', ROM, '--sf', BANK, '--wav', os.devnull,
                '--trace', TR, '--driver', 'sdk'], capture_output=True)
out = subprocess.run([sys.executable, DATA + r'\tests\notes_diff.py', TR, DRV,
                      '--dframes', WIN], capture_output=True, text=True,
                     errors='replace', cwd=DATA + r'\tests').stdout
ok = [l for l in out.splitlines() if l.startswith('OK ')]
bad = [l for l in out.splitlines() if l.startswith('   ') and '/' in l]
print('registers OK %d, differ %d' % (len(ok), len(bad)))
for l in bad:
    print(l)
if '--show' in sys.argv:
    print(out)

sys.path.insert(0, HERE)
import noteprobe_lib as NP  # noqa: E402
lo, hi = (int(x) for x in WIN.split(':'))
A = NP.notes(TR, 0, 1 << 62)
B = NP.notes(DRV, lo, hi)
n = min(len(A), len(B))
same = sum(1 for i in range(n) if A[i]['voice'] == B[i]['voice'])
first = next((i for i in range(n) if A[i]['voice'] != B[i]['voice']), n)
print('notes %d/%d, voices same %d (%.1f %%), first difference at note %d'
      % (len(A), len(B), same, 100.0 * same / max(n, 1), first))

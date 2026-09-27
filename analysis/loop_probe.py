# -*- coding: utf-8 -*-
"""Which ROM data does a long looping note really play over time?

cutoff_probe2.py showed that our core renders the block 33 ROM-noise note
(loop 0x0700F9..0x072152, IP 0xE000) with a level that sinks ~1 dB per
50 ms and jumps back every ~0.5-0.9 s - independent of any IFATN writes -
while the 86Box core stays flat. Same note, filter fully open (IFATN
0xFF00), 2 s, rendered by both cores. For each 50 ms chunk the output is
matched against the ROM (0x06F000..0x078000) by normalised FFT
cross-correlation: best address, correlation, and level.

    python loop_probe.py
"""
import os
import subprocess

import numpy as np
import soundfile as sf

import replay25 as R

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
EXE = r'C:\prenos\AWE32Emu\bin\x64\Release\AWE32Emu.exe'
ROM_PATH = r'C:\prenos\AWE32EmuData\rom\awe32rom.bin'
IFATN_PTR = 0x003D
DUR = 2.0
LO, HI = 0x06F000, 0x078000

vm, k, q = R.rt_to_frame()
ev = [e for e in vm.events if e.block == 33 and e.text.startswith('cutoff 0..255, 1 ')][0]
f0 = int(round(k * ev.rt + q))
setup, pend = [], None
for ln in open(os.path.join(HERE, 'trace', 'full25.trace')):
    p = ln.split()
    if len(p) != 3 or not p[0].isdigit():
        continue
    t, port, val = int(p[0]), int(p[1], 16), int(p[2], 16)
    if t < f0 - 200:
        continue
    if t > f0:
        break
    if port == 0xE22:
        pend = (t, port, val)
        continue
    setup.append((pend, (t, port, val)))

base = 22050
t0 = setup[0][0][0]
lines = []
for (pt, pp, pv), (t, port, val) in setup:
    if port == 0xE20 and pv == IFATN_PTR:
        val = 0xFF00
    lines.append((base + pt - t0, pp, pv))
    lines.append((base + t - t0, port, val))
on = base + setup[-1][1][0] - t0
lines.append((on + int(DUR * SR), 0xE22, (5 << 5) | 29))
lines.append((on + int(DUR * SR), 0xA20, 0x0080))
trace = os.path.join(HERE, 'loop_probe.trace')
with open(trace, 'w') as f:
    # stable sort by time only: a pointer write must stay in front of the
    # data write that shares its timestamp
    for t, port, val in sorted(lines, key=lambda r: r[0]):
        f.write('%d %03X %04X\n' % (t, port, val))

rom = np.frombuffer(open(ROM_PATH, 'rb').read(), '<i2').astype(np.float64)
src = rom[LO:HI]
M = 2048
csum = np.cumsum(np.r_[0.0, src])
csum2 = np.cumsum(np.r_[0.0, src * src])
wmean = (csum[M:] - csum[:-M]) / M
wstd = np.sqrt(np.maximum((csum2[M:] - csum2[:-M]) / M - wmean ** 2, 1e-9))
L = len(src)
FS = np.fft.rfft(src, n=2 * L)


def locate(chunk):
    kern = (chunk - chunk.mean()) / (chunk.std() + 1e-12)
    FK = np.fft.rfft(kern, n=2 * L)
    corr = np.fft.irfft(FS * np.conj(FK), n=2 * L)[:L - M + 1]
    r = corr / M / wstd[:L - M + 1]
    i = int(np.argmax(np.abs(r)))
    return LO + i, r[i]


for chip in ('ours', '86box'):
    wav = os.path.join(HERE, 'loop_probe_%s.wav' % chip)
    cmd = [EXE, '--replay', trace, '--rom', ROM_PATH, '--wav', wav]
    if chip == '86box':
        cmd += ['--chip', '86box']
    else:
        cmd += ['--eq', 'off']
    subprocess.run(cmd, check=True, capture_output=True)
    x = sf.read(wav, dtype='float32')[0][:, 0].astype(np.float64)
    print('%s:' % chip)
    rows = []
    for j in range(int(DUR / 0.05) - 1):
        a = on + int(j * 0.05 * SR) + 200
        chunk = x[a:a + M]
        addr, r = locate(chunk)
        lev = 10 * np.log10(np.mean(chunk ** 2) + 1e-14)
        rows.append((j * 0.05, addr, r, lev))
    for tt, addr, r, lev in rows:
        print('  t %.2f s  ROM 0x%06X  r %+.3f  level %6.1f dB' % (tt, addr, r, lev))

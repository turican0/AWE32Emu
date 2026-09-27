"""A chromatic run with one instrument - checks tuning and level over the whole range.

When some part of the range is systematically quieter or out of tune, it
shows here at once. It also measures how much energy goes above 15 kHz
(interpolation artefacts).

    python tests/sweep.py 33          # program 33 = Electric Bass
    python tests/sweep.py 0 --lo 21 --hi 96
"""
import os
import struct
import subprocess
import sys

import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'bin', 'x64', 'Release', 'AWE32Emu.exe')
OUT = os.path.join(ROOT, 'tests', 'out')
os.makedirs(OUT, exist_ok=True)

SYNTHGM = os.path.join(ROOT, 'cdrom', '2', 'WIN95', 'DRIVERS', 'SYNTHGM.SBK')
ROMARG = ['--rom', os.path.join(ROOT, 'rom', 'awe32.raw'), '--sf', SYNTHGM]

TPQ = 480
NOTE_TICKS = 240          # 0.25 s at 120 BPM
SEC_PER_NOTE = 0.25


def vlq(n):
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def make_midi(path, program, lo, hi, velocity=100):
    ev = [(0, b'\xFF\x51\x03' + struct.pack('>I', 500000)[1:]),
          (0, bytes([0xB0, 7, 127])),
          (0, bytes([0xC0, program]))]
    t = 0
    for n in range(lo, hi + 1):
        ev.append((t, bytes([0x90, n, velocity])))
        ev.append((t + NOTE_TICKS - 10, bytes([0x80, n, 0])))
        t += NOTE_TICKS
    ev.append((t + 10, b'\xFF\x2F\x00'))
    ev.sort(key=lambda e: e[0])

    trk = bytearray()
    prev = 0
    for tick, data in ev:
        trk += vlq(tick - prev) + data
        prev = tick
    open(path, 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, TPQ)
                           + b'MTrk' + struct.pack('>I', len(trk)) + bytes(trk))


def main():
    program = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    lo = int(sys.argv[sys.argv.index('--lo') + 1]) if '--lo' in sys.argv else 24
    hi = int(sys.argv[sys.argv.index('--hi') + 1]) if '--hi' in sys.argv else 96

    mid = os.path.join(OUT, 'sweep.mid')
    wav = os.path.join(OUT, 'sweep.wav')
    make_midi(mid, program, lo, hi)
    subprocess.run([EXE, mid, '--wav', wav] + ROMARG,
                   capture_output=True, text=True, timeout=300)

    x, sr = sf.read(wav, always_2d=True, dtype='float32')
    x = x.mean(axis=1)

    print("program %d, notes %d..%d" % (program, lo, hi))
    print("%5s %9s %11s %9s %9s %9s" %
          ('note', 'exp. Hz', 'measured', 'cents', 'rms dB', '>15kHz dB'))

    levels = []
    for i, n in enumerate(range(lo, hi + 1)):
        t0 = int((i * SEC_PER_NOTE + 0.03) * sr)
        t1 = int((i * SEC_PER_NOTE + 0.22) * sr)
        seg = x[t0:t1]
        if len(seg) < 256:
            break
        expect = 440.0 * 2 ** ((n - 69) / 12.0)
        nfft = 1 << 15
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
        f = np.fft.rfftfreq(nfft, 1 / sr)
        m = (f >= expect * 0.97) & (f <= expect * 1.03)
        if m.any() and sp[m].max() > 0:
            got = f[m][sp[m].argmax()]
            cents = 1200 * np.log2(got / expect)
        else:
            got, cents = 0.0, float('nan')
        rms = float(np.sqrt((seg ** 2).mean()) + 1e-12)
        hf = sp[f > 15000].sum() / max(sp.sum(), 1e-12)
        levels.append((n, 20 * np.log10(rms)))
        print("%5d %9.2f %11.2f %9.1f %9.1f %9.1f"
              % (n, expect, got, cents, 20 * np.log10(rms), 10 * np.log10(hf + 1e-12)))

    if levels:
        arr = np.array([l for _, l in levels])
        low = arr[:12].mean()
        mid_ = arr[len(arr) // 2 - 6:len(arr) // 2 + 6].mean()
        print("\naverage level: lowest 12 notes %.1f dB, middle %.1f dB, difference %+.1f dB"
              % (low, mid_, low - mid_))


if __name__ == '__main__':
    main()

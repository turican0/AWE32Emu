"""Regression test of the emulation - checks what is already measured and must not break.

    python tests/regress.py

It deliberately does NOT measure "the strongest peak" (that can jump to a
harmonic when the timbre changes), but the amplitude at a specific expected
frequency.
"""
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

# This script lies in the data directory (AWE32EmuData), not in the
# repository. The built player is next to it, in the project - see
# AWE32Emu/docs/DATA.md.
DATA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.environ.get('AWE32EMU_PROJECT') or os.path.normpath(
    os.path.join(DATA, '..', 'AWE32Emu'))
EXE = os.path.join(PROJECT, 'bin', 'x64', 'Release', 'AWE32Emu.exe')
OUT = os.path.join(DATA, 'tests', 'out')
os.makedirs(OUT, exist_ok=True)

# The authentic GM bank by E-mu that the drivers themselves use. It has no
# `smpl` chunk - it describes all samples directly in the wave ROM. When it
# is not on the disk, it falls back to 1mgm.sf2 through --rombank.
_SYNTHGM = os.path.join(DATA, 'cdrom', '2', 'WIN95', 'DRIVERS', 'SYNTHGM.SBK')
if os.path.exists(_SYNTHGM):
    ROM = ['--rom', os.path.join(DATA, 'rom', 'awe32.raw'), '--sf', _SYNTHGM]
else:
    ROM = ['--rom', os.path.join(DATA, 'rom', 'awe32.raw'),
           '--rombank', os.path.join(DATA, 'rom', '1mgm.sf2')]

failures = []


def check(name, ok, detail):
    mark = 'OK  ' if ok else 'CHYBA'
    print("  [%s] %-42s %s" % (mark, name, detail))
    if not ok:
        failures.append(name)


def render(src, out, extra=()):
    r = subprocess.run([EXE, src, '--wav', out] + ROM + list(extra),
                       capture_output=True, text=True, timeout=300)
    return r.stdout + r.stderr


def mono(path):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    return x.mean(axis=1), sr


def amp_at(x, sr, t0, t1, freq, tol=0.015):
    seg = x[int(t0 * sr):int(t1 * sr)]
    n = 1 << 16
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    m = (f >= freq * (1 - tol)) & (f <= freq * (1 + tol))
    if not m.any():
        return 0.0, 0.0
    i = sp[m].argmax()
    return sp[m][i], f[m][i]


def main():
    if not os.path.exists(EXE):
        print("%s is missing - build the project first" % EXE)
        return 1

    print("== GM klavir (../AWE32EmuData/SAMPLES/MIDI/BACH/MINUET.MID) ==")
    src = os.path.join(DATA, 'SAMPLES', 'MIDI', 'BACH', 'MINUET.MID')
    if os.path.exists(src):
        out = os.path.join(OUT, 'regress_minuet.wav')
        render(src, out)
        x, sr = mono(out)
        # the first bar has note 50 = D3 = 146.83 Hz
        a, f = amp_at(x, sr, 0.05, 0.55, 146.83)
        cents = 1200 * np.log2(f / 146.83) if f else 999
        check('note 50 in tune within +-5 cents', abs(cents) < 5,
              '%.2f Hz = %+.1f cents' % (f, cents))
        check('piano is not silent', a > 1.0, 'amplitude %.1f' % a)
    else:
        print("  (skipped, %s missing)" % src)

    print("== RELAX (SAMPLES2) ==")
    # RELAX_VX.MID is the full version with vocals; RELAX_BK is only the backing.
    # The user bank belongs in MIDI bank 1 - that is how the song selects it
    # through CC0 on channels 0, 3 and 10.
    src = os.path.join(DATA, 'SAMPLES2', 'RELAX_VX.MID')
    ref = os.path.join(DATA, 'SAMPLES2', '3 - Relax.flac')
    if os.path.exists(src) and os.path.exists(ref):
        out = os.path.join(OUT, 'regress_relax.wav')
        log = render(src, out, ['--sf', os.path.join(DATA, 'SAMPLES2', 'RELAX.SBK') + '@1',
                                '--debug-voices', '99999'])
        x, sr = mono(out)
        b, _ = mono(ref)
        ratio = (len(x) / sr) / (len(b) / 44100)
        check('length within 2 % of the reference', abs(ratio - 1) < 0.02,
              '%.1f s vs %.1f s (pomer %.3f)' % (len(x) / sr, len(b) / 44100, ratio))
        fallback = log.count('substitute sine')
        check('no voice falls back to the substitute', fallback == 0,
              '%d voices on the substitute' % fallback)
        check('output does not clip', np.abs(x).max() < 0.999,
              'peak %.3f' % np.abs(x).max())
    else:
        print("  (skipped, SAMPLES2 missing)")

    print()
    if failures:
        print("SELHALO: %s" % ', '.join(failures))
        return 1
    print("all passed")
    return 0


if __name__ == '__main__':
    sys.exit(main())

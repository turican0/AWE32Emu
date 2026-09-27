"""Renders many variants with different settings and ranks them by the match
with a reference recording.

Used to decide the parameters that can be read neither from the hardware nor
from the drivers (reverb room size, return levels, interpolation type).
Everything else is derived and should not be tuned here.

    python tests/sweep_params.py                 # the basic set
    python tests/sweep_params.py --quick         # interpolation only

The score is an energy-weighted spectral deviation: each band contributes its
deviation in dB weighted by the reference's energy share. Lower = better. The
overall level is subtracted first (the median ratio), so the score does not
mix the loudness scale into the spectral shape.
"""
import itertools
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'bin', 'x64', 'Release', 'AWE32Emu.exe')
OUT = os.path.join(ROOT, 'tests', 'out', 'sweep')
os.makedirs(OUT, exist_ok=True)

SYNTHGM = os.path.join(ROOT, 'cdrom', '2', 'WIN95', 'DRIVERS', 'SYNTHGM.SBK')

# (name, midi, reference, further banks)
CASES = [
    ('relax', os.path.join(ROOT, 'SAMPLES2', 'RELAX_BK.MID'),
     os.path.join(ROOT, 'SAMPLES2', '3 - Relax.flac'),
     [os.path.join(ROOT, 'SAMPLES2', 'RELAX.SBK')]),
    ('jump', os.path.join(ROOT, 'SAMPLES2', 'JUMP_BK.MID'),
     os.path.join(ROOT, 'SAMPLES2', '6 - Jump.flac'), []),
]

BANDS = [(0, 100), (100, 200), (200, 400), (400, 800), (800, 1600),
         (1600, 3200), (3200, 6400), (6400, 12800), (12800, 22050)]

_ref_cache = {}


def band_energy(path):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    x = x.mean(axis=1)
    nfft = 8192
    win = np.hanning(nfft)
    acc = np.zeros(nfft // 2 + 1)
    n = 0
    for i in range(0, len(x) - nfft, nfft // 2):
        acc += np.abs(np.fft.rfft(x[i:i + nfft] * win)) ** 2
        n += 1
    acc /= max(1, n)
    f = np.fft.rfftfreq(nfft, 1 / sr)
    return np.array([acc[(f >= lo) & (f < hi)].sum() for lo, hi in BANDS])


def score(ours, ref):
    """Energy-weighted deviation in dB after subtracting the overall level."""
    ratios = ours / np.maximum(ref, 1e-30)
    med = np.median(ratios)
    dev = np.abs(10 * np.log10(np.maximum(ratios / med, 1e-30)))
    w = ref / ref.sum()
    return float((dev * w).sum())


def run(name, midi, ref, extra_banks, args, tag):
    wav = os.path.join(OUT, '%s_%s.wav' % (name, tag))
    cmd = [EXE, midi, '--wav', wav,
           '--rom', os.path.join(ROOT, 'rom', 'awe32.raw'),
           '--sf', SYNTHGM]
    for b in extra_banks:
        cmd += ['--sf', b]
    cmd += args
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if r.returncode != 0 or not os.path.exists(wav):
        return None
    if ref not in _ref_cache:
        _ref_cache[ref] = band_energy(ref)
    return score(band_energy(wav), _ref_cache[ref])


def main():
    quick = '--quick' in sys.argv

    if quick:
        variants = [(['--interp', i], 'i%s' % i) for i in ('linear', 'cubic')]
    else:
        variants = []
        for interp, room, damp in itertools.product(
                ('linear', 'cubic'), (0.60, 0.72, 0.82, 0.90), (0.20, 0.35, 0.50)):
            variants.append((['--interp', interp,
                              '--rev-room', str(room), '--rev-damp', str(damp)],
                             'i%s_r%.2f_d%.2f' % (interp, room, damp)))

    print("variants: %d, cases: %d -> %d renders"
          % (len(variants), len(CASES), len(variants) * len(CASES)))

    results = []
    for args, tag in variants:
        scores = []
        for name, midi, ref, banks in CASES:
            if not (os.path.exists(midi) and os.path.exists(ref)):
                continue
            s = run(name, midi, ref, banks, args, tag)
            if s is not None:
                scores.append(s)
        if scores:
            results.append((float(np.mean(scores)), tag, scores))
            print("  %-28s score %.3f  %s"
                  % (tag, results[-1][0], ' '.join('%.3f' % x for x in scores)))

    results.sort()
    print("\nranking (lower = closer to the reference):")
    for s, tag, per in results[:12]:
        print("  %7.3f  %s" % (s, tag))
    if results:
        print("\nbest: %s" % results[0][1])


if __name__ == '__main__':
    main()

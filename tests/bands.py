"""Compares the energy distribution into frequency bands between our render and a reference.

    python tests/bands.py tests/out/game3_v2.wav ogg/002_C2GAME3.ogg
"""
import sys
import numpy as np
import soundfile as sf

BANDS = [(0, 100), (100, 200), (200, 400), (400, 800), (800, 1600),
         (1600, 3200), (3200, 6400), (6400, 12800), (12800, 22050)]


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    return x.mean(axis=1), sr


def band_energy(x, sr, nfft=8192):
    # average spectrum over the whole recording
    hop = nfft
    frames = max(1, len(x) // hop)
    acc = np.zeros(nfft // 2 + 1)
    win = np.hanning(nfft)
    n = 0
    for i in range(frames):
        seg = x[i * hop:i * hop + nfft]
        if len(seg) < nfft:
            break
        acc += np.abs(np.fft.rfft(seg * win)) ** 2
        n += 1
    acc /= max(1, n)
    freqs = np.fft.rfftfreq(nfft, 1 / sr)
    out = []
    for lo, hi in BANDS:
        m = (freqs >= lo) & (freqs < hi)
        out.append(acc[m].sum())
    total = sum(out) or 1.0
    return np.array(out) / total


def main(a_path, b_path):
    a, sra = load(a_path)
    b, srb = load(b_path)
    ea = band_energy(a, sra)
    eb = band_energy(b, srb)
    print("%-14s %8s %8s   %s" % ('band [Hz]', 'nas %', 'ref %', 'nas / ref'))
    for (lo, hi), va, vb in zip(BANDS, ea, eb):
        bar_a = '#' * int(va * 100)
        bar_b = '=' * int(vb * 100)
        ratio = va / vb if vb > 1e-9 else float('inf')
        print("%5d-%-8d %7.2f%% %7.2f%%   %6.2fx  %s|%s"
              % (lo, hi, va * 100, vb * 100, ratio, bar_a, bar_b))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

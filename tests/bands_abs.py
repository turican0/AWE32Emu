"""Compares the ABSOLUTE energy in bands, not shares.

A share misleads: when we have an excess in the mids, the share of the bass
falls, even if the bass is absolutely right. This script tells which bands
are really weak and which strong.

    python tests/bands_abs.py tests/out/relax.wav "SAMPLES2/3 - Relax.flac"
"""
import sys
import numpy as np
import soundfile as sf

BANDS = [(0, 100), (100, 200), (200, 400), (400, 800), (800, 1600),
         (1600, 3200), (3200, 6400), (6400, 12800), (12800, 22050)]


def spectrum(path, nfft=8192):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    x = x.mean(axis=1)
    win = np.hanning(nfft)
    acc = np.zeros(nfft // 2 + 1)
    n = 0
    for i in range(0, len(x) - nfft, nfft // 2):
        acc += np.abs(np.fft.rfft(x[i:i + nfft] * win)) ** 2
        n += 1
    return np.fft.rfftfreq(nfft, 1 / sr), acc / max(1, n), x


def bands(freqs, power):
    return np.array([power[(freqs >= lo) & (freqs < hi)].sum() for lo, hi in BANDS])


def main(a_path, b_path):
    fa, pa, xa = spectrum(a_path)
    fb, pb, xb = spectrum(b_path)
    ea, eb = bands(fa, pa), bands(fb, pb)

    rms_a = float(np.sqrt((xa ** 2).mean()))
    rms_b = float(np.sqrt((xb ** 2).mean()))
    print("rms nas %.4f, ref %.4f  (pomer %.2f = %+.1f dB)"
          % (rms_a, rms_b, rms_a / rms_b, 20 * np.log10(rms_a / rms_b)))

    ratios = ea / np.maximum(eb, 1e-30)
    # the median ratio is taken as "the overall volume", the deviations from it
    # are the real shape of the spectrum
    med = float(np.median(ratios))
    print("median ratio over the bands: %.2f (%+.1f dB) - that is the overall level"
          % (med, 10 * np.log10(med)))
    print()
    print("%-14s %10s %10s %9s %9s" % ('band [Hz]', 'nas', 'ref', 'abs dB', 'after correction'))
    for (lo, hi), va, vb in zip(BANDS, ea, eb):
        r = va / max(vb, 1e-30)
        db = 10 * np.log10(max(r, 1e-30))
        rel = 10 * np.log10(max(r / med, 1e-30))
        bar = '+' * int(min(20, max(0, rel))) or ('-' * int(min(20, max(0, -rel))))
        print("%5d-%-8d %10.3e %10.3e %+8.1f %+8.1f  %s" % (lo, hi, va, vb, db, rel, bar))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

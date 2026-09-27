"""Compares a rendered .wav with a reference .ogg recording.

    python tests/compare.py tests/out/intro_v1.wav ogg/004_C2INTRO.ogg

It measures what can be measured objectively:
  - length
  - the volume course over time (RMS per second) and its correlation
  - the spectral centroid (whether the result is duller/sharper than the reference)
  - the correlation of the log spectrogram (a rough "how it sounds" match)
"""
import sys
import numpy as np
import soundfile as sf


def load(path, sr=44100):
    data, rate = sf.read(path, always_2d=True, dtype='float32')
    mono = data.mean(axis=1)
    if rate != sr:
        idx = np.linspace(0, len(mono) - 1, int(len(mono) * sr / rate))
        mono = np.interp(idx, np.arange(len(mono)), mono)
    return mono


def rms_curve(x, sr=44100, win=1.0):
    n = int(sr * win)
    m = len(x) // n
    if m == 0:
        return np.zeros(1)
    return np.sqrt((x[:m * n].reshape(m, n) ** 2).mean(axis=1) + 1e-12)


def spectrogram(x, sr=44100, nfft=2048, hop=1024):
    frames = 1 + (len(x) - nfft) // hop
    if frames < 1:
        return np.zeros((1, nfft // 2))
    win = np.hanning(nfft)
    out = np.empty((frames, nfft // 2), dtype=np.float32)
    for i in range(frames):
        seg = x[i * hop:i * hop + nfft] * win
        out[i] = np.abs(np.fft.rfft(seg)[:nfft // 2])
    return out


def centroid(spec, sr=44100, nfft=2048):
    freqs = np.fft.rfftfreq(nfft, 1 / sr)[:spec.shape[1]]
    energy = spec.sum(axis=1) + 1e-12
    return float(((spec * freqs).sum(axis=1) / energy).mean())


def main(a_path, b_path):
    a = load(a_path)
    b = load(b_path)
    sr = 44100
    print("nas    : %-42s %8.2f s  peak %.3f  rms %.4f"
          % (a_path, len(a) / sr, np.abs(a).max(), np.sqrt((a ** 2).mean())))
    print("ref    : %-42s %8.2f s  peak %.3f  rms %.4f"
          % (b_path, len(b) / sr, np.abs(b).max(), np.sqrt((b ** 2).mean())))

    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    ca, cb = rms_curve(a), rms_curve(b)
    m = min(len(ca), len(cb))
    ca, cb = ca[:m], cb[:m]
    if m > 2 and ca.std() > 0 and cb.std() > 0:
        r = float(np.corrcoef(ca, cb)[0, 1])
    else:
        r = float('nan')
    print("\nvolume course: correlation %.3f  (1.0 = the same shape of the piece)" % r)
    ticks = max(1, m // 20)
    print("  cas[s] nas / ref:")
    for i in range(0, m, ticks):
        bar_a = '#' * int(min(40, ca[i] * 200))
        bar_b = '#' * int(min(40, cb[i] * 200))
        print("   %4d  %-40s | %s" % (i, bar_a, bar_b))

    sa, sb = spectrogram(a), spectrogram(b)
    print("\nspectral centroid: ours %.0f Hz, ref %.0f Hz  (ratio %.2f)"
          % (centroid(sa), centroid(sb), centroid(sa) / max(1.0, centroid(sb))))

    k = min(len(sa), len(sb))
    la = np.log1p(sa[:k]).ravel()
    lb = np.log1p(sb[:k]).ravel()
    if la.std() > 0 and lb.std() > 0:
        print("log spectrogram correlation: %.3f" % float(np.corrcoef(la, lb)[0, 1]))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

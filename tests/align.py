"""Finds the time offset between our render and a reference.

All windowed measurements are useless if the recordings do not start at the
same place. The volume envelope (RMS per 100 ms) is correlated, which is
robust against differences in timbre.

    python tests/align.py tests/out/game3_v5.wav ogg/002_C2GAME3.ogg
"""
import sys
import numpy as np
import soundfile as sf

HOP = 0.1   # s


def env(path):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    x = x.mean(axis=1)
    n = int(sr * HOP)
    m = len(x) // n
    e = np.sqrt((x[:m * n].reshape(m, n) ** 2).mean(axis=1) + 1e-12)
    return e, len(x) / sr


def main(a_path, b_path):
    ea, da = env(a_path)
    eb, db = env(b_path)
    print("ours %.1f s (%d windows), ref %.1f s (%d windows)" % (da, len(ea), db, len(eb)))

    # normalisation, so the correlation measures the shape, not the volume
    a = (ea - ea.mean()) / (ea.std() + 1e-12)
    b = (eb - eb.mean()) / (eb.std() + 1e-12)

    n = min(len(a), len(b))
    best = (-2.0, 0)
    span = min(n - 10, int(60 / HOP))       # search for an offset within +-60 s
    for shift in range(-span, span):
        if shift >= 0:
            x, y = a[shift:shift + n - abs(shift)], b[:n - abs(shift)]
        else:
            x, y = a[:n - abs(shift)], b[-shift:-shift + n - abs(shift)]
        if len(x) < 50:
            continue
        r = float(np.dot(x, y) / len(x))
        if r > best[0]:
            best = (r, shift)

    r, shift = best
    print("best correlation %.3f at offset %+.1f s "
          "(positive = our render is late)" % (r, shift * HOP))

    r0 = float(np.dot(a[:n], b[:n]) / n)
    print("correlation without offset: %.3f" % r0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

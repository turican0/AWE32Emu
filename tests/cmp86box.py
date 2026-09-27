"""Comparison of our render with a render through the unmodified snd_emu8k.c of 86Box."""
import sys
import numpy as np
import soundfile as sf

def load(p):
    x, sr = sf.read(p, always_2d=True, dtype='float64')
    return x, sr

def rms_db(x):
    r = float(np.sqrt(np.mean(x**2)))
    return 20*np.log10(r) if r > 0 else -999.0

def bands(x, sr, label):
    mono = x.mean(axis=1)
    n = 1 << 15
    edges = [0,100,200,400,800,1600,3200,6400,12800,sr//2]
    acc = np.zeros(len(edges)-1)
    cnt = 0
    for off in range(0, len(mono)-n, n*8):
        seg = mono[off:off+n] * np.hanning(n)
        S = np.abs(np.fft.rfft(seg))**2
        f = np.fft.rfftfreq(n, 1/sr)
        for i in range(len(edges)-1):
            acc[i] += S[(f>=edges[i]) & (f<edges[i+1])].sum()
        cnt += 1
    return acc / max(cnt,1), edges

a_path, b_path = sys.argv[1], sys.argv[2]
A, sr = load(a_path)
B, _  = load(b_path)
n = min(len(A), len(B))
A, B = A[:n], B[:n]

print(f"{'':22} {'RMS dB':>9} {'peak':>9}")
for name, x in (("nas (" + a_path.split('/')[-1] + ")", A), ("86box (" + b_path.split('/')[-1] + ")", B)):
    print(f"{name:22} {rms_db(x):9.2f} {np.abs(x).max():9.4f}")

print(f"\nlength: {n} frames ({n/sr:.1f} s)")
d = A - B
print(f"RMS of the difference: {rms_db(d):.2f} dB   (identical = -999)")
if rms_db(A) > -900:
    print(f"error to signal ratio: {rms_db(d)-rms_db(A):+.2f} dB")

# correlation on a rough volume envelope
w = sr // 10
na = n // w
ea = np.sqrt((A.mean(axis=1)[:na*w]**2).reshape(na, w).mean(axis=1))
eb = np.sqrt((B.mean(axis=1)[:na*w]**2).reshape(na, w).mean(axis=1))
if ea.std() > 0 and eb.std() > 0:
    print(f"volume envelope correlation: {np.corrcoef(ea, eb)[0,1]:.4f}")

pa, edges = bands(A, sr, "nas")
pb, _     = bands(B, sr, "86box")
print(f"\n{'band Hz':>14}  {'ours dB':>8} {'86box dB':>9} {'diff':>8}")
for i in range(len(edges)-1):
    da = 10*np.log10(pa[i]+1e-30); db_ = 10*np.log10(pb[i]+1e-30)
    print(f"{edges[i]:6d}-{edges[i+1]:<7d} {da:8.1f} {db_:9.1f} {da-db_:+8.1f}")

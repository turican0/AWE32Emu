#!/usr/bin/env python
"""Finds which MIDI belongs to which recording - and whether at all.

It compares **note onsets**, not the sound: the MIDI becomes an impulse
course of note-ons, the audio a spectral flux (energy increase between
frames). Both are normalised and correlated. When the recording really is
that piece, a sharp peak comes out; when not, the correlation stays near
zero.

That makes it insensitive to which instrument plays it - exactly what is
needed, because different syntheses of the same score are compared.

    # which MIDI matches this recording?
    python tests/match_tracks.py recording.ogg --midi midi

    # does this pair belong together?
    python tests/match_tracks.py recording.ogg --midi one.xmi

    # a long recording with several pieces: first cut it by silence
    python tests/match_tracks.py long.wav --split --midi folder/

Result: `score` is the maximum of the normalised cross-correlation (0..1).
Above ~0.3 the match is practically certain, below ~0.1 they have nothing in
common.
"""
import argparse
import glob
import os
import sys

import numpy as np

try:
    import soundfile as sf
except ImportError:
    sys.exit("the soundfile module is missing (pip install soundfile)")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midi_read  # noqa: E402

HOP = 0.02          # 20 ms step of both courses
SMOOTH = 3          # smoothing in steps


def _smooth(x, k):
    if k <= 1:
        return x
    ker = np.ones(k) / k
    return np.convolve(x, ker, mode="same")


def onsets_from_samples(x, sr, hop=HOP):
    """Spectral flux: how much energy was ADDED in every frame.

    Computed over the whole array at once - a per-frame loop was an order of
    magnitude slower on a four-minute recording than the whole rest of the
    comparison.
    """
    n = int(sr * hop)
    frames = len(x) // n
    if frames < 8:
        return np.zeros(0)
    fr = x[:frames * n].reshape(frames, n) * np.hanning(n)
    spec = np.abs(np.fft.rfft(fr, axis=1))
    d = np.diff(spec, axis=0)
    out = np.zeros(frames)
    out[1:] = np.sum(np.maximum(d, 0.0), axis=1)
    return _smooth(out, SMOOTH)


def audio_onsets(path, hop=HOP):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    return onsets_from_samples(x.mean(axis=1), sr, hop), hop


def midi_onsets(path, hop=HOP):
    """Impulses at the note-on times, weighted by velocity."""
    song = midi_read.read(path)
    n = int(song.length / hop) + 2
    out = np.zeros(n)
    for t0, dur, ch, note, vel in song.notes:
        i = int(t0 / hop)
        if 0 <= i < n:
            out[i] += vel
    return _smooth(out, SMOOTH), song


def score(a, b, min_overlap=0.8):
    """The best normalised correlation and offset (in steps).

    Watch two traps that were hit:

    1) Dividing by a constant does not work. At an offset where the arrays
       overlap only partly, the sum comes from few samples and after dividing
       by the whole length it can be larger than 1 - a short MIDI then "wins"
       over everything. So it is divided by **the number of really
       overlapping samples** at that offset.
    2) Even so a short section is unreliable, because hitting a few onsets by
       chance is enough. Only offsets where at least `min_overlap` of the
       shorter course overlaps are taken.

    The correlation is computed through the FFT - a direct `np.correlate` is
    quadratic, and for four-minute recordings times seventy candidates it
    takes tens of minutes.
    """
    la, lb = len(a), len(b)
    if la < 8 or lb < 8:
        return 0.0, 0
    a = (a - a.mean()) / (a.std() + 1e-12)
    b = (b - b.mean()) / (b.std() + 1e-12)

    m = 1 << int(np.ceil(np.log2(la + lb)))
    c = np.fft.irfft(np.fft.rfft(b, m) * np.conj(np.fft.rfft(a, m)), m)
    c = np.concatenate([c[-(la - 1):], c[:lb]])          # offsets -(la-1) .. lb-1
    lags = np.arange(-(la - 1), lb)

    # number of overlapping samples for every offset
    ov = np.minimum(la, lb) - np.maximum(0, lags + la - lb) - np.maximum(0, -lags)
    ov = np.maximum(ov, 1)
    ok = ov >= min_overlap * min(la, lb)
    if not ok.any():
        return 0.0, 0
    c = c / ov
    c[~ok] = -np.inf
    i = int(np.argmax(c))
    return float(c[i]), int(lags[i])


def split_on_silence(path, thresh_db=-45.0, min_gap=2.0, min_len=20.0):
    """Cuts a long recording into pieces by silence. Returns [(from, to)] in s."""
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    n = int(sr * 0.05)
    frames = len(x) // n
    e = np.array([np.sqrt(np.mean(x[i * n:(i + 1) * n] ** 2)) for i in range(frames)])
    db = 20 * np.log10(e + 1e-12)
    loud = db > thresh_db
    out, i = [], 0
    gap = int(min_gap / 0.05)
    while i < frames:
        if not loud[i]:
            i += 1
            continue
        j = i
        run = 0
        while j < frames:
            if loud[j]:
                run = 0
            else:
                run += 1
                if run >= gap:
                    break
            j += 1
        end = j - run
        if (end - i) * 0.05 >= min_len:
            out.append((i * 0.05, end * 0.05))
        i = j
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio", nargs="+")
    ap.add_argument("--midi", required=True, help="a MIDI/XMI file or folder")
    ap.add_argument("--split", action="store_true",
                    help="the recording holds several pieces - cut by silence")
    ap.add_argument("--top", type=int, default=5)
    ap.add_argument("--len-tol", dest="lentol", type=float, default=0.25,
                    help="how far the MIDI length may differ from the recording length "
                         "(default 0.25 = +-25 %%); 0 switches it off")
    ap.add_argument("--out", help="folder to save the cut pieces to")
    args = ap.parse_args()

    if os.path.isdir(args.midi):
        cands = sorted(glob.glob(os.path.join(args.midi, "**", "*.mid"), recursive=True) +
                       glob.glob(os.path.join(args.midi, "**", "*.MID"), recursive=True) +
                       glob.glob(os.path.join(args.midi, "**", "*.xmi"), recursive=True))
    else:
        cands = [args.midi]
    if not cands:
        sys.exit("no MIDI found")

    print(f"candidates: {len(cands)}")
    ref = {}
    for c in cands:
        try:
            ref[c] = midi_onsets(c)
        except Exception as ex:
            print(f"  skipping {os.path.basename(c)}: {ex}")

    for path in args.audio:
        x, sr = sf.read(path, always_2d=True, dtype="float64")
        x = x.mean(axis=1)
        parts = (split_on_silence(path) if args.split
                 else [(0.0, len(x) / sr)])
        if args.split:
            print(f"{os.path.basename(path)}: cut into {len(parts)} pieces")

        for k, (t0, t1) in enumerate(parts):
            seg = x[int(t0 * sr):int(t1 * sr)]
            if args.split and args.out:
                os.makedirs(args.out, exist_ok=True)
                sf.write(os.path.join(args.out, f"part{k:02d}.wav"), seg, sr)
            a = onsets_from_samples(seg, sr)

            # Length sieve. We look for WHOLE pieces, so the MIDI length must match
            # the recording length. Without it a short piece wins over
            # everything - the seven-second CREATIVE.MID fits into a
            # four-minute recording at a hundred places and hits by chance
            # somewhere.
            dur = t1 - t0
            res = []
            for c, (m, song) in ref.items():
                if args.lentol and song.length > 0:
                    r = song.length / dur
                    if not (1 - args.lentol <= r <= 1 + args.lentol):
                        continue
                sc, lag = score(m, a)
                res.append((sc, lag * HOP, c, song))
            res.sort(reverse=True, key=lambda r: r[0])
            if not res:
                print(f"{os.path.basename(path)} part {k}: no candidate "
                      f"has a similar length ({dur:.1f} s)\n")
                continue

            head = (f"{os.path.basename(path)} cast {k}: {t0:.1f}-{t1:.1f} s"
                    if args.split
                    else f"{os.path.basename(path)} ({t1-t0:.1f} s)")
            print(head)
            for sc, lag, c, song in res[:args.top]:
                mark = ("  <== match" if sc > 0.30
                        else ("  (weak)" if sc > 0.12 else ""))
                print(f"   score {sc:5.3f}  offset {lag:+7.2f} s  "
                      f"length {song.length:6.1f} s  {os.path.basename(c)}{mark}")
            if res and res[0][0] <= 0.12:
                print("   -> no MIDI matches this recording")
            print()


if __name__ == "__main__":
    main()

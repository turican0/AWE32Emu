#!/usr/bin/env python
"""Takes both the recording and our render apart into single notes and looks
for those that differ **differently from the whole piece**.

Why like this: recordings from hardware went through YouTube, another preamp
and another equalisation, so they **always** differ from our render. That
alone says nothing. It becomes interesting when a note differs differently
from the rest - then it is not the transfer path but the chip computing it
differently.

Procedure:
  1. `align2.py` gives the curve that maps the times of our render onto the
     recording.
  2. Note onsets (time, voice, registers) come from the trace, the channel,
     key and sample address from `--dump-notes`.
  3. For every note the same window is cut out of both recordings and the
     spectral difference in bands is computed.
  4. The median over all notes = **the distortion of the whole** (that
     equalisation).
  5. What remains after subtracting it is suspicious. It is summed by groups -
     a single note is noise, twenty notes of the same sample are not.

    python tests/note_probe.py --warp w.json --trace n.trace \\
        --notes n.csv --ours ours.wav --ref ref.wav
    ... --by sample     # groups by sample address (default)
    ... --by ch         # by channel
    ... --list          # also print the worst single notes
"""
import argparse
import csv
import json
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SR = 44100
NFFT = 4096
# Third-octave bands. The lower limit is given by the recordings being
# from YouTube (below 60 Hz it is only noise), the upper by the lossy codec
# cutting above 15 kHz.
EDGES = np.array([60 * (2 ** (i / 3.0)) for i in range(0, 25)])

# A common floor for empty bands - see band_db().
FLOOR = 1e-12


def load(path, stereo=False):
    """Returns mono, or a pair of channels when the pan is measured."""
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    if sr != SR:
        n = int(round(len(x) * SR / sr))
        idx = np.linspace(0, len(x) - 1, n)
        x = np.stack([np.interp(idx, np.arange(len(x)), x[:, c])
                      for c in range(x.shape[1])], axis=1)
    if not stereo:
        return x.mean(axis=1)
    if x.shape[1] == 1:
        return np.repeat(x, 2, axis=1)
    return x[:, :2]


def load_mono(path):
    return load(path, stereo=False)


def band_energy(seg):
    """Energy in third-octave bands, linear (not in dB)."""
    if len(seg) < NFFT:
        seg = np.pad(seg, (0, NFFT - len(seg)))
    S = np.abs(np.fft.rfft(seg[:NFFT] * np.hanning(NFFT))) ** 2
    f = np.fft.rfftfreq(NFFT, 1.0 / SR)
    out = np.empty(len(EDGES) - 1)
    for i in range(len(EDGES) - 1):
        m = (f >= EDGES[i]) & (f < EDGES[i + 1])
        out[i] = S[m].sum() if m.any() else 0.0
    return out


def band_db(seg):
    """Energy in third-octave bands, in dB.

    An empty band gets a **fixed** floor, not -200 dB and not a floor derived
    from the frame energy. The latter burned: when each side has its own
    floor, empty bands stop cancelling each other and the score jumped from
    4.7 to 118. A fixed common floor means "both sides silent" gives a zero
    difference and "one side silent" a large but finite one.
    """
    e = band_energy(seg)
    return 10 * np.log10(np.maximum(e, FLOOR))


def warp_range(anchors):
    """From when to when the alignment really applies (in our render time)."""
    a = np.array(anchors, dtype=float)
    return float(a[0, 0]), float(a[-1, 0])


def warp_fn(anchors):
    a = np.array(anchors, dtype=float)
    xo, xr = a[:, 0], a[:, 1]

    def f(t):
        return np.interp(t, xo, xr, left=xr[0] + (t - xo[0]) if False else None,
                         right=None)
    # np.interp holds the edge value outside the range; for us it is better to
    # extend the edge offset, so notes before the first and after the last
    # anchor are not dropped.
    def g(t):
        t = np.asarray(t, dtype=float)
        out = np.interp(t, xo, xr)
        lo = t < xo[0]
        hi = t > xo[-1]
        out[lo] = t[lo] + (xr[0] - xo[0])
        out[hi] = t[hi] + (xr[-1] - xo[-1])
        return out
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warp", required=True)
    ap.add_argument("--trace", required=True)
    ap.add_argument("--notes", required=True, help="csv z --dump-notes")
    ap.add_argument("--ours", required=True)
    ap.add_argument("--ref", required=True)
    # `atten` and `vel` reveal an error in the **slope** of the volume curve:
    # if the attenuation were converted differently than on the hardware, the
    # deviation would grow with the attenuation value instead of being random.
    ap.add_argument("--by", default="sample",
                    choices=["sample", "ch", "note", "atten", "vel",
                             "q", "cutoff"])
    ap.add_argument("--win", type=float, default=0.12, help="note window [s]")
    ap.add_argument("--gap", type=float, default=0.05,
                    help="how long before a note no other may start [s]")
    ap.add_argument("--min-notes", type=int, default=8,
                    help="groups with fewer notes are not printed")
    ap.add_argument("--list", action="store_true", help="also print single notes")
    ap.add_argument("--score", action="store_true",
                    help="print only one number - for comparing variants")
    ap.add_argument("--stereo", action="store_true",
                    help="measure the **pan** (L/R ratio per note) instead of the colour")
    ap.add_argument("--delay", type=float, default=0.0,
                    help="shift the window after the note onset [s] - this gets"
                         " to the decay instead of the onset")
    # Some presets have several layers - one note starts two voices at the same
    # time. The window is then a mix of both, but the group is named after the
    # sample of the first, so the "by sample" analysis misleads for them. This
    # separates them.
    # Band weight: `global` is the mean over all notes (as it was from the
    # start), `note` takes each note's **own** energy distribution. The
    # difference shows on presets that have nothing in some band: for the
    # kalimba the global weight counts 60 Hz with the weight of the whole
    # piece, where the bass is - and the measurement then catches noise there.
    ap.add_argument("--weight", default="note", choices=["global", "note"],
                    help="band weight: the mean over the piece, or the energy of that note")
    ap.add_argument("--layers", default="vse", choices=["vse", "1", "2+"],
                    help="only single-layer notes (1), only multi-layer (2+),"
                         " or both (default)")
    # When the "ours" side is a recording too (and not our render), it must also
    # be converted from render time into its own. That measures **two
    # recordings of the same card against each other** - the noise floor of the
    # metric: nothing of it can our emulation influence.
    ap.add_argument("--warp-ours", dest="warp_ours",
                    help="alignment for the --ours side (when it is not our render)")
    ap.add_argument("--extrapolate", action="store_true",
                    help="measure also notes outside the anchor range (the default is"
                         " to skip them - the alignment does not hold there)")
    args = ap.parse_args()

    from notes_diff import real_notes

    _ank = json.load(open(args.warp, encoding="utf-8"))["anchors"]
    warp = warp_fn(_ank)
    if args.warp_ours:
        _anko = json.load(open(args.warp_ours, encoding="utf-8"))["anchors"]
        warp_o = warp_fn(_anko)
    else:
        warp_o = None
    # Outside the anchor range time is only extended by the edge offset, which
    # is an estimate, not an alignment. When our render covers more music than
    # the recording (a looped XMI against one pass in the game), such notes
    # would be compared with completely different music - and the score comes
    # out absurdly high.
    t_lo, t_hi = warp_range(_ank)
    ours = load(args.ours, args.stereo)
    ref = load(args.ref, args.stereo)

    notes = real_notes(args.trace)
    rows = list(csv.DictReader(open(args.notes, encoding="utf-8")))
    if len(rows) != len(notes):
        print("warning: the trace has %d notes, the csv %d - pairing in order up to %d"
              % (len(notes), len(rows), min(len(notes), len(rows))))
    n = min(len(rows), len(notes))

    # Isolated notes: when another note starts right before the window, a mix is measured.
    frames = np.array([notes[i][0] for i in range(n)], dtype=float)
    t_ours = frames / SR
    prev_gap = np.diff(np.concatenate([[-1e9], t_ours]))

    # How many voices the same note starts: consecutive records with the same
    # channel, note and velocity at the same time. The driver starts them one
    # right after another, so comparing the neighbour is enough.
    vrstev = [1] * n
    i = 0
    while i < n:
        j = i
        while (j + 1 < n
               and prev_gap[j + 1] < 1e-6
               and rows[j + 1]["ch"] == rows[i]["ch"]
               and rows[j + 1]["note"] == rows[i]["note"]):
            j += 1
        for k in range(i, j + 1):
            vrstev[k] = j - i + 1
        i = j + 1

    w = int(args.win * SR)
    items = []
    weights = []
    mimo = 0
    for i in range(n):
        if prev_gap[i] < args.gap:
            continue
        if args.layers == "1" and vrstev[i] != 1:
            continue
        if args.layers == "2+" and vrstev[i] < 2:
            continue
        if not args.extrapolate and not (t_lo <= t_ours[i] <= t_hi):
            mimo += 1
            continue
        t_o = t_ours[i] + args.delay
        if warp_o is not None:
            t_o = float(warp_o(np.array([t_o]))[0])
        a0 = int(t_o * SR)
        b0 = int(float(warp(np.array([t_ours[i] + args.delay]))[0]) * SR)
        if a0 < 0 or b0 < 0 or a0 + w > len(ours) or b0 + w > len(ref):
            continue
        sa = ours[a0:a0 + w]
        sb = ref[b0:b0 + w]
        if np.sqrt((sa ** 2).mean()) < 1e-5 or np.sqrt((sb ** 2).mean()) < 1e-5:
            continue
        if args.stereo:
            # Pan: the L-R difference in dB per band. When a note has another position
            # in the stereo field for us than in the recording, it shows here -
            # and the mono probe does not see it at all, because both channels
            # are summed.
            da = band_db(sa[:, 0]) - band_db(sa[:, 1])
            db_ = band_db(sb[:, 0]) - band_db(sb[:, 1])
            items.append((i, db_ - da))
            eb = band_energy(sb[:, 0]) + band_energy(sb[:, 1])
        else:
            items.append((i, band_db(sb) - band_db(sa)))
            eb = band_energy(sb)
        # Band weight = its share of this note's energy. Without it an almost
        # silent top band gets the same weight as the band where all the music
        # is - and there even a tiny absolute difference explodes in decibels
        # into a number that has almost nothing to do with what is audible.
        weights.append(eb / max(eb.sum(), 1e-300))

    if not items:
        raise SystemExit("no usable note - try a larger --gap or another pair")

    D = np.array([d for _, d in items])
    W = np.array(weights)
    imp = W.mean(axis=0)                       # average importance of a band
    imp = imp / max(imp.sum(), 1e-300)

    if args.score:
        # One number for ranking variants: what remains after subtracting the
        # distortion of the whole. Neither the level nor the colour of the
        # transfer path enters it - it measures only whether the notes differ
        # **among themselves** differently than in the recording. Lower is
        # better. Weighted by band energy (see above) - otherwise almost silent
        # bands could outweigh what is really audible.
        g = np.median(D, axis=0)
        r = D - g
        # It must be divided by the number of notes **and** the sum of the
        # weights. Without it the score grows with the square root of the note
        # count (667 notes = a factor of 25.8) and pairs with different note
        # counts cannot be compared.
        if args.weight == "note":
            # The rows of W are already normalised to a sum of 1, so W.sum() = the note
            # count and the result stays in decibels, comparable with the
            # global weight.
            score = float(np.sqrt((W * r ** 2).sum() / max(W.sum(), 1e-300)))
        else:
            score = float(np.sqrt((imp * r ** 2).sum()
                                  / max(len(r) * imp.sum(), 1e-300)))
        # The second number: **colour**. The score above deliberately subtracts
        # the overall curve (the equalisation of the recording path is in it),
        # so a difference in colour is invisible to it. It is measured with the
        # same weight as the score, so empty bands do not count. For an idea:
        # two recordings of the same card have 0.7 dB, our render against them
        # 2.4 dB.
        if args.weight == "note":
            wcurve = W.mean(axis=0)
        else:
            wcurve = imp
        wcurve = wcurve / max(wcurve.sum(), 1e-300)
        barva = float(np.sqrt((wcurve * (g - (wcurve * g).sum()) ** 2).sum()))
        print("%.4f  colour %.4f  (notes %d, outside anchors %d,"
              " spread of the overall curve %.1f dB)"
              % (score, barva, len(items), mimo, g.max() - g.min()))
        return

    print("notes in total %d, isolated and usable %d%s"
          % (n, len(items),
             "" if args.layers == "vse" else " (layers only: %s)" % args.layers))
    if mimo:
        print("   %d notes skipped - they lie outside the anchor range (%.0f-%.0f s)"
              % (mimo, t_lo, t_hi))
    print()

    # Distortion of the whole: the median over all notes. The median, not the
    # mean - one wild note must not move the curve.
    glob = np.median(D, axis=0)
    print("distortion of the whole (recording minus our render, median over notes):")
    print("   (and the band's share of the note energy - a small number = almost silence,")
    print("    a large difference there hardly shows in what is audible)")
    for i in range(0, len(glob), 3):
        print("   %5.0f Hz %+6.1f dB   (energy %4.1f %%)"
              % (EDGES[i], glob[i], 100 * imp[i]))
    print()
    print("   spread of the curve %.1f dB (if it were flat, only the volume would differ)"
          % (glob.max() - glob.min()))
    vazeny = float(np.sqrt((imp * glob ** 2).sum() / max(imp.sum(), 1e-300)))
    print("   WEIGHTED spread %.1f dB (the same, but per band with the note energy -"
          % vazeny)
    print("    this is the number that corresponds to what is really audible)")

    # What remains after subtracting the overall curve.
    R = D - glob

    key = {}
    for poradi, ((i, _), r) in enumerate(zip(items, R)):
        row = rows[i]
        if args.by == "sample":
            k = "sample %s" % row["ccca"]
        elif args.by == "ch":
            k = "channel %s" % row["ch"]
        elif args.by == "note":
            k = "note %s" % row["note"]
        elif args.by == "q":
            # Filter resonance. Q = 0 with a fully open cutoff does not go through the
            # filter at all, so this group is the control: when the deviation
            # fits only Q > 0, the cause is in the filter.
            k = "Q %d" % int(row["Q"], 16)
        elif args.by == "cutoff":
            c = int(row["cutoff"], 16)
            k = "cutoff %02X-%02X" % (c & ~0x1F, (c & ~0x1F) + 31)
        elif args.by == "atten":
            # in sixteens, so a group has enough notes
            k = "atten %02X-%02X" % (int(row["atten"], 16) & ~0xF,
                                     (int(row["atten"], 16) & ~0xF) + 15)
        else:
            k = "velocity %d" % (int(row["vel"]) // 16 * 16)
        key.setdefault(k, []).append((r, W[poradi]))

    print()
    print("deviations from the distortion of the whole by groups (%s):" % args.by)
    print("%-18s %5s %8s   %s" % ("group", "not", "deviation", "where most"))
    out = []
    for k, v in key.items():
        if len(v) < args.min_notes:
            continue
        m = np.median(np.array([r for r, _ in v]), axis=0)
        # The measure is made from the absolute deviation - we care about the
        # size, not the direction. Weighted by band energy - otherwise an almost
        # silent top band wins, where a few dB of absolute difference explode in
        # relative decibels (found on canon: a sample with "20 dB" at 12191 Hz
        # had 0.1 % of the note energy in that band - the weighted difference
        # was 1.2 dB, not 24). The weight comes from **this group**, not from
        # the whole piece - otherwise an instrument gets a band counted in which
        # it has nothing itself.
        wv = imp if args.weight == "global" else np.mean(
            np.array([w for _, w in v]), axis=0)
        wv = wv / max(wv.sum(), 1e-300)
        sila = float(np.sqrt((wv * m ** 2).sum() / max(wv.sum(), 1e-300)))
        j = int(np.argmax(wv * np.abs(m)))
        out.append((sila, k, len(v), EDGES[j], m[j]))
    out.sort(reverse=True)
    for sila, k, cnt, fq, val in out[:20]:
        print("%-18s %5d %6.1f dB   %5.0f Hz %+.1f dB" % (k, cnt, sila, fq, val))

    if not out:
        print("   (no group has at least %d notes - lower --min-notes)"
              % args.min_notes)
        return

    print()
    med = np.median([s for s, _, _, _, _ in out])
    print("median deviation strength %.1f dB; groups above %.1f dB are worth a look"
          % (med, med * 2))

    if args.list:
        print()
        print("worst single notes:")
        sila = np.sqrt((W * R ** 2).sum(axis=1) / np.maximum(W.sum(axis=1), 1e-300))
        for j in np.argsort(sila)[::-1][:15]:
            i = items[j][0]
            row = rows[i]
            print("   %7.2f s  channel %-3s note %-4s sample %-8s  %.1f dB"
                  % (t_ours[i], row["ch"], row["note"], row["ccca"], sila[j]))


if __name__ == "__main__":
    main()

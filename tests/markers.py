#!/usr/bin/env python
"""Pairing a recording and a MIDI through **markers**, not through the whole piece.

Why not a cross-correlation of the whole (`match_tracks.py`): a recording need
not be a whole piece. It is often just a part, can start anywhere, can repeat
and can hold two pieces in a row. A correlation of whole courses does not
cope - a small tempo shift or a cut start and it drops to zero.

This script looks for **matching sections**:

  1. Markers are extracted from both sides - note onset times. From MIDI the
     note-ons (simultaneous notes merge into one marker), from audio the
     peaks of the spectral flux.
  2. Fingerprints are made from the markers: for every marker a triple
     `(dt1, dt2)` to the two following ones, rounded to 20 ms. A triple is
     specific enough to keep random matches few, and it does not depend on
     absolute time.
  3. Every common fingerprint votes for an offset `t_ref - t_query`. When the
     recording and the MIDI belong together, all votes fall into one offset.

The result is not one number but **a list of sections**: how many markers
lined up and at which offset. Two strong peaks = the piece is in the
recording twice.

    python tests/markers.py recording.mp3 --midi midi
    python tests/markers.py recording.mp3 --midi one.xmi --detail
"""
import argparse
import collections
import glob
import math
import os
import sys

import numpy as np

try:
    import soundfile as sf
except ImportError:
    sys.exit("the soundfile module is missing (pip install soundfile)")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midi_read  # noqa: E402

HOP = 0.01           # 10 ms frame for the spectral flux
MIN_GAP = 0.06       # smallest spacing of two markers
QUANT = 0.02         # rounding of the spacing in a fingerprint
FAN = 5              # how many following markers go into a fingerprint
BIN = 0.05           # bin width when voting for an offset
CHROMA_HOP = 0.05


def audio_chroma(path, hop=CHROMA_HOP):
    """12-class pitch fingerprint of the sound in short frames.

    Markers only say *when* something happened. With regular rhythmic music
    (Doom is the typical case) two different pieces can get almost the same
    score that way. Chroma adds *which tones* sound at that moment and is
    independent of the instrument used.
    """
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    n = max(256, int(sr * hop))
    frames = len(x) // n
    if frames < 2:
        return np.zeros((0, 12))
    fr = x[:frames * n].reshape(frames, n) * np.hanning(n)
    mag = np.abs(np.fft.rfft(fr, n=4096, axis=1))[:, 1:]
    freq = np.fft.rfftfreq(4096, 1.0 / sr)[1:]
    valid = (freq >= 27.5) & (freq <= 5000.0)
    pc = np.mod(np.rint(69 + 12 * np.log2(freq[valid] / 440.0)).astype(int), 12)
    out = np.zeros((frames, 12))
    for c in range(12):
        out[:, c] = mag[:, valid][:, pc == c].sum(axis=1)
    out /= np.linalg.norm(out, axis=1, keepdims=True) + 1e-12
    return out


def midi_chroma(song, hop=CHROMA_HOP):
    n = int(song.length / hop) + 1
    out = np.zeros((n, 12))
    for t0, dur, ch, note, vel in song.notes:
        i0, i1 = max(0, int(t0 / hop)), min(n, int(math.ceil((t0 + dur) / hop)))
        if i1 > i0:
            out[i0:i1, int(note) % 12] += max(float(vel), 1.0)
    out /= np.linalg.norm(out, axis=1, keepdims=True) + 1e-12
    return out


def chroma_score(query, ref, offset, scale=1.0, hop=CHROMA_HOP):
    """Mean chroma match at a known affine alignment (0..1)."""
    if not len(query) or not len(ref):
        return 0.0
    ix = np.rint((scale * np.arange(len(query)) * hop + offset) / hop).astype(int)
    ok = (ix >= 0) & (ix < len(ref))
    if ok.sum() < max(20, len(query) // 4):
        return 0.0
    # Silent MIDI frames are no musical evidence.
    active = np.linalg.norm(ref[ix[ok]], axis=1) > 1e-9
    if not active.any():
        return 0.0
    q = query[ok][active]
    r = ref[ix[ok]][active]
    # Subtracting the average colour (e.g. constant bass and drums) leaves only
    # the changes of harmony; those matter more for telling two pieces apart.
    q = q - q.mean(axis=0)
    r = r - r.mean(axis=0)
    q /= np.linalg.norm(q, axis=1, keepdims=True) + 1e-12
    r /= np.linalg.norm(r, axis=1, keepdims=True) + 1e-12
    return float(np.mean(np.sum(q * r, axis=1)))


def audio_markers(path, hop=HOP):
    """Note onset times from audio - peaks of the spectral flux."""
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    n = int(sr * hop)
    frames = len(x) // n
    if frames < 8:
        return np.zeros(0)
    fr = x[:frames * n].reshape(frames, n) * np.hanning(n)
    spec = np.abs(np.fft.rfft(fr, axis=1))
    flux = np.zeros(frames)
    flux[1:] = np.sum(np.maximum(np.diff(spec, axis=0), 0.0), axis=1)

    # a threshold floating with the surroundings, so it works in quiet passages too
    w = int(0.5 / hop)
    pad = np.pad(flux, w, mode="edge")
    local = np.array([pad[i:i + 2 * w + 1].mean() for i in range(frames)])
    thr = local * 1.6 + 1e-9

    peaks, last = [], -1e9
    for i in range(1, frames - 1):
        if flux[i] > thr[i] and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]:
            t = i * hop
            if t - last >= MIN_GAP:
                peaks.append(t)
                last = t
    return np.array(peaks)


def midi_markers(path):
    """Note-on times; simultaneous notes merge into one marker."""
    song = midi_read.read(path)
    out, last = [], -1e9
    for t0, dur, ch, note, vel in song.notes:
        if t0 - last >= MIN_GAP:
            out.append(t0)
            last = t0
    return np.array(out), song


def hashes(marks):
    """{fingerprint: [times]} - triples of spacings, independent of absolute time."""
    h = collections.defaultdict(list)
    for i in range(len(marks)):
        for j in range(i + 1, min(i + 1 + FAN, len(marks))):
            d1 = marks[j] - marks[i]
            if d1 > 4.0:
                break
            for k in range(j + 1, min(j + 1 + FAN, len(marks))):
                d2 = marks[k] - marks[i]
                if d2 > 8.0:
                    break
                key = (int(round(d1 / QUANT)), int(round(d2 / QUANT)))
                h[key].append(marks[i])
    return h


def vote(hq, hr):
    """Voting for the offset. Returns a sorted list (marker count, offset).

    The key is to count **distinct markers**, not fingerprint matches. One
    marker makes up to FAN*FAN fingerprints, so if every match voted
    separately, thousands of votes would come out at every offset and the
    difference between the right and a random candidate would vanish in the
    noise (it happened - the calibration gave a score of 4.96 and three
    equally strong offsets next to each other).
    """
    box = collections.defaultdict(set)
    for key, tq_list in hq.items():
        tr_list = hr.get(key)
        if not tr_list:
            continue
        for tq in tq_list:
            for tr in tr_list:
                box[round((tr - tq) / BIN)].add(round(tq, 3))
    return sorted(((len(v), b * BIN) for b, v in box.items()), reverse=True)


REFINE_TOL = 0.08        # tolerance of the direct marker comparison (s)
SCALE_RANGE = 0.01       # +-1 % tempo drift search
SCALE_STEPS = 11         # how many steps to try in this range


def refine(mq, mr, offset, scale=1.0, tol=REFINE_TOL, want_matches=False):
    """Direct match count: every query marker looks for the nearest reference
    marker within `tol`, regardless of the fingerprint triples.

    Unlike `vote()` this is not sensitive to the exact spacing of neighbouring
    markers - it finds a match even where the whole recording is slightly
    "stretched" in time against the MIDI (another sound card clock when
    recording real hardware, a slightly different sample rate when
    digitising, etc.), where hashing drifts out of the 20 ms fingerprint bins
    over time even though the markers still match in order.

    `want_matches=True` also returns the indices of the query markers that
    hit - useful for `segments()`, when the hit markers are to be removed from
    the query and the search goes on in the rest.
    """
    if len(mq) == 0 or len(mr) == 0:
        return (0, []) if want_matches else 0
    # The match must be one-to-one. The previous variant looked for the
    # nearest reference marker for every query marker independently. Two (or
    # more) dense audio peaks could then count one MIDI note-on repeatedly,
    # and an unrelated piece got an artefact score of up to 1.00.
    # Both time lists are sorted and scale is positive, so a single pass is
    # enough; once paired, a MIDI marker is not used again.
    mr_sorted = np.sort(mr)
    hit = 0
    idx = []
    ri = 0
    for qi, t in enumerate(mq):
        target = scale * t + offset
        while ri < len(mr_sorted) and mr_sorted[ri] < target - tol:
            ri += 1
        if ri < len(mr_sorted) and mr_sorted[ri] <= target + tol:
            hit += 1
            idx.append(qi)
            ri += 1
    return (hit, idx) if want_matches else hit


# --- protection against random matches ------------------------------------
#
# `refine()` with a free scale is useful exactly where there are many markers
# spread over a long section - there chance cannot keep "hitting" under
# different scales across the whole span. But with a small handful of markers
# squeezed into a short window (typically the rest after removing a real match
# in `segments()`, or simply an unrelated recording against a dense
# reference) the free scale has so many degrees of freedom that it can "bend"
# time to fit almost anything. Verified by a test: 15 random points in a 3 s
# window against a dense reference found a match with 100 % success (14.7 of
# 15 points on average) - that is no signal, it is an artefact of too free a
# search on a small sample.
#
# Solution: the match count is not compared with a fixed threshold but with
# how many would come out by pure chance at the same local density of the
# reference (a Poisson estimate), and a margin of several standard deviations
# is required on top of a minimal time span (so that a small dense cluster,
# where the density estimate is unreliable, does not pass just because of
# that).

SIG_Z = 6.0        # how many standard deviations above chance count as "certain"
MIN_SPAN = 3.0      # smallest span of matching markers (s)


def _chance_hits(n, tol, density):
    """Expected count and standard deviation of random matches (Poisson)."""
    if n <= 0 or density <= 0:
        return 0.0, 0.0
    p = 1.0 - math.exp(-2.0 * tol * density)
    p = min(max(p, 0.0), 1.0)
    e = n * p
    sd = math.sqrt(n * p * (1.0 - p))
    return e, sd


def _density(mr):
    if len(mr) < 2:
        return 0.0
    span = float(np.max(mr) - np.min(mr))
    return len(mr) / span if span > 0 else 0.0


def significant(hit, span, n, density, tol=REFINE_TOL, z=SIG_Z):
    """Is `hit` matches over the span `span` statistically significant, or does
    chance explain it at the given reference density `density`?
    """
    if span < MIN_SPAN:
        return False
    e, sd = _chance_hits(n, tol, density)
    if sd <= 0:
        return hit > e
    return hit >= e + z * sd


EARLY_CHECK_AFTER = 2   # after how many (strongest) candidates of vote()
                         # check whether it makes sense to go on


def best_alignment(mq, hq, mr, hr, top=5, scale_range=SCALE_RANGE,
                    steps=SCALE_STEPS, tol=REFINE_TOL, allow_reject=True,
                    early_check_after=EARLY_CHECK_AFTER, z=SIG_Z):
    """The best marker match: a rough offset estimate from hashing (`vote`),
    then a fine scale refinement around the `top` strongest candidates.

    Returns ((match_count, offset, scale), original_list_from_vote,
    rejected_early). The third value says whether further refinement stopped
    early because it was already clear with high confidence that the file
    does not match - then it is (match_count=0, ...) and the caller can write
    a score of 0 right away.

    The first idea for a quick rejection - estimating an upper bound from the
    rough `vote()` score without real refinement - proved unreliable: a
    calibration on random and real (also drifted) pairs showed that the
    ranges of top-vote counts overlap (unrelated pairs up to 6, real matches
    from 2). And a separate "extra coarse scale sample" was needlessly slow,
    because it duplicated the work of the main search.

    Instead, **the same** work that would be done anyway is evaluated as it
    goes: after refining the scale on the first `early_check_after` strongest
    candidates of `vote()` (typically 2 * 11 = 22 calls of `refine()` out of
    5 * 11 = 55) it asks whether the best result so far can be statistically
    significant even with a generous reserve (2x + 8 on top, so the remaining
    weaker candidates could in theory still add). If not, it makes no sense
    to go on and the search ends at once - without losing the work done.
    """
    v = vote(hq, hr)
    if not v:
        return (0, 0.0, 1.0), v, True
    density = _density(mr) if allow_reject else 0.0
    e, sd = _chance_hits(len(mq), tol, density) if allow_reject else (0.0, 0.0)
    best = (0, 0.0, 1.0)
    for oi, (_, off) in enumerate(v[:top]):
        for i in range(steps):
            scale = 1.0 - scale_range + 2 * scale_range * i / max(steps - 1, 1)
            hit = refine(mq, mr, off, scale, tol)
            if hit > best[0]:
                best = (hit, off, scale)
        if allow_reject and oi + 1 == early_check_after:
            ceiling = best[0] * 2 + 8
            if ceiling < e + z * sd:
                return (0, 0.0, 1.0), v, True
    return best, v, False


MIN_SEG_HITS = 12    # absolute lower limit (even if it would pass statistically with fewer)
MAX_SEGMENTS = 6


def segments(mq, mr, tol=REFINE_TOL, max_segments=MAX_SEGMENTS):
    """Splits the query markers into sections, each corresponding to one
    part of the reference (one playing of the piece in the recording).

    Greedy: finds the best alignment for ALL remaining query markers, checks it
    with the significance test (`significant`) - not just by an absolute
    count - removes the hit markers and repeats on the rest. It ends when no
    alignment passes the test any more, when the markers run out, or after
    `max_segments` sections.

    Returns a list of sections sorted by time:
        [(t_start, t_end, offset, scale, match_count), ...]
    An empty list = there is no statistically significant section in the
    recording.
    """
    remaining = np.array(sorted(mq), dtype=float)
    density = _density(mr)
    hr = hashes(mr)
    out = []
    for _ in range(max_segments):
        if len(remaining) < MIN_SEG_HITS:
            break
        hq = hashes(remaining)
        # Here it is no longer about searching hundreds of foreign candidates but
        # about decomposing a pair just confirmed in pair_finder. The heuristic
        # quick rejection is too coarse for dense/rhythmic pieces and could
        # stop even a valid first section. The statistical test
        # `significant()` below stays the final protection against noise.
        (hit, off, scale), v, rejected = best_alignment(
            remaining, hq, mr, hr, tol=tol, allow_reject=False)
        if rejected or hit < MIN_SEG_HITS or not v:
            break
        _, idx = refine(remaining, mr, off, scale, tol, want_matches=True)
        if len(idx) < MIN_SEG_HITS:
            break
        matched = remaining[idx]
        span = float(matched.max() - matched.min())
        if not significant(len(idx), span, len(remaining), density, tol):
            break
        out.append((float(matched.min()), float(matched.max()), off, scale, len(idx)))
        remaining = np.delete(remaining, idx)
    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio", nargs="+")
    ap.add_argument("--midi", required=True)
    ap.add_argument("--top", type=int, default=4)
    ap.add_argument("--peaks", type=int, default=3,
                    help="how many offset peaks to print (repetitions of the piece)")
    ap.add_argument("--detail", action="store_true")
    args = ap.parse_args()

    if os.path.isdir(args.midi):
        cands = sorted(sum((glob.glob(os.path.join(args.midi, "**", p), recursive=True)
                            for p in ("*.mid", "*.MID", "*.xmi", "*.XMI")), []))
    else:
        cands = [args.midi]
    if not cands:
        sys.exit("no MIDI found")

    ref = {}
    for c in cands:
        try:
            m, song = midi_markers(c)
            if len(m) >= 12:
                ref[c] = (m, hashes(m), len(m), song)
        except Exception as ex:
            print(f"  skipping {os.path.basename(c)}: {ex}")
    print(f"candidates: {len(ref)}")

    for path in args.audio:
        am = audio_markers(path)
        ah = hashes(am)
        print(f"\n{os.path.basename(path)}: {len(am)} markers from audio")
        if len(am) < 12:
            print("   too few markers")
            continue

        res = []
        skipped = 0
        for c, (m, h, nm, song) in ref.items():
            (hit, off, scale), v, rejected = best_alignment(am, ah, m, h)
            if rejected:
                # quick_reject() determined with high confidence that refining the
                # scale cannot find a statistically significant match - score
                # 0 right away, without the expensive loop over scales.
                skipped += 1
                continue
            if not v:
                continue
            # score = the share of query markers that fit one offset
            # (after refining the scale - see `refine`/`best_alignment`)
            res.append((hit / max(len(am), 1), off, scale, v, c, nm, song))
        res.sort(reverse=True, key=lambda r: r[0])
        if skipped:
            print(f"   ({skipped} candidates rejected early - clearly no match)")

        for sc, off, scale, v, c, nm, song in res[:args.top]:
            # Thresholds calibrated for the original `vote()` score. `refine` also
            # finds matches spread by tempo drift, so scores come out generally
            # somewhat higher than before this extension - take the thresholds
            # as a starting point, not a final number, and recalibrate on a
            # known right/wrong pair if it behaves differently.
            mark = "  <== match" if sc > 0.40 else ("  (weak)" if sc > 0.25 else "")
            drift = f"  (scale {scale:+.3f})" if abs(scale - 1.0) > 1e-6 else ""
            print(f"   {sc:6.2f}  {os.path.basename(c):24} "
                  f"{nm:5} markers, {song.length:6.1f} s{mark}{drift}")
            if args.detail or sc > 0.40:
                print(f"           refined offset {off:+8.2f} s")
                for cnt, voff in v[:args.peaks]:
                    print(f"           {cnt:6} votes at offset {voff:+8.2f} s (rough, from hashing)")
                if args.detail:
                    segs = segments(am, m, tol=REFINE_TOL)
                    if len(segs) > 1:
                        print(f"           sections ({len(segs)}, statistically verified):")
                        for t0, t1, soff, sscale, shit in segs:
                            print(f"             {t0:7.1f}-{t1:7.1f} s in the recording  "
                                  f"({shit} matches, offset {soff:+.2f} s)")
        if not res or res[0][0] <= 0.25:
            print("   -> no MIDI matches this recording")


if __name__ == "__main__":
    main()

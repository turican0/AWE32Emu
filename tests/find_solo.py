#!/usr/bin/env python
"""Finds sections of a piece where **only one** instrument sounds (or just a few).

What it is for: when the whole mix differs from a real recording, one cannot
tell why. In a section where a single track plays, one can - and when it has
only a few notes, the difference can be attributed to a specific property
(envelope, filter, loop, effect).

    python tests/find_solo.py SAMPLES2/RELAX_VX.MID
    python tests/find_solo.py ... --guard 2.0 --min 1.5
    python tests/find_solo.py ... --max-ch 3

`--guard` is the silence the other channels must keep around the section, so
their reverb does not reach into it. The larger, the cleaner the section, but
the fewer hits.

`--max-ch N` allows up to N simultaneously sounding channels - useful for
dense arrangements where there is no pure solo anywhere.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midi_read  # noqa: E402

GM = {
    0: "Piano", 16: "Organ", 24: "Nylon Gtr", 25: "Steel Gtr", 30: "Dist Gtr",
    31: "Gtr Harmonics", 36: "Slap Bass 1", 37: "Slap Bass 2", 48: "Strings",
    50: "Synth Strings", 52: "Choir Aahs", 61: "Brass Sect", 62: "Synth Brass",
    80: "Square Lead", 81: "Saw Lead", 87: "Bass+Lead", 95: "Sweep Pad",
}


def timeline(song, res, tail):
    """For every channel a bit array 'sounds' in steps of `res` seconds."""
    n = int(song.length / res) + 2
    grid = {}
    for t0, dur, ch, note, vel in song.notes:
        g = grid.setdefault(ch, bytearray(n))
        a = int(t0 / res)
        b = min(n, int((t0 + dur + tail) / res) + 1)
        for i in range(a, b):
            g[i] = 1
    return grid, n


def dilate(mask, n, k):
    """Widens the ones by `k` steps on both sides (guard around foreign notes)."""
    if k <= 0:
        return mask
    out = bytearray(n)
    i = 0
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            for x in range(max(0, i - k), min(n, j + k)):
                out[x] = 1
            i = j
        else:
            i += 1
    return out


def runs(mask, n, want=0):
    """Maximal sections where mask == want."""
    out, i = [], 0
    while i < n:
        if mask[i] != want:
            i += 1
            continue
        j = i
        while j < n and mask[j] == want:
            j += 1
        out.append((i, j))
        i = j
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song")
    ap.add_argument("--guard", type=float, default=1.0,
                    help="silence of the other channels around the section (s), default 1.0")
    ap.add_argument("--min", dest="minlen", type=float, default=1.0,
                    help="shortest interesting section (s), default 1.0")
    ap.add_argument("--tail", type=float, default=0.3,
                    help="decay of a note after its length (s), default 0.3")
    ap.add_argument("--res", type=float, default=0.02)
    ap.add_argument("--ch", type=int, help="only this channel")
    ap.add_argument("--max-ch", dest="maxch", type=int, default=1,
                    help="how many channels may sound in the section, default 1")
    args = ap.parse_args()

    song = midi_read.read(args.song)
    grid, n = timeline(song, args.res, args.tail)
    guard = int(args.guard / args.res)

    print(f"{os.path.basename(args.song)}: {len(song.notes)} not, "
          f"{len(grid)} channels, {song.length:.1f} s")
    print(f"guard {args.guard} s, min. length {args.minlen} s, "
          f"at most {args.maxch} channel(s)\n")

    found = []
    if args.maxch == 1:
        # for every channel: the window where all others are silent (guard included)
        for ch in sorted(grid):
            if args.ch is not None and ch != args.ch:
                continue
            blocked = bytearray(n)
            for c, g in grid.items():
                if c == ch:
                    continue
                d = dilate(g, n, guard)
                for i in range(n):
                    if d[i]:
                        blocked[i] = 1
            for a, b in runs(blocked, n, 0):
                if (b - a) * args.res < args.minlen:
                    continue
                if not any(grid[ch][a:b]):
                    continue
                # trim to what really sounds in the window
                s = next(i for i in range(a, b) if grid[ch][i])
                e = max(i for i in range(a, b) if grid[ch][i]) + 1
                if (e - s) * args.res >= args.minlen:
                    found.append((s * args.res, e * args.res, [ch]))
    else:
        # windows where at most N channels sound at once
        count = bytearray(n)
        for g in grid.values():
            d = dilate(g, n, guard)
            for i in range(n):
                if d[i]:
                    count[i] += 1
        ok = bytearray(1 if 0 < c <= args.maxch else 0 for c in count)
        for a, b in runs(ok, n, 1):
            if (b - a) * args.res < args.minlen:
                continue
            chs = sorted(c for c, g in grid.items() if any(g[a:b]))
            found.append((a * args.res, b * args.res, chs))

    if not found:
        print("nothing found - try a smaller --guard or a larger --max-ch")
        return 1

    print(f"{'from':>7} {'to':>7} {'length':>6}  channels  notes")
    for t0, t1, chs in sorted(found):
        ns = [x for x in song.notes if x[2] in chs and t0 - 0.05 <= x[0] < t1]
        pitches = sorted({x[3] for x in ns})
        vels = sorted({x[4] for x in ns})
        desc = ", ".join(f"ch{c}/pr{song.program_of(c, t0)}"
                         f"{' ' + GM[song.program_of(c, t0)] if song.program_of(c, t0) in GM else ''}"
                         for c in chs)
        print(f"{t0:7.2f} {t1:7.2f} {t1-t0:6.2f}  {desc}")
        if ns:
            print(f"{'':22}{len(ns):3} notes, pitch {pitches[0]}..{pitches[-1]}, "
                  f"vel {vels[0]}..{vels[-1]}")
    print(f"\ntotal {len(found)} sections, "
          f"longest {max(t1-t0 for t0, t1, _ in found):.2f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())

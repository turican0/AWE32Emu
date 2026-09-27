#!/usr/bin/env python
"""Compares the registers at note-on between our emulation and the real driver.

The input is two port-write traces (see tests/trace_diff.py). Real notes are
recognised by `IFATN` not being `FF00` - with that value the driver silences
all 32 voices at the start.

    python tests/notes_diff.py ours.trace driver.trace
    python tests/notes_diff.py ours.trace driver.trace --note 0
    python tests/notes_diff.py ours.trace driver.trace --pair --dframes 0:6000000

`--dframes` trims the driver's trace to the frames of one piece. It is
necessary when the game played several pieces in a row during the
measurement - otherwise notes that are not in our input file at all get into
the comparison (see docs/re-notes/86box_comparison.md 16.1).

The first table says how many of the notes match for each register. The
second looks for a systematic relation (ratio, difference, constant) in the
mismatching registers, because exactly that shows where the conversion is
wrong.
"""
import argparse
import collections
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_diff import parse, note_ons  # noqa: E402


def start_addr(r):
    """24-bit sample address from CCCA (low word + low byte of CCCA^)."""
    return ((r.get("CCCA^", 0) & 0xFF) << 16) | (r.get("CCCA", 0) & 0xFFFF)


def real_notes(path, frames=None):
    """Real musical notes: they have a sample address in CCCA and a non-zero envelope.

    The drivers silence all 32 voices at the start. `SBAWE.VXD` also writes a
    non-zero CCCA then, but a zero envelope (`ATKHLDV = 0`), so both
    conditions decide. Formerly it filtered by `IFATN != 0xFF00`, but that
    dropped real notes with zero attenuation - the Magic Carpet 2 music has
    such notes.
    """
    ev, _ = parse(path)
    out = [n for n in note_ons(ev)
           if n[2].get("CCCA", 0) != 0 and n[2].get("ATKHLDV", 0) != 0]
    if frames:
        lo, hi = frames
        out = [n for n in out if lo <= n[0] < hi]
    return out


def agreement(a_notes, b_notes):
    n = min(len(a_notes), len(b_notes))
    regs = set()
    for _, _, r in a_notes[:n]:
        regs |= set(r)
    for _, _, r in b_notes[:n]:
        regs |= set(r)
    regs = sorted(r for r in regs if not r.startswith("HW?") and r != "CHIPID")

    rows = []
    for reg in regs:
        same = 0
        tot = 0
        first = None
        for (_, _, ra), (_, _, rb) in zip(a_notes[:n], b_notes[:n]):
            x, y = ra.get(reg), rb.get(reg)
            if x is None or y is None:
                continue
            tot += 1
            if x == y:
                same += 1
            elif first is None:
                first = (x, y)
        if tot:
            rows.append((100.0 * same / tot, reg, same, tot, first))
    return sorted(rows, reverse=True)


def relation(a_notes, b_notes, reg, get=lambda v: v):
    """Tries to find a systematic relation between our value and the driver's."""
    n = min(len(a_notes), len(b_notes))
    pairs = []
    for (_, _, ra), (_, _, rb) in zip(a_notes[:n], b_notes[:n]):
        if reg in ra and reg in rb:
            pairs.append((get(ra[reg]), get(rb[reg])))
    if not pairs:
        return None

    tests = [
        ("the driver is a constant", collections.Counter(f"{b:04X}" for _, b in pairs)),
        ("difference driver-ours", collections.Counter(b - a for a, b in pairs)),
        ("ratio driver/ours", collections.Counter(round(b / a, 3) for a, b in pairs if a)),
    ]
    for label, c in tests:
        if not c:
            continue
        val, cnt = c.most_common(1)[0]
        if cnt == len(pairs):
            return f"{label} = {val}"
    return "no simple relation: " + ", ".join(
        f"{k}x{v}" for k, v in tests[1][1].most_common(4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("driver")
    ap.add_argument("--note", type=int, help="print one specific note")
    ap.add_argument("--pair", action="store_true",
                    help="pair notes by pitch and sample instead of by order")
    ap.add_argument("--dframes", help="trim of the driver trace, e.g. 0:6000000")
    ap.add_argument("--oframes", help="trim of our trace")
    args = ap.parse_args()

    rng = lambda t: tuple(int(x, 0) for x in t.split(":")) if t else None
    a = real_notes(args.ours, rng(args.oframes))
    b = real_notes(args.driver, rng(args.dframes))
    print(f"ours:    {len(a)} note-on  ({args.ours})")
    print(f"driver:  {len(b)} note-on  ({args.driver})")
    if not a or not b:
        return 1
    print()

    if args.note is not None:
        for label, notes in (("ours", a), ("driver", b)):
            frame, voice, regs = notes[args.note]
            print(f"{label}: frame {frame} voice {voice}")
            print("   " + "  ".join(f"{k}={regs[k]:04X}" for k in sorted(regs)))
            print()
        return 0

    if args.pair:
        # When the driver trace does not start at the start of the piece (e.g. the
        # game had been running a while), order pairing does not fit. Here every
        # driver note is paired with our note of the same pitch and sample, so
        # only the parameter conversion is compared, not the sequencing.
        # The key is (pitch, sample). The sample is recognised by the address
        # in CCCA, but only approximately - that very address can differ by a
        # few words, and if it were part of the key, a difference in it could
        # never show. So our note with the nearest address is taken, if within
        # 256 words.
        idx = collections.defaultdict(list)
        for _, _, r in a:
            idx[r.get("IP")].append(r)
        pairs = []
        for _, _, rb in b:
            cand = idx.get(rb.get("IP"))
            if not cand:
                continue
            sb = start_addr(rb)
            ra = min(cand, key=lambda r: abs(start_addr(r) - sb))
            if abs(start_addr(ra) - sb) < 256:
                pairs.append((ra, rb))
        print(f"paired {len(pairs)} of {len(b)} driver notes\n")
        if not pairs:
            return 1
        regs = set()
        for ra, rb in pairs:
            regs |= set(ra) & set(rb)
        regs = sorted(r for r in regs if not r.startswith("HW?") and r != "CHIPID")
        print(f"{'register':10} {'match':>12}   example")
        bad = []
        for reg in regs:
            same = sum(1 for ra, rb in pairs if ra.get(reg) == rb.get(reg))
            ex = next((f"{ra[reg]:04X} -> {rb[reg]:04X}"
                       for ra, rb in pairs if ra.get(reg) != rb.get(reg)), "")
            pct = 100.0 * same / len(pairs)
            print(f"{'OK ' if pct == 100 else '   '}{reg:10} "
                  f"{same:5}/{len(pairs):<5} {pct:5.1f}%  {ex}")
            if pct < 100:
                bad.append(reg)
        if not bad:
            print("\nall registers match")
            return 0
        print("\nrelations of the mismatching ones:")
        for reg in bad:
            d = collections.Counter(rb[reg] - ra[reg] for ra, rb in pairs)
            print(f"  {reg:10} difference: " +
                  ", ".join(f"{k}x{v}" for k, v in d.most_common(4)))
        return 0

    rows = agreement(a, b)
    print(f"{'register':10} {'match':>10}   example")
    for pct, reg, same, tot, first in rows:
        mark = "OK " if pct == 100 else "   "
        ex = f"{first[0]:04X} -> {first[1]:04X}" if first else ""
        print(f"{mark}{reg:10} {same:4}/{tot:<4}{pct:6.1f}%   {ex}")

    bad = [r for r in rows if r[0] < 100]
    if not bad:
        print("\nall registers match")
        return 0

    print(f"\n{'register':10} systematic relation")
    for _, reg, _, _, _ in bad:
        print(f"{reg:10} {relation(a, b, reg)}")
        if reg == "IFATN":
            print(f"{'  cutoff':10} {relation(a, b, reg, lambda v: v >> 8)}")
            print(f"{'  atten':10} {relation(a, b, reg, lambda v: v & 0xFF)}")
        if reg in ("DCYSUSV", "DCYSUS", "ATKHLDV", "ATKHLD"):
            print(f"{'  high':10} {relation(a, b, reg, lambda v: (v >> 8) & 0x7F)}")
            print(f"{'  low':10} {relation(a, b, reg, lambda v: v & 0x7F)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

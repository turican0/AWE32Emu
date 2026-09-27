#!/usr/bin/env python
"""Compares two port-write traces of the EMU8000.

Typical use: one trace is from our player (`AWE32Emu.exe --trace`), the other
from 86Box, in which the real Creative driver played (`set
EMU8K_TRACE=...`). The goal is to verify the MIDI -> registers layer, not the
chip core, so times are ignored and the sequence of writes is compared.

    python tests/trace_diff.py A.trace                 # overview of one
    python tests/trace_diff.py A.trace B.trace         # overview of both
    python tests/trace_diff.py A.trace B.trace --notes # note-on breakdown
    python tests/trace_diff.py A.trace B.trace --seq   # first differences
"""
import argparse
import collections
import sys

# --- register map ----------------------------------------------------------
# The key is (port after masking with 0xF02, register index).
PORT_NAME = {0x600: "D0", 0x602: "D0H", 0xA00: "D1", 0xA02: "D2", 0xE00: "D3"}

REG_NAME = {
    ("D0", 0): "CPF",    ("D0", 1): "PTRX",   ("D0", 2): "CVCF",  ("D0", 3): "VTFT",
    ("D0", 4): "Z2",     ("D0", 5): "Z1",     ("D0", 6): "PSST",  ("D0", 7): "CSL",
    ("D0H", 0): "CPF^",  ("D0H", 1): "PTRX^", ("D0H", 2): "CVCF^", ("D0H", 3): "VTFT^",
    ("D0H", 4): "Z2^",   ("D0H", 5): "Z1^",   ("D0H", 6): "PSST^", ("D0H", 7): "CSL^",
    ("D1", 0): "CCCA",   ("D1", 2): "INIT1",  ("D1", 3): "INIT3",
    ("D1", 4): "ENVVOL", ("D1", 5): "DCYSUSV", ("D1", 6): "ENVVAL", ("D1", 7): "DCYSUS",
    ("D2", 0): "CCCA^",  ("D2", 2): "INIT2",  ("D2", 3): "INIT4",
    ("D2", 4): "ATKHLDV", ("D2", 5): "LFO1VAL", ("D2", 6): "ATKHLD", ("D2", 7): "LFO2VAL",
    ("D3", 0): "IP",     ("D3", 1): "IFATN",  ("D3", 2): "PEFE",   ("D3", 3): "FMMOD",
    ("D3", 4): "TREMFRQ", ("D3", 5): "FM2FRQ2", ("D3", 6): "UNK6C", ("D3", 7): "CHIPID",
}

# On D1/D2 register 1 the "voice" selects a specific hardware register.
HWCF_NAME = {
    9: "HWCF4", 10: "HWCF5", 13: "HWCF6", 14: "HWCF7",
    20: "SMALR", 21: "SMARR", 22: "SMALW", 23: "SMARW",
    26: "SMLD", 27: "WC", 29: "HWCF1", 30: "HWCF2", 31: "HWCF3",
}


class Event:
    __slots__ = ("frame", "port", "reg", "voice", "value", "name")

    def __init__(self, frame, port, reg, voice, value, name):
        self.frame = frame
        self.port = port
        self.reg = reg
        self.voice = voice
        self.value = value
        self.name = name

    def key(self):
        """What is compared - without the time."""
        return (self.name, self.voice, self.value)

    def __str__(self):
        return f"{self.name:8} v{self.voice:<2} = {self.value:04X}"


def parse(path):
    """Returns (list of events, line count)."""
    events = []
    pointer = 0
    raw = 0
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            try:
                frame = int(parts[0])
                port = int(parts[1], 16)
                value = int(parts[2], 16)
            except ValueError:
                continue
            raw += 1

            sel = port & 0xF02
            if sel == 0xE02:                 # pointer
                pointer = value
                continue
            pname = PORT_NAME.get(sel)
            if pname is None:
                continue

            reg = (pointer >> 5) & 7
            voice = pointer & 0x1F
            if pname in ("D1", "D2") and reg == 1:
                name = HWCF_NAME.get(voice, f"HW?{voice}")
                if pname == "D2" and voice not in (26, 27):
                    name += "^"
                events.append(Event(frame, pname, reg, voice, value, name))
                continue
            name = REG_NAME.get((pname, reg), f"{pname}.{reg}")
            events.append(Event(frame, pname, reg, voice, value, name))
    return events, raw


def summary(events):
    return collections.Counter(e.name for e in events)


def note_ons(events):
    """Note-on breakdown: the registers written since the last start of the voice.

    Note-on = a DCYSUSV write that switches the envelope generator on, i.e.
    bit 0x0080 changes from set to clear. That is exactly how 86Box recognises
    it (DCYSUSV_GENERATOR_ENGINE_ON), and so does our core. Bit 0x8000 means
    release, not start.
    """
    pending = collections.defaultdict(dict)
    engine_on = collections.defaultdict(bool)
    out = []
    for e in events:
        # INIT1..INIT4 are addressed by "voice", but have nothing to do with notes.
        if e.voice < 32 and not e.name.startswith("INIT"):
            pending[e.voice][e.name] = e.value
        if e.name == "DCYSUSV":
            on = not (e.value & 0x0080)
            if on and not engine_on[e.voice]:
                out.append((e.frame, e.voice, dict(pending[e.voice])))
                pending[e.voice] = {}
            engine_on[e.voice] = on
    return out


def show_summary(paths, sets):
    names = set()
    for ev in sets:
        names |= {e.name for e in ev}

    counters = [summary(ev) for ev in sets]
    width = max(len(p.split("/")[-1].split("\\")[-1]) for p in paths)
    width = max(width, 10)

    print(f"{'register':10}" + "".join(f"{p.split('/')[-1].split(chr(92))[-1]:>{width + 2}}"
                                      for p in paths))
    order = sorted(names, key=lambda n: -max(c[n] for c in counters))
    for n in order:
        row = f"{n:10}"
        for c in counters:
            row += f"{c[n]:>{width + 2}}"
        if len(counters) == 2:
            a, b = counters[0][n], counters[1][n]
            if a == 0 or b == 0:
                row += "   <-- only in one"
        print(row)
    print()
    for p, ev in zip(paths, sets):
        print(f"{p}: {len(ev)} register writes")


def show_notes(paths, sets, limit):
    for p, ev in zip(paths, sets):
        notes = note_ons(ev)
        print(f"\n=== {p}: {len(notes)} note-on ===")
        for frame, voice, regs in notes[:limit]:
            keys = sorted(regs)
            print(f"  frame {frame:>9}  voice {voice:2}  ({len(regs)} registers)")
            print("     " + "  ".join(f"{k}={regs[k]:04X}" for k in keys))


def show_seq(paths, sets, limit):
    a, b = sets
    ka = [e.key() for e in a]
    kb = [e.key() for e in b]
    n = min(len(ka), len(kb))
    shown = 0
    for i in range(n):
        if ka[i] != kb[i]:
            print(f"  #{i}: {paths[0]}: {a[i]}   |   {paths[1]}: {b[i]}")
            shown += 1
            if shown >= limit:
                print("  ...")
                break
    if shown == 0:
        print(f"  the first {n} writes match")
    if len(ka) != len(kb):
        print(f"  different length: {len(ka)} vs {len(kb)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("traces", nargs="+")
    ap.add_argument("--notes", action="store_true", help="note-on breakdown")
    ap.add_argument("--seq", action="store_true", help="first differences in the sequence")
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args()

    sets = []
    for p in args.traces:
        ev, raw = parse(p)
        sets.append(ev)
        print(f"{p}: {raw} lines, {len(ev)} register writes")
    print()

    if args.notes:
        show_notes(args.traces, sets, args.limit)
        return 0
    if args.seq:
        if len(sets) != 2:
            print("--seq needs exactly two traces")
            return 1
        show_seq(args.traces, sets, args.limit)
        return 0

    show_summary(args.traces, sets)
    return 0


if __name__ == "__main__":
    sys.exit(main())

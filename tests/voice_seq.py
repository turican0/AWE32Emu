#!/usr/bin/env python
"""Prints what happens with **one voice** in a trace, write by write.

`notes_diff.py` compares only the note-on moment. This tool shows the whole
history of a voice including note-offs and the writes in between - exactly
what the register comparison never covered.

    python tests/voice_seq.py some.trace --voice 3
    python tests/voice_seq.py some.trace --voice 3 --from 1228000 --to 1300000
    python tests/voice_seq.py some.trace --note 2        # the voice's 2nd note-on
    python tests/voice_seq.py A.trace B.trace --note 2    # two traces side by side

The write order is what interests us: the driver must set DCYSUSV last,
because it is what starts the envelope engine.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_diff  # noqa: E402


def load(path):
    events, _raw = trace_diff.parse(path)
    return events


def voice_events(events, voice):
    return [e for e in events if e.voice == voice]


def note_spans(events, voice):
    """Splits the history of a voice into sections between starts (a DCYSUSV write without 0x80)."""
    evs = voice_events(events, voice)
    spans = []
    cur = []
    for e in evs:
        if e.name == "DCYSUSV" and not (e.value & 0x8000) and not (e.value & 0x0080):
            if cur:
                spans.append(cur)
            cur = [e]
        else:
            cur.append(e)
    if cur:
        spans.append(cur)
    return spans


def fmt(e, base):
    rel = e.frame - base
    return "%+9d %-8s %04X" % (rel, e.name, e.value)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("traces", nargs="+")
    ap.add_argument("--voice", type=int, default=None,
                    help="which voice; the default is the first that plays in the trace")
    ap.add_argument("--note", type=int, default=None,
                    help="which start of the voice to print (1 = the first)")
    ap.add_argument("--from", dest="t0", type=int, default=None)
    ap.add_argument("--to", dest="t1", type=int, default=None)
    ap.add_argument("--limit", type=int, default=80)
    args = ap.parse_args()

    for path in args.traces:
        events = load(path)
        voice = args.voice
        if voice is None:
            for e in events:
                if e.name == "DCYSUSV" and not (e.value & 0x0080):
                    voice = e.voice
                    break
            voice = voice if voice is not None else 0

        print("=== %s, voice %d" % (os.path.basename(path), voice))

        if args.note is not None:
            spans = note_spans(events, voice)
            if args.note > len(spans):
                print("  the voice has only %d starts" % len(spans))
                continue
            span = spans[args.note - 1]
            base = span[0].frame
            for e in span[:args.limit]:
                print("  " + fmt(e, base))
            if len(span) > args.limit:
                print("  ... and %d more writes" % (len(span) - args.limit))
            print()
            continue

        evs = voice_events(events, voice)
        if args.t0 is not None:
            evs = [e for e in evs if e.frame >= args.t0]
        if args.t1 is not None:
            evs = [e for e in evs if e.frame <= args.t1]
        base = evs[0].frame if evs else 0
        for e in evs[:args.limit]:
            print("  " + fmt(e, base))
        if len(evs) > args.limit:
            print("  ... and %d more writes" % (len(evs) - args.limit))
        print()


if __name__ == "__main__":
    main()

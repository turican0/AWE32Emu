#!/usr/bin/env python
"""Common reading of .mid and .xmi into one list of events in seconds.

Returns a `Song` with:
    notes   [(time, length, channel, note, velocity)]
    ctrl    [(time, channel, controller, value)]
    progs   [(time, channel, program)]
    length  time of the last event

In `.xmi` a note-on carries its length directly (a peculiarity of the XMI
format), in `.mid` it is paired with the note-off. The length is useful to
compute when each channel really sounds - see `find_solo.py`.
"""
import struct


class Song:
    def __init__(self):
        self.notes = []
        self.ctrl = []
        self.progs = []
        self.length = 0.0
        self.division = 0
        self.tempo_bpm = 0.0

    def channels(self):
        return sorted({n[2] for n in self.notes})

    def program_of(self, channel, t=None):
        """The program in effect on a channel at time `t` (or the last known one)."""
        p = 0
        for tt, ch, prog in self.progs:
            if ch == channel and (t is None or tt <= t):
                p = prog
        return p


def _vlq(b, p):
    v = 0
    while True:
        x = b[p]
        p += 1
        v = (v << 7) | (x & 0x7F)
        if x < 0x80:
            return v, p


def read_mid(path):
    b = open(path, "rb").read()
    if b[:4] != b"MThd":
        raise ValueError("not a Standard MIDI File")
    ntrk, div = struct.unpack(">HH", b[10:14])
    song = Song()
    song.division = div

    # first collect all events in ticks, then convert to seconds
    raw = []          # (tick, track_order, type, channel, a, b)
    tempos = []       # (tick, us_per_quarter)
    off = 8 + struct.unpack(">I", b[4:8])[0]
    for tr in range(ntrk):
        if off + 8 > len(b):
            print(f"  warning: track {tr} - MTrk header missing, rest of the file skipped")
            break
        if b[off:off + 4] != b"MTrk":
            print(f"  warning: track {tr} - expected MTrk at {off}, found {b[off:off+4]!r}")
            break
        ln = struct.unpack(">I", b[off + 4:off + 8])[0]
        p, end, tick, run = off + 8, min(off + 8 + ln, len(b)), 0, 0
        try:
            while p < end:
                d, p = _vlq(b, p)
                tick += d
                if p >= end:
                    # only "orphan" bytes after the last event remain
                    # (e.g. padding appended by some XMI->MID converters)
                    break
                st = b[p]
                if st < 0x80:
                    st = run
                else:
                    p += 1
                run = st
                hi, ch = st & 0xF0, st & 0x0F
                if hi in (0x80, 0x90):
                    note, vel = b[p], b[p + 1]
                    p += 2
                    on = (hi == 0x90 and vel > 0)
                    raw.append((tick, tr, "on" if on else "off", ch, note, vel))
                elif hi == 0xB0:
                    raw.append((tick, tr, "cc", ch, b[p], b[p + 1]))
                    p += 2
                elif hi == 0xC0:
                    raw.append((tick, tr, "prog", ch, b[p], 0))
                    p += 1
                elif hi in (0xA0, 0xE0):
                    p += 2
                elif hi == 0xD0:
                    p += 1
                elif st == 0xFF:
                    meta = b[p]
                    p += 1
                    ln2, p = _vlq(b, p)
                    if meta == 0x51:
                        tempos.append((tick, int.from_bytes(b[p:p + 3], "big")))
                    p += ln2
                    if meta == 0x2F:
                        # end of track - always the last event per the SMF specification;
                        # anything after it (e.g. an extra junk byte in the
                        # chunk length) is ignored instead of breaking the
                        # parsing as a false next event
                        break
                elif st in (0xF0, 0xF7):
                    ln2, p = _vlq(b, p)
                    p += ln2
                else:
                    break
        except IndexError:
            print(f"  warning: track {tr} - an event ran past the end of the data, "
                  f"track truncated ({len(raw)} events read in total)")
        off = off + 8 + ln

    raw.sort(key=lambda e: (e[0], e[1]))
    tempos.sort()

    # ticks to seconds, respecting tempo changes
    def seconds(tick):
        us, last, t = 500000, 0, 0.0
        for tt, uu in tempos:
            if tt >= tick:
                break
            t += (tt - last) * us / div / 1e6
            last, us = tt, uu
        return t + (tick - last) * us / div / 1e6

    open_notes = {}
    for tick, _, kind, ch, a, v in raw:
        t = seconds(tick)
        song.length = max(song.length, t)
        if kind == "on":
            # the velocity of the note-ON is stored - a note-off almost always has it
            # zero, and with it all notes would come out silent
            open_notes.setdefault((ch, a), []).append((t, v))
        elif kind == "off":
            st = open_notes.get((ch, a))
            if st:
                t0, v0 = st.pop(0)
                song.notes.append((t0, t - t0, ch, a, v0))
        elif kind == "cc":
            song.ctrl.append((t, ch, a, v))
        elif kind == "prog":
            song.progs.append((t, ch, a))
    # notes without a note-off (should not happen) - give them 1 s
    for (ch, a), starts in open_notes.items():
        for t0, v0 in starts:
            song.notes.append((t0, 1.0, ch, a, v0))
    song.notes.sort()
    song.tempo_bpm = 60e6 / tempos[-1][1] if tempos else 120.0
    return song


def read_xmi(path):
    import xmi_events
    b = open(path, "rb").read()
    chunks = xmi_events.find_evnt(b)
    if not chunks:
        raise ValueError("no EVNT chunk in the XMI")
    s, e = chunks[0]
    song = Song()
    song.division = 120                      # XMI has a fixed clock of 120 Hz
    for tick, st, a, v, dur in xmi_events.events(b, s, e):
        t = tick / 120.0
        song.length = max(song.length, t)
        ch = st & 0x0F
        if st & 0xF0 == 0x90:
            song.notes.append((t, dur / 120.0, ch, a, v))
        elif st & 0xF0 == 0xB0:
            song.ctrl.append((t, ch, a, v))
        elif st & 0xF0 == 0xC0:
            song.progs.append((t, ch, a))
    song.notes.sort()
    return song


def read(path):
    return read_xmi(path) if path.lower().endswith(".xmi") else read_mid(path)


if __name__ == "__main__":
    import sys
    sg = read(sys.argv[1])
    print(f"{len(sg.notes)} notes, {len(sg.ctrl)} controllers, length {sg.length:.1f} s")
    for ch in sg.channels():
        n = [x for x in sg.notes if x[2] == ch]
        print(f"  channel {ch:2}  program {sg.program_of(ch):3}  {len(n):5} notes, "
              f"first at {n[0][0]:.2f} s")

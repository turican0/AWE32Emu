#!/usr/bin/env python
"""Finds in the game's EXE by what it selects the music file.

Bullfrog games have several variants of every piece for different sound
cards (`004_C2INTRO_f/_g/_r/_w.xmi`). Which belongs to the AWE32 can be
derived from the behaviour (register trace), but it is more reliable to find
it directly in the code - the string the game builds the file name from.

    python tests/find_music_sel.py NETHERW.EXE
"""
import argparse
import re


def okoli(d, i, pred=48, za=48):
    a = max(0, i - pred)
    b = min(len(d), i + za)
    s = d[a:b]
    txt = "".join(chr(c) if 32 <= c < 127 else "." for c in s)
    return "0x%06X  %s" % (i, txt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--max", type=int, default=6, help="how many hits to print")
    args = ap.parse_args()

    d = open(args.exe, "rb").read()
    print("%s: %d B" % (args.exe, len(d)))
    print()

    # Strings that should reveal how the file name is built.
    vzory = [
        (b"%s_%c", "name template with a suffix"),
        (b"_%c.", "suffix before the extension"),
        (b"%s%c", "template with a character"),
        (b".XMI", "extension upper case"),
        (b".xmi", "extension lower case"),
        (b"XMID", "XMI marker"),
        (b"C2INTRO", "piece name"),
        (b"SBAWE32", "AWE32 driver"),
        (b".SBK", "bank"),
        (b"MUSIC", "music file"),
    ]
    for pat, popis in vzory:
        hits = [m.start() for m in re.finditer(re.escape(pat), d)]
        if not hits:
            continue
        print("=== %s  (%s) - %d occurrences ===" % (pat.decode("latin1"),
                                                 popis, len(hits)))
        for i in hits[:args.max]:
            print("   " + okoli(d, i))
        print()

    # The suffixes alone: look for short strings like "_w" next to each other in a table.
    print("=== places where the suffixes _f/_g/_r/_w are close together ===")
    for m in re.finditer(rb"_[fgrw]", d):
        i = m.start()
        blok = d[max(0, i - 24):i + 24]
        if sum(blok.count(bytes("_" + c, "ascii")) for c in "fgrw") >= 3:
            print("   " + okoli(d, i, 32, 32))


if __name__ == "__main__":
    main()

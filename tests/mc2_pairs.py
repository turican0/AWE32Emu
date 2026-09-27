#!/usr/bin/env python
"""Checks which MIDI each Magic Carpet 2 recording belongs to.

Two questions at once:

1. **Which variant?** Every piece has four files in `midi/` - `_f`, `_g`,
   `_r`, `_w` - i.e. four target sound cards. The intro recording did not
   align with our render of the `_w` variant at all (0.046), so the question
   is whether it was taken from another variant. The alignment is by **onset
   times**, not by colour, so the difference in what played it does not
   matter - it decides whether it is the same arrangement.

2. **Which other pieces have a usable pair?** Besides the intro and the menu
   `ogg/` holds the in-game and cut-scene music too.

Note: the music **in the game** has the last two or three channels muted
(the game's CC119 trigger), so the recording has fewer notes than the XMI.
That need not hurt the onset alignment, but it does hurt colour
measurements.

    python tests/mc2_pairs.py                 # everything
    python tests/mc2_pairs.py --only 004      # the intro only
    python tests/mc2_pairs.py --variants      # also _f/_g/_r
"""
import argparse
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
ROOT = os.path.join(os.path.dirname(DATA), "AWE32Emu")
EXE = os.path.join(ROOT, "bin", "x64", "Release", "AWE32Emu.exe")
ROM = os.path.join(DATA, "rom", "awe32.raw")
GM = os.path.join(DATA, "cdrom", "2", "WIN95", "DRIVERS", "SYNTHGM.SBK")
BULLFROG = os.path.join(DATA, "sbk", "BULLFROG.SBK")
CONF = os.path.join(DATA, "conf", "mc2.conf")
XMI = os.path.join(DATA, "midi")
OGG = os.path.join(DATA, "ogg")
OUT = os.path.join(DATA, "tests", "out", "mc2pairs")


def render(mid, wav):
    if os.path.exists(wav):
        return True
    cmd = [EXE, mid, "--rom", ROM, "--sf", GM, "--sf", BULLFROG,
           "--driver", "dos", "--conf", CONF, "--wav", wav]
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return r.returncode == 0 and os.path.exists(wav)


def align(ours, ref):
    """Tries both features and returns the better result.

    The onset (note starts) is enough for most pieces; legato music has few
    onsets and only chroma (which tones sound) locks.
    """
    best = (0.0, 0, 0.0, "-")
    for rys in ("onset", "chroma"):
        r = subprocess.run(
            [sys.executable, os.path.join(HERE, "align2.py"), ours, ref,
             "--feature", rys],
            capture_output=True, text=True, errors="replace")
        hruby = re.search(
            r"rough estimate: scale [\d.]+\s+offset \S+ s\s+match ([\d.]+)", r.stdout)
        kotvy = re.search(r"after the monotonicity check (\d+)", r.stdout)
        shoda = re.search(r"mean anchor match ([\d.]+)", r.stdout)
        cand = (float(hruby.group(1)) if hruby else 0.0,
                int(kotvy.group(1)) if kotvy else 0,
                float(shoda.group(1)) if shoda else 0.0,
                rys)
        if cand[2] > best[2]:
            best = cand
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="only pieces whose name contains this")
    ap.add_argument("--variants", action="store_true",
                    help="try the _f/_g/_r variants too, not only _w")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    oggs = sorted(glob.glob(os.path.join(OGG, "*.ogg")))
    if args.only:
        oggs = [o for o in oggs if args.only in os.path.basename(o)]

    print("%-24s %-22s %7s %6s %7s %-7s" %
          ("recording", "midi", "rough", "anchors", "match", "feature"), flush=True)
    for o in oggs:
        base = os.path.basename(o)[:-4]           # without .ogg
        cislo = base.split("_")[0]
        # `_danger` is a variant of the same recording, the MIDI is shared
        vars_ = ["w"] + (["f", "g", "r"] if args.variants else [])
        for v in vars_:
            pat = os.path.join(XMI, "%s_*_%s.xmi" % (cislo, v))
            mids = sorted(glob.glob(pat))
            if not mids:
                continue
            mid = mids[0]
            wav = os.path.join(OUT, os.path.basename(mid)[:-4] + ".wav")
            if not render(mid, wav):
                print("%-24s %-22s render failed" % (base, os.path.basename(mid)))
                continue
            hruby, kotev, shoda, rys = align(wav, o)
            znak = ""
            if shoda >= 0.45:
                znak = "  <== match"
            elif shoda > 0:
                znak = "  (weak)"
            print("%-24s %-22s %7.3f %6d %7.3f %-7s%s"
                  % (base, os.path.basename(mid), hruby, kotev, shoda, rys,
                     znak), flush=True)

    print()
    print("An anchor match below ~0.4 means it is not the same arrangement.")
    print("The rough estimate is only a first guess; the anchor match decides.")


if __name__ == "__main__":
    main()

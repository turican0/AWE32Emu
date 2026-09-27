#!/usr/bin/env python
"""A workshop for tuning the EMU8000 against recordings of real hardware.

One piece is not enough to decide - the differences at stake are tenths of a
dB, and on a single track they could be pulled into noise. So this script
measures **over all verified pairs at once** and decides only by the mean.

The pairs are from `SAMPLES3/tracks/` (a recording of a real 486 with a Sound
Blaster AWE32 CT2760, Windows 3.11, Creative MIDI - see
`SAMPLES3/tracklist.txt`), the AWE32 demo CD, Magic Carpet 2 and Hi-Octane.
Only those where the name and the measurement make clear it is the same piece
are taken; disputed cases of `pairs.txt` are not included.

    python tests/tune.py --warp        # once: align
    python tests/tune.py               # measure the default state
    python tests/tune.py --try "--filter-top 12000"
    python tests/tune.py --try "--interp linear" --try ""
    python tests/tune.py --only dance --by sample --detail

The score comes from `note_probe.py`: what remains of the per-note spectral
differences after subtracting the distortion of the whole (the equalisation
of the recording path). **Lower is better.** Neither the absolute level nor
the colour of the transfer path enters it - it measures only whether the
notes differ among themselves the same way as in the recording.
"""
import argparse
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
MIDI = os.path.join(DATA, "SAMPLES", "MIDI")
TRACKS = os.path.join(DATA, "SAMPLES3", "tracks")
S2 = os.path.join(DATA, "SAMPLES2")
XMI = os.path.join(DATA, "midi")
OGG = os.path.join(DATA, "ogg")
BULLFROG = os.path.join(DATA, "sbk", "BULLFROG.SBK")
# Hi-Octane has its own, much larger user bank (450 kB against 12 samples
# for MC2) - extracted from HIOCTANE.ISO, see split_musicdat.py.
HOBANK = os.path.join(DATA, "cdrom", "hioctane", "files", "SOUND",
                      "BULLFROG.SBK")
HOXMI = os.path.join(DATA, "midi", "hioctane")
HOREC = os.path.join(DATA, "SAMPLES3", "new")
S4 = os.path.join(DATA, "SAMPLES4")
MC2CONF = os.path.join(DATA, "conf", "mc2.conf")
OUT = os.path.join(DATA, "tests", "out", "tune")

# The recordings are older than Win95, so the conversion tables are those
# of the `dos` family (16-bit SBAWE32.DRV; see 86box_comparison.md section 8).
DRV = "dos"


def pair(name, mid, ref, banks=None, drv=None, extra=()):
    """One pair: what to play and what to compare against."""
    return dict(name=name, mid=mid, ref=ref, banks=banks or [GM],
                drv=drv or DRV, extra=list(extra))


PAIRS = [
    # --- recording of a real 486 (YouTube, lossy) ------------------------
    pair("dance", os.path.join(MIDI, "DEMO", "DANCE.MID"),
         os.path.join(TRACKS, "01_DANCE.wav")),
    pair("pop", os.path.join(MIDI, "DEMO", "POP.MID"),
         os.path.join(TRACKS, "03_POP.wav")),
    pair("starman", os.path.join(MIDI, "DEMO", "STARMAN.MID"),
         os.path.join(TRACKS, "05_STARMAN.wav")),
    pair("styles1", os.path.join(MIDI, "DEMO", "STYLES1.MID"),
         os.path.join(TRACKS, "06_STYLES1.wav")),
    pair("styles2", os.path.join(MIDI, "DEMO", "STYLES2.MID"),
         os.path.join(TRACKS, "07_STYLES2.wav")),
    pair("symphony", os.path.join(MIDI, "DEMO", "SYMPHONY.MID"),
         os.path.join(TRACKS, "08_SYMPHONY.wav")),
    pair("canon", os.path.join(MIDI, "CLASSIC", "GMCANON.MID"),
         os.path.join(TRACKS, "09_CANON.wav")),
    pair("nutcrack", os.path.join(MIDI, "CLASSIC", "GMNUTRAC.MID"),
         os.path.join(TRACKS, "10_GMANUTCR.wav")),
    pair("violin", os.path.join(MIDI, "CLASSIC", "GMVIOLIN.MID"),
         os.path.join(TRACKS, "14_GMVIOLIN.wav")),
    pair("mozart", os.path.join(MIDI, "CLASSIC", "GMMOZART.MID"),
         os.path.join(TRACKS, "02_GMAMOZAR.wav")),

    # `pairs.txt` listed these three as unproven, but `tracklist.txt` has a
    # candidate for them. They are verified by the curve alignment, which is
    # stricter than the original marker method - when the anchor match does
    # not work out, they do not belong in the measurement.
    pair("rag", os.path.join(MIDI, "PIANOIMP", "BBDRAG.MID"),
         os.path.join(TRACKS, "11_GMARAG.wav")),
    pair("concer", os.path.join(MIDI, "CLASSIC", "GMCONCER.MID"),
         os.path.join(TRACKS, "12_GMCONCER.wav")),
    pair("mviol", os.path.join(MIDI, "CLASSIC", "GMMVIOL.MID"),
         os.path.join(TRACKS, "13_GMMVIOL.wav")),

    # --- AWE32 demo CD, **lossless** FLAC ------------------------------
    # More valuable than YouTube: no codec touched them. The same MIDI files
    # already match 32/32 at the register level against SBAWE.VXD.
    pair("georgia", os.path.join(S2, "GEORG_BK.MID"),
         os.path.join(S2, "5 - Georgia On My Mind.flac")),
    pair("jump", os.path.join(S2, "JUMP_BK.MID"),
         os.path.join(S2, "6 - Jump.flac")),
    pair("relax", os.path.join(S2, "RELAX_BK.MID"),
         os.path.join(S2, "3 - Relax.flac")),
    pair("crazy", os.path.join(S2, "CRAZY_BK.MID"),
         os.path.join(S2, "4 - Crazy.flac")),
    pair("mars", os.path.join(S2, "MARS_BK.MID"),
         os.path.join(S2, "2 - Vocal intro-Mars.flac")),

    # --- Magic Carpet 2 ---------------------------------------------------
    # The game sends its own settings of all channels before the first note;
    # without them the render starts elsewhere - hence `--conf`. BULLFROG is
    # layered over GM.
    pair("mc2-intro", os.path.join(XMI, "004_C2INTRO_w.xmi"),
         os.path.join(OGG, "004_C2INTRO.ogg"),
         banks=[GM, BULLFROG], extra=["--conf", MC2CONF]),
    pair("mc2-menu", os.path.join(XMI, "003_C2SETUP_w.xmi"),
         os.path.join(OGG, "003_C2SETUP.ogg"),
         banks=[GM, BULLFROG],
         extra=["--conf", MC2CONF, "--master-volume", "127"]),

    # In-game music. The alignment works (0.47 / 0.46 / 0.71), but careful when
    # measuring colour: in the game the last two or three channels are muted,
    # so our render plays notes that are not in the recording. It does not
    # matter for the onset alignment, but it does for `note_probe` - false
    # deviations will come from the channels the game muted.
    pair("mc2-game1", os.path.join(XMI, "000_C2GAME1_w.xmi"),
         os.path.join(OGG, "000_C2GAME1.ogg"),
         banks=[GM, BULLFROG], extra=["--conf", MC2CONF]),
    pair("mc2-game2", os.path.join(XMI, "001_C2GAME2_w.xmi"),
         os.path.join(OGG, "001_C2GAME2.ogg"),
         banks=[GM, BULLFROG], extra=["--conf", MC2CONF]),
    pair("mc2-game3", os.path.join(XMI, "002_C2GAME3_w.xmi"),
         os.path.join(OGG, "002_C2GAME3.ogg"),
         banks=[GM, BULLFROG], extra=["--conf", MC2CONF]),

    # --- Hi-Octane --------------------------------------------------------
    # The recordings lay in SAMPLES3 as unproven, because there was no MIDI.
    # It can be extracted from the game's ISO (`iso_list.py` +
    # `split_musicdat.py`), so the pairs can be found - `ho_pairs.py` found
    # them with an anchor match of 0.70-0.81, above most pairs accepted so far.
    #
    # Each recording holds **two** pieces in a row (TR1_2 etc.), so only part
    # of it aligns; `note_probe.py` skips notes outside the anchor range.
    pair("ho-tr1", os.path.join(HOXMI, "ho_10.xmi"),
         os.path.join(HOREC, "HO_TR1_2.mp3"), banks=[GM, HOBANK]),
    pair("ho-tr3", os.path.join(HOXMI, "ho_11.xmi"),
         os.path.join(HOREC, "HO_TR3_4.mp3"), banks=[GM, HOBANK]),
    pair("ho-tr5", os.path.join(HOXMI, "ho_12.xmi"),
         os.path.join(HOREC, "HO_TR5_6.mp3"), banks=[GM, HOBANK]),

    # --- three recordings of the same DANCE.MID from one card (CT3980) --------
    # Verified by `pair_finder.py`: an anchor match of 0.95-0.97 with a margin
    # of 2.4-2.5x over the second candidate and a time conversion of
    # **1.000000 * audio + 0.35 s**, i.e. without tempo drift. No other
    # material has such a margin.
    #
    # Why it is worth having all three: they differ only in the recording
    # method and the driver version, so the difference between them measures
    # what is still noise of the recording path and what is a chip property.
    #   -hw   the card's analogue output
    #   -bp   digital capture ("bit perfect"), correct drivers
    #   -bpx  the same digital capture with drivers the author called broken;
    #         kept as a control sample, not as a target
    pair("dance-hw", os.path.join(MIDI, "DEMO", "DANCE.MID"),
         os.path.join(S4, "Sound Blaster AWE32 - Dance (CT3980 Real Hardware"
                          " Recording) - Infomaniac95.mp3")),
    pair("dance-bp", os.path.join(MIDI, "DEMO", "DANCE.MID"),
         os.path.join(S4, "Sound Blaster AWE32 - Dance (CT3980 Bit Perfect"
                          " Recording) (PROPER DRIVERS) - Infomaniac95.mp3")),
    pair("dance-bpx", os.path.join(MIDI, "DEMO", "DANCE.MID"),
         os.path.join(S4, "Sound Blaster AWE32 - Dance (CT3980 Bit Perfect"
                          " Recording) (LATEST BROKEN DRIVERS)"
                          " - Infomaniac95.mp3")),
]


def render(p, base, extra=(), chip="nas", notes=False):
    cmd = [EXE, p["mid"], "--rom", ROM, "--driver", p["drv"],
           "--chip", chip, "--wav", base + ".wav"]
    for b in p["banks"]:
        cmd += ["--sf", b]
    if notes:
        cmd += ["--dump-notes", base + ".csv", "--trace", base + ".trace"]
    cmd += p["extra"] + list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return r.returncode == 0


def score(base, ref, extra_args=()):
    cmd = [sys.executable, os.path.join(HERE, "note_probe.py"),
           "--warp", base + "_warp.json", "--trace", base + ".trace",
           "--notes", base + ".csv", "--ours", base + ".wav", "--ref", ref]
    cmd += list(extra_args)
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    m = re.match(r"\s*([\d.]+)(?:\s+colour\s+([\d.]+))?", r.stdout)
    if not m:
        return None
    # Returns the pair (score, colour). `colour` is the weighted spread of the
    # overall curve, i.e. what the score deliberately does not measure - see
    # note_probe.py.
    return (float(m.group(1)),
            float(m.group(2)) if m.group(2) else float("nan"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warp", action="store_true",
                    help="render the default state and compute the alignment (once)")
    ap.add_argument("--try", dest="variants", action="append", default=[],
                    help="a parameter variant; can be repeated")
    ap.add_argument("--only", action="append", default=[],
                    help="only pairs whose name contains this; "
                         "can be repeated (one match is enough)")
    ap.add_argument("--exact", action="store_true",
                    help="take --only as a whole name, not a part")
    ap.add_argument("--chip", default="nas")
    ap.add_argument("--diverge", action="store_true",
                    help="how far our core is from snd_emu8k.c (the goal is 0)")
    ap.add_argument("--detail", action="store_true",
                    help="print a breakdown by groups instead of the score")
    ap.add_argument("--by", default="sample")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    def chce(name):
        if not args.only:
            return True
        if args.exact:
            return name in args.only
        return any(o in name for o in args.only)

    pairs = [p for p in PAIRS if chce(p["name"])]
    if args.only and not pairs:
        print("no pair matches --only", args.only)
        return

    if args.warp:
        for p in pairs:
            name = p["name"]
            refp = p["ref"]
            base = os.path.join(OUT, name)
            if not os.path.exists(p["mid"]) or not os.path.exists(refp):
                print("%-10s input MISSING" % name)
                continue
            render(p, base, notes=True)
            best = None
            # The onset (note starts) is the default and the best on most pieces.
            # On legato music, though, there are so few onsets that it does not
            # lock - there chroma helps, which follows **which tones sound**.
            # Both are tried and the better is kept.
            for rys in ("onset", "chroma"):
                cmd = [sys.executable, os.path.join(HERE, "align2.py"),
                       base + ".wav", refp, "--feature", rys,
                       "--json", base + "_warp_%s.json" % rys]
                r = subprocess.run(cmd, capture_output=True, text=True,
                                   errors="replace")
                sc = re.search(r"mean anchor match ([\d.]+)", r.stdout)
                n = re.search(r"after the monotonicity check (\d+)", r.stdout)
                ujeti = re.search(r"drift of (\d+) ms", r.stdout)
                if not sc:
                    continue
                cand = (float(sc.group(1)), int(n.group(1)) if n else 0,
                        ujeti.group(1) if ujeti else "?", rys)
                if best is None or cand[0] > best[0]:
                    best = cand
            if best is None:
                print("%-10s did not align" % name, flush=True)
                continue
            import shutil
            shutil.copyfile(base + "_warp_%s.json" % best[3],
                            base + "_warp.json")
            print("%-10s anchors %-4d match %-6.3f drift %-6s ms  (%s)"
                  % (name, best[1], best[0], best[2], best[3]), flush=True)
        print()
        print("The anchor match also checks the pair: below ~0.4 it is most likely")
        print("not the same piece and such a pair does not belong in the measurement.")
        return

    if args.diverge:
        # Both cores are driven by **the same** layer and write the same registers,
        # so they are compared directly, without alignment. This measures how
        # far `--chip ours` and `--chip 86box` are apart. It is no measure of
        # correctness - the standard for the chip is the recordings, not
        # 86Box.
        ident = os.path.join(OUT, "id_warp.json")
        if not os.path.exists(ident):
            import json
            json.dump(dict(anchors=[[0.0, 0.0], [10000.0, 10000.0]]),
                      open(ident, "w", encoding="utf-8"))
        print("%-11s %9s  %s" % ("pair", "difference", "where most"), flush=True)
        vals = []
        for p in pairs:
            base = os.path.join(OUT, p["name"])
            if not os.path.exists(base + ".trace"):
                continue
            for chip, suffix in (("nas", "_dn"), ("86box", "_d8")):
                render(p, base + suffix, chip=chip)
            r = subprocess.run(
                [sys.executable, os.path.join(HERE, "note_probe.py"),
                 "--warp", ident, "--trace", base + ".trace",
                 "--notes", base + ".csv", "--ours", base + "_dn.wav",
                 "--ref", base + "_d8.wav"],
                capture_output=True, text=True, errors="replace")
            # The WEIGHTED spread, not the bare "spread of the curve" - the latter can
            # explode on an almost silent band (found on canon/mozart: a 24 dB
            # "difference" at 7680 Hz was 99 % an artefact of the scale, see
            # emu8000_tuning.md). "where most" is therefore also picked by the
            # band energy, not by the bare dB.
            m = re.search(r"WEIGHTED spread ([\d.]+) dB", r.stdout)
            band = re.findall(
                r"^\s+(\d+) Hz\s+([+-][\d.]+) dB\s+\(energy\s+([\d.]+) %\)",
                r.stdout, re.M)
            worst = (max(band, key=lambda b: abs(float(b[1])) * float(b[2]))
                     if band else None)
            if m:
                vals.append(float(m.group(1)))
                print("%-11s %6.1f dB  %s" %
                      (p["name"], float(m.group(1)),
                       ("%s Hz %s dB (energy %s %%)" % worst) if worst else ""),
                      flush=True)
        if vals:
            print()
            print("mean spread %.1f dB over %d pairs (goal 0)"
                  % (sum(vals) / len(vals), len(vals)))
        return

    if args.detail:
        for p in pairs:
            base = os.path.join(OUT, p["name"])
            print("=" * 60)
            print(p["name"])
            subprocess.run(
                [sys.executable, os.path.join(HERE, "note_probe.py"),
                 "--warp", base + "_warp.json", "--trace", base + ".trace",
                 "--notes", base + ".csv", "--ours", base + ".wav",
                 "--ref", p["ref"], "--by", args.by])
        return

    variants = args.variants or [""]
    print("%-24s %s" % ("variant",
                        " ".join("%-8s" % p["name"][:8] for p in pairs)),
          flush=True)
    for v in variants:
        extra = v.split()
        vals = []
        barvy = []
        for p in pairs:
            base = os.path.join(OUT, p["name"])
            if not os.path.exists(base + ".trace"):
                vals.append(None)
                barvy.append(None)
                continue
            # It renders into a side file, so the default render the alignment
            # stands on is not overwritten.
            tmp = base + "_v"
            render(p, tmp, extra, chip=args.chip)
            cmd = [sys.executable, os.path.join(HERE, "note_probe.py"),
                   "--warp", base + "_warp.json", "--trace", base + ".trace",
                   "--notes", base + ".csv", "--ours", tmp + ".wav",
                   "--ref", p["ref"], "--score"]
            r = subprocess.run(cmd, capture_output=True, text=True,
                               errors="replace")
            m = re.match(r"\s*([\d.]+)(?:\s+colour\s+([\d.]+))?", r.stdout)
            if m:
                vals.append(float(m.group(1)))
                barvy.append(float(m.group(2)) if m.group(2) else None)
            else:
                vals.append(None)
                barvy.append(None)
        ok = [x for x in vals if x is not None]
        okb = [x for x in barvy if x is not None]
        # The second mean is **colour**: the weighted spread of the overall curve,
        # i.e. what the score deliberately does not measure. For an idea of how
        # far one can get: two recordings of the same card have a colour of
        # 0.7 dB and a score of 2.4.
        print("%-24s %s  mean %s%s"
              % (v or "(default)",
                 " ".join("%-8s" % ("%.3f" % x if x is not None else "-")
                          for x in vals),
                 "%.4f" % (sum(ok) / len(ok)) if ok else "-",
                 "  colour %.4f" % (sum(okb) / len(okb)) if okb else ""),
              flush=True)


if __name__ == "__main__":
    main()

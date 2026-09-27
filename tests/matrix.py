#!/usr/bin/env python
"""Test matrix: our render against traces of the real drivers.

Every case says what to play and what to compare against. The result is the
number of registers matching 100 % and the list of those that do not.
Everything is stored in `matrix_results.json`, so runs can be compared.

    python tests/matrix.py                # run everything
    python tests/matrix.py --only mc2     # only some
    python tests/matrix.py --save         # promote the last run to the
                                          # baseline (does not measure)
    python tests/matrix.py --list         # only list the cases

Adding a case = one line in CASES. Columns:
    name, input (MIDI/XMI), banks, driver family, configuration,
    driver trace, frame window (or None = the whole trace)
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
ROOT = os.path.join(os.path.dirname(DATA), "AWE32Emu")
EXE = os.path.join(ROOT, "bin", "x64", "Release", "AWE32Emu.exe")
ROM = os.path.join(DATA, "rom", "awe32.raw")
OUT = os.path.join(DATA, "tests", "out", "matrix")
RESULTS = os.path.join(HERE, "matrix_results.json")
# Every run is written here; `--save` then only promotes the last run to
# the baseline instead of measuring everything again (a full pass takes ~13
# minutes).
LAST = os.path.join(HERE, "matrix_last.json")

GM = os.path.join(DATA, "cdrom", "2", "WIN95", "DRIVERS", "SYNTHGM.SBK")
BULLFROG = os.path.join(DATA, "sbk", "BULLFROG.SBK")
MDI = os.path.join(DATA, "cdrom", "mc2iso", "SOUND", "SBAWE32.MDI")
MC2CONF = os.path.join(DATA, "conf", "mc2.conf")
MIDI = os.path.join(DATA, "midi")
S2 = os.path.join(DATA, "SAMPLES2")
TOUT = os.path.join(DATA, "tests", "out")


def case(name, src, banks, drv, trace, conf=None, frames=None, extra=()):
    return dict(name=name, src=src, banks=banks, drv=drv, conf=conf,
                trace=trace, frames=frames, extra=list(extra))


CASES = [
    # --- family win95, Media Player -------------------------------------
    case("georgia", os.path.join(S2, "GEORG_BK.MID"), [GM], "win95",
         os.path.join(TOUT, "g_win95_c.trace")),
    case("jump", os.path.join(S2, "JUMP_BK.MID"), [GM], "win95",
         os.path.join(TOUT, "jump_win95.trace")),
    case("relax", os.path.join(S2, "RELAX_BK.MID"), [GM], "win95",
         os.path.join(TOUT, "relax_win95.trace")),
    # The only piece with channel pressure (1264 messages on eight channels) -
    # it is in the tests because of it; handler 0xC0FFCEAE has nothing else to
    # verify it. The bank is RELAX.SBK as for relax-vx (it replaced SYNTHGM in
    # the guest), so ROCK's programs are not in it and all notes play preset 0.
    case("rock", os.path.join(DATA, "SAMPLES", "MIDI", "GENERIC", "ROCK.MID"),
         [os.path.join(S2, "RELAX.SBK")], "win95",
         os.path.join(TOUT, "rock_win95.trace")),
    case("minuet", os.path.join(DATA, "SAMPLES", "MIDI", "BACH", "MINUET.MID"),
         [GM], "win95", os.path.join(TOUT, "win95.trace")),

    # --- family dos, the game Magic Carpet 2 -------------------------------
    # The intro ends at note 260 in the game; after that the trace holds the
    # menu music.
    case("mc2-intro", os.path.join(MIDI, "004_C2INTRO_w.xmi"), [GM, BULLFROG],
         "dos", os.path.join(TOUT, "dos_mdi.trace"), conf=MC2CONF,
         frames="0:5990000"),
    # The same run continues after a twelve-second gap with the menu music.
    # Careful: after ~65 s of inactivity the game **starts the intro again**
    # (recognised by the signature F400 F400 F400 B959), so the window ends
    # before that. The master volume **is not fixed**: the intro plays at 100,
    # the menu at 127. Measured - with each value the corresponding part
    # matches 24/24.
    case("mc2-menu", os.path.join(MIDI, "003_C2SETUP_w.xmi"), [GM, BULLFROG],
         "dos", os.path.join(TOUT, "mc2_full.trace"), conf=MC2CONF,
         frames="6800000:9690000", extra=["--master-volume", "127"]),
    # The same two runs with the GM bank the game really uses: the tables
    # compiled into SBAWE32.MDI instead of SYNTHGM.SBK (the game loads only
    # BULLFROG.SBK).
    case("mc2-intro-mdi", os.path.join(MIDI, "004_C2INTRO_w.xmi"), [MDI, BULLFROG],
         "dos", os.path.join(TOUT, "dos_mdi.trace"), conf=MC2CONF,
         frames="0:5990000"),
    case("mc2-menu-mdi", os.path.join(MIDI, "003_C2SETUP_w.xmi"), [MDI, BULLFROG],
         "dos", os.path.join(TOUT, "mc2_full.trace"), conf=MC2CONF,
         frames="6800000:9690000", extra=["--master-volume", "127"]),

    # --- prepared, waiting for a VM trace --------------------------------
    # The matrix skips them until the trace exists; once it does, they are
    # measured automatically. All are pieces of the same demo CD as Georgia
    # and JUMP.
    case("crazy", os.path.join(S2, "CRAZY_BK.MID"), [GM], "win95",
         os.path.join(TOUT, "crazy_win95.trace")),
    case("mars", os.path.join(S2, "MARS_BK.MID"), [GM], "win95",
         os.path.join(TOUT, "mars_win95.trace")),
    # RELAX.SBK (6.7 MB of vocals) **replaced** SYNTHGM.SBK in the guest, so
    # the driver has only it - our render therefore has only it too, not both.
    # The card must have  onboard_ram = 8192  in 86box-win95.cfg  in the
    # section [Sound Blaster AWE32 PnP]; in [Sound] it is ignored.
    case("relax-vx", os.path.join(S2, "RELAX_VX.MID"),
         [os.path.join(S2, "RELAX.SBK")], "win95",
         os.path.join(TOUT, "relaxvx_win95.trace")),

    # Bank swap in the guest: instead of SYNTHGM.SBK, SYNTH02S.SBK (542 kB, 38
    # presets, own samples) was put into WINDOWS/SYSTEM. It tests another bank
    # **through the real driver**, including the DRAM upload.
    case("bank-synth02s", os.path.join(DATA, "SAMPLES", "MIDI", "BACH", "MINUET.MID"),
         [os.path.join(DATA, "sbk", "SFONT1", "SYNTH02S.SBK")], "win95",
         os.path.join(TOUT, "bank_synth02s.trace")),

    # --- family sdk ----------------------------------------------------
    # DOSMid with the /awe switch has no driver of its own: it calls the
    # Creative AWE32 DOS SDK (RAWE32L.LIB), i.e. the `sdk` family. The GM
    # presets are built into the SDK (embed.c, in the format of the
    # SBAWE32.MDI tables), extracted into sbk/AWESDK_GM.MDI. The trace holds
    # five pieces in a row; the window picks the first (TEST.MID = Georgia,
    # 3331 notes) - trace_split.py finds the boundaries.
    case("dosmid-georgia", os.path.join(S2, "GEORG_BK.MID"),
         [os.path.join(DATA, "sbk", "AWESDK_GM.MDI")], "sdk",
         os.path.join(TOUT, "dosmid_awe.trace"), frames="1526205:8063958"),
]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True,
                          errors="replace").stdout


def measure(c):
    os.makedirs(OUT, exist_ok=True)
    tr = os.path.join(OUT, c["name"] + ".trace")
    cmd = [EXE, c["src"], "--rom", ROM, "--wav", os.devnull,
           "--trace", tr, "--driver", c["drv"],
           "--dump-notes", os.path.join(OUT, c["name"] + ".csv")]
    for b in c["banks"]:
        cmd += ["--sf", b]
    if c["conf"]:
        cmd += ["--conf", c["conf"]]
    cmd += c.get("extra", [])
    run(cmd)

    cmp_cmd = [sys.executable, os.path.join(HERE, "notes_diff.py"),
               tr, c["trace"]]
    if c["frames"]:
        cmp_cmd += ["--dframes", c["frames"]]
    txt = run(cmp_cmd)

    ok = len(re.findall(r"^OK ", txt, re.M))
    bad = {}
    for line in txt.splitlines():
        m = re.match(r"^   (\w+\^?)\s+(\d+)/(\d+)", line)
        if m:
            bad[m.group(1)] = "%s/%s" % (m.group(2), m.group(3))
    notes = re.search(r"driver:\s+(\d+) note-on", txt)
    return dict(ok=ok, total=ok + len(bad), bad=bad,
                notes=int(notes.group(1)) if notes else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="only cases whose name contains this")
    ap.add_argument("--save", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for c in CASES:
            print("%-14s %-40s %s" % (c["name"], os.path.basename(c["src"]),
                                      os.path.basename(c["trace"])))
        return

    base = {}
    if os.path.exists(RESULTS):
        base = json.load(open(RESULTS, encoding="utf-8"))

    # `--save` measures nothing - it promotes the result of the last run to
    # the baseline. Measuring again just to save is a waste of time; a full
    # pass takes about 13 minutes.
    if args.save:
        if not os.path.exists(LAST):
            print("nothing to save - run the matrix without --save first")
            sys.exit(1)
        last = json.load(open(LAST, encoding="utf-8"))
        base.update(last)
        json.dump(base, open(RESULTS, "w", encoding="utf-8"), indent=1)
        print("baseline updated from the last run (%d cases)"
              % len(last))
        return

    cur = {}
    print("%-14s %-8s %-7s %s" % ("case", "registry", "not", "what does not match"))
    worse = 0
    for c in CASES:
        if args.only and args.only not in c["name"]:
            continue
        if not (os.path.exists(c["src"]) and os.path.exists(c["trace"])):
            print("%-14s input or trace missing" % c["name"])
            continue
        r = measure(c)
        cur[c["name"]] = r
        b = base.get(c["name"])
        mark = ""
        if b:
            if r["ok"] < b["ok"]:
                mark = "  ERROR (was %d)" % b["ok"]
                worse += 1
            elif r["ok"] > b["ok"]:
                mark = "  improvement (was %d)" % b["ok"]
        print("%-14s %-8s %-7d %s%s" % (
            c["name"], "%d/%d" % (r["ok"], r["total"]), r["notes"],
            ", ".join("%s %s" % kv for kv in sorted(r["bad"].items())) or "-",
            mark))

    json.dump(cur, open(LAST, "w", encoding="utf-8"), indent=1)

    if worse:
        print()
        print("REGRESSION: %d cases" % worse)
        sys.exit(1)


if __name__ == "__main__":
    main()

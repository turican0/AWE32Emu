#!/usr/bin/env python
"""Runs a large set of MIDI files through the render and looks for where it crashes or goes wrong.

There is nothing to compare against - matching the driver is `matrix.py`.
This is a stress test of the parser and the synthesis: every MIDI is played
and it is watched whether the render finishes, plays something and no
nonsensical register value comes up.

    python tests/render_sweep.py
    python tests/render_sweep.py --limit 40 --driver dos
"""
import argparse
import glob
import datetime
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
ROOT = os.path.join(os.path.dirname(DATA), "AWE32Emu")
EXE = os.path.join(ROOT, "bin", "x64", "Release", "AWE32Emu.exe")
ROM = os.path.join(DATA, "rom", "awe32.raw")
GM = os.path.join(DATA, "cdrom", "2", "WIN95", "DRIVERS", "SYNTHGM.SBK")
OUT = os.path.join(DATA, "tests", "out", "sweep")
# The list of files that already passed once without a problem.
DONE = os.path.join(HERE, "sweep_done.json")
# The live state of the run - written after every file, so one can look
# during the run how far it is and what has gone wrong.
STATUS = os.path.join(HERE, "sweep_status.json")

sys.path.insert(0, HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0, help="how many files (0 = all)")
    ap.add_argument("--driver", default="win95")
    ap.add_argument("--bank", default=GM)
    # A pass takes about an hour, so finished files are written aside and
    # skipped with --resume. After an interruption (or a partial run) it
    # continues instead of repeating.
    ap.add_argument("--resume", action="store_true",
                    help="skip files that already passed in an earlier run")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    files = sorted(glob.glob(os.path.join(DATA, "SAMPLES", "MIDI", "**", "*.mid"),
                             recursive=True) +
                   glob.glob(os.path.join(DATA, "SAMPLES", "MIDI", "**", "*.MID"),
                             recursive=True))
    files = sorted(set(files))
    total_all = len(files)

    done = set()
    if os.path.exists(DONE):
        done = set(json.load(open(DONE, encoding="utf-8")))
    if args.resume:
        skipped = len([f for f in files if f in done])
        files = [f for f in files if f not in done]
        print("skipped %d already tested" % skipped, flush=True)
    if args.limit:
        files = files[:args.limit]

    from notes_diff import real_notes            # noqa: E402

    # Progress output, so one can tell during the run how far it is - a full
    # pass takes about an hour and could not be followed before.
    print("%d files in total" % len(files), flush=True)
    print("%-34s %-8s %-8s %s" % ("file", "not", "voices", "note"),
          flush=True)
    bad = 0
    empty = 0
    broken = 0
    problems = []
    started = datetime.datetime.now()

    def save_status(k, cur, finished=False):
        st = dict(zacatek=started.isoformat(timespec="seconds"),
                  aktualizovano=datetime.datetime.now().isoformat(
                      timespec="seconds"),
                  hotovo=k, celkem_beh=len(files), celkem_kolekce=total_all,
                  aktualni=os.path.basename(cur) if cur else "",
                  problemovych=bad, prazdnych=empty, poskozenych=broken,
                  problemy=problems, dobehlo=finished)
        json.dump(st, open(STATUS, "w", encoding="utf-8"),
                  indent=1, ensure_ascii=False)

    save_status(0, "")
    for k, f in enumerate(files, 1):
        if k % 10 == 0 or k == len(files):
            print("... %d/%d" % (k, len(files)), flush=True)
        tr = os.path.join(OUT, "s.trace")
        r = subprocess.run(
            [EXE, f, "--rom", ROM, "--sf", args.bank, "--wav", os.devnull,
             "--trace", tr, "--driver", args.driver],
            capture_output=True, text=True, errors="replace", timeout=600)
        out = r.stdout + r.stderr
        note = ""
        n = 0
        voices = 0
        if r.returncode != 0:
            # A non-zero return code does not yet mean a crash. When the render says
            # "Error loading", the input is damaged and the behaviour is right -
            # such files are only listed, not counted as a problem. Found on
            # `title2.mid` of WarCraft 2, whose track header says 11004 B while
            # only 8170 remain in the file (it is truncated).
            if "Error loading" in out:
                note = "damaged input (rightly rejected)"
                broken += 1
            else:
                note = "CRASH (code %d)" % r.returncode
                bad += 1
        else:
            try:
                notes = real_notes(tr)
                n = len(notes)
                voices = len(set(v for _f, v, _r in notes))
                if n == 0:
                    note = "played nothing"
                    empty += 1
                elif voices > 32:
                    note = "SUSPICIOUS: voices %d" % voices
                    bad += 1
            except Exception as e:
                note = "cannot read the trace: %s" % e
                bad += 1
        if note:
            print("%-34s %-8d %-8d %s" % (os.path.basename(f)[:34], n,
                                          voices, note), flush=True)
        else:
            # Only cleanly finished files; damaged inputs are not stored, so the next
            # run verifies again that they are rejected.
            done.add(f)
            json.dump(sorted(done), open(DONE, "w", encoding="utf-8"), indent=1)
        if note:
            problems.append("%s: %s" % (os.path.basename(f), note))
        save_status(k, f)

    save_status(len(files), "", finished=True)
    print()
    print("files %d, problematic %d, empty %d, damaged %d"
          % (len(files), bad, empty, broken))
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main()

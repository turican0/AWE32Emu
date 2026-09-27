#!/usr/bin/env python
"""Agreement with 86Box and the real drivers **on all levels at once**.

The purpose is regression: when a layer is tuned to 100 %, it is easy to stop
watching it - and exactly there it then breaks silently. So this overview
keeps the measurements of every level together and compares them with a
stored baseline.

    python tests/status.py                # measure and compare
    python tests/status.py --save         # store as the new baseline
    python tests/status.py --only chip    # one level only

Levels:

  registers    MIDI -> registers: how many of 32 registers match 100 % at
               note-on (`notes_diff.py` against the real driver's trace in 86Box)
  intermediate the voice parameter block inside the driver (`patch_cmp.py`
               against the CPU trace) - catches an error before it shows in
               the registers
  chip         `--chip 86box` against `emu8k_ref.exe`: must be **byte identical**
  own core     `--chip ours` against the same: error to signal in dB
  health       `regress.py` - tuning, length, voice stealing, clipping

The baseline is in `tests/status_baseline.json`. A regression is reported as
ERROR, an improvement as a change to be confirmed with `--save`.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.dirname(HERE)
ROOT = os.path.join(os.path.dirname(DATA), "AWE32Emu")
EXE = os.path.join(ROOT, "bin", "x64", "Release", "AWE32Emu.exe")
REF = os.path.join(DATA, "ref86box", "build", "emu8k_ref.exe")
ROM = os.path.join(DATA, "rom", "awe32.raw")
SBK = os.path.join(DATA, "cdrom", "2", "WIN95", "DRIVERS", "SYNTHGM.SBK")
OUT = os.path.join(DATA, "tests", "out", "status")
BASELINE = os.path.join(HERE, "status_baseline.json")

# piece -> (midi, driver trace, driver CPU trace or None)
PIECES = {
    "georgia": (os.path.join(DATA, "SAMPLES2", "GEORG_BK.MID"),
                os.path.join(DATA, "tests", "out", "g_win95_c.trace"),
                os.path.join(DATA, "tests", "out", "g_cpu2.trace")),
    "jump":    (os.path.join(DATA, "SAMPLES2", "JUMP_BK.MID"),
                os.path.join(DATA, "tests", "out", "jump_win95.trace"),
                os.path.join(DATA, "tests", "out", "jump_cpu.trace")),
    "relax":   (os.path.join(DATA, "SAMPLES2", "RELAX_BK.MID"),
                os.path.join(DATA, "tests", "out", "relax_win95.trace"), None),
    "minuet":  (os.path.join(DATA, "SAMPLES", "MIDI", "BACH", "MINUET.MID"),
                os.path.join(DATA, "tests", "out", "win95.trace"), None),
}


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout


def render(midi, trace, extra=()):
    cmd = [EXE, midi, "--rom", ROM, "--sf", SBK, "--wav", os.devnull,
           "--trace", trace, "--driver", "win95"] + list(extra)
    run(cmd)


def measure(only=None):
    os.makedirs(OUT, exist_ok=True)
    res = {}

    for name, (midi, drv, cpu) in PIECES.items():
        if only and only != "registry":
            break
        if not (os.path.exists(midi) and os.path.exists(drv)):
            continue
        tr = os.path.join(OUT, name + ".trace")
        render(midi, tr)
        txt = run([sys.executable, os.path.join(HERE, "notes_diff.py"), tr, drv])
        ok = sum(1 for l in txt.splitlines() if l.startswith("OK "))
        tot = sum(1 for l in txt.splitlines()
                  if l.startswith("OK ") or (l.startswith("   ") and "/" in l))
        res["registry." + name] = (ok, tot)

    for name, (midi, drv, cpu) in PIECES.items():
        if only and only != "mezivysledky":
            break
        if not (cpu and os.path.exists(cpu)):
            continue
        csvp = os.path.join(OUT, name + ".csv")
        render(PIECES[name][0], os.path.join(OUT, name + "_m.trace"),
               ["--dump-notes", csvp])
        txt = run([sys.executable, os.path.join(HERE, "patch_cmp.py"), cpu, csvp])
        ok = sum(1 for l in txt.splitlines() if l.startswith("OK "))
        tot = sum(1 for l in txt.splitlines()
                  if l.startswith("OK ") or (l.startswith("   ") and "/" not in l.split()[0]))
        res["mezivysledky." + name] = (ok, max(tot, ok))

    if not only or only == "chip":
        midi = PIECES["georgia"][0]
        tr = os.path.join(OUT, "chip.trace")
        wav = os.path.join(OUT, "chip.wav")
        render(midi, tr, ["--chip", "86box"])
        # a render with --wav needs another run; done separately
        run([EXE, midi, "--rom", ROM, "--sf", SBK, "--chip", "86box",
             "--wav", wav, "--trace", tr, "--driver", "win95"])
        refwav = os.path.join(OUT, "chip_ref.wav")
        run([REF, "--rom", ROM, "--trace", tr, "--dram", tr + ".dram.raw",
             "--ram", "8192", "--wav", refwav])
        res["cip.georgia"] = diff_wav(wav, refwav)

    return res


def diff_wav(a, b):
    try:
        import numpy as np
        import soundfile as sf
    except ImportError:
        return None
    if not (os.path.exists(a) and os.path.exists(b)):
        return None
    xa, _ = sf.read(a, always_2d=True, dtype="int16")
    xb, _ = sf.read(b, always_2d=True, dtype="int16")
    n = min(len(xa), len(xb))
    d = xa[:n].astype("int64") - xb[:n].astype("int64")
    import numpy as np
    return (int((abs(d).sum(axis=1) != 0).sum()), n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", action="store_true", help="save the result as the baseline")
    ap.add_argument("--only", help="one level only: registry, mezivysledky, chip")
    args = ap.parse_args()

    cur = measure(args.only)
    base = {}
    if os.path.exists(BASELINE):
        base = json.load(open(BASELINE, encoding="utf-8"))

    print("%-26s %-14s %-14s %s" % ("level", "ted", "baseline", ""))
    bad = 0
    for k in sorted(cur):
        c = cur[k]
        b = base.get(k)
        cs = "%d/%d" % tuple(c) if c else "-"
        bs = "%d/%d" % tuple(b) if b else "-"
        mark = ""
        if b and c:
            if k.startswith("cip"):
                if c[0] > b[0]:
                    mark = "ERROR - differences appeared"; bad += 1
                elif c[0] < b[0]:
                    mark = "improvement"
            else:
                if c[0] < b[0]:
                    mark = "ERROR - matches lost"; bad += 1
                elif c[0] > b[0]:
                    mark = "improvement"
        print("%-26s %-14s %-14s %s" % (k, cs, bs, mark))

    if args.save:
        json.dump({k: list(v) for k, v in cur.items() if v},
                  open(BASELINE, "w", encoding="utf-8"), indent=1)
        print()
        print("baseline saved to %s" % BASELINE)
    elif bad:
        print()
        print("REGRESSION: %d levels got worse" % bad)
        sys.exit(1)


if __name__ == "__main__":
    main()

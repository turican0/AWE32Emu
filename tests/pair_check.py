"""Finds which variant in midi/ matches the recording in ogg/.

Renders all variants (_f/_g/_r/_w) through AWE32Emu.exe and compares the
length and structure with the reference .ogg. Run from the project root:

    python tests/pair_check.py
"""
import os, re, subprocess, sys, wave, struct, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'bin', 'x64', 'Release', 'AWE32Emu.exe')
OUT = os.path.join(ROOT, 'tests', 'out')
os.makedirs(OUT, exist_ok=True)

import soundfile as sf


def ogg_durations():
    d = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'ogg', '*.ogg'))):
        info = sf.info(p)
        d[os.path.splitext(os.path.basename(p))[0]] = info.duration
    return d


def render(path, out_wav):
    r = subprocess.run([EXE, path, '--wav', out_wav],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stdout.strip() + r.stderr.strip()
    ev = re.search(r'\((\d+) events', r.stdout)
    with wave.open(out_wav) as w:
        dur = w.getnframes() / w.getframerate()
    # subtract the tail main.cpp appends after the last event
    return (dur - 1.5, int(ev.group(1)) if ev else -1), None


def main():
    oggs = ogg_durations()
    print("%-14s %8s | %-6s %8s %8s %7s" %
          ('piece', 'ogg [s]', 'var', 'mid [s]', 'xmi [s]', 'events'))
    print('-' * 66)
    for name, odur in oggs.items():
        rows = []
        for var in ('f', 'g', 'r', 'w'):
            cells = {}
            for ext in ('mid', 'xmi'):
                src = os.path.join(ROOT, 'midi', '%s_%s.%s' % (name, var, ext))
                if not os.path.exists(src):
                    cells[ext] = None
                    continue
                res, err = render(src, os.path.join(OUT, '%s_%s.%s.wav' % (name, var, ext)))
                cells[ext] = res
            rows.append((var, cells))
        first = True
        for var, cells in rows:
            m = cells.get('mid')
            x = cells.get('xmi')
            print("%-14s %8.1f | %-6s %8s %8s %7s" % (
                name if first else '', odur if first else 0, var,
                '%.1f' % m[0] if m else '-',
                '%.1f' % x[0] if x else '-',
                x[1] if x else (m[1] if m else '-')))
            first = False
        print()


if __name__ == '__main__':
    main()

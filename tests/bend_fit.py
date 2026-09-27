"""Measured triples (bend, range) -> the offset the driver used.

Our IP and our offset are now taken directly from the note dump, so the base
pitch is `our_IP - our_offset` and the driver's offset
`driver_IP - base pitch`.
"""
import csv
import sys

sys.path.insert(0, 'C:/prenos/AWE32EmuData/tests')
from notes_diff import real_notes                    # noqa: E402

game = real_notes(r'C:\prenos\AWE32EmuData\tests\out\relax_win95.trace')
ours = real_notes(r'C:\prenos\AWE32EmuData\tests\out\relax_a.trace')
rows = list(csv.DictReader(open(r'C:\prenos\AWE32EmuData\tests\out\relax_a.csv',
                                newline='')))
n = min(len(game), len(ours), len(rows))
print('not %d' % n)

trojice = {}
bad = 0
for i in range(n):
    a = game[i][2].get('IP')
    b = ours[i][2].get('IP')
    if a is None or b is None:
        continue
    r = rows[i]
    bend = int(r['bend'])
    rng = int(r['bendRange'])
    off = int(r['bendOffset'])
    drv = a - (b - off)
    if a != b:
        bad += 1
    trojice.setdefault((bend, rng), set()).add((off, drv))

print('differ %d' % bad)
print()
print('%-8s %-6s %-9s %-9s %-8s %s' %
      ('bend', 'range', 'our offset', 'drv offset', 'difference', 'exact b*r*341/8192'))
shown = 0
for (bend, rng), pairs in sorted(trojice.items()):
    for off, drv in sorted(pairs):
        if off == drv:
            continue
        print('%-8d %-6d %-9d %-9d %-8d %.4f' %
              (bend, rng, off, drv, drv - off, bend * rng * 341 / 8192.0))
        shown += 1
        if shown >= 25:
            break
    if shown >= 25:
        break
if shown == 0:
    print('  no difference in the bend offset - the error is elsewhere')

"""Measured pairs for the envelope delay conversion: our value vs the driver.

The ENVVAL/ENVVOL register carries `0x8000 - steps`, so the steps are computed
from both sides and the ms -> steps conversion the driver really uses is
searched for.
"""
import csv
import os
import sys
from collections import Counter

sys.path.insert(0, 'C:/prenos/AWE32EmuData/tests')
from patch_cmp import driver_notes            # noqa: E402
from patch_struct import word                 # noqa: E402

CPU = sys.argv[1]
OURS = sys.argv[2]

ours = list(csv.DictReader(open(OURS, newline='')))
pairs = Counter()
for i, buf in enumerate(driver_notes(CPU)):
    if i >= len(ours):
        break
    for name, off in (('envvalDelay', 0x32), ('envvolDelay', 0x42)):
        d = word(buf, off)
        o = int(ours[i][name], 16) if not ours[i][name].startswith('0x') else int(ours[i][name], 16)
        if d is None:
            continue
        pairs[(name, o, d)] += 1

print('%-13s %-8s %-8s %-8s %-8s %-7s %s' % (
    'pole', 'ours', 'driver', 'our st.', 'drv kr.', 'count', 'difference'))
for (name, o, d), n in sorted(pairs.items(), key=lambda kv: (-kv[1])):
    so, sd = 0x8000 - o, 0x8000 - d
    mark = '' if o == d else ('  <-- differs by %+d' % (sd - so))
    print('%-13s %04X     %04X     %-8d %-8d %-7d%s' % (name, o, d, so, sd, n, mark))

"""An exact transcription of the branch C119C116 and a back-search of the input.

Measured from driver traces: delayVolEnv 20 ms -> 27 steps, delayModEnv
140 ms -> 193 steps and 440 ms -> 606 steps. The routine takes **timecents**,
so someone converts the SF1 milliseconds to timecents before it. Here it is
found out which timecents they would have to be. (Later: SF1 does not go
through this routine at all - the step is a linear 725 us, see
docs/re-notes/driver_note_on.md.)
"""
import math


def branch(tc):
    """C119C116: timecents -> ENVVAL/ENVVOL register value."""
    if tc <= -12000:
        return 0x8000
    if tc >= 5484:
        return 0
    eax = (tc + 0x30E4) << 16
    q = int(eax / 1200)                 # idiv truncates towards zero
    frac = q & 0xFFFF
    edi = frac + 0x10000
    intpart = q >> 16                   # sar, q is positive here
    edi >>= (16 - intpart)
    res = 0x8000 - edi
    return res if res >= 0 else 0


MER = [(20, 27), (140, 193), (440, 606)]

print('%-8s %-8s %-24s %s' % ('ms', 'steps', 'timecents that give it', 'exact 1200*log2(ms/1000)'))
for ms, steps in MER:
    want = 0x8000 - steps
    hits = [tc for tc in range(-11999, 5484) if branch(tc) == want]
    exact = 1200.0 * math.log2(ms / 1000.0)
    rng = '%d..%d' % (hits[0], hits[-1]) if hits else 'none'
    print('%-8d %-8d %-24s %.1f   difference %s' % (
        ms, steps, rng, exact,
        ('%+.0f..%+.0f' % (hits[0] - exact, hits[-1] - exact)) if hits else '-'))

print()
print('check: what the branch gives for exactly rounded timecents')
for ms, steps in MER:
    for r, nm in ((math.floor, 'down'), (round, 'math.'), (int, 'to zero')):
        tc = int(r(1200.0 * math.log2(ms / 1000.0)))
        print('  ms %-5d %-7s tc %-7d -> %-5d steps (driver %d)' % (
            ms, nm, tc, 0x8000 - branch(tc), steps))

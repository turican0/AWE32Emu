"""Does the note timing match between our render and the game?

When the spacings match, pairing by order is right and the pitch differences
are real. When not, the pairing is wrong and the match percentages mean
nothing.
"""
import sys

sys.path.insert(0, 'C:/prenos/AWE32EmuData/tests')
from notes_diff import real_notes                    # noqa: E402

game = real_notes('C:/prenos/AWE32EmuData/tests/out/dos_mdi.trace')
ours = real_notes('C:/prenos/AWE32EmuData/tests/out/mc2_2b.trace')

g0, o0 = game[0][0], ours[0][0]
print('game %d notes (first at %.2f s), ours %d notes (first at %.2f s)'
      % (len(game), g0 / 44100.0, len(ours), o0 / 44100.0))
print()
print('%-5s %-12s %-12s %-10s' % ('i', 'game [s]', 'ours [s]', 'difference [ms]'))
for i in list(range(0, 14)) + [20, 30, 50, 80, 120, 180, 240, 260]:
    if i >= min(len(game), len(ours)):
        break
    gt = (game[i][0] - g0) / 44100.0
    ot = (ours[i][0] - o0) / 44100.0
    print('%-5d %-12.3f %-12.3f %+.0f' % (i, gt, ot, (ot - gt) * 1000))

# where the timing diverges by more than 100 ms
n = min(len(game), len(ours))
first = None
for i in range(n):
    gt = (game[i][0] - g0) / 44100.0
    ot = (ours[i][0] - o0) / 44100.0
    if abs(ot - gt) > 0.100:
        first = i
        break
print()
print('first note with a deviation over 100 ms: %s' % first)
if first:
    print('  game %.3f s, ours %.3f s' % ((game[first][0] - g0) / 44100.0,
                                       (ours[first][0] - o0) / 44100.0))

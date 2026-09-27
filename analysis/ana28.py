# -*- coding: utf-8 -*-
"""Analysis of AWETST28 blocks 43-46: card (line out) against the render.
Without AWE28_CARD it runs on the pseudo-card (align28.py) - every card /
render difference must then come out ~0 and every value above the noise.

    python ana28.py [headroom slide ladder interp filter reverb chorus]
"""
import os
import sys

import numpy as np

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'align28.py'), encoding='utf-8').read().split("if __name__ == '__main__':")[0])

WANT = [a for a in sys.argv[1:] if not a.startswith('--')] or \
    ['headroom', 'slide', 'ladder', 'interp', 'filter', 'reverb', 'chorus']
RL = nofx[:, 0]                  # render dry left
RR = mix[:, 1] - nofx[:, 1]      # render returns right
ML = mix[:, 0]                   # render full left (clipping included)
CL, CR = card[:, 0], card[:, 1]
BI = {c: i for i, c in enumerate(BANDS)}


def seg(x, t, a, b):
    return x[int((t + a) * SR):int((t + b) * SR)]


def h3(x, f0):
    n = len(x)
    X = np.abs(np.fft.rfft(x * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1.0 / SR)

    def pk(fr):
        i = int(round(fr * n / SR))
        return X[max(i - 3, 0):i + 4].max()
    return 20 * np.log10(pk(3 * f0) / pk(f0))


def env_db(x, f0, hop):
    """Band-limited amplitude envelope (FFT Hilbert around f0), dB, hop in s."""
    n = len(x)
    X = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1.0 / SR)
    X[(f < f0 / 1.5) | (f > f0 * 1.5)] = 0
    a = np.abs(np.fft.ifft(X)) * 2
    h = max(1, int(hop * SR))
    m = len(a) // h
    return 20 * np.log10(a[:m * h].reshape(m, h).mean(axis=1) + 1e-12)


def row(name, vals, fmt='%+6.1f'):
    return '  %-8s ' % name + ' '.join(fmt % v for v in vals)


# ---------------------------------------------------------------- 43 headroom
if 'headroom' in WANT:
    print('\n== 43 headroom: level of the left output against 1 voice of the same pass, 3rd harmonic')
    groups = {}
    for e, tc, tr in events(43, 'headroom:'):
        nv = int(e.text.split()[1])
        pas = 'mixer -12' if 'mixer' in e.text else ('atten 48' if 'atten 48' in e.text else 'default')
        c, r = hp(seg(CL, tc, 0.3, 1.3)), hp(seg(ML, tr, 0.3, 1.3))
        groups.setdefault(pas, []).append((nv, db(c), db(r), h3(c, 170.4), h3(r, 170.4)))
    for pas, g in groups.items():
        c1, r1 = g[0][1], g[0][2]
        print(' pass %s   (ideal = 20 log N)' % pas)
        for nv, c, r, hc, hr in g:
            print('   N %d ideal %+5.1f | card %+5.1f  render %+5.1f | 3rd harm card %5.1f render %5.1f dB'
                  % (nv, 20 * np.log10(nv), c - c1, r - r1, hc, hr))
    cal = events(43, 'calibration')
    if cal:
        e, tc, tr = cal[0]
        pkc = np.abs(seg(CL, tc, 0.2, 1.8)).max()
        print(' calibration: card peak %.3f of full scale (%s)' % (pkc, 'CLIPS - recording level too high!' if pkc > 0.97 else 'ok'))

# ---------------------------------------------------------------- 43 slide
if 'slide' in WANT:
    print('\n== 43 volume slide: IFATN steps during a held 678 Hz sine (envelope, 0.5 ms steps from the step)')
    acc = {}
    for e, tc, tr in events(43, 'slide: held'):
        for name, x, t in (('card', CL, tc), ('render', RL, tr)):
            ed = env_db(seg(x, t, 0.0, 2.9), 678.5, 0.0005)
            for j, (ts, lab) in enumerate(((0.7, '-22.5 dB'), (1.4, '+22.5 dB'), (2.1, '-13.5 dB'))):
                i0 = int(ts / 0.0005)
                w = ed[i0 - 200:i0 + 200]
                pre = np.median(w[20:120])
                post = np.median(w[-80:])
                # first frame a fifth of the way to the new level
                k = 100 + int(np.argmax(np.abs(w[100:] - pre) > 0.2 * abs(post - pre)))
                acc.setdefault((lab, name), []).append(w[k - 2:k + 24] - pre)
    for (lab, name), v in sorted(acc.items()):
        print(row('%s %s' % (lab, name), np.mean(v, axis=0)[::2], '%5.0f'))
    print(' (columns 1 ms apart from 1 ms before the step)')
    ris = {}
    for e, tc, tr in events(43, 'slide: note-on'):
        for name, x, t in (('card', CL, tc), ('render', RL, tr)):
            ed = env_db(seg(x, t, -0.05, 0.10), 2713.0, 0.0001)
            top = np.median(ed[900:1300])
            k = 400 + int(np.argmax(ed[400:] > top - 30))
            ris.setdefault(name, []).append(ed[k - 2:k + 40] - top)
    for name, v in ris.items():
        print(row('rise %s' % name, np.mean(v, axis=0)[::3], '%5.0f'))
    print(' (note-on rise, columns 0.3 ms apart from the -30 dB point)')

# ---------------------------------------------------------------- 43 ladder
if 'ladder' in WANT:
    print('\n== 43 chorus ladder (preset 6, no LFO): R echo vs the dry tick, and held tones')
    tl = {}
    for e, tc, tr in events(43, 'chorus ladder: feedback'):
        fb = e.text.split('0x')[1][:2]
        s = int(e.text.split()[-1])
        for name, l, r, t in (('card', CL, CR, tc), ('render', RL, RR, tr)):
            d = db(seg(l, t, 0.0, 0.03))
            ech = [db(seg(r, t, 0.024 + 0.064 * n, 0.064 + 0.064 * n)) - d for n in range(8 if fb == 'C0' else 1)]
            tl.setdefault((fb, s, name), []).append(ech)
    for (fb, s, name), v in sorted(tl.items()):
        print(row('fb %s s%3d %s' % (fb, s, name), np.mean(v, axis=0)))
    ht = {}
    for e, tc, tr in events(43, 'chorus ladder: preset'):
        p = int(e.text.split()[3].rstrip(','))
        att = int(e.text.split('atten ')[1].split(',')[0])
        s = int(e.text.split()[-1])
        for name, l, r, t in (('card', CL, CR, tc), ('render', RL, RR, tr)):
            ht[(p, att, s, name)] = (db(hp(seg(l, t, 0.5, 2.8))), db(hp(seg(r, t, 0.5, 2.8))), h3(seg(r, t, 0.5, 2.8), 678.5))
    for p in (1, 4):
        for name in ('card', 'render'):
            dry = ht[(p, 12, 0, name)][0]
            print('  p%d %-6s ' % (p, name) + '  '.join('atten %d: return %+5.1f dB, 3rd %5.1f' % (a, ht[(p, a, 255, name)][1] - dry + (a - 12) * 0.375, ht[(p, a, 255, name)][2])
                                                     for a in (12, 24, 36)))
    print(' (return against the dry tone, corrected by the atten step 0.375 dB: linear = same number)')

# ---------------------------------------------------------------- 44 interpolation
if 'interp' in WANT:
    print('\n== 44 interpolation: noise, card - render per band, minus the IP 0 difference')
    cols = [i for i, c in enumerate(BANDS) if c >= 900]
    res = {}
    for e, tc, tr in events(44, 'interpolation: noise'):
        off = int(e.text.split()[-1])
        res[off] = bands(seg(CL, tc, 0.2, 1.4)) - bands(seg(RL, tr, 0.2, 1.4))
    base = res[0]
    print('  bands  ' + ' '.join('%6d' % BANDS[i] for i in cols))
    for off in sorted(res):
        print(row('IP %+d' % off, (res[off] - base)[cols]))
    print(' sine: strongest spur against the fundamental')
    for e, tc, tr in events(44, 'interpolation: sine'):
        out = []
        for x, t in ((CL, tc), (RL, tr)):
            s = seg(x, t, 0.2, 1.1)
            X = np.abs(np.fft.rfft(s * np.hanning(len(s))))
            i1 = int(np.argmax(X))
            m = X.copy()
            m[max(i1 - 6, 0):i1 + 7] = 0
            m[:int(150 * len(s) / SR)] = 0
            out.append(20 * np.log10(m.max() / X[i1]))
        print('  %-38s card %6.1f render %6.1f dB' % (e.text, out[0], out[1]))

# ---------------------------------------------------------------- 44 filter
if 'filter' in WANT:
    print('\n== 44 filter on noise: H = band - anchor, card - render (dB)')
    fe = events(44, 'filter:')
    ancs = {}
    for x in fe:
        if 'anchor' in x[0].text:
            a = int(x[0].text.split('atten ')[1])
            ancs[a] = (bands(seg(CL, x[1], 0.2, 1.4)), bands(seg(RL, x[2], 0.2, 1.4)))
    cols = [BI[c] for c in BANDS if c in (BANDS[4], BANDS[7], BANDS[10], BANDS[13], BANDS[16], BANDS[19], BANDS[21])]
    print('  bands              ' + ' '.join('%6d' % BANDS[i] for i in cols))
    for e, tc, tr in fe:
        if 'anchor' in e.text:
            continue
        ca, ra = ancs[int(e.text.split('atten ')[1])]
        hc = bands(seg(CL, tc, 0.2, 1.4)) - ca
        hr = bands(seg(RL, tr, 0.2, 1.4)) - ra
        lab = 'c%s q%s' % (e.text.split('cutoff ')[1].split(',')[0], e.text.split('Q ')[1].split(',')[0])
        print('  %-9s card    ' % lab + ' '.join('%+6.1f' % v for v in hc[cols]))
        print('  %-9s diff    ' % '' + ' '.join('%+6.1f' % v for v in (hc - hr)[cols]))

# ---------------------------------------------------------------- 45 reverb
if 'reverb' in WANT:
    print('\n== 45 reverb with noise: return (R) against the dry burst (L, 20-190 ms)')
    cols = [BI[c] for c in (BANDS[4], BANDS[7], BANDS[10], BANDS[13], BANDS[16], BANDS[19])]
    print('  bands                    ' + ' '.join('%6d' % BANDS[i] for i in cols))
    for e, tc, tr in events(45, 'reverb preset'):
        if 'burst' not in e.text:
            continue
        for name, l, r, t in (('card', CL, CR, tc), ('render', ML, RR, tr)):
            d = bands(seg(l, t, 0.02, 0.19))
            w1 = bands(seg(r, t, 0.55, 0.90)) - d
            w2 = bands(seg(r, t, 0.90, 1.70)) - d
            if name == 'card':
                c1, c2 = w1, w2
            else:
                print('  %-24s 550-900 ms ' % e.text[:24] + ' '.join('%+6.1f' % v for v in c1[cols]) + '   diff ' + ' '.join('%+5.1f' % v for v in (c1 - w1)[cols]))
                print('  %-24s 900-1700   ' % '' + ' '.join('%+6.1f' % v for v in c2[cols]) + '   diff ' + ' '.join('%+5.1f' % v for v in (c2 - w2)[cols]))
    print(' click: return envelope, 2 ms, 0-60 ms (card / render)')
    for e, tc, tr in events(45, 'reverb preset'):
        if 'click' not in e.text:
            continue
        out = []
        for l, r, t in ((CL, CR, tc), (ML, RR, tr)):
            d = db(seg(l, t, 0.0, 0.01))
            s = seg(r, t, 0.0, 0.06)
            h = int(0.002 * SR)
            out.append(10 * np.log10((s[:30 * h] ** 2).reshape(30, h).mean(axis=1) + 1e-20) - d)
        print('  %-40s ' % e.text[:40] + ' '.join('%4.0f' % v for v in out[0][::2]))
        print('  %-40s ' % '' + ' '.join('%4.0f' % v for v in out[1][::2]))
    eq = {}
    for e, tc, tr in events(45, 'reverb EQ'):
        tre = int(e.text.split('treble ')[1].split(',')[0])
        for name, l, r, t in (('card', CL, CR, tc), ('render', ML, RR, tr)):
            eq[(tre, name)] = (bands(seg(r, t, 0.55, 1.4)), bands(seg(l, t, 0.02, 0.19)))
    if eq:
        print(' EQ treble 11 - 5: return and dry burst (card / render)')
        for name in ('card', 'render'):
            print(row('ret ' + name, (eq[(11, name)][0] - eq[(5, name)][0])[cols]))
            print(row('dry ' + name, (eq[(11, name)][1] - eq[(5, name)][1])[cols]))

# ---------------------------------------------------------------- 46 chorus
if 'chorus' in WANT:
    print('\n== 46 chorus with noise: return (R) spectrum against the dry noise of block 44 (IP 0)')
    dn = [x for x in events(44, 'interpolation: noise') if x[0].text.endswith(' 0')][0]
    dc, dr = bands(seg(CL, dn[1], 0.2, 1.4)), bands(seg(RL, dn[2], 0.2, 1.4))
    cols = [BI[c] for c in (BANDS[4], BANDS[7], BANDS[10], BANDS[13], BANDS[16], BANDS[19])]
    print('  bands                    ' + ' '.join('%6d' % BANDS[i] for i in cols))
    for e, tc, tr in events(46, 'chorus preset'):
        if 'noise' not in e.text:
            continue
        c = bands(seg(CR, tc, 0.5, 2.8)) - dc
        r = bands(seg(RR, tr, 0.5, 2.8)) - dr
        print('  %-24s card ' % e.text[:24] + ' '.join('%+6.1f' % v for v in c[cols]) + '   diff ' + ' '.join('%+5.1f' % v for v in (c - r)[cols]))
    for e, tc, tr in events(46, 'chorus preset'):
        if 'held' not in e.text:
            continue
        L = 11.0 if '12 s' in e.text else 7.0
        out = []
        for r, t in ((CR, tc), (RR, tr)):
            s = hp(seg(r, t, 0.5, 0.5 + L))
            h = int(0.02 * SR)
            f = 10 * np.log10((s[:len(s) // h * h] ** 2).reshape(-1, h).mean(axis=1) + 1e-20)
            ac = np.correlate(f - f.mean(), f - f.mean(), 'full')[len(f) - 1:]
            lag = int(np.argmax(ac[25:]) + 25) * 0.02
            out.append((np.percentile(f, 97) - np.percentile(f, 3), lag))
        print('  %-44s modulation card %4.1f dB (period %.1f s), render %4.1f dB (%.1f s)'
              % (e.text[:44], out[0][0], out[0][1], out[1][0], out[1][1]))
    for e, tc, tr in events(46, 'chorus to reverb'):
        out = []
        for l, r, t in ((CL, CR, tc), (ML, RR, tr)):
            d = db(seg(l, t, 0.01, 0.09))
            out.append((db(seg(r, t, 0.07, 0.30)) - d, db(seg(r, t, 0.55, 1.6)) - d))
        print('  %-48s echo %+5.1f / tail 550-1600 ms %+6.1f | render %+5.1f / %+6.1f'
              % (e.text, out[0][0], out[0][1], out[1][0], out[1][1]))
    eq = {}
    for e, tc, tr in events(46, 'chorus EQ'):
        tre = int(e.text.split('treble ')[1].split(',')[0])
        for name, r, t in (('card', CR, tc), ('render', RR, tr)):
            eq[(tre, name)] = bands(seg(r, t, 0.3, 1.9))
    if eq:
        print(' EQ treble 11 - 5 on the chorus return (card / render)')
        for name in ('card', 'render'):
            print(row(name, (eq[(11, name)] - eq[(5, name)])[cols]))

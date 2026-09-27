# -*- coding: utf-8 -*-
"""Filter check with sine probes, corrected for the internal capture tilt.

A sine at a fixed frequency is not affected by the capture path's tilt in
the way noise is - only by the path gain at that single frequency. That gain
is taken from block 4 (unfiltered sine sweep, card - render per semitone)
and subtracted. What remains for blocks 28 and 36 is the filter itself.

    python sines25.py                          # replay_cham.wav vs replay_nas.wav
"""
import sys

import numpy as np
import soundfile as sf

import replay25 as R

RENDERS = [('cham', sys.argv[1] if len(sys.argv) > 1 else 'replay_q2.wav'), ('tpt', sys.argv[2] if len(sys.argv) > 2 else 'replay_os.wav')]
vm, k, q = R.rt_to_frame()


def freq_of_ip(ip):
    return 44100.0 / 65.0 * 2 ** ((ip - 57344) / 4096.0)


results = {}
for label, path in RENDERS:
    ours = sf.read(R.os.path.join(R.HERE, path), dtype='float32')[0][:, 0]
    base = R.collect(vm, k, q, ours, {1, 2, 8, 9, 10, 11, 12}, 0.05, 0.35, 20.0)
    ref = float(np.median([r['lc'] - r['lo'] for r in base]))
    tilt_rows = R.collect(vm, k, q, ours, {4}, 0.08, 0.38, 15.0)
    tf, tv = [], []
    for r in tilt_rows:
        if r['text'].startswith('semitone'):
            ip = int(r['text'].split('IP')[1])
            tf.append(np.log2(freq_of_ip(ip)))
            tv.append(r['lc'] - r['lo'] - ref)
    order = np.argsort(tf)
    tf, tv = np.array(tf)[order], np.array(tv)[order]

    def tilt(f):
        return float(np.interp(np.log2(f), tf, tv))

    rows = R.collect(vm, k, q, ours, {28, 36}, 0.05, 0.35, 6.0)
    out = []
    for r in rows:
        t = r['text']
        ip = None
        if 'IP ' in t:
            ip = int(t.split('IP ')[1].split(',')[0])
        if ip is None:
            if t.startswith('env->filter') or t.startswith('LFO1->filter'):
                ip = 57344 + 4096            # 1357 Hz probe in block 28
            else:
                continue
        f = freq_of_ip(ip)
        out.append((r['blk'], t, f, r['lc'] - r['lo'] - ref - tilt(f)))
    results[label] = out

print('residual = card - render - reference - capture tilt at the probe frequency [dB]')
print('tilt from block 4: '
      + ', '.join('%.0f Hz %+.1f' % (2 ** a, b) for a, b in zip(tf[::12], tv[::12])))

cham = {(b, t): v for b, t, f, v in results['cham']}
tpt = {(b, t): v for b, t, f, v in results['tpt']}
freqs = {(b, t): f for b, t, f, v in results['cham']}

print('\n=== block 28: cutoff sweep per probe (cham / tpt)')
probes = sorted({freqs[key] for key in cham if key[1].startswith('filter sine')})
for pf in probes:
    keys = [key for key in cham if key[1].startswith('filter sine') and abs(freqs[key] - pf) < 1]
    keys.sort(key=lambda kk: int(kk[1].split('cutoff')[1]))
    print('  probe %.0f Hz:' % pf)
    print('    ' + ' '.join('%s:%+.1f/%+.1f' % (kk[1].split('cutoff ')[1], cham[kk], tpt.get(kk, np.nan))
                            for kk in keys[::2]))
    c = np.array([cham[kk] for kk in keys])
    t_ = np.array([tpt.get(kk, np.nan) for kk in keys])
    print('    rms cham %.2f dB, tpt %.2f dB' % (np.sqrt(np.nanmean(c ** 2)), np.sqrt(np.nanmean(t_ ** 2))))

for prefix in ('env->filter', 'LFO1->filter'):
    keys = [key for key in cham if key[1].startswith(prefix)]
    print('\n=== block 28: %s (1357 Hz, cutoff 128) cham / tpt' % prefix)
    print('  ' + ' '.join('%s:%+.1f/%+.1f' % (kk[1].split('depth ')[1].split(',')[0], cham[kk], tpt.get(kk, np.nan)) for kk in keys))

print('\n=== block 36: map sine 1357 Hz, cutoff x Q (cham / tpt)')
for qq in (0, 5, 10, 15):
    keys = [key for key in cham if key[1].startswith('map sine') and key[1].endswith('Q %d' % qq)]
    keys.sort(key=lambda kk: int(kk[1].split('cutoff ')[1].split(',')[0]))
    c = np.array([cham[kk] for kk in keys])
    t_ = np.array([tpt.get(kk, np.nan) for kk in keys])
    print('  Q %2d: rms cham %.2f dB, tpt %.2f dB | ' % (qq, np.sqrt(np.nanmean(c ** 2)), np.sqrt(np.nanmean(t_ ** 2)))
          + ' '.join('%s:%+.1f/%+.1f' % (kk[1].split('cutoff ')[1].split(',')[0], cham[kk], tpt.get(kk, np.nan)) for kk in keys[::3]))
print('\n=== block 36: clamps (cham / tpt)')
for key in cham:
    if key[1].startswith('clamp'):
        print('  %-58s %+.1f / %+.1f' % (key[1], cham[key], tpt.get(key, np.nan)))

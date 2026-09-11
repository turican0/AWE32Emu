# -*- coding: utf-8 -*-
"""Cteni AWETEST.LOG od v25 (razitka u not).

Radek udalosti:
    '  403300 10 decay 0x04, sustain 0x60\t@rt 1234567:40000 cap 368800:1834567'

  ms     planovany cas (g_ms) - jen orientacne
  block  cislo bloku
  text   popis
  rt     skutecny cas nastupu: tik BIOSu a faze PITu (1193182 cyklu za s,
         65536 na tik) -> sekundy = (tik * 65536 + faze) / 1193182
  cap    soubor zaznamu ("from ms" z radku FILE) a ramec v nem - PRESNY
         okamzik nastupu v nahravce, az na konstantni zpozdeni ADC

Starsi logy (bez razitka) se ctou taky, jen rt/cap jsou None.

    from awelog import parse
    log = parse('AWETEST.LOG')
    for ev in log.events:
        if ev.block == 10 and ev.file:
            t = ev.frame / 44100.0          # sekundy v ev.file
"""
import re
from collections import namedtuple

PIT_HZ = 1193182.0
RATE = 44100

Event = namedtuple('Event', 'ms block text rt cap_from frame file')
FileRec = namedtuple('FileRec', 'end_ms name nbytes from_ms quality')

_EV = re.compile(r'^\s*(\d+)\s+(\d+)\s+(.*?)(?:\t@rt (\d+):(\d+)(?: cap (\d+):(\d+))?)?\s*$')
_FILE = re.compile(r'^\s*(\d+)\s+--\s+FILE\s+(\S+?),\s*(\d+)\s*B,\s*from\s+(\d+)\s*ms')
_QUAL = re.compile(r'^\s*(\d+)\s+--\s+QUALITY\s+(.*)$')
_RT = re.compile(r'dma rt (\d+):(\d+)\.\.(\d+):(\d+)')
_FRAMES = re.compile(r'frames (\d+)')


def rt_seconds(tick, phase):
    return (int(tick) * 65536 + int(phase)) / PIT_HZ


class Log(object):
    def __init__(self):
        self.version = None
        self.header = []          # radky '# ...' bez mrizky
        self.events = []
        self.files = []

    def section(self, prefix):
        """Radky hlavicky se zadanym prefixem, napr. 'CARD', 'CHIP', 'LEVEL'."""
        return [h[len(prefix):].strip() for h in self.header if h.startswith(prefix)]

    def block(self, n, startswith=None):
        return [e for e in self.events if e.block == n
                and (startswith is None or e.text.startswith(startswith))]

    def capture_rates(self):
        """Skutecna vzorkovaci frekvence kazdeho souboru z radku QUALITY."""
        out = []
        for f in self.files:
            if not f.quality:
                continue
            m, k = _RT.search(f.quality), _FRAMES.search(f.quality)
            if not (m and k):
                continue
            dt = rt_seconds(m.group(3), m.group(4)) - rt_seconds(m.group(1), m.group(2))
            if dt > 0:
                out.append((f.name, int(k.group(1)) / dt))
        return out


def parse(path):
    log = Log()
    pending_quality = None
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\r\n')
        if line.startswith('#'):
            body = line[1:].strip()
            if body.startswith('AWETEST v'):
                log.version = body.split('v', 1)[1].strip()
            log.header.append(body)
            continue
        m = _QUAL.match(line)
        if m:
            pending_quality = m.group(2)
            continue
        m = _FILE.match(line)
        if m:
            log.files.append(FileRec(int(m.group(1)), m.group(2), int(m.group(3)),
                                     int(m.group(4)), pending_quality))
            pending_quality = None
            continue
        m = _EV.match(line)
        if m:
            rt = rt_seconds(m.group(4), m.group(5)) if m.group(4) else None
            cap_from = int(m.group(6)) if m.group(6) else None
            frame = int(m.group(7)) if m.group(7) else None
            log.events.append(Event(int(m.group(1)), int(m.group(2)), m.group(3).strip(),
                                    rt, cap_from, frame, None))
    names = {f.from_ms: f.name for f in log.files}
    log.events = [e._replace(file=names.get(e.cap_from)) if e.cap_from is not None else e
                  for e in log.events]
    return log


if __name__ == '__main__':
    import sys
    lg = parse(sys.argv[1])
    print('verze', lg.version, '- udalosti', len(lg.events), '- souboru', len(lg.files))
    for h in lg.header:
        if h.split(' ', 1)[0] in ('CARD', 'CHIP', 'LEVEL', 'STEREO', 'CAPTURE'):
            print('#', h)
    for name, hz in lg.capture_rates():
        print('%s: %.2f Hz' % (name, hz))

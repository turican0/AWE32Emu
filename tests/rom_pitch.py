"""Measures the real fundamental frequency of samples directly in the wave ROM.

It tells the "right" root note of a sample and compares it with what the
SoundFont says about it (shdr originalKey and the overridingRootKey
generator).

    python tests/rom_pitch.py
"""
import struct
import numpy as np

ROM = r'C:/prenos/AWE32Emu/rom/awe32.raw'
SF2 = r'C:/prenos/AWE32Emu/rom/1mgm.sf2'
POOL = 495          # start of the sample pool in words
SR = 44100


def load_rom():
    raw = open(ROM, 'rb').read()
    return np.frombuffer(raw, dtype='<i2')


def sf2_samples():
    d = open(SF2, 'rb').read()
    # shdr lies at a known offset; found by an honest walk
    def walk(pos, end, out):
        while pos + 8 <= end:
            cid = d[pos:pos+4].decode('latin1')
            size = struct.unpack_from('<I', d, pos+4)[0]
            data = pos + 8
            if cid == 'LIST':
                walk(data+4, min(data+size, end), out)
            else:
                out.setdefault(cid, (data, size))
            pos = data + size + (size & 1)
    c = {}
    walk(12, 8 + struct.unpack_from('<I', d, 4)[0], c)
    off, size = c['shdr']
    out = []
    for i in range(size // 46):
        o = off + i * 46
        name = d[o:o+20].split(b'\0')[0].decode('latin1').strip()
        s, e, ls, le, sr = struct.unpack_from('<5I', d, o+20)
        key = d[o+40]
        out.append((name, s, e, ls, le, sr, key))
    return out


def fundamental(x):
    """Fundamental frequency estimate by autocorrelation."""
    x = x.astype(np.float64)
    x -= x.mean()
    if len(x) < 512 or np.abs(x).max() < 1:
        return 0.0
    n = 1 << int(np.ceil(np.log2(len(x) * 2)))
    f = np.fft.rfft(x, n)
    ac = np.fft.irfft(f * np.conj(f))[:len(x) // 2]
    ac[:20] = 0                      # exclude the zero lag
    lag = int(np.argmax(ac[:4000]))
    return SR / lag if lag else 0.0


def note_name(midi):
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return "%s%d" % (names[int(round(midi)) % 12], int(round(midi)) // 12 - 1)


def main():
    rom = load_rom()
    want = ['kpianob1', 'kpianog2', 'femalevoiceg2', 'oohvoicec3', 'marimbac3',
            'squarewave', 'sinewave', 'bsawtoothwavea3', 'recorderax2', 'stringsdx4']
    print("%-18s %10s %9s %8s %8s" % ('sample', 'measured Hz', 'measured', 'shdr key', 'length'))
    for name, s, e, ls, le, sr, key in sf2_samples():
        if name not in want:
            continue
        seg = rom[POOL + ls: POOL + le]           # the loop is measured
        if len(seg) < 256:
            seg = rom[POOL + s: POOL + e]
        f = fundamental(seg)
        midi = 69 + 12 * np.log2(f / 440.0) if f > 0 else 0
        print("%-18s %10.1f %9s %8d %8d"
              % (name, f, note_name(midi) if f else '-', key, e - s))


if __name__ == '__main__':
    main()

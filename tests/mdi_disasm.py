#!/usr/bin/env python
"""Disassembler of AIL/Miles drivers (`.MDI`) - a flat 16-bit binary file.

`SBAWE32.MDI` is the AWE32 driver for AIL/Miles used by DOS games (for us
Magic Carpet 2). It is the `dos` driver family, see AWE32Emu/src/Awe32Driver.h.
The file starts with an `AIL3MDI` header, the code is then at flat offsets.

    python tests/mdi_disasm.py sbk/SBAWE32.MDI --at 0x2102 --count 80
    python tests/mdi_disasm.py sbk/SBAWE32.MDI --find-io
    python tests/mdi_disasm.py sbk/SBAWE32.MDI --find-imm 0x2710
"""
import argparse
import struct
import sys

import capstone

IO_OPCODES = {0xEE: "out dx, al", 0xEF: "out dx, ax",
              0xEC: "in al, dx", 0xED: "in ax, dx"}


def load(path):
    data = open(path, "rb").read()
    if data[:7] != b"AIL3MDI":
        print("warning: the AIL3MDI header is missing", file=sys.stderr)
    return data


def disasm(md, code, start, count):
    n = 0
    for ins in md.disasm(code[start:], start):
        print(f"  {ins.address:04X}:  {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")
        n += 1
        if n >= count:
            break


def aligned_window(md, code, target, back=24):
    """Finds the alignment at which an instruction starts exactly at `target`."""
    for b in range(2, back):
        start = target - b
        if start < 0:
            continue
        if target in (i.address for i in md.disasm(code[start:target + 1], start)):
            return start
    return target


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--at", type=lambda x: int(x, 0))
    ap.add_argument("--count", type=int, default=60)
    ap.add_argument("--find-io", action="store_true")
    ap.add_argument("--find-imm", type=lambda x: int(x, 0))
    args = ap.parse_args()

    code = load(args.path)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)

    if args.find_io:
        for pos, b in enumerate(code):
            if b not in IO_OPCODES:
                continue
            start = aligned_window(md, code, pos)
            if start == pos:
                continue
            print(f"  {pos:04X}: {IO_OPCODES[b]}")
            disasm(md, code, start, pos - start + 1)
            print()
        return 0

    if args.find_imm is not None:
        pat = struct.pack("<H", args.find_imm)
        pos = code.find(pat)
        while pos >= 0:
            print(f"--- constant {args.find_imm:#06x} at {pos:#06x}")
            disasm(md, code, aligned_window(md, code, pos), 10)
            print()
            pos = code.find(pat, pos + 1)
        return 0

    if args.at is None:
        print(f"{len(code)} bytes; give --at, --find-io or --find-imm")
        return 1

    disasm(md, code, args.at, args.count)
    return 0


if __name__ == "__main__":
    sys.exit(main())

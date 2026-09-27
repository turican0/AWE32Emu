#!/usr/bin/env python
"""Viewer of an instruction trace from 86Box, with disassembly from the VXD image.

The trace is taken by our build of 86Box (see docs/TESTING.md). Every `I`
line carries the linear EIP, the opcode and the registers; `P` lines are
accesses to the EMU8000 ports and `M` memory windows.

The address the VXD object is loaded at is given by --base. It was found by
matching the call sites of the port trace against offsets found statically -
for `SBAWE.VXD` object 1 it came out at 0xC0FF8B49.

    python tests/insn_view.py insn.trace --port-line 137614 --before 60
    python tests/insn_view.py insn.trace --find-reg eax=0x21a0
"""
import argparse
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le_disasm import LE  # noqa: E402

REGS = ["eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"]


def load_image(vxd, obj):
    le = LE(vxd)
    return le.obj_bytes(obj)


def disasm_at(md, image, off):
    if off < 0 or off >= len(image):
        return "?"
    for ins in md.disasm(image[off:off + 16], off):
        return f"{ins.mnemonic} {ins.op_str}"
    return "?"


def parse(path):
    """Returns the list of lines as (type, order, fields)."""
    out = []
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            if line.startswith(("I ", "P ")):
                out.append((line[0], n, line.split()))
    return out


def show(rows, image, md, base, start, end, highlight=None):
    for kind, lineno, p in rows[start:end]:
        if kind == "I":
            eip = int(p[1], 16)
            regs = [int(x, 16) for x in p[3:11]]
            text = disasm_at(md, image, eip - base)
            rs = " ".join(f"{n}={v:08X}" for n, v in zip(REGS, regs)
                          if highlight is None or n in highlight)
            print(f"  {eip:08X} +{eip-base:05X}  {text:34} {rs}")
        else:
            print(f"  >>> PORT {p[1]} {p[2]} = {p[3]}  (line {lineno})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--vxd", default="../AWE32EmuData/SoundBlaster AWE32/traced-drivers/SBAWE.VXD")
    ap.add_argument("--obj", type=int, default=1)
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0xC0FF8B48)
    ap.add_argument("--port-line", type=int, help="number of the P line around which to print")
    ap.add_argument("--before", type=int, default=50)
    ap.add_argument("--after", type=int, default=4)
    ap.add_argument("--regs", help="print only these registers, e.g. eax,ecx")
    args = ap.parse_args()

    image = load_image(args.vxd, args.obj)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rows = parse(args.trace)
    highlight = args.regs.split(",") if args.regs else None

    if args.port_line is None:
        print(f"{len(rows)} records; give --port-line")
        return 1

    idx = next((i for i, r in enumerate(rows) if r[1] == args.port_line), None)
    if idx is None:
        print("line not found")
        return 1
    show(rows, image, md, args.base,
         max(0, idx - args.before), idx + args.after, highlight)
    return 0


if __name__ == "__main__":
    sys.exit(main())

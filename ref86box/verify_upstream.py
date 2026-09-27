#!/usr/bin/env python
"""Verifies that the copy of the 86Box sources is still exact.

The files are in `upstream/` next to this script. `include/86box/snd_emu8k.h`
is not checked: the harness builds the patched chip, so it carries the
patched header.

Without arguments it compares only the stored SHA-256 (offline). With
--online it downloads the files from GitHub again and compares against them,
so it also shows when upstream has moved on meanwhile.

    python ref86box/verify_upstream.py
    python ref86box/verify_upstream.py --online
    python ref86box/verify_upstream.py --online --ref v5.3
"""
import argparse
import hashlib
import os
import pathlib
import sys
import urllib.request

# The copies of the 86Box sources lie next to this script.
DATA = pathlib.Path(__file__).resolve().parent

# Files taken from 86Box unchanged, with their path in its tree.
FILES = {
    "upstream/snd_emu8k.c": "src/sound/snd_emu8k.c",
    "upstream/snd_emu8k.h": "src/include/86box/snd_emu8k.h",
}

# SHA-256 of the state the harness was written against (86Box master, August 2026).
PINNED = {
    "upstream/snd_emu8k.c": "a4944d8283659f7a0389ee06815fc3fa027f4c65d9182e745ffc0268b1e1c454",
    "upstream/snd_emu8k.h": "96d6b4ea7ec7fdb431da10bb555d4f1139af4eb00c56a513c8fb33b4d640b776",
}

RAW = "https://raw.githubusercontent.com/86Box/86Box/{ref}/{path}"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--online", action="store_true",
                    help="download the files from GitHub and compare against them")
    ap.add_argument("--ref", default="master",
                    help="86Box branch or tag (default: master)")
    args = ap.parse_args()

    bad = 0
    for local, remote in FILES.items():
        p = DATA / local
        if not p.exists():
            print(f"MISSING {local}")
            bad += 1
            continue

        data = p.read_bytes()
        digest = sha256(data)

        if digest == PINNED[local]:
            print(f"OK     {local}  (matches the stored hash)")
        else:
            print(f"CHANGED {local}")
            print(f"       expected {PINNED[local]}")
            print(f"       found    {digest}")
            bad += 1

        if args.online:
            url = RAW.format(ref=args.ref, path=remote)
            try:
                with urllib.request.urlopen(url, timeout=60) as r:
                    upstream = r.read()
            except Exception as e:  # the network may not be available
                print(f"       (online check failed: {e})")
                continue
            if sha256(upstream) == digest:
                print(f"       == 86Box {args.ref}")
            else:
                print(f"       != 86Box {args.ref}  (upstream has moved on,"
                      f" hash {sha256(upstream)})")
                bad += 1

    if bad:
        print(f"\n{bad} problems")
    else:
        print("\nupstream unchanged")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

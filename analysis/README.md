# Analysis scripts of the AWETEST rounds

One-off scripts written while tuning the chip against the recordings in
`../recordings/`. Each answers one question about one round; the first lines
of every script say which. They are kept because the conclusions in
[docs/re-notes/emu8000_tuning.md](https://github.com/turican0/AWE32Emu/blob/main/docs/re-notes/emu8000_tuning.md)
refer to them, not as maintained tools.

**They will not run unchanged on another machine.** Most have absolute paths
to the development layout (`C:\prenos\AWE32Emu`, `C:\prenos\AWE32EmuData`,
renders and VM traces in the data directory) at the top.

Requirements: Python 3.10+, `numpy`, `soundfile`; `omfdis.py` needs
`capstone`. The AWETST25 scripts (`attack25`, `clock25`, `eq35`, `ext25`,
`flac25`, `fx25`, `replay25`) import `awe25` / `awelog` from
`tools/awetest` on `main` – put that directory on `PYTHONPATH`.

## By round

| round | scripts |
|---|---|
| AWETST25 (2026-09-12) | `attack25`, `attack_ext`, `clock25`, `eq35`, `ext25`, `fit_eq`, `fitfilt25c`, `flac25`, `fx25`, `modfilt`, `replay25`, `sines25` |
| AWETST26 (2026-09-23) | `chospec26`, `tail26l` |
| AWETST27 (2026-09-26) | `align27`, `amL23`, `cho23x`, `dctail42`, `echo27`, `fit22x`, `fit22z`, `intext27`, `loop42c`, `rev27`, `sat27`, `slide42` |
| AWETST28 (2026-09-27) | `align28`, `ana28`, `clickspec`, `damp45b`, `er2check`, `erdeconv`, `erl22`, `ev28`, `interp28`, `revspec45`, `stuck28`, `tick22er2` |
| effect fits | `fit_cho`, `fit_rev` |
| chip and driver checks | `chipcheck` (our chip against 86Box), `drvdiff`, `sdkcheck`, `statecmp`, `noteprobe` / `noteprobe_lib`, `loop_probe`, `attack7f` |
| Magic Carpet 2 | `segmap`, `stemcorr` |
| AWE32 SDK | `omfdis`, `omfdata` (OMF object disassembler and data dump) |

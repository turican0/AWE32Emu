#!/usr/bin/env python
"""Shows how far the stress run (`render_sweep.py`) is.

It reads only `sweep_status.json`, which the run writes after every file -
it starts nothing and does not interfere with the run, so it can be called at
any time and as often as needed.

    python tests/sweep_status.py
    python tests/sweep_status.py --watch    # repeatedly, every 15 s
"""
import argparse
import datetime
import json
import os
import time

HERE = os.path.dirname(os.path.abspath(__file__))
STATUS = os.path.join(HERE, "sweep_status.json")


def show():
    if not os.path.exists(STATUS):
        print("the run has not run yet - start render_sweep.py")
        return True

    st = json.load(open(STATUS, encoding="utf-8"))
    k = st["hotovo"]
    n = st["celkem_beh"]
    start = datetime.datetime.fromisoformat(st["zacatek"])
    upd = datetime.datetime.fromisoformat(st["aktualizovano"])
    elapsed = (upd - start).total_seconds()

    # How much of the whole collection is done, earlier runs included.
    hotovo_celkem = st["celkem_kolekce"] - n + k
    sirka = 40
    plnych = int(sirka * k / n) if n else sirka
    bar = "#" * plnych + "." * (sirka - plnych)

    print("[%s] %d/%d  (collection %d/%d)"
          % (bar, k, n, hotovo_celkem, st["celkem_kolekce"]))

    if st["dobehlo"]:
        print("DOBEHLO za %s" % druh_casu(elapsed))
    elif k:
        rate = elapsed / k
        zbyva = rate * (n - k)
        print("running %s, pace %.1f s/file, ~%s left  (%s)"
              % (druh_casu(elapsed), rate, druh_casu(zbyva),
                 st["aktualni"] or "?"))
        # When the state does not change for a long time, the render hangs or no longer runs.
        ticho = (datetime.datetime.now() - upd).total_seconds()
        if ticho > max(120, rate * 6):
            print("CAREFUL: the state has not changed for %s - is it still running?"
                  % druh_casu(ticho))
    print("problematic %d, empty %d, damaged %d"
          % (st["problemovych"], st["prazdnych"], st["poskozenych"]))
    for line in st["problemy"]:
        print("   " + line)
    return st["dobehlo"]


def druh_casu(sec):
    sec = int(sec)
    if sec < 90:
        return "%d s" % sec
    if sec < 5400:
        return "%d min" % (sec // 60)
    return "%d h %d min" % (sec // 3600, (sec % 3600) // 60)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--watch", action="store_true",
                    help="repeat until the run finishes")
    args = ap.parse_args()
    while True:
        done = show()
        if not args.watch or done:
            return
        print()
        time.sleep(15)


if __name__ == "__main__":
    main()

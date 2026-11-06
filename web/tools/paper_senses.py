#!/usr/bin/env python3
"""web/paper_senses.json: the senses each network really saw, for the paper's diagrams, from the lab's logs (WSPLOG= for a fight, WAPLOG= for a dock).
  paper_senses.py camera.log interceptor.log fight.log dock.log out.json"""
import json, sys


def fig(x): return float("%.3g" % float(x))


def fight(path, secs=30, hz=10):
    rows = [l.split() for l in open(path, errors="replace") if l.startswith("D ")]
    rows = [(int(r[1]), [fig(x) for x in r[2:25]]) for r in rows]
    step, n = 50 // hz, secs * 50
    if len(rows) > n:       # (the window with the target drawn biggest)
        best = max(range(0, len(rows) - n, 25), key=lambda i: sum(r[1][8] for r in rows[i:i + n]))
        rows = rows[best:best + n]
    return [r[1] for r in rows[::step]]


def dock(path, secs=40, hz=5):
    rows = [l.split("|")[0].split() for l in open(path, errors="replace")]
    rows = [[fig(x) for x in r[1:]] for r in rows if len(r) > 40]
    return rows[-secs * 50:][::50 // hz]


if __name__ == "__main__":
    c, i, f, d, out = sys.argv[1:6]
    json.dump(dict(camera=fight(c), interceptor=fight(i), fight=fight(f), dock=dock(d)), open(out, "w"), separators=(",", ":"))

#!/usr/bin/env python3
"""Evolve the docking network's named parameters (CMA-ES), scored in the docking lab: the seconds from the
hyperspace exit to docked, over recorded starts (run_sim DOCKSNAP=dir), each candidate on the same batch.

  es_dock.py GENS PER_GEN TRAIN_LIST VAL_LIST [TAG]

A timeout counts as the lab's window plus 60 s, a death as the window plus 120 s; the line-up's ugly manoeuvres add to
the cost (stick reversals, crawling far out, the aim swinging past 60 deg). evaluate() returns (cost, docked, n,
median seconds of the docked, median reversals, mean crawl seconds, mean swings). The parameters the senses use
for their geometry (the arc's circle, the field's heading, room0, k_lo, k_hi) are left out: the C passes its own.
"""
import json, math, os, random, re, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import cma

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from elite_wsp import dock_author as A
from elite_wsp.dock_export import arrays, save_wsp

LAB = os.path.join(os.path.dirname(os.path.dirname(HERE)), "cplayer", "lab")
WINDOW = 240

# name: (step, kind, lo, hi): "log" moves by exp(step x), "lin" by step x, clamped
SPACE = dict(
    lead=(0.25, "log"), chi=(5.0, "lin", 20, 80), k_min=(0.25, "log"), k_side=(0.25, "log"), c_side=(0.25, "log"),
    side_k0=(0.25, "log"), side_c=(0.3, "lin", 0.3, 4.0), side_w=(0.25, "log"),
    le_frac=(0.02, "lin", 0.02, 0.3), le_c=(0.25, "log"), le_w=(0.25, "log"), le_lead=(0.25, "log"), le2_w=(0.25, "log"),
    ls_frac=(0.05, "lin", 0.1, 0.8), al_final=(0.25, "log"), v_side=(3, "lin", 8, 40),
    roll_gain=(0.2, "log"), roll_ramp=(0.25, "log"), roll_max=(0.2, "log"), pitch_gain=(0.2, "log"), pitch_cone=(0.25, "log"), hyst=(0.1, "lin", 0.0, 0.8),
    dead=(0.25, "log"), off_deg=(0.25, "log"), hold_g=(0.25, "log"), hold_cap=(0.25, "log"), fade_z=(0.25, "log"),
    v_min=(2.0, "lin", 2, 30), turn_k=(0.25, "log"), turn_g=(0.25, "log"), room0=(0.25, "log"),
    f_v_near=(2, "lin", 2, 40), f_v_mid=(3, "lin", 4, 40), f_v_far=(3, "lin", 4, 40), f_r_mid=(0.25, "log"), f_r_far=(0.25, "log"),
    cen_deg=(0.25, "log"), lvl_deg=(0.25, "log"), off8=(0.25, "log"), commit_r=(0.25, "log"),
)
# the line-up's ugliness, added to the seconds: each stick reversal, each second crawling far out, each swing of the aim
W_REV, W_CRAWL, W_SWING = 0.5, 1.0, 3.0
NAMES = list(SPACE)
BASE = dict(A.PARAMS)


def decode(x, base=BASE):
    P = dict(base)
    for v, n in zip(x, NAMES):
        s = SPACE[n]
        P[n] = base[n] * math.exp(s[0] * v) if s[1] == "log" else min(s[3], max(s[2], base[n] + s[0] * v))
    return P


def weights_file(P, d):
    fd, path = tempfile.mkstemp(suffix=".wsp", dir=d); os.close(fd)
    save_wsp(arrays(A.build(P)), path, {k: P[k] for k in A.SENSE_KEYS})
    return path


def run(paths, wfile, procs=1):
    """The lab on these starts with these weights: [(path, result, seconds)]."""
    chunks = [paths[i::procs] for i in range(procs)]
    def one(chunk):
        fd, lst = tempfile.mkstemp(suffix=".lst"); os.close(fd)
        open(lst, "w").write("\n".join(chunk) + "\n")
        env = dict(os.environ, FAIR="1", LABDOCK="1", WSPAPP="1", WSPAPPW=wfile, LABWIN=str(WINDOW), LOG="3")
        p = subprocess.run([LAB, lst], env=env, capture_output=True, text=True)
        os.unlink(lst)
        ug = ugliness(p.stderr)
        rows = []
        for l in p.stdout.splitlines():
            if l.startswith("DOCKLAB"):
                t = l.split(); rows.append((t[1], t[2], (int(t[4]) - int(t[-1])) / 50, ug[len(rows)] if len(rows) < len(ug) else (0, 0, 0)))
        return rows
    with ThreadPoolExecutor(procs) as ex:
        return [r for rs in ex.map(one, chunks) for r in rs]


DK = re.compile(r"DK ([FL]) fr \d+ .*? dist (\d+) along (-?\d+) lat \d+ off ([\d.]+) .*? jx (-?\d+) jy (-?\d+) spd (\d+)")


def ugliness(err):
    """Per docking (the lab's delimiters), from the DK frame lines: stick reversals in the line-up, seconds crawling (14 or
    under) more than 1500 out, and the times the aim swings past 60 deg off."""
    out, L = [], []
    for l in err.splitlines():
        m = DK.search(l)
        if m:
            if m.group(1) == "L": L.append((int(m.group(2)), float(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7))))
        elif l.startswith("STATUS Docked") or l.startswith("timeout") or l.startswith("died"):
            rev = 0
            for k in (2, 3):
                last = 0
                for x in L:
                    sg = 1 if x[k] >= 8 else -1 if x[k] <= -8 else 0
                    if sg and last and sg != last: rev += 1
                    if sg: last = sg
            crawl = sum(1 for x in L if x[4] <= 14 and x[0] > 1500) / 50
            swing = sum(1 for i in range(1, len(L)) if L[i][1] > 60 >= L[i - 1][1])
            out.append((rev, crawl, swing)); L = []
    return out


def cost(rows):
    c = [(s if r == "docked" else WINDOW + (60 if r == "timeout" else 120)) + W_REV * u[0] + W_CRAWL * u[1] + W_SWING * u[2] for _, r, s, u in rows]
    secs = [s for _, r, s, u in rows if r == "docked"]
    return (float(np.mean(c)), sum(r == "docked" for _, r, _, _ in rows), len(rows), round(float(np.median(secs)), 1) if secs else None,
            round(float(np.median([u[0] for _, _, _, u in rows])), 1), round(float(np.mean([u[1] for _, _, _, u in rows])), 1), round(float(np.mean([u[2] for _, _, _, u in rows])), 2))


def evaluate(P, paths, d, procs=1):
    w = weights_file(P, d)
    try:
        rows = run(paths, w, procs)
    finally:
        os.unlink(w)
    return cost(rows)


def main(gens, per_gen, train_list, val_list, tag="es_app"):
    train = [l.strip() for l in open(train_list) if l.strip()]
    val = [l.strip() for l in open(val_list) if l.strip()]
    state_path = os.path.join(HERE, tag + "_state.json")
    st = json.load(open(state_path)) if os.path.exists(state_path) else dict(gen=0, x=[0.0] * len(NAMES), sigma=0.5, log=[])
    es = cma.CMAEvolutionStrategy(st["x"], st["sigma"], {"popsize": 8, "seed": 11 + st["gen"], "verbose": -9})
    tmp = tempfile.mkdtemp()
    if st["gen"] == 0 and "val0" not in st:
        st["val0"] = evaluate(BASE, val, tmp, procs=2)
        print(json.dumps(dict(as_written_val=st["val0"])), flush=True)
    for g in range(st["gen"], st["gen"] + gens):
        t = time.time()
        paths = random.Random(500 + g).sample(train, min(per_gen, len(train)))
        X = es.ask()
        with ThreadPoolExecutor(2) as ex:
            res = list(ex.map(lambda x: evaluate(decode(x), paths, tmp), X))
        es.tell(X, [r[0] for r in res])
        rec = dict(gen=g, best=min(r[0] for r in res), median=float(np.median([r[0] for r in res])), sigma=round(es.sigma, 3), secs=round(time.time() - t))
        if g % 3 == 2:
            v = evaluate(decode(es.mean), val, tmp, procs=2)
            rec.update(val=v)
            if v[0] < st.get("best_val", 1e9):
                st["best_val"] = v[0]
                json.dump(decode(es.mean), open(os.path.join(HERE, tag + "_best.json"), "w"), indent=1)
        st["log"].append(rec)
        st.update(gen=g + 1, x=list(map(float, es.mean)), sigma=float(es.sigma))
        json.dump(st, open(state_path, "w"), indent=1)
        json.dump(decode(es.mean), open(os.path.join(HERE, tag + "_params.json"), "w"), indent=1)
        print(json.dumps(rec), flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    main(int(a[0]), int(a[1]), a[2], a[3], *a[4:])

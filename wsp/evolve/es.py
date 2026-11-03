"""CMA-ES over the written network's named parameters, scored in the fight lab.

Each generation draws a minibatch of training fights (the same for every candidate: common random numbers),
writes each candidate's weights from author.build(params), runs them two at a time, and scores points a
minute. Every few generations the distribution's mean is scored on the held-out val set. The fights never
change; the network does. Resumes from es_state.json.

  python3 es.py [generations] [per-kind batch]
"""
import json, math, os, random, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import cma

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import lab
from elite_wsp import author
from elite_wsp.export import arrays, save_wsp

# name: (scale of one step, kind) - "log" moves by a factor exp(0.25 x), "lin" by scale x; bounds after
SPACE = dict(
    lead=(1.0, "lin", 0, 12), roll_gain=(0.25, "log"), roll_ramp=(0.25, "log"), roll_cap=(0.2, "lin", 0.2, 3),
    pitch_gain=(0.25, "log"), pitch_cone=(0.3, "lin", 0, 3), back_gain=(2.0, "lin", 0, 8), back_cone=(0.3, "lin", 0, 3),
    hyst=(0.1, "lin", 0, 1), fire_r=(0.15, "lin", 0.5, 3), fire_range=(1.0, "lin", 1, 8), hot=(0.03, "lin", 0.5, 1),
    turn_off=(0.02, "lin", 0.01, 0.5), spd_turn=(4, "lin", 0, 40), near=(0.08, "lin", 0, 2), mid=(0.1, "lin", 0, 3),
    far=(0.2, "lin", 0, 6), spd_close=(3, "lin", 0, 40), spd_near=(3, "lin", 0, 40), spd_mid=(4, "lin", 0, 40),
    spd_far=(4, "lin", 0, 40), acq_th=(0.04, "lin", 0.02, 0.6),
)
NAMES = list(SPACE)


def decode(x, base=author.PARAMS):
    P = dict(base)
    for v, n in zip(x, NAMES):
        s = SPACE[n]
        P[n] = base[n] * math.exp(s[0] * v) if s[1] == "log" else min(s[3], max(s[2], base[n] + s[0] * v))
    return P


def weights_file(P, d):
    fd, path = tempfile.mkstemp(suffix=".wsp", dir=d); os.close(fd)
    save_wsp(arrays(author.build(P)), path)
    return path


def evaluate(P, paths, d, procs=1):
    w = weights_file(P, d)
    try:
        rows, aim = lab.run(paths, wsp=w, procs=procs)
    finally:
        os.unlink(w)
    s = lab.score(rows)["all"]
    return s["ppm"] - 2.0 * s["died"], s, aim


def batch(gen, per_kind):
    train = lab.read_set("train")
    rng = random.Random(1000 + gen)
    out = []
    for k, n in [("ws", 2 * per_kind), ("pack", per_kind), ("small", per_kind)]:     # witchspace weighted: the gap, and most points
        ps = [p for p in train if lab.KINDS.get(p) == k]
        out += rng.sample(ps, min(n, len(ps)))
    return out


BASE = dict(author.PARAMS)


def main(gens=30, per_kind=20, tag="es", base=None):
    global BASE
    if base:
        BASE = {**author.PARAMS, **json.load(open(os.path.join(HERE, base)))}
    state_path = os.path.join(HERE, tag + "_state.json")
    st = json.load(open(state_path)) if os.path.exists(state_path) else dict(gen=0, x=[0.0] * len(NAMES), sigma=0.5, log=[])
    if len(st["x"]) < len(NAMES):
        st["x"] += [0.0] * (len(NAMES) - len(st["x"]))
    es = cma.CMAEvolutionStrategy(st["x"], st["sigma"], {"popsize": 10, "seed": 7 + st["gen"], "verbose": -9})
    best_val = st.get("best_val", -1e9)
    tmp = tempfile.mkdtemp()
    val = lab.read_set("val")
    for g in range(st["gen"], st["gen"] + gens):
        t = time.time()
        paths = batch(g, per_kind)
        X = es.ask()
        with ThreadPoolExecutor(2) as ex:
            res = list(ex.map(lambda x: evaluate(decode(x, BASE), paths, tmp), X))
        es.tell(X, [-r[0] for r in res])
        mean_fit = evaluate(decode(es.mean, BASE), paths, tmp, procs=2)[0]
        rec = dict(gen=g, best=max(r[0] for r in res), median=float(np.median([r[0] for r in res])), mean=mean_fit,
                   sigma=es.sigma, secs=round(time.time() - t))
        if g % 4 == 3:
            f, s, aim = evaluate(decode(es.mean, BASE), val, tmp, procs=2)
            rec.update(val=s["ppm"], val_died=s["died"], val_hold=aim["hold"])
            if s["ppm"] > best_val and s["died"] == 0:
                best_val = s["ppm"]; st["best_val"] = best_val
                json.dump(decode(es.mean, BASE), open(os.path.join(HERE, tag + "_best.json"), "w"), indent=1)
        st["log"].append(rec)
        st.update(gen=g + 1, x=list(map(float, es.mean)), sigma=float(es.sigma))
        json.dump(st, open(state_path, "w"), indent=1)
        json.dump(decode(es.mean, BASE), open(os.path.join(HERE, tag + "_params.json"), "w"), indent=1)
        print(json.dumps(rec), flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    main(int(a[0]), int(a[1]), *a[2:])

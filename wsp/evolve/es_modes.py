#!/usr/bin/env python3
"""Evolve or measure the mode networks (elite_wsp/author_modes.py) in the fight lab, on fights recorded in that mode.

  es_modes.py sets WORK                       make the fight sets from WORK/rec/*/fights/index
  es_modes.py score eff|cam SET [wfile]       a set's score; no wfile: the C rules (eff: wfile "v2": the Fair network)
  es_modes.py evolve eff|cam GENS BATCH PROCS

eff: points a minute, each fight in its own process (lab.py), Optimal mode, the interceptor flying.
cam: the show score (tools/show.py), Curious mode, the show on: the camera pilot flying, the interceptor when the show is off.
Each generation's candidates fly the same fights. Resumes from its state file.
"""
import json, math, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE)); sys.path.insert(0, os.path.join(ROOT, "tools"))
import lab
from elite_wsp import author_modes as M
from elite_wsp.export import arrays, save_wsp

WORK = os.environ.get("MODES_WORK", "/home/dom/elite-agent-work")
LAB = os.path.join(ROOT, "cplayer", "lab")

# name: (step, "log") a factor exp(step x), or (step, "lin", lo, hi)
SPACE = dict(
    eff=dict(lead=(0.8, "lin", 0, 10), roll_gain=(0.25, "log"), roll_ramp=(0.25, "log"), roll_cap=(0.2, "lin", 0.2, 3),
             pitch_gain=(0.25, "log"), pitch_cone=(0.25, "lin", 0, 3), ff_pitch=(0.4, "lin", 0, 4), ff_roll=(0.3, "lin", 0, 3),
             hard_th=(0.04, "lin", 0.03, 0.8), hard_gain=(1.5, "lin", 0, 8), hard_cone=(0.25, "lin", 0, 3), hyst=(0.1, "lin", 0, 1),
             fire_r=(0.15, "lin", 0.5, 3), fire_range=(1.0, "lin", 1, 8), hot=(0.03, "lin", 0.5, 1),
             v_base=(3, "lin", 0, 40), v_k=(0.3, "log"), r_hold=(0.08, "lin", 0.02, 2), v_off=(0.3, "log"), v_th=(0.03, "lin", 0, 0.5),
             v_close=(0.3, "log"), close_ok=(0.1, "lin", -0.5, 1.5)),
    cam=dict(lead=(0.6, "lin", 0, 8), dead=(0.02, "lin", 0, 0.3), roll_gain=(0.25, "log"), roll_ramp=(0.25, "log"), roll_cap=(0.15, "lin", 0.2, 3),
             pitch_gain=(0.25, "log"), pitch_cone=(0.25, "lin", 0, 3), hard_th=(0.04, "lin", 0.03, 0.8), hard_gain=(1.5, "lin", 0, 8),
             hard_cone=(0.25, "lin", 0, 3), hyst=(0.1, "lin", 0, 1), pass_rho=(0.25, "log"), pass_k=(0.3, "log"), pass_close=(0.05, "lin", -0.3, 1),
             fire_r=(0.15, "lin", 0.5, 3), fire_range=(0.6, "lin", 0.5, 8), hot=(0.03, "lin", 0.5, 1), fire_rho=(0.25, "log"), fire_rho_ws=(0.25, "log"),
             v_base=(3, "lin", 0, 40), v_k=(0.3, "log"), r_hold=(0.06, "lin", 0.05, 2), v_off=(0.3, "log"), v_th=(0.03, "lin", 0, 0.5),
             v_close=(0.3, "log"), close_ok=(0.08, "lin", -0.5, 1.5)),
)
BUILD = dict(eff=M.build_interceptor, cam=M.build_camera)
DEF = dict(eff=M.INTERCEPTOR, cam=M.CAMERA)


def set_path(kind, name): return os.path.join(WORK, "sets", f"{kind}_{name}.lst")
def read_set(kind, name): return [l.strip() for l in open(set_path(kind, name)) if l.strip()]


def make_sets():
    os.makedirs(os.path.join(WORK, "sets"), exist_ok=True)
    kinds = {}
    for kind, runs in dict(eff=["eff13", "eff101"], cam=["cur13", "cur101"]).items():
        by = dict(ws=[], pack=[], small=[])
        for r in runs:
            d = os.path.join(WORK, "rec", r, "fights")
            for line in open(os.path.join(d, "index.jsonl")):
                j = json.loads(line)
                if not j["enemies"] or j["frames"] < 100: continue         # (nothing to fight: an empty scanner, a blip)
                k = "ws" if j["witchspace"] else "pack" if len(j["enemies"]) >= 3 else "small"
                p = os.path.join(d, f"{j['id']:05d}.fight")
                if os.path.exists(p):
                    by[k].append(p); kinds[p] = k
        rng = random.Random(42)
        train, val, test = [], [], []
        for k, ps in by.items():
            rng.shuffle(ps)
            n = len(ps); a, b = min(100, n // 2), min(25, n // 8)
            train += ps[:a]; val += ps[a:a + b]; test += ps[a + b:a + b + 150]
        for name, ps in dict(train=train, val=val, test=test).items():
            open(set_path(kind, name), "w").write("\n".join(ps) + "\n")
        print(kind, {k: len(v) for k, v in by.items()}, "train", len(train), "val", len(val), "test", len(test))
    json.dump(kinds, open(os.path.join(WORK, "sets", "kinds.json"), "w"))


if os.path.exists(os.path.join(WORK, "sets", "kinds.json")):
    lab.KINDS.update(json.load(open(os.path.join(WORK, "sets", "kinds.json"))))


def decode(kind, x, base=None):
    base = base or DEF[kind]; P = dict(base)
    for v, n in zip(x, SPACE[kind]):
        s = SPACE[kind][n]
        P[n] = base[n] * math.exp(s[0] * v) if s[1] == "log" else min(s[3], max(s[2], base[n] + s[0] * v))
    return P


def wfile(kind, P, d):
    fd, path = tempfile.mkstemp(suffix=".wsp", dir=d); os.close(fd)
    save_wsp(arrays(BUILD[kind](P)), path)
    return path


def cam_env(w):
    env = dict(os.environ, CURIOUS="1", UNDAMPED="1", DCAUSE="1", COLLOG="1", PILOTS="1")     # (PILOTS: as the page flies it: the modes' own pilots, Optimal's target ranking)
    if not w: env.update(PILOTS="0", WSP="0")           # (the C rules and show.c: no pilots)
    if w:
        env.update(WSP="1", WSPSHOW="1", WSPWS=w)
        eff = os.path.join(HERE, "eff_interceptor.wsp")
        if os.path.exists(eff): env["WSPW"] = eff
    return env


def run_show(paths, w, procs):
    """The show score of each fight: tools/show.py on the lab's lines, each fight in its own process."""
    import show
    env = cam_env(w)
    def one(p):
        fd, lst = tempfile.mkstemp(suffix=".lst"); os.close(fd)
        open(lst, "w").write(p + "\n")
        r = subprocess.run([LAB, lst], cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
        os.unlink(lst)
        return "\n".join(l for l in (r.stdout + "\n" + r.stderr).splitlines() if l[:3] in ("SCN", "SHO", "DCA", "COL") or l.endswith(".fight") or ".fight " in l[:120])
    with ThreadPoolExecutor(procs) as ex:
        text = "\n".join(ex.map(one, paths))
    fd, t = tempfile.mkstemp(suffix=".txt"); os.close(fd)
    open(t, "w").write(text + "\n")
    F = show.load(t); os.unlink(t)
    return F


def evaluate(kind, w, paths, procs):
    if kind == "eff":
        env = dict(os.environ); env.pop("WSP", None)
        rows, aim = lab.run(paths, wsp=w if w != "C" else None, procs=procs, extra_env={"WSP": "1"} if w == "v2" else None) if w != "v2" else lab.run(paths, procs=procs, extra_env={"WSP": "1"})
        s = lab.score(rows, by_kind=True)
        a = s["all"]
        return a["ppm"] - 0.5 * a["died"], dict(ppm=round(a["ppm"], 3), died=a["died"], clr=a["cleared"], n=a["n"], kinds={k: round(v["ppm"], 2) for k, v in s.items() if k != "all"}), rows
    F = run_show(paths, None if w == "C" else w, procs)
    if not F: return -1, dict(n=0), []
    m = sum(f["score"] for f in F) / len(F); died = sum(1 for f in F if f["died"])
    mean = lambda k: round(sum(f["parts"].get(k, 0) for f in F) / len(F), 3)
    return m, dict(score=round(m, 2), died=died, n=len(F), screen=mean("screen"), close=mean("close"), air=mean("air"), ckill=mean("ckill"), motion=mean("motion"), passes=mean("passes"),
                   frames=round(sum(f["frames"] for f in F) / len(F)), complete=round(sum(f["complete"] for f in F) / len(F), 3)), F


def evolve(kind, gens, per, procs, tag=None):
    import cma
    tag = tag or kind
    sp = os.path.join(HERE, f"modes_{tag}_state.json")
    names = list(SPACE[kind])
    st = json.load(open(sp)) if os.path.exists(sp) else dict(gen=0, x=[0.0] * len(names), sigma=0.6, log=[], best_val=-1e9)
    es = cma.CMAEvolutionStrategy(st["x"], st["sigma"], {"popsize": 8, "seed": 17 + st["gen"], "verbose": -9})
    tmp = tempfile.mkdtemp()
    train, val = read_set(kind, "train"), read_set(kind, "val")
    per_cand = max(1, procs // 2)
    for g in range(st["gen"], st["gen"] + gens):
        t = time.time()
        rng = random.Random(1000 + g); batch = []
        for k, n in [("ws", 2 * per // 4), ("pack", per // 4), ("small", per // 4)]:
            ps = [p for p in train if lab.KINDS.get(p) == k]; batch += rng.sample(ps, min(n, len(ps)))
        X = es.ask()
        def ev(x):
            w = wfile(kind, decode(kind, x), tmp)
            try: return evaluate(kind, w, batch, per_cand)[:2]
            finally: os.unlink(w)
        with ThreadPoolExecutor(2) as ex:
            res = list(ex.map(ev, X))
        es.tell(X, [-r[0] for r in res])
        rec = dict(gen=g, best=round(max(r[0] for r in res), 3), median=round(float(np.median([r[0] for r in res])), 3), sigma=round(float(es.sigma), 3), secs=round(time.time() - t))
        if g % 3 == 2:
            w = wfile(kind, decode(kind, es.mean), tmp)
            f, info, _ = evaluate(kind, w, val, procs); os.unlink(w)
            rec.update(val=info)
            if f > st["best_val"]:
                st["best_val"] = f
                json.dump(decode(kind, es.mean), open(os.path.join(HERE, f"modes_{tag}_best.json"), "w"), indent=1)
        st["log"].append(rec); st.update(gen=g + 1, x=list(map(float, es.mean)), sigma=float(es.sigma))
        json.dump(st, open(sp, "w"), indent=1)
        print(json.dumps(rec), flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "sets": make_sets()
    elif a[0] == "score":
        kind, name = a[1], a[2]; w = a[3] if len(a) > 3 else "C"
        if w == "written":
            d = tempfile.mkdtemp(); w = wfile(kind, DEF[kind], d)
        elif w.endswith(".json"):
            d = tempfile.mkdtemp(); w = wfile(kind, {**DEF[kind], **json.load(open(w))}, d)
        t = time.time(); f, info, _ = evaluate(kind, w, read_set(kind, name), int(os.environ.get("NPROC", 2)))
        print(kind, name, a[3] if len(a) > 3 else "C", json.dumps(info), round(time.time() - t), "s", flush=True)
    elif a[0] == "evolve":
        evolve(a[1], int(a[2]), int(a[3]), int(a[4]), *a[5:])

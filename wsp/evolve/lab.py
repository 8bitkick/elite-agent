"""Run cplayer/lab over a fight list in parallel and score it.

Each lab line is one fight: path, points (0 if it died), minutes, seconds to clear, damage, died, pod, ...
Points a minute is summed points over summed minutes, the measure the C pilot was tuned on.
"""
import json, os, re, subprocess, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAB = os.path.join(ROOT, "cplayer", "lab")
KINDS = json.load(open(os.path.join(os.path.dirname(__file__), "sets", "kinds.json")))
AIM = re.compile(r"AIM acquire ([\d.]+) s avg over (\d+) \(never (\d+)\) \| hold ([\d.]+)% of (\d+) locked frames \| off ([\d.]+) deg")


def read_set(name):
    p = name if os.path.exists(name) else os.path.join(os.path.dirname(__file__), "sets", name + ".lst")
    return [l.strip() for l in open(p) if l.strip()]


def _run(paths, env):
    with tempfile.NamedTemporaryFile("w", suffix=".lst", delete=False) as f:
        f.write("\n".join(paths) + "\n")
        lst = f.name
    try:
        r = subprocess.run([LAB, lst], cwd=ROOT, env=env, capture_output=True, text=True, timeout=1800)
    finally:
        os.unlink(lst)
    rows = {}
    for line in r.stdout.splitlines():
        t = line.split()
        if len(t) >= 7 and t[0].endswith(".fight"):
            rows[t[0]] = dict(pts=float(t[1]), mins=float(t[2]), clear=float(t[3]), dmg=int(t[4]), died=int(t[5]), pod=int(t[6]), kills=int(t[7]) if len(t) > 7 else 0)
    ttk = [list(map(int, l.split()[1:5])) for l in r.stderr.splitlines() if l.startswith("TTK ")]
    if len(paths) == 1 and ttk and paths[0] in rows:
        rows[paths[0]]["ttk"] = ttk[-1]            # frames: slewing onto a target, tracking, in the sights, no target
    m = AIM.search(r.stderr)
    aim = dict(hold=float(m.group(4)), locked=int(m.group(5)), off=float(m.group(6))) if m else None
    return rows, aim


def run(paths, wsp=None, procs=2, extra_env=None):
    """wsp: None for the C pilot, else a weights file for the network. Each fight runs in a fresh process: the
    lab keeps state from one fight to the next in a process (targets, timers, the planner), which made a fight's
    result depend on the fights before it."""
    env = dict(os.environ)
    env.pop("WSP", None); env.pop("WSPW", None)
    if wsp:
        env["WSP"] = "1"; env["WSPW"] = os.path.abspath(wsp)
    env.update(extra_env or {})
    order = sorted(paths, key=lambda p: -(2 if KINDS.get(p) == "ws" else 1))       # long fights first
    with ThreadPoolExecutor(procs) as ex:
        res = list(ex.map(lambda p: _run([p], env), order))
    rows, aims = {}, []
    for r, a in res:
        rows.update(r)
        if a: aims.append(a)
    locked = sum(a["locked"] for a in aims) or 1
    hold = sum(a["hold"] * a["locked"] for a in aims) / locked
    return rows, dict(hold=hold)


def score(rows, paths=None, by_kind=False):
    paths = paths or list(rows)
    def agg(ps):
        rs = [rows[p] for p in ps if p in rows]
        pts = sum(r["pts"] for r in rs); mins = sum(r["mins"] for r in rs) or 1e-9
        return dict(n=len(rs), ppm=pts / mins, kpm=sum(r.get("kills", 0) for r in rs) / mins, pts=pts, mins=mins, dmg=sum(r["dmg"] for r in rs),
                    died=sum(r["died"] for r in rs), pod=sum(r["pod"] for r in rs),
                    cleared=sum(1 for r in rs if r["mins"] < 3.99 and not r["died"]))
    out = {"all": agg(paths)}
    if by_kind:
        for k in ["ws", "pack", "small"]:
            ps = [p for p in paths if KINDS.get(p) == k]
            if ps: out[k] = agg(ps)
    return out


def fmt(s):
    return "  ".join(f"{k}: {v['ppm']:5.2f} ppm {v['kpm']:5.2f} kpm, dmg {v['dmg']}, died {v['died']}, clr {v['cleared']}/{v['n']}" for k, v in s.items())


def compare(rows_a, rows_b, paths=None, n=2000, seed=0):
    """Paired bootstrap over fights: points a minute of a over b, as a ratio, with a 90% interval."""
    import random
    paths = [p for p in (paths or rows_a) if p in rows_a and p in rows_b]
    rng = random.Random(seed)
    def ratio(ps):
        a = sum(rows_a[p]["pts"] for p in ps) / max(1e-9, sum(rows_a[p]["mins"] for p in ps))
        b = sum(rows_b[p]["pts"] for p in ps) / max(1e-9, sum(rows_b[p]["mins"] for p in ps))
        return a / b
    rs = sorted(ratio([rng.choice(paths) for _ in paths]) for _ in range(n))
    return ratio(paths), rs[int(0.05 * n)], rs[int(0.95 * n)]

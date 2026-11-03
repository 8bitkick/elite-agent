"""PPO on a learned residual over the written (and evolved) network, in the real game.

  policy mean = clamp(written network) + residual          (residual's last layer starts at zero)
  roll rate, pitch rate, speed ~ Gaussian(mean, sigma)      laser ~ coin, p = sigmoid(8 (mean - 0.5))

Rollouts run in C (cplayer/lab_rl with WSPW, WSPR, WSPN, WSPLOG): each recorded fight is an episode,
replayed from its snapshot. Each action is held for HOLD frames. Python reads the logs, scores each
decision, and updates the residual and a value net. Only the residual and the noise scales learn; the
written network is never touched.

Reward per decision: points scored - LAM * frames elapsed - DMG * damage taken; a death forfeits the fight's
points (as the lab scores it) and costs DEATH more. LAM is the points-a-frame rate to beat (Dinkelbach: with
LAM at the best rate so far, maximising this maximises points a minute).

  python3 ppo.py iterations
"""
import json, math, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import torch, torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE)); sys.path.insert(0, HERE)
import lab
from elite_wsp.export import save_wsp

LAB_RL = os.path.join(ROOT, "cplayer", "lab_rl")
NF, HOLD = 23, 1
LAM, DMG, DEATH = 21.0 / 3000, 0.004, 20.0
GAMMA, GAE = 0.99, 0.95
WARM, EVAL = 2, 3          # value-only iterations first; val every EVAL
torch.set_num_threads(2)


def read_wsp(path):
    t = open(path).read().split()
    assert t[0] == "WSP2"
    nl, i, out = int(t[1]), 2, []
    for _ in range(nl):
        r, c = int(t[i]), int(t[i + 1]); i += 2
        W = np.array(t[i:i + r * c], dtype=np.float32).reshape(r, c); i += r * c
        b = np.array(t[i:i + r], dtype=np.float32); i += r
        out.append((W, b))
    return out


class Written(nn.Module):
    """The written network, fixed, its outputs clamped to what the hands can do (as wsp_pilot.c does)."""
    def __init__(self, path):
        super().__init__()
        self.Wb = [(torch.tensor(W), torch.tensor(b)) for W, b in read_wsp(path)]

    @torch.no_grad()
    def forward(self, f):
        h = f
        for i, (W, b) in enumerate(self.Wb):
            h = h @ W.T + b
            if i < len(self.Wb) - 1: h = torch.relu(h)
        lo = torch.tensor([-31, -8, -1e9, 0.0]); hi = torch.tensor([31, 8, 1e9, 40.0])
        return torch.max(torch.min(h, hi), lo)


def mlp(n_in, n_out, hidden=64, zero_last=False):
    m = nn.Sequential(nn.Linear(n_in, hidden), nn.ReLU(), nn.Linear(hidden, hidden), nn.ReLU(), nn.Linear(hidden, n_out))
    if zero_last:
        nn.init.zeros_(m[-1].weight); nn.init.zeros_(m[-1].bias)
    return m


class Policy(nn.Module):
    def __init__(self, written, hidden=32):
        super().__init__()
        self.written = written
        self.res = mlp(NF, 4, hidden, zero_last=True)
        self.log_std = nn.Parameter(torch.tensor([math.log(2.0), math.log(0.7), math.log(2.0)]))
        self.value = mlp(NF, 1, 64)

    def mean(self, f):
        return self.written(f) + self.res(f)

    def dist(self, f):
        m = self.mean(f)
        std = self.log_std.exp()
        return m, std

    def logp(self, f, a):
        m, std = self.dist(f)
        g = a[:, [0, 1, 3]]; mg = m[:, [0, 1, 3]]
        lp = (-0.5 * ((g - mg) / std) ** 2 - torch.log(std) - 0.5 * math.log(2 * math.pi)).sum(1)
        p = torch.sigmoid(8 * (m[:, 2] - 0.5)).clamp(1e-6, 1 - 1e-6)
        lp = lp + torch.where(a[:, 2] > 0.5, torch.log(p), torch.log(1 - p))
        ent = (0.5 + 0.5 * math.log(2 * math.pi) + torch.log(std)).sum() - (p * torch.log(p) + (1 - p) * torch.log(1 - p)).mean()
        return lp, ent

    def save_residual(self, path):
        layers = [l for l in self.res if isinstance(l, nn.Linear)]
        save_wsp([(l.weight.detach().numpy(), l.bias.detach().numpy()) for l in layers], path)


# ---------------------------------------------------------------------------------------------- rollouts
def run_chunk(paths, env, logpath):
    with tempfile.NamedTemporaryFile("w", suffix=".lst", delete=False) as f:
        f.write("\n".join(paths) + "\n"); lst = f.name
    e = dict(env, WSPLOG=logpath)
    r = subprocess.run([LAB_RL, lst], cwd=ROOT, env=e, capture_output=True, text=True, timeout=3600)
    os.unlink(lst)
    rows = {}
    for line in r.stdout.splitlines():
        t = line.split()
        if len(t) >= 7 and t[0].endswith(".fight"):
            rows[t[0]] = dict(pts=float(t[1]), mins=float(t[2]), dmg=int(t[4]), died=int(t[5]))
    return rows


def rollout(paths, written_path, res_path, std, seed, tmp, explore=True):
    env = dict(os.environ, WSP="1", WSPW=written_path, WSPR=res_path)
    sa, sb, ss = std
    env["WSPN"] = f"{sa:.4f} {sb:.4f} {1 if explore else 0} {ss:.4f} {HOLD} {seed}" if explore else f"0 0 0 0 {HOLD} 1"
    chunks = [[], []]; cost = [0, 0]
    for p in sorted(paths, key=lambda p: -(2 if lab.KINDS.get(p) == "ws" else 1)):
        i = cost.index(min(cost)); chunks[i].append(p); cost[i] += 2 if lab.KINDS.get(p) == "ws" else 1
    logs = [os.path.join(tmp, f"log{i}.txt") for i in range(2)]
    with ThreadPoolExecutor(2) as ex:
        res = list(ex.map(lambda i: run_chunk(chunks[i], dict(env, WSPN=env["WSPN"].rsplit(" ", 1)[0] + f" {seed * 2 + i}") if explore else env, logs[i]), range(2)))
    rows = {**res[0], **res[1]}
    return rows, logs


def parse(logs, rows):
    """Episodes: features, actions, rewards, from the decision logs and the lab's verdicts."""
    eps = []
    for lp in logs:
        cur = None
        for line in open(lp):
            if line.startswith("E "):
                t = line.split()
                cur = dict(path=t[1], t0=float(t[2]), F=[], A=[], frame=[], tally=[], health=[])
                eps.append(cur)
            elif line.startswith("D ") and cur is not None:
                a, b, c = line[2:].split("|")
                t = a.split()
                cur["frame"].append(int(t[0])); cur["F"].append([float(v) for v in t[1:]])
                cur["A"].append([float(v) for v in b.split()])
                tt = c.split(); cur["tally"].append(float(tt[0])); cur["health"].append(int(tt[1]) + int(tt[2]) + int(tt[3]))
    out = []
    for e in eps:
        r = rows.get(e["path"])
        n = len(e["F"])
        if r is None or n == 0:
            continue
        tally = np.array(e["tally"] + [e["t0"] + (0 if r["died"] else r["pts"])])
        frames = np.array(e["frame"] + [max(e["frame"][-1] + 1, int(r["mins"] * 3000))])
        health = np.array(e["health"] + [e["health"][-1]])
        rew = np.diff(tally) - LAM * np.diff(frames) - DMG * np.maximum(0, -np.diff(health))
        if r["died"]:
            rew[-1] -= (tally[-2] - e["t0"]) + DEATH
        out.append(dict(path=e["path"], F=np.array(e["F"], np.float32), A=np.array(e["A"], np.float32), R=rew.astype(np.float32)))
    return out


def gae(eps, pol):
    for e in eps:
        with torch.no_grad():
            v = pol.value(torch.tensor(e["F"])).squeeze(1).numpy()
        adv = np.zeros_like(e["R"]); last = 0.0
        for t in reversed(range(len(e["R"]))):
            nv = v[t + 1] if t + 1 < len(v) else 0.0
            d = e["R"][t] + GAMMA * nv - v[t]
            last = d + GAMMA * GAE * last
            adv[t] = last
        e["ADV"], e["RET"] = adv, adv + v


def update(pol, opt, eps, epochs=4, mb=4096, clip=0.2, vcoef=0.5, ecoef=0.0, policy=True):
    F = torch.tensor(np.concatenate([e["F"] for e in eps])); A = torch.tensor(np.concatenate([e["A"] for e in eps]))
    ADV = torch.tensor(np.concatenate([e["ADV"] for e in eps])); RET = torch.tensor(np.concatenate([e["RET"] for e in eps]))
    ADV = (ADV - ADV.mean()) / (ADV.std() + 1e-8)
    with torch.no_grad():
        old, _ = pol.logp(F, A)
    n = len(F); stats = []
    for _ in range(epochs):
        idx = torch.randperm(n)
        for s in range(0, n, mb):
            j = idx[s:s + mb]
            lp, ent = pol.logp(F[j], A[j])
            ratio = (lp - old[j]).exp()
            pl = -torch.min(ratio * ADV[j], ratio.clamp(1 - clip, 1 + clip) * ADV[j]).mean()
            vl = ((pol.value(F[j]).squeeze(1) - RET[j]) ** 2).mean()
            loss = (pl - ecoef * ent if policy else 0) + vcoef * vl
            opt.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(pol.parameters(), 1.0); opt.step()
            stats.append((pl.item(), vl.item(), (ratio - 1).abs().mean().item()))
    return np.mean(stats, 0), n


def batch(it, per_kind):
    train = lab.read_set("train")
    if per_kind <= 0:
        return train
    rng = random.Random(5000 + it); out = []
    for k in ["ws", "pack", "small"]:
        ps = [p for p in train if lab.KINDS.get(p) == k]; out += rng.sample(ps, min(per_kind, len(ps)))
    return out


def evaluate(pol, written_path, tmp, setname="val"):
    rp = os.path.join(tmp, "res_eval.wsp"); pol.save_residual(rp)
    rows, logs = rollout(lab.read_set(setname), written_path, rp, (0, 0, 0), 1, tmp, explore=False)
    return lab.score({p: dict(r, clear=0, pod=0) for p, r in rows.items()}, by_kind=True)


def main(iters=60, per_kind=14, written="../elite_pilot.wsp", out="ppo"):
    tmp = tempfile.mkdtemp()
    written_path = os.path.abspath(os.path.join(HERE, written))
    pol = Policy(Written(written_path))
    opt = torch.optim.Adam([p for n, p in pol.named_parameters() if not n.startswith("written")], lr=1e-4)
    ck = os.path.join(HERE, out + ".pt")
    start, best = 0, -1e9
    log = []
    if os.path.exists(ck):
        s = torch.load(ck, weights_only=False); pol.load_state_dict(s["pol"]); opt.load_state_dict(s["opt"]); start = s["it"]; best = s["best"]; log = s["log"]
    for it in range(start, start + iters):
        t = time.time()
        rp = os.path.join(tmp, "res.wsp"); pol.save_residual(rp)
        std = pol.log_std.exp().tolist()
        rows, logs = rollout(batch(it, per_kind), written_path, rp, std, it + 1, tmp)
        eps = parse(logs, rows)
        gae(eps, pol)
        (pl, vl, dr), n = update(pol, opt, eps, policy=it >= WARM)
        sc = lab.score({p: dict(r, clear=0, pod=0) for p, r in rows.items()})["all"]
        rec = dict(it=it, ppm=round(sc["ppm"], 3), died=sc["died"], n=n, std=[round(v, 3) for v in std], pl=round(pl, 4), vl=round(vl, 3),
                   dr=round(dr, 4), res=round(float(sum(p.detach().abs().sum() for p in pol.res[-1].parameters())), 3), secs=round(time.time() - t))
        if it % EVAL == EVAL - 1 and it >= WARM:
            v = evaluate(pol, written_path, tmp)
            rec["val"] = round(v["all"]["ppm"], 3); rec["val_died"] = v["all"]["died"]
            if rec["val"] > best and rec["val_died"] == 0:
                best = rec["val"]; pol.save_residual(os.path.join(HERE, out + "_best.wsp"))
        log.append(rec)
        print(json.dumps(rec), flush=True)
        torch.save(dict(pol=pol.state_dict(), opt=opt.state_dict(), it=it + 1, best=best, log=log), ck)


if __name__ == "__main__":
    a = sys.argv[1:]
    main(int(a[0]) if a else 60, *(int(x) for x in a[1:2]), *a[2:])

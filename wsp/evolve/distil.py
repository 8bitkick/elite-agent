"""DAgger: distil the C pilot's acquisition planner into the residual network.

The fight lab's time-to-kill split shows the written network loses its gap to the C in one phase: the capture,
from near the nose into the sights (0.58 s a kill against 0.47 s in witchspace). The C flies that phase with a
planner that simulates Elite's rotation (pilot.c plan_stick). Here the network flies, and at every acquisition
frame the planner is asked what it would do from the network's own state (WSPTEACH=1 logs it). The residual
learns: the planner's roll and pitch rates where the planner would act, and zero everywhere else, so the
written law's tracking stays as it is. Each round flies the newest network and adds its states (DAgger).

  python3 distil.py rounds
"""
import json, os, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import torch, torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lab
from ppo import Written, mlp, read_wsp

TH, RHO = 5, 8                       # feature indices: off angle / pi, angular radius x 16
GATE_S, GATE_M = 3000.0, 64.0


class GatedRes(nn.Module):
    """A residual that acts only beyond three hit radii off the nose, where the C's planner acts. The gate is a
    fixed step unit carried through the layers (sat(th - 3 rho / 16 pi)); each output is relu(z + M g - M) minus
    its mirror, so inside the gate the residual is exactly zero and the written law tracks alone."""
    def __init__(self, hidden=64):
        super().__init__()
        self.l1 = nn.Linear(NF, hidden); self.l2 = nn.Linear(hidden, hidden); self.l3 = nn.Linear(hidden, 4)
        nn.init.normal_(self.l3.weight, std=1e-3); nn.init.zeros_(self.l3.bias)    # (not zero: at exactly 0 both gate ReLUs sit at their kink and pass no gradient)
        g = torch.zeros(NF); g[TH] = 1.0; g[RHO] = -3.0 / (16 * np.pi)
        self.register_buffer("g", g)
    def gate(self, f):
        u = GATE_S * (f @ self.g)
        return torch.relu(u) - torch.relu(u - 1)
    def forward(self, f):
        z = self.l3(torch.relu(self.l2(torch.relu(self.l1(f)))))
        g = self.gate(f)[:, None]
        return torch.relu(z + GATE_M * g - GATE_M) - torch.relu(-z + GATE_M * g - GATE_M)
    def export(self):
        """As a plain ReLU MLP for the C: the gate's two step units ride along in each hidden layer."""
        H = self.l1.out_features
        W1 = np.zeros((H + 2, NF), np.float32); b1 = np.zeros(H + 2, np.float32)
        W1[:H] = self.l1.weight.detach().numpy(); b1[:H] = self.l1.bias.detach().numpy()
        W1[H] = GATE_S * self.g.numpy(); W1[H + 1] = GATE_S * self.g.numpy(); b1[H + 1] = -1.0
        W2 = np.zeros((H + 2, H + 2), np.float32); b2 = np.zeros(H + 2, np.float32)
        W2[:H, :H] = self.l2.weight.detach().numpy(); b2[:H] = self.l2.bias.detach().numpy()
        W2[H, H] = 1; W2[H + 1, H + 1] = 1
        Wz, bz = self.l3.weight.detach().numpy(), self.l3.bias.detach().numpy()
        W3 = np.zeros((8, H + 2), np.float32); b3 = np.zeros(8, np.float32)
        for k in range(4):
            for s, sg in [(0, 1.0), (1, -1.0)]:
                W3[2 * k + s, :H] = sg * Wz[k]; b3[2 * k + s] = sg * bz[k] - GATE_M
                W3[2 * k + s, H] = GATE_M; W3[2 * k + s, H + 1] = -GATE_M
        W4 = np.zeros((4, 8), np.float32)
        for k in range(4):
            W4[k, 2 * k] = 1; W4[k, 2 * k + 1] = -1
        return [(W1, b1), (W2, b2), (W3, b3), (W4, np.zeros(4, np.float32))]
from elite_wsp.export import save_wsp

NF = 23
torch.set_num_threads(2)


def collect(paths, written, residual, tmp):
    """Fly each fight (fresh process) with the planner as teacher; return features, planner labels and a mask."""
    env = dict(os.environ, WSP="1", WSPW=written, WSPTEACH="1")
    if residual:
        env["WSPR"] = residual
    def one(i):
        log = os.path.join(tmp, f"d{i}.log"); lst = os.path.join(tmp, f"d{i}.lst")
        open(lst, "w").write(paths[i] + "\n")
        subprocess.run([lab.LAB, lst], cwd=ROOT, env=dict(env, WSPLOG=log), capture_output=True, timeout=1800)
        F, Y, M = [], [], []
        last = None
        for line in open(log):
            if line[0] == "D":
                a = line[2:].split("|")[0].split()
                F.append([float(v) for v in a[1:]]); Y.append([0.0, 0.0]); M.append(0); last = int(a[0])
            elif line[0] == "P" and F:
                t = line.split()
                if int(t[1]) == last:
                    Y[-1] = [float(t[2]), float(t[3])]; M[-1] = 1
        os.unlink(log); os.unlink(lst)
        return np.array(F, np.float32).reshape(-1, NF), np.array(Y, np.float32).reshape(-1, 2), np.array(M, np.float32)
    with ThreadPoolExecutor(2) as ex:
        parts = list(ex.map(one, range(len(paths))))
    return [np.concatenate([p[i] for p in parts]) for i in range(3)]


def train(res, written, F, Y, M, epochs=30, lr=1e-3):
    """Fit written + residual to the planner where it acts (rates scaled to the stick's range), residual to 0 elsewhere."""
    F = torch.tensor(F); Y = torch.tensor(Y); M = torch.tensor(M)
    base = written(F)[:, :2]
    scale = torch.tensor([31.0, 8.0])
    opt = torch.optim.Adam(res.parameters(), lr=lr)
    w = torch.where(M > 0, torch.tensor(1.0), torch.tensor(0.3))       # the capture frames are the point
    n = len(F)
    for ep in range(epochs):
        idx = torch.randperm(n); tot = 0.0
        for s in range(0, n, 8192):
            j = idx[s:s + 8192]
            out = res(F[j])
            pred = base[j] + out[:, :2]
            tgt = torch.where(M[j, None] > 0, Y[j], base[j])            # the planner where it acts, else the written law
            loss = (w[j, None] * ((pred - tgt) / scale) ** 2).mean() + 0.1 * ((out[:, 2:] ) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step(); tot += loss.item() * len(j)
    with torch.no_grad():
        out = res(F)
        pred = base + out[:, :2]
        m = M > 0
        err_acq = ((pred[m] - Y[m]).abs()).mean(0).tolist() if m.any() else [0, 0]
        err_rest = (out[~m, :2].abs()).mean(0).tolist()
    return tot / n, err_acq, err_rest


def save_res(res, path):
    save_wsp(res.export(), path)


def main(rounds=4, written="written_es14.wsp", out="distil", per_round=None):
    tmp = tempfile.mkdtemp()
    written = os.path.abspath(os.path.join(HERE, written))
    W = Written(written)
    res = GatedRes(64)
    train_set = lab.read_set("train")
    data = []
    rp = None
    for r in range(rounds):
        t = time.time()
        F, Y, M = collect(train_set, written, rp, tmp)
        data.append((F, Y, M))
        AF, AY, AM = [np.concatenate([d[i] for d in data]) for i in range(3)]
        loss, ea, er = train(res, W, AF, AY, AM)
        rp = os.path.join(HERE, f"{out}_r{r}.wsp"); save_res(res, rp)
        rows, aim = lab.run(lab.read_set("val"), wsp=written, extra_env={"WSPR": rp})
        s = lab.score(rows)["all"]
        rec = dict(round=r, frames=len(AF), acq=int(AM.sum()), loss=round(loss, 4), err_acq=[round(v, 2) for v in ea],
                   err_rest=[round(v, 3) for v in er], val=round(s["ppm"], 3), val_died=s["died"], hold=round(aim["hold"], 1), secs=round(time.time() - t))
        print(json.dumps(rec), flush=True)


if __name__ == "__main__":
    main(*(int(a) for a in sys.argv[1:2]))

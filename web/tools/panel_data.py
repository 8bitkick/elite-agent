#!/usr/bin/env python3
"""The page's network panels as data (web/panel.json): both networks' weights, the units each panel shows in its six
columns, the routes between them, and each unit's scale (the 95th percentile of its activity over a recorded career),
as the film's panels have them (the film scripts' render.py, dock_graph.py).

  panel_data.py FIGHT_LOG DOCK_LOG out.json      (run_sim's WSPLOG and WAPLOG from a fair career)
"""
import json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "wsp"))
from elite_wsp.net import Net
from elite_wsp.features import NAMES as FEATS
from elite_wsp import dock_author as DA, dock_features as DF
sys.path.insert(0, HERE)
import dock_panel as DP

def dense(Ws): return [{"W": W.round(6).tolist(), "b": b.round(6).tolist()} for W, b in Ws]

def scales(layers, X):
    h, out = X.T.astype(np.float64), [np.maximum(np.percentile(np.abs(X), 95, axis=0), 1e-3)]
    for i, (W, b, relu) in enumerate(layers):
        h = W @ h + b[:, None]
        if relu: h = np.maximum(h, 0)
        out.append(np.maximum(np.percentile(np.abs(h), 95, axis=1), 1e-3))
    return [s.round(5).tolist() for s in out]

SHOW = {'ux': 'aim right', 'uy': 'aim up', 'uz': 'aim ahead', 'wx': 'drift x', 'wy': 'drift y', 'th': 'off nose', 'cx': 'clock x',
        'cy': 'clock y', 'rho': 'size', 'dist': 'range', 'close': 'closing', 'own': 'our speed', 'hot': 'laser heat', 'energy': 'energy',
        'ws': 'witchspace', 'jx': 'stick x', 'jy': 'stick y', 'seen': 'target', 'one': 'bias', 'hseen': 'hdg seen', 'age': 'age',
        'rsd': 'range ±', 'vsd': 'motion ±', 'arm': 'laser power'}

def read_fight_log(path):
    F = []
    for line in open(path):
        a = line.split("|")[0].split()
        if len(a) == 2 + len(FEATS) and a[0] == "D": F.append([float(x) for x in a[2:]])
    return np.array(F[:: max(1, len(F) // 4000)], np.float32)

def fight_net(net, F):
    """a dogfight-type network (24 senses, 4 outputs, any number of layers) as the panel draws it"""
    flayers = [(net.W[i], net.b[i], i < net.n - 1) for i in range(net.n)]
    return dict(layers=dense([(W, b) for W, b, _ in flayers]),
                cols=[["INPUTS", [SHOW[str(x)] for x in net.features]]] + [[f"LAYER {i + 1}", [str(x) for x in net.names[i]]] for i in range(net.n - 1)]
                     + [["OUTPUT", ["roll", "pitch", "fire", "throttle"]]],
                scale=scales(flayers, F)[:-1], outscale=[31, 8, 1, 40])

def add(name, npz, log, out):
    """panel_data.py --add NAME net.npz WSPLOG out.json: another dogfight-type network into an existing panel.json"""
    d = json.load(open(out))
    d[name] = fight_net(Net(npz), read_fight_log(log))
    json.dump(d, open(out, "w"), separators=(",", ":"))
    print(f"{out}: {name} added, {os.path.getsize(out) // 1024} KB")

def main(fight_log, dock_log, out):
    # ---- the dogfight network: its own columns, every unit
    net = Net(os.path.join(REPO, "wsp", "elite_pilot.npz"))
    SHOW = {'ux': 'aim right', 'uy': 'aim up', 'uz': 'aim ahead', 'wx': 'drift x', 'wy': 'drift y', 'th': 'off nose', 'cx': 'clock x',
            'cy': 'clock y', 'rho': 'size', 'dist': 'range', 'close': 'closing', 'own': 'our speed', 'hot': 'laser heat', 'energy': 'energy',
            'ws': 'witchspace', 'jx': 'stick x', 'jy': 'stick y', 'seen': 'target', 'one': 'bias', 'hseen': 'hdg seen', 'age': 'age',
            'rsd': 'range ±', 'vsd': 'motion ±', 'arm': 'laser power'}
    F = []
    for line in open(fight_log):
        a = line.split("|")[0].split()
        if len(a) == 2 + len(FEATS) and a[0] == "D": F.append([float(x) for x in a[2:]])
    F = np.array(F[:: max(1, len(F) // 4000)], np.float32)
    flayers = [(net.W[i], net.b[i], i < net.n - 1) for i in range(net.n)]
    fight = fight_net(net, F)
    # ---- the docking network: the curated columns, the routes between them
    DP.load()
    X = []
    for line in open(dock_log):
        a = line.split("|")[0].split()
        if len(a) == DF.N + 1: X.append([float(x) for x in a[1:]])
    X = np.array(X[:: max(1, len(X) // 4000)], np.float32)
    dlayers = [(W, b, l.relu) for (W, b), l in zip(DP.W, DP.L)]
    dock = dict(layers=dense([(W, b) for W, b, _ in dlayers]),
                cols=[[t, li, [[DP.IDX[li][n], lab] + ([DP.IDX[li][n[:-3] + "z+"], DP.IDX[li][n[:-3] + "ng"]] if t == "REGIME" and n.endswith("z+o") else [])
                                for n, lab in rows]] for t, li, rows in DP.COLS],
                links=DP.LINKS, scale=scales(dlayers, X), outscale=[32, 8, 40, 1, 1, 1], senses=DF.NAMES)
    json.dump(dict(fight=fight, dock=dock), open(out, "w"), separators=(",", ":"))
    print(f"{out}: {os.path.getsize(out) // 1024} KB")

if __name__ == "__main__":
    if sys.argv[1] == "--add": add(*sys.argv[2:6])
    else: main(*sys.argv[1:4])

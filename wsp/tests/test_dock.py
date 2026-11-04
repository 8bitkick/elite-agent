"""The docking network: the C senses and forward pass (cplayer/wsp_dock.c) against the Python (dock_features,
dock_author) on random states, and a few plain-language checks of what the written network decides."""
import math, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from elite_wsp import dock_author as A, dock_features as D

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
rng = np.random.default_rng(3)


def forward(layers, f):
    h = np.asarray(f, np.float32)
    for i, L in enumerate(layers):
        W, b = L.W(); h = W @ h + b
        if i < len(layers) - 1: h = np.maximum(h, 0)
    return h


def unit(v): return list(v / np.linalg.norm(v))


def random_input():
    P = rng.normal(size=3) * 60000
    st = int(rng.random() < 0.8)
    S = rng.normal(size=3) * rng.uniform(300, 20000)
    if rng.random() < 0.5: S[2] = abs(S[2])
    nv = unit(rng.normal(size=3))
    if rng.random() < 0.5: nv = unit(-S + rng.normal(size=3) * 300)     # the slot facing roughly at us
    rv = np.cross(nv, rng.normal(size=3)); rv = unit(rv)
    col = int(rng.random() < 0.1)
    return dict(P=list(P), altitude=int(rng.integers(0, 256)), station=st, S=list(S), nv=list(nv), rv=list(rv), spin=int(rng.choice([-1, 1])),
                own=int(rng.integers(0, 41)), fsh=int(rng.integers(0, 256)), ash=int(rng.integers(0, 256)), clear=int(rng.random() < 0.8),
                col=col, miss=list(rng.normal(size=3) * 400), jsty=int(rng.integers(1, 256)), up=int(rng.random() < 0.5),
                final=int(rng.random() < 0.3), commit=int(rng.random() < 0.2))


def params():
    """The parameters the compiled-in weights were written with (elite_dock.npz: evolution's, over the written defaults)."""
    z = np.load(os.path.join(ROOT, "wsp", "elite_dock.npz"))
    return dict(zip(z["param_names"], z["param_values"]))


def test_c_matches_python():
    """The compiled-in network: C senses and outputs equal the Python's on 400 random states."""
    exe = os.path.join(tempfile.mkdtemp(), "dock_check")
    subprocess.run(["gcc", "-O2", "-o", exe, os.path.join(HERE, "dock_check.c"), os.path.join(ROOT, "cplayer", "wsp_dock.c"), "-lm"], check=True)
    P = params(); layers = A.build(P)
    ins = [random_input() for _ in range(400)]
    lines = []
    for i in ins:
        lines.append(" ".join(str(x) for x in [*i["P"], i["altitude"], i["station"], *i["S"], *i["nv"], *i["rv"], i["spin"], i["own"], i["fsh"], i["ash"],
                                                 i["clear"], i["col"], *i["miss"], i["jsty"], i["up"], i["final"], i["commit"]]))
    out = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(ins)
    for i, line in zip(ins, out):
        a, b = line.split("|")
        fc = np.array(a.split(), np.float32); oc = [float(x) for x in b.split()]
        fp = D.features(i["P"], i["altitude"], i["station"], i["S"], i["nv"], i["rv"], i["spin"], i["own"], i["fsh"], i["ash"], i["clear"],
                        i["col"], i["miss"], i["jsty"], i["up"], i["final"], i["commit"], G={k: P[k] for k in A.SENSE_KEYS})
        assert np.allclose(fc, fp, atol=2e-5), (fc - fp)
        op = forward(layers, fp)
        assert abs(op[0] - oc[0]) < 0.05 and abs(op[1] - oc[1]) < 0.05, (op, oc)      # (float32 senses through steep tests)
        assert abs(min(40, max(0, op[2])) - oc[2]) < 0.6, (op, oc)
        for k in range(3, 7): assert abs(op[k] - 0.5) < 0.02 or (op[k] > 0.5) == bool(oc[k]), (k, op, oc)


def decide(P, alt, st, S, nv, rv, spin=1, own=30, fsh=200, ash=200, clear=1, col=0, miss=(0, 0, 0), jsty=128, up=1, final=0, commit=0):
    f = D.features(list(P), alt, st, list(S), list(nv), list(rv), spin, own, fsh, ash, clear, col, list(miss), jsty, up, final, commit)
    o = forward(A.build(), f)
    return dict(alpha=o[0], beta=o[1], speed=o[2], jump=o[3] > 0.5, up=o[4] > 0.5, final=o[5] > 0.5, commit=o[6] > 0.5)


def test_decisions():
    """What the written network (the defaults, before evolution) decides, in a few plain situations."""
    # the cruise: full speed at the planet, the jump when it's on the nose, away from it when low
    o = decide([0, 0, 1e5], 200, 0, [0, 0, 0], [0, 0, 1], [0, 1, 0]); assert abs(o["speed"] - 40) < 0.01 and o["jump"] and not o["final"]
    o = decide([math.sin(.35) * 1e5, 0, math.cos(.35) * 1e5], 200, 0, [0, 0, 0], [0, 0, 1], [0, 1, 0]); assert not o["jump"] and abs(abs(o["alpha"]) - 16) < 0.01
    o = decide([0, 0, 1e5], 30, 0, [0, 0, 0], [0, 0, 1], [0, 1, 0]); assert abs(o["speed"] - 20) < 0.01 and o["beta"] < 0 and not o["jump"]
    # a collision course: the swerve, at 30
    o = decide([0, 0, 1e5], 200, 0, [0, 0, 0], [0, 0, 1], [0, 1, 0], col=1, miss=(300, 0, 0)); assert abs(o["speed"] - 30) < 0.01 and not o["jump"]
    # lined up out in front: the final, the roll at the station's spin, the final's speed band
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 3000], [0, 0, -1], [1, 0, 0]); assert o["final"]
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 3000], [0, 0, -1], [1, 0, 0], final=1); assert abs(o["alpha"] - 16) < 0.01 and abs(o["speed"] - 22) < 0.01 and o["commit"]
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 3000], [0, 0, -1], [1, 0, 0], final=1, spin=-1); assert abs(o["alpha"] + 16) < 0.01
    # the slot tilted: the trim on top of the spin, no commit; committed: 40; far off level and close: the crawl
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 3000], [0, 0, -1], [math.cos(.2), math.sin(.2), 0], final=1); assert abs(o["alpha"] - 24) < 0.01 and not o["commit"]
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 1500], [0, 0, -1], [1, 0, 0], final=1, commit=1); assert abs(o["speed"] - 40) < 0.01
    o = decide([0, 0, 1e5], 200, 1, [0, 0, 500], [0, 0, -1], [math.cos(.5), math.sin(.5), 0], final=1); assert abs(o["speed"] - 1) < 0.01
    # the final with the slot well off the nose: the steering, not the roll match, and slow
    o = decide([0, 0, 1e5], 200, 1, [800, 0, 3000], [0, 0, -1], [1, 0, 0], final=1); assert abs(abs(o["alpha"]) - 16) < 0.01 and abs(o["speed"] - 4) < 0.01
    # behind the station: the way round at full speed, not the final
    o = decide([0, 0, 1e5], 200, 1, [1000, 0, 8000], [0, 0, 1], [0, 1, 0]); assert abs(o["speed"] - 40) < 0.01 and not o["final"]
    # far out in front, off the axis: pitch towards it
    o = decide([0, 0, 1e5], 200, 1, [0, -5000, 20000], [0, 0, -1], [1, 0, 0]); assert o["beta"] < -20 and abs(o["speed"] - 40) < 0.01


if __name__ == "__main__":
    test_c_matches_python(); test_decisions(); print("ok")

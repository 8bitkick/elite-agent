"""The written v2 network against its spec, and the C forward pass against numpy."""
import json, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from elite_wsp import author
from elite_wsp.features import features
from elite_wsp.reference import policy, margin

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rng = np.random.default_rng(1)


def forward(layers, f):
    h = np.asarray(f, np.float32)
    for i, L in enumerate(layers):
        W, b = L.W()
        h = W @ h + b
        if i < len(layers) - 1:
            h = np.maximum(h, 0)
    return h


def random_state():
    near = rng.random() < 0.5
    pos = rng.normal(size=3) * (rng.uniform(200, 3000) if near else rng.uniform(1000, 30000))
    if rng.random() < 0.4:
        pos[2] = abs(pos[2]) * 20                       # often near the nose, where the law is finest
    n = rng.normal(size=3); n /= np.linalg.norm(n)
    sp = rng.uniform(0, 40) * 1.5
    return features(*pos, *(n * sp), rng.uniform(-40, 40), rng.uniform(30, 100), int(rng.integers(0, 256)), int(rng.integers(100, 256)),
                    int(rng.random() < 0.5), int(rng.integers(1, 256)), int(rng.integers(1, 256)),
                    int(rng.random() < 0.6), rng.uniform(0, 2), rng.uniform(0, 0.5), rng.uniform(0, 20))


def check_spec(P=None, n=20000):
    layers = author.build(P)
    worst, tested = np.zeros(4), 0
    for _ in range(n):
        f = random_state()
        if margin(f, P) < 2:
            continue
        got = forward(layers, f)
        want = np.array(policy(f, P))
        got[0] = np.clip(got[0], -31, 31); want[0] = np.clip(want[0], -31, 31)        # the stick's range
        got[1] = np.clip(got[1], -8, 8); want[1] = np.clip(want[1], -8, 8)
        worst = np.maximum(worst, np.abs(got - want)); tested += 1
    return worst, tested


def test_matches_spec():
    worst, n = check_spec()
    assert n > 10000
    assert worst[0] < 0.1 and worst[1] < 0.1, f"rates differ by {worst[:2]}"      # (float32; the stick takes whole units)
    assert worst[2] < 0.01 and worst[3] < 0.05, f"fire/speed differ by {worst[2:]}"
    worst, n = check_spec(dict(lead_guess=0.3, fire_r_guess=0.6, nose_hold=2.0))     # the confidence gates and the nose hold on
    assert n > 10000
    assert worst[0] < 0.1 and worst[1] < 0.1, f"rates differ by {worst[:2]}"      # (float32; the stick takes whole units)
    assert worst[2] < 0.01 and worst[3] < 0.05, f"fire/speed differ by {worst[2:]}"


def test_matches_spec_evolved():
    """The same with the evolved parameters, if there are any."""
    p = os.path.join(ROOT, "wsp", "evolve", "es_params.json")
    if not os.path.exists(p):
        return
    worst, n = check_spec(json.load(open(p)))
    assert worst[0] < 0.1 and worst[1] < 0.1 and worst[2] < 0.01 and worst[3] < 0.05, worst     # (float rounding; the stick takes whole steps)


def test_fire():
    W = author.build()
    assert forward(W, features(0, 0, 5000, 0, 0, 30, 30, 80, 0))[2] > 0.5      # dead ahead, cool: fire
    assert forward(W, features(0, 0, 5000, 0, 0, 30, 30, 80, 250))[2] < 0.5    # hot: hold
    assert forward(W, features(400, 0, 5000, 0, 0, 30, 30, 80, 0))[2] < 0.5    # 400 > 1.431 x 80 off the nose: hold
    assert forward(W, features(0, 0, -5000, 0, 0, 30, 30, 80, 0))[2] < 0.5     # behind


def test_roll_and_pitch_directions():
    W = author.build()
    a, b, _, _ = forward(W, features(500, 2000, 5000, 0, 0, 0, 30, 80))     # up and to the right
    assert a < 0 and b > 0                                     # roll it into the vertical plane, pull up
    a, b, _, _ = forward(W, features(500, -2000, 5000, 0, 0, 0, 30, 80))    # down and to the right
    assert a > 0 and b < 0
    a, b, _, _ = forward(W, features(0, 100, -5000, 0, 0, 0, 30, 80))       # behind, above
    assert b > 0


def test_c_matches_numpy():
    """The C forward pass on every decision of a real fight, if the lab and a recorded fight are here."""
    lab = os.path.join(ROOT, "cplayer", "lab_rl")
    fights = [l.strip() for l in open(os.path.join(ROOT, "wsp", "evolve", "sets", "val.lst"))]
    if not os.path.exists(lab) or not os.path.exists(os.path.join(ROOT, fights[0])):
        return
    from elite_wsp.export import arrays, save_wsp
    layers = author.build()
    d = tempfile.mkdtemp()
    save_wsp(arrays(layers), os.path.join(d, "w.wsp")); open(os.path.join(d, "one.lst"), "w").write(fights[0] + "\n")
    env = dict(os.environ, WSP="1", WSPW=os.path.join(d, "w.wsp"), WSPLOG=os.path.join(d, "log"))
    subprocess.run([lab, os.path.join(d, "one.lst")], cwd=ROOT, env=env, capture_output=True, timeout=600)
    n = bad = 0
    for line in open(os.path.join(d, "log")):
        if not line.startswith("D "):
            continue
        a, b, _ = line[2:].split("|")
        f = np.array([float(v) for v in a.split()[1:]], np.float32)
        o = forward(layers, f)
        alpha, beta, fire = [float(v) for v in b.split()[:3]]
        n += 1
        if abs(np.clip(o[0], -31, 31) - alpha) > 0.5 or abs(np.clip(o[1], -8, 8) - beta) > 0.5 or (o[2] > 0.5) != (fire > 0.5):
            bad += 1
    assert n > 500 and bad < n * 0.01, f"{bad} of {n} decisions differ"


if __name__ == "__main__":
    for t in [v for k, v in list(globals().items()) if k.startswith("test_")]:
        t(); print("ok", t.__name__)

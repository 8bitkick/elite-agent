"""Weight-space programming: the dogfight law written straight into an MLP.

No data, no loss, no gradients. Each hidden unit is named and its incoming
weights are written by hand from reference.py's spec. The primitives are the
ones a ReLU network offers natively:

  relu(u), relu(-u)                      the two halves of a signed value; |u| is their sum
  sat(u) = relu(S*u) - relu(S*u - 1)     a step 0..1 (S sharp): a boolean from a comparison
  relu(v + M*g - M)                      v gated by boolean g (|v| < M): "v if g else 0"
  relu(a + b - 1)                        AND of two booleans

Run as a script to write elite_pilot.npz.
"""
import numpy as np

from .features import NAMES as FEATURES

S = 1000.0     # step sharpness: a comparison is decided within 1/S of a scaled unit (4 game units; sharper loses float32 precision)
M = 32.0       # gate headroom: positions are clipped to ±8 scaled, aim points to a little more

ROLL_GAIN, PITCH_GAIN, LEAD_C = 593.0, 1362.0, 4.5 / 64.0
FIRE_R, FIRE_ZMAX, HOT, FAR, MID, STICK = 1.431, 30000 / 4096.0, 0.85, 2.0, 0.5, 127.0
ROLL_KICK, PITCH_KICK, KICK_AT = 32.0, 36.0, 0.002
BAND_GAIN = (3.0, 1.0, 0.3)   # 1/z at each band's typical range (0.3, 1, 3.5 scaled): the gains act on the angle   # the stick's dead zone (jstx_for, jsty_for), and the error that earns the kick


class Layer:
    """A ReLU layer whose units are written one at a time by name."""

    def __init__(self, inputs, relu=True):
        self.inputs, self.relu = list(inputs), relu
        self.names, self.rows, self.biases, self.notes = [], [], [], []

    def unit(self, name, weights, bias=0.0, note=""):
        row = np.zeros(len(self.inputs))
        for src, w in weights.items():
            row[self.inputs.index(src)] += w
        self.names.append(name); self.rows.append(row); self.biases.append(bias); self.notes.append(note)
        return name

    def passthrough(self, name):
        """The same value one layer on (relu of a non-negative value is itself)."""
        return self.unit(name, {name: 1.0}, note="carried forward")

    def sat_pair(self, name, weights, bias, note=""):
        """sat(u) = relu(S u) - relu(S u - 1): two units; combine with sat_ref()."""
        w = {k: S * v for k, v in weights.items()}
        self.unit(name + "+", w, S * bias, note)
        self.unit(name + "-", w, S * bias - 1.0, note + " (the saturating half)")
        return name

    def W(self):
        return np.array(self.rows, dtype=np.float32), np.array(self.biases, dtype=np.float32)


def sat_ref(name, scale=1.0):
    return {name + "+": scale, name + "-": -scale}


def build():
    # ------------------------------------------------------------------ layer 1: decode the geometry
    L1 = Layer(FEATURES)
    aim = {"x": {"tx": 1, "vx": LEAD_C}, "y": {"ty": 1, "vy": LEAD_C}, "z": {"tz": 1, "vz": LEAD_C, "own": -LEAD_C}}
    for k, w in aim.items():                      # the lead aim point, split into its two signed halves
        L1.unit(f"a{k}+", w, note=f"aim {k}, positive part")
        L1.unit(f"a{k}-", {s: -v for s, v in w.items()}, note=f"aim {k}, negative part")
    for k in "xy":                                 # the target's actual lateral position, for the fire test
        L1.unit(f"{k}+", {f"t{k}": 1}, note=f"target {k}, positive part")
        L1.unit(f"{k}-", {f"t{k}": -1}, note=f"target {k}, negative part")
    L1.sat_pair("up", aim["y"], 0.0, "target above the nose (after lead)")
    L1.sat_pair("behind", {s: -v for s, v in aim["z"].items()}, 0.0, "target behind us (after lead)")
    L1.sat_pair("zpos", {"tz": 1}, 0.0, "in front")
    L1.sat_pair("znear", {"tz": -1}, FIRE_ZMAX, "within laser range")
    L1.sat_pair("cool", {"hot": -1}, HOT, "laser cool enough to fire")
    L1.sat_pair("mid", {"tz": 1}, -MID, "throttle band: past MID")
    L1.sat_pair("far", {"tz": 1}, -FAR, "throttle band: past FAR")
    L1.unit("fr", {"r": FIRE_R / 16.0}, note="hit radius in position units")
    L1.unit("seen", {"seen": 1}, note="a target exists")

    # ------------------------------------------------------------------ layer 2: roll, pitch, fire geometry
    L2 = Layer(L1.names)
    UP = sat_ref("up")
    g_on = {k: M * v for k, v in UP.items()}; g_on_b = -M        # gate: v + M*up - M
    g_off = {k: -M * v for k, v in UP.items()}; g_off_b = 0.0    # gate: v - M*up
    # alpha = -ax when the target is above, +ax when below (each as a positive half)
    L2.unit("roll_up+", {"ax-": 1, "ax+": -1, **g_on}, g_on_b, "relu(-ax) if up")
    L2.unit("roll_up-", {"ax+": 1, "ax-": -1, **g_on}, g_on_b, "relu(+ax) if up")
    L2.unit("roll_dn+", {"ax+": 1, "ax-": -1, **g_off}, g_off_b, "relu(+ax) if below")
    L2.unit("roll_dn-", {"ax-": 1, "ax+": -1, **g_off}, g_off_b, "relu(-ax) if below")
    # beta = relu(ay - |ax|) - relu(-ay - |ax|): pitch towards the target, eased while the roll error is large
    L2.unit("pitch_up", {"ay+": 1, "ay-": -1, "ax+": -1, "ax-": -1}, note="relu(ay - |ax|)")
    L2.unit("pitch_dn", {"ay-": 1, "ay+": -1, "ax+": -1, "ax-": -1}, note="relu(-ay - |ax|)")
    # behind us: pitch hard on the chosen side (AND of two booleans)
    L2.unit("behind_up", {**sat_ref("behind"), **UP, "seen": 1}, -2.0, "behind AND up AND seen")
    L2.unit("behind_dn", {**sat_ref("behind"), **{k: -v for k, v in UP.items()}, "seen": 1}, -1.0, "behind AND NOT up AND seen")
    # fire geometry: |x| < fr, |y| < fr as booleans
    L2.sat_pair("in_x", {"fr": 1, "x+": -1, "x-": -1}, 0.0, "|x| inside the hit radius")
    L2.sat_pair("in_y", {"fr": 1, "y+": -1, "y-": -1}, 0.0, "|y| inside the hit radius")
    for n in ["zpos+", "zpos-", "znear+", "znear-", "cool+", "cool-", "mid+", "mid-", "far+", "far-", "seen"]:
        L2.passthrough(n)

    # ------------------------------------------------------------------ layer 3: the fire decision; the rest carried
    L3 = Layer(L2.names)
    L3.unit("fire", {**sat_ref("in_x"), **sat_ref("in_y"), **sat_ref("zpos"), **sat_ref("znear"), **sat_ref("cool"), "seen": 1},
            -5.0, "AND of six booleans")
    for n in ["behind_up", "behind_dn", "mid+", "mid-", "far+", "far-", "seen"]:
        L3.passthrough(n)
    # the gains should act on the angle, not the lateral offset: 1/z, as three range bands (forward pass 2 on the
    # real game: full stick at the slightest error far out, too gentle close in). Each steering term gated by a band:
    # near = NOT mid, middle = mid AND NOT far, far = far
    MID_, FAR_ = sat_ref("mid"), sat_ref("far")
    for n in ["roll_up+", "roll_up-", "roll_dn+", "roll_dn-", "pitch_up", "pitch_dn"]:
        L3.unit(n + "@near", {n: 1, **{k: -M * v for k, v in MID_.items()}}, 0.0, f"{n} if z < MID")
        L3.unit(n + "@mid", {n: 1, **{k: M * v for k, v in MID_.items()}, **{k: -M * v for k, v in FAR_.items()}}, -M, f"{n} if MID < z < FAR")
        L3.unit(n + "@far", {n: 1, **{k: M * v for k, v in FAR_.items()}}, -M, f"{n} if z > FAR")
    # Elite's stick has a dead zone (jstx_for/jsty_for never put it under 32 off centre), so any error at all
    # gets a kick past it: a step on each signed steering term (forward pass 1 on the real game: 6 deg off, 7% hold)
    for n in ["roll_up+", "roll_up-", "roll_dn+", "roll_dn-", "pitch_up", "pitch_dn"]:
        L3.sat_pair("on_" + n, {n: 1}, -KICK_AT, f"{n} is non-zero")

    # ------------------------------------------------------------------ output: linear, read by the stick
    OUT = Layer(L3.names, relu=False)
    def banded(n, g):
        return {n + "@near": g * BAND_GAIN[0], n + "@mid": g * BAND_GAIN[1], n + "@far": g * BAND_GAIN[2]}
    OUT.unit("jstx", {**banded("roll_up+", ROLL_GAIN), **banded("roll_up-", -ROLL_GAIN), **banded("roll_dn+", ROLL_GAIN), **banded("roll_dn-", -ROLL_GAIN),
                      **sat_ref("on_roll_up+", ROLL_KICK), **sat_ref("on_roll_up-", -ROLL_KICK),
                      **sat_ref("on_roll_dn+", ROLL_KICK), **sat_ref("on_roll_dn-", -ROLL_KICK)},
             128.0, "roll stick: 128 + ROLL_GAIN * alpha / range band, kicked past the dead zone")
    OUT.unit("jsty", {**banded("pitch_up", -PITCH_GAIN), **banded("pitch_dn", PITCH_GAIN), "behind_up": -STICK, "behind_dn": STICK,
                      **sat_ref("on_pitch_up", -PITCH_KICK), **sat_ref("on_pitch_dn", PITCH_KICK)},
             128.0, "pitch stick: 128 - PITCH_GAIN * beta / range band, kicked past the dead zone (nose up is < 128)")
    OUT.unit("fire", {"fire": 1.0}, 0.0, "press the laser when > 0.5")
    OUT.unit("speed", {"seen": -25.0, **sat_ref("mid", 10.0), **sat_ref("far", 15.0)}, 40.0, "40 alone; 15/25/40 by range")
    return [L1, L2, L3, OUT]


def save(path="elite_pilot.npz"):
    layers = build()
    arrays, meta = {}, {}
    for i, L in enumerate(layers):
        W, b = L.W()
        arrays[f"W{i}"], arrays[f"b{i}"] = W, b
        meta[f"names{i}"] = np.array(L.names)
        meta[f"notes{i}"] = np.array(L.notes)
    np.savez(path, **arrays, **meta, features=np.array(FEATURES))
    n = sum(W.size + b.size for W, b in (L.W() for L in layers))
    return path, [L.W()[0].shape for L in layers], n


if __name__ == "__main__":
    path, shapes, n = save()
    print(f"wrote {path}: layers {' -> '.join(str(s[1]) for s in shapes)} -> {shapes[-1][0]}, {n} weights, 0 trained")

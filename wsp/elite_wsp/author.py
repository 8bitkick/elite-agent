"""The v2 pilot, written unit by unit. Every constant is a named parameter (PARAMS); evolution moves them.

The law is the C pilot's steer() and throttle, re-expressed for a ReLU network over the senses
(features.py), with the hands (the stick maps) left to wsp_pilot.c:

  roll   alpha = -roll_gain * s * min(|lead x| / roll_ramp, roll_cap * |cx|) * sign      s = +1 above, -1 below
  pitch  beta  =  pitch_gain * relu(s * lead y - pitch_cone * |lead x|) * s  +  back_gain when far off the nose
  fire   off angle < fire_r * angular radius, ahead, in range, laser cool
  speed  the C's bands: stop to turn, then by range

Run as a script to write elite_pilot.npz, elite_pilot.wsp (run-time) and cplayer/pilot_weights.h.
"""
import math, sys
from .features import NAMES as FEATURES
from .layers import Layer, sat_ref, neg, add

# the defaults are the C pilot's own tuned numbers (combat.c, g_cp) translated to these units
PARAMS = dict(
    lead=4.5,                 # game loops ahead to aim
    roll_gain=256 / 1.726,    # roll rate a radian of roll error (steer: 256 / roll_iters)
    roll_ramp=0.06,           # rad: within this of the nose the roll eases off with the lateral offset
    roll_cap=1.0,             # the roll error's cap as clock-x
    pitch_gain=256 / 3.007,   # pitch rate a radian (steer: 256 / pitch_iters)
    pitch_cone=1.0,           # pitch eases off as |lead x| grows (steer: cos^3 of the roll error)
    back_gain=8.0,            # pitch hard when the target is far off the nose and near the vertical plane
    back_cone=1.0,
    acq_th=0.5,               # "far off": off angle / pi (0.5: behind, as the C law has it)
    # fair mode: what to do when the senses are unsure (features hseen, age, rsd, vsd; all "sure" in efficient mode)
    nose_hold=0.0,            # inside this many hit radii of the nose, no roll: pitch alone brings it in (steer's g_nose_hold)
    lead_guess=1.0,           # the lead's share when the heading is guessed from motion (1: as seen; 0: aim at the dot)
    fire_r_guess=1.0,         # the hit radius's scale when the type is a stand-in (the heading guessed)
    hyst=0.34,                # above/below kept on the side the stick is pitching (steer: 0.35 rad)
    fire_r=1.431,             # hit radii (combat.c fire_r)
    fire_range=30000 / 4096,
    hot=224 / 255,            # laser temperature to hold fire above
    turn_off=15 / 180,        # off angle (th units) past which it turns rather than flies
    spd_turn=0.0,
    near=1000 / 4096, mid=1800 / 4096, far=4000 / 4096,
    spd_close=0.0, spd_near=1.0, spd_mid=31.0, spd_far=27.0,
)
M = 4.0                       # gate headroom (the gated values are under roll_cap + 1)


def build(P=None):
    P = {**PARAMS, **(P or {})}
    # ------------------------------------------------------------------ layer 1: read the sightline
    L1 = Layer(FEATURES)
    lead = P["lead"] / 16                                   # wx, wy are rad a loop x 16
    lg = P["lead_guess"]
    for k in "xy":
        # the lead when the heading is seen; a share lead_guess of it when guessed: lead x (lg + (1 - lg) hseen) x w
        # = lg lead w + (1 - lg) lead (hseen AND w): the product by a gate, split into its halves (w's halves x hseen)
        L1.unit(f"l{k}+", {f"u{k}": 1, f"w{k}": lg * lead}, note=f"lead aim {k} on the view, positive half (the guessed-heading share)")
        L1.unit(f"l{k}-", {f"u{k}": -1, f"w{k}": -lg * lead}, note=f"lead aim {k} on the view, negative half")
        if lg < 1:
            L1.unit(f"hw{k}+", {f"w{k}": (1 - lg) * lead, "hseen": 8.0}, -8.0, f"the rest of the lead {k}, positive half, only when the heading is seen")
            L1.unit(f"hw{k}-", {f"w{k}": -(1 - lg) * lead, "hseen": 8.0}, -8.0, f"the rest of the lead {k}, negative half, only when the heading is seen")
        L1.unit(f"c{k}+", {f"c{k}": 1}, note=f"clock {k}, positive half")
        L1.unit(f"c{k}-", {f"c{k}": -1}, note=f"clock {k}, negative half")
    L1.sat_pair("up", {"cy": 1, "jy": -P["hyst"]}, 0.0, "target above the nose (kept while the stick pitches that way)")
    L1.sat_pair("offnose", {"th": 1}, -P["acq_th"], "target far off the nose: acquire it")
    L1.sat_pair("aligned", {"th": -1}, P["turn_off"], "within turn_off of the nose")
    for b in ["near", "mid", "far"]:
        L1.sat_pair("d" + b, {"dist": 1}, -P[b], f"range past {b}")
    frg = P["fire_r_guess"]
    # the fire cone: th < fire_r rho/(16 pi) x (frg + (1 - frg) hseen); the hseen product as a gated pair one layer on
    L1.unit("fcone", {"rho": frg * P["fire_r"] / (16 * math.pi)}, note="the fire cone, the guessed-type share")
    L1.unit("fcone_h", {"rho": (1 - frg) * P["fire_r"] / (16 * math.pi), "hseen": 8.0}, -8.0, "the rest of the fire cone, only when the type is seen")
    L1.unit("th", {"th": 1}, note="off angle, carried")
    L1.sat_pair("outhold", {"th": 1, "rho": -P["nose_hold"] / (16 * math.pi)}, 0.0, "outside nose_hold hit radii of the nose: roll allowed", S=20000.0)
    L1.sat_pair("ahead", {"uz": 1}, 0.0, "target ahead")
    L1.sat_pair("inrange", {"dist": -1}, P["fire_range"], "within laser range")
    L1.sat_pair("cool", {"hot": -1}, P["hot"], "laser cool enough")
    L1.unit("seen", {"seen": 1}, note="a target")

    # ------------------------------------------------------------------ layer 2: roll magnitude, pitch, behind, ANDs
    L2 = Layer(L1.names)
    r0, cap = P["roll_ramp"], P["roll_cap"]
    L2.passthrough("cx+", "cx-")
    LX = {"lx+": 1, "lx-": -1}; LY = {"ly+": 1, "ly-": -1}       # the signed aim point, as read from its halves
    if lg < 1:
        LX = add(LX, {"hwx+": 1, "hwx-": -1}); LY = add(LY, {"hwy+": 1, "hwy-": -1})
    L2.unit("lx+", LX, note="aim x, positive half (lead by confidence)"); L2.unit("lx-", neg(LX), note="aim x, negative half")
    L2.passthrough("up+", "up-")
    # |aim x| = relu(x) + relu(-x), from the signed value through two of this layer's own halves: built here as a pair
    L2.unit("ax+", LX, note="|aim x|, positive half"); L2.unit("ax-", neg(LX), note="|aim x|, negative half")
    L2.unit("ly+", LY, note="aim y, positive half"); L2.unit("ly-", neg(LY), note="aim y, negative half")
    # (pitch needs |aim x| and aim y in one unit: one layer on, so the pitch units move to layer 3 with the roll)
    absc = {"cx+": P["back_cone"], "cx-": P["back_cone"]}
    L2.unit("b_up", add({"cy+": 1, "cy-": -1}, neg(absc), sat_ref("offnose", 2.0)), -2.0, "far off, above: pull up hard")
    L2.unit("b_dn", add({"cy-": 1, "cy+": -1}, neg(absc), sat_ref("offnose", 2.0)), -2.0, "far off, below: push down hard")
    L2.passthrough("aligned+", "aligned-")
    for b in ["near", "mid", "far"]:
        L2.unit("al_" + b, add(sat_ref("aligned"), sat_ref("d" + b)), -1.0, f"aligned AND range past {b}")
    L2.sat_pair("fin", {"fcone": 1, "fcone_h": 1, "th": -1}, 0.0, "nose inside the fire cone", S=20000.0)
    L2.passthrough("ahead+", "ahead-", "inrange+", "inrange-", "cool+", "cool-", "seen", "outhold+", "outhold-")

    # ------------------------------------------------------------------ layer 3: the roll, on the side chosen
    L3 = Layer(L2.names)
    UP = sat_ref("up")
    for s in "+-":
        L3.unit("over" + s, {"lx" + s: 1 / r0, "cx" + s: -cap}, note=f"how far aim x/roll_ramp exceeds the clock cap ({s})")
    absx = {"ax+": 1, "ax-": 1}
    L3.unit("p_up", add({"ly+": 1, "ly-": -1}, neg(absx, P["pitch_cone"])), note="relu(aim y - cone*|aim x|): pitch up onto it")
    L3.unit("p_dn", add({"ly-": 1, "ly+": -1}, neg(absx, P["pitch_cone"])), note="relu(-aim y - cone*|aim x|): pitch down onto it")
    L3.unit("fire", add(sat_ref("fin"), sat_ref("ahead"), sat_ref("inrange"), sat_ref("cool"), {"seen": 1}), -4.0,
            "AND of five: in the sights, ahead, in range, cool, a target")
    L3.passthrough("lx+", "lx-", "up+", "up-", "b_up", "b_dn", "aligned+", "aligned-", "al_near", "al_mid", "al_far", "outhold+", "outhold-")
    L4 = Layer(L3.names)
    OH = sat_ref("outhold")
    for s in "+-":
        m = {"lx" + s: 1 / r0, "over" + s: -1}                         # min(|aim x|/ramp, cap*|cx|)
        # gated twice: by the side (up / below) and by being outside the nose hold: v + M(up) + M(outhold) - 2M
        L4.unit("roll_up" + s, add(m, {k: M * v for k, v in UP.items()}, {k: M * v for k, v in OH.items()}), -2 * M, f"roll magnitude ({s}) if above, outside the nose hold")
        L4.unit("roll_dn" + s, add(m, {k: -M * v for k, v in UP.items()}, {k: M * v for k, v in OH.items()}), -M, f"roll magnitude ({s}) if below, outside the nose hold")
    L4.passthrough("p_up", "p_dn", "b_up", "b_dn", "aligned+", "aligned-", "al_near", "al_mid", "al_far", "fire")
    L3_ = L3; L3 = L4

    # ------------------------------------------------------------------ output: rates, fire, speed
    OUT = Layer(L4.names, relu=False)
    g = P["roll_gain"]
    OUT.unit("alpha", {"roll_up+": -g, "roll_up-": g, "roll_dn+": g, "roll_dn-": -g}, note="roll rate (stick: jstx_for)")
    OUT.unit("beta", {"p_up": P["pitch_gain"], "p_dn": -P["pitch_gain"], "b_up": P["back_gain"], "b_dn": -P["back_gain"]},
             note="pitch rate, + nose up (stick: jsty_for)")
    OUT.unit("fire", {"fire": 1.0}, note="press the laser when > 0.5")
    st, sc, sn, sm, sf = P["spd_turn"], P["spd_close"], P["spd_near"], P["spd_mid"], P["spd_far"]
    OUT.unit("speed", add({k: sc - st for k in []}, {"aligned+": sc - st, "aligned-": -(sc - st), "al_near": sn - sc,
                                                       "al_mid": sm - sn, "al_far": sf - sm}), st,
             "stop to turn; aligned, by range")
    return [L1, L2, L3_, L4, OUT]


if __name__ == "__main__":
    from .export import save_all
    print(save_all(build()))

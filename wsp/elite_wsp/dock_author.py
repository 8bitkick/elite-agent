"""The docking network, written unit by unit: hyperspace exit to docked as one small network over the senses
(dock_features.py), in the dogfight network's style (author.py). Every constant is a named parameter (PARAMS);
evolution moves them.

What it decides each game loop (the hands, the stick maps, stay wsp_dock.c's):

  where to aim   one of five sightlines the senses give, by where we are: the planet (or away from it, low); behind
                 the station, the side mark (the way round); in front, the approach mark on the slot's axis; lined
                 up, the slot; on a collision course, the escape heading. Each sightline is let through by a gate
                 that clips it (min(u, g)), so at a regime's edge two aims add and the steering flies their mean
  the steering   the dogfight network's law on that aim: roll it into the vertical plane (above or below, with
                 hysteresis), pitch onto it; the roll held to the station's spin rate (16)
  the final      lined up: the roll at the station's spin, trimmed to level the slot; the pitch onto the slot's drawn
                 centre through Elite's rotation (y - a x, a the roll), with the sideways hold
  the speed      the cruise's 40 (20 low); the way round's 40; the approach's by the room left for the turn still
                 needed; the final's bands by range, 40 once committed, a crawl with the slot far from level
  the bits       the jump (the planet within jump_off of the nose), the final, the commit; the steering's above/below

Layers: 1 the halves and the tests; 2 the gates' clips and the regime units; 3 the aim and its statistics; 4 the rates
gated; out. Run as a script to write elite_dock.npz / .wsp and cplayer/dock_weights.h.
"""
import math
from .dock_features import NAMES as FEATURES, U, SENSES
from .layers import Layer, sat_ref, neg, add

PARAMS = dict(
    **SENSES,                              # the marks' geometry (the senses')
    # where to aim: the regimes' edges (ship units) and the width each is decided over
    side_k0=2000.0, side_c=1.8, side_w=500.0,   # the way round (the side mark) until along > side_k0 + side_c lat: room for the approach to settle onto the axis
    le_frac=0.08, le_c=60.0, le_w=20.0,    # lined up (to start the final): lat < le_frac along + le_c
    le_lead=150.0, le2_w=300.0,            # the slot's sightline opens this much before, over this width (the aim slides onto it)
    ls_frac=0.4, ls_c=150.0, ls_w=300.0,   # lined up (to stay in the final)
    al_final=1200.0,                       # the final only past this along
    low_alt=60.0, jump_cos=0.9848,         # the cruise: aim away under low_alt; the jump with the planet within acos(jump_cos)
    # the steering (the dogfight network's law)
    roll_gain=256 / 3.0, roll_ramp=0.06, roll_cap=1.0, roll_max=16.0, pitch_gain=256 / 3.0, pitch_cone=1.0,
    back_gain=8.0, back_cone=1.0, hyst=0.34, dead=0.017, dead_w=0.02,
    # the final
    off_deg=10.0,                          # off the slot by more than this: steer onto it rather than hold the roll match
    trim_gain=256 / 25 / 0.20, trim_cap=8.0, a16=16.0, pitch_f=256.0, hold_g=256 * 2.4, hold_cap=27.0, fade_z=500.0, fade_w=200.0,
    # the speeds
    v_cruise=40.0, v_low=20.0, v_evade=30.0, v_side=40.0, v_min=8.0, v_max=40.0, turn_k=2.08, turn_g=19.4, room0=1800.0,
    f_v_in=4.0, f_v_near=9.0, f_v_mid=14.0, f_v_far=22.0, f_r_near=400.0, f_r_mid=1000.0, f_r_far=2500.0,
    v_commit=40.0, v_crawl=1.0, crawl_r=600.0, crawl_lvl=20.0, v_off=4.0, off8=8.0,
    # the commit
    cosa=0.985, cen_deg=4.4, lvl_deg=7.0, shf=100.0, commit_r=1800.0,
)
SIGHTS = {"p": ("px", "py", "pz"), "s": ("sx", "sy", "sz"), "a": ("ax", "ay", "az"), "b": ("bx", "by", "bz"), "e": ("ex", "ey", "ez")}
HALF = {"+": 1.0, "-": -1.0}
# the aims: each a sightline (or its opposite) and the gate that lets it through
AIMS = {"cruise": ("p", 1), "away": ("p", -1), "swerve": ("e", 1), "slot": ("s", 1), "appr": ("a", 1), "side": ("b", 1)}


def gate(L, name, val, pos=(), neg_=(), M=2.0, bias=0.0, note=""):
    """relu(val + M (sum of pos - n_pos) - M sum of neg): val when every pos is 1 and every neg 0. A pos or neg is a
    boolean unit's name or a saturating test's (read through sat_ref)."""
    w = dict(val)
    for p in pos: w = add(w, {k: M * v for k, v in ref(L, p).items()})
    for n in neg_: w = add(w, {k: -M * v for k, v in ref(L, n).items()})
    return L.unit(name, w, bias - M * len(pos), note)


def ref(L, x):
    return sat_ref(x) if x + "+" in L.inputs and x + "-" in L.inputs and x not in L.inputs else {x: 1.0}


def half_name(c, k, s, sign):
    return f"{c}{k}{s}" if sign > 0 else f"{c}{k}{'-' if s == '+' else '+'}"


def build(P=None):
    P = {**PARAMS, **(P or {})}
    u = 1 / U
    # ------------------------------------------------------------------ layer 1: the halves, the tests
    L1 = Layer(FEATURES)
    for c, comps in SIGHTS.items():
        for k, n in zip("xyz", comps):
            for s, sg in HALF.items(): L1.unit(f"{c}{k}{s}", {n: sg}, note=f"sightline {c} {k}, {s} half")
        for k in "xy":
            for s, sg in HALF.items(): L1.unit(f"{c}c{k}{s}", {f"{c}_c{k}": sg}, note=f"sightline {c} clock {k}, {s} half")
    L1.sat_pair("side", {"along": -1, "lat": P["side_c"]}, P["side_k0"] * u, "not yet far enough out in front for the approach to settle: the way round, by the side mark", S=U / P["side_w"])
    L1.sat_pair("le", {"along": P["le_frac"], "lat": -1}, P["le_c"] * u, "lined up: start the final", S=U / P["le_w"])
    L1.sat_pair("le2", {"along": P["le_frac"], "lat": -1}, (P["le_c"] + P["le_lead"]) * u, "nearly lined up: the slot's sightline opens", S=U / P["le2_w"])
    L1.sat_pair("ls", {"along": P["ls_frac"], "lat": -1}, P["ls_c"] * u, "lined up: stay in the final", S=U / P["ls_w"])
    L1.sat_pair("alf", {"along": 1}, -P["al_final"] * u, "out past al_final along the axis", S=U / 100)
    L1.sat_pair("al0", {"along": 1}, 0.0, "in front of the station", S=U / 100)
    L1.sat_pair("low", {"alt": -255}, P["low_alt"], "low over the planet", S=0.2)
    L1.sat_pair("pjump", {"pz": 1}, -P["jump_cos"], "the planet within jump_off of the nose", S=200)
    L1.sat_pair("off10", {"ths": 180}, -P["off_deg"], "the slot more than off_deg off the nose", S=5.0)
    L1.sat_pair("off8", {"ths": 180}, -P["off8"], "the slot more than off8 off the nose", S=0.5)
    for n, r in (("d400", P["f_r_near"]), ("d1000", P["f_r_mid"]), ("d2500", P["f_r_far"])):
        L1.sat_pair(n, {"dist": 1}, -r * u, f"range past {r:.0f}", S=U / 100)
    L1.sat_pair("d600", {"dist": -1}, P["crawl_r"] * u, "inside crawl_r", S=U / 100)
    L1.sat_pair("d1800", {"dist": -1}, P["commit_r"] * u, "inside commit_r", S=U / 100)
    L1.sat_pair("fade", {"dist": 1}, -P["fade_z"] * u, "the sideways hold fades out inside fade_z", S=U / P["fade_w"])
    L1.sat_pair("sq", {"nvo": -180}, math.degrees(math.acos(P["cosa"])), "facing squarely into the slot", S=0.5)
    L1.sat_pair("cen", {"cen": -180}, P["cen_deg"], "the station's centre within cen_deg of the nose", S=1.0)
    L1.sat_pair("fsh", {"fsh": 255}, -P["shf"], "the front shield up", S=0.1)
    for n in ("dx", "dy", "lvl"):
        for s, sg in HALF.items(): L1.unit(f"{n}{s}", {n: sg}, note=f"{n}, {s} half")
    tg, tc = P["trim_gain"], P["trim_cap"]
    for s, sg in HALF.items():
        L1.unit(f"trim{s}", {"lvl": sg * tg}, note=f"the slot-levelling trim, {s} half")
        L1.unit(f"trim{s}c", {"lvl": sg * tg}, -tc, f"the trim past its cap, {s} half")
    # spin x dx: dx's halves let through by the station's roll sign
    L1.unit("sdx_a", {"dx": 1, "spin": 1}, -1, "dx+ with the station rolling +")
    L1.unit("sdx_b", {"dx": -1, "spin": 1}, -1, "dx- with the station rolling +")
    L1.unit("sdx_c", {"dx": 1, "spin": -1}, -1, "dx+ with the station rolling -")
    L1.unit("sdx_d", {"dx": -1, "spin": -1}, -1, "dx- with the station rolling -")
    L1.unit("sp+", {"spin": 1}, note="the station rolling +"); L1.unit("sp-", {"spin": -1}, note="the station rolling -")
    # the turn still needed, past the room to make it: turn_k x (the angle to face into the slot + 2 x the angle to the slot) - (range - room0), thousands
    L1.unit("turn", {"nvo": P["turn_k"] * math.pi, "ths": 2 * P["turn_k"] * math.pi, "dist": -U / 1000}, P["room0"] / 1000, "the turn needed past the room for it (thousands)")
    for n in ("st", "col", "clear", "final", "commit", "jy"): L1.unit(n, {n: 1}, note="carried")
    L1.unit("jy-", {"jy": -1}, note="the stick, negative half")

    # ------------------------------------------------------------------ layer 2: the gates' clips, the regimes
    L2 = Layer(L1.names)
    # each aim's gate g, a linear sum of tests (1 when it is the aim, 0 or less when not): the clip over = relu(u - g),
    # and the aim let through is u - over + relu(-g) = min(u, max(g, 0)) (layer 3 reads all three)
    SIDE, LE2, LS, LOW = sat_ref("side"), sat_ref("le2"), sat_ref("ls"), sat_ref("low")
    GATES = {"cruise": (add({"st": -1, "col": -1}, neg(LOW)), 1.0),
             "away": (add(LOW, {"st": -1, "col": -1}), 0.0),
             "swerve": ({"col": 1, "final": -1}, 0.0),
             "slot": (add(LE2, LS, {"final": 1, "st": 1, "col": -1}), -2.0),
             "appr": (add({"st": 1, "final": -1, "col": -1}, neg(SIDE), neg(LE2)), 0.0),
             "side": (add(SIDE, neg(LE2), {"st": 1, "col": -1, "final": -1}), -1.0)}
    for name, (c, sign) in AIMS.items():
        g, g0 = GATES[name]
        L2.unit(f"{name}ng", neg(g), -g0, f"{name}: how far its gate is below 0 (the clip's excess, taken back)")
        for k in "xyz":
            for s in HALF:
                src = half_name(c, k, s, sign)
                L2.unit(f"{name}{k}{s}", {src: 1}, note=f"{name}: aim {k}, {s} half")
                L2.unit(f"{name}{k}{s}o", add({src: 1}, neg(g)), -g0, f"{name}: aim {k} {s} past its gate")
        for k in "xy":
            for s in HALF:
                src = half_name(c, "c" + k, s, sign)
                L2.unit(f"{name}c{k}{s}", {src: 1}, note=f"{name}: clock {k}, {s} half")
                L2.unit(f"{name}c{k}{s}o", add({src: 1}, neg(g)), -g0, f"{name}: clock {k} {s} past its gate")
    L2.unit("fin_on", {"final": 1, "off10+": -1, "off10-": 1}, note="the final's law: in the final, the slot within off_deg")
    L2.sat_pair("lvl7", {"lvl+": -180, "lvl-": -180}, P["lvl_deg"], "the slot within lvl_deg of level", S=1.0)
    L2.sat_pair("lvl20", {"lvl+": 180, "lvl-": 180}, -P["crawl_lvl"], "the slot more than crawl_lvl off level", S=0.5)
    L2.unit("enter", add(sat_ref("alf"), sat_ref("le")), -1, "lined up out past al_final: start the final")
    L2.unit("stay", add({"final": 1}, sat_ref("al0"), sat_ref("ls")), -2, "in the final, still lined up: stay")
    L2.unit("okp", add(sat_ref("sq"), sat_ref("cen"), sat_ref("fsh"), {"clear": 1, "st": 1, "final": 1}), -5, "in the final: square, centred, shielded, clear")
    L2.unit("keep", add({"commit": 1}, sat_ref("d1800"), {"clear": 1}), -2, "committed, inside commit_r, clear: stay committed")
    L2.unit("jump", add(sat_ref("pjump"), neg(LOW), {"st": -1, "col": -1}), 0, "the in-system jump")
    # the speeds, each let through by its regime's gate as the aims are: v - relu(v - M g) + relu(-M g) = min(v, max(M g, 0))
    def vgate(L, name, val, bias, g, g0, note):
        L.unit(name, val, bias, note)
        L.unit(name + "o", add(val, neg(g, 48)), bias - 48 * g0, note + ", past its gate")
        L.unit(name + "ng", neg(g, 48), -48 * g0, note + ": the gate below 0")
    vgate(L2, "v_cruise", neg(LOW, P["v_cruise"] - P["v_low"]), P["v_cruise"], {"st": -1, "col": -1}, 1.0, "the cruise's speed")
    vgate(L2, "v_swerve", {}, P["v_evade"], {"col": 1, "final": -1}, 0.0, "the swerve's speed")
    vgate(L2, "v_side", {}, P["v_side"], add(SIDE, neg(LE2), {"st": 1, "col": -1, "final": -1}), -1.0, "the way round's speed")
    L2.unit("turn", {"turn": 1}, 0, "the turn still needed past the room")
    bands = {"d400+": P["f_v_near"] - P["f_v_in"], "d400-": -(P["f_v_near"] - P["f_v_in"]), "d1000+": P["f_v_mid"] - P["f_v_near"], "d1000-": -(P["f_v_mid"] - P["f_v_near"]),
             "d2500+": P["f_v_far"] - P["f_v_mid"], "d2500-": -(P["f_v_far"] - P["f_v_mid"])}
    L2.unit("v_bands", add(bands, neg(sat_ref("off8"), 24), {"commit": -24}), 0, "the final's speed over f_v_in by range, unless off the slot or committed")
    L2.unit("v_commit", {"commit": P["v_commit"] - P["f_v_in"]}, 0, "committed: v_commit")
    hg, hc, MH = P["hold_g"], P["hold_cap"], 400.0
    SDX = {"sdx_a": 1, "sdx_b": -1, "sdx_c": -1, "sdx_d": 1}
    for s, sg in HALF.items():
        gate(L2, f"hold{s}", {k: sg * hg * v for k, v in SDX.items()}, ["fade"], [], MH, note=f"the sideways hold (spin x dx), {s} half, outside fade_z")
        gate(L2, f"hold{s}c", {k: sg * hg * v for k, v in SDX.items()}, ["fade"], [], MH, bias=-hc, note=f"the hold past its cap, {s} half")
    for n in ("st", "col", "clear", "final", "commit", "jy", "jy-", "dx+", "dx-", "dy+", "dy-", "sp+", "sp-", "sdx_a", "sdx_b", "sdx_c", "sdx_d",
              "trim+", "trim+c", "trim-", "trim-c", "d600+", "d600-", "side+", "side-", "le2+", "le2-"):
        L2.passthrough(n)

    # ------------------------------------------------------------------ layer 3: the aim and its statistics; the final's rates
    L3 = Layer(L2.names)
    def thru(k, s):     # the halves let through: u - relu(u - g) + relu(-g) = min(u, max(g, 0))
        return add({f"{n}{k}{s}": 1.0 for n in AIMS}, {f"{n}{k}{s}o": -1.0 for n in AIMS}, {f"{n}ng": 1.0 for n in AIMS})
    VX = add(thru("x", "+"), neg(thru("x", "-"))); VY = add(thru("y", "+"), neg(thru("y", "-"))); VZ = add(thru("z", "+"), neg(thru("z", "-")))
    CX = add(thru("cx", "+"), neg(thru("cx", "-"))); CY = add(thru("cy", "+"), neg(thru("cy", "-")))
    ABSX = add(thru("x", "+"), thru("x", "-")); ABSY = add(thru("y", "+"), thru("y", "-")); ABSCX = add(thru("cx", "+"), thru("cx", "-"))
    r0, cap = P["roll_ramp"], P["roll_cap"]
    for s, sg in HALF.items():
        L3.unit(f"rx{s}", {k: sg * v / r0 for k, v in VX.items()}, note=f"aim x / roll_ramp, {s} half")
        L3.unit(f"over{s}", add({k: sg * v / r0 for k, v in VX.items()}, {k: -sg * cap * v for k, v in CX.items()}), note=f"how far aim x / roll_ramp exceeds roll_cap x clock x ({s})")
        L3.unit(f"cy{s}", {k: sg * v for k, v in CY.items()}, note=f"the aim's clock y, {s} half")
    L3.unit("abscx", ABSCX, note="|clock x| of the aim")
    L3.unit("p_up", add(VY, neg(ABSX, P["pitch_cone"])), note="relu(aim y - cone |aim x|): pitch up onto it")
    L3.unit("p_dn", add(neg(VY), neg(ABSX, P["pitch_cone"])), note="relu(-aim y - cone |aim x|): pitch down onto it")
    L3.sat_pair("up", add(CY, {"jy": -P["hyst"], "jy-": P["hyst"]}), 0.0, "the aim above the nose (kept while the stick pitches that way)")
    L3.sat_pair("back", neg(VZ), 0.0, "the aim behind us", S=4)
    L3.sat_pair("live", add(ABSX, ABSY), -P["dead"], "the aim outside the dead band: roll", S=1 / P["dead_w"])
    TRIM = {"trim+": 1, "trim+c": -1, "trim-": -1, "trim-c": 1}
    RM = add({"sp+": P["a16"], "sp-": -P["a16"]}, TRIM)
    HOLD = {"hold+": 1, "hold+c": -1, "hold-": -1, "hold-c": 1}
    PF = add({"dy+": P["pitch_f"], "dy-": -P["pitch_f"]}, {k: -P["a16"] * v for k, v in SDX.items()}, HOLD)
    for s, sg in HALF.items():
        gate(L3, f"rf{s}", {k: sg * v for k, v in RM.items()}, ["fin_on"], [], 40.0, note=f"the final's roll, {s} half")
        gate(L3, f"pf{s}", {k: sg * v for k, v in PF.items()}, ["fin_on"], [], 400.0, note=f"the final's pitch, {s} half")
    L3.unit("crawl", add(sat_ref("d600"), sat_ref("lvl20")), -1, "inside crawl_r with the slot far from level: crawl")
    L3.unit("v_appr0", {"turn": -P["turn_g"]}, P["v_max"] - P["v_min"], "the approach's speed over v_min: by the room for the turn")
    vgate(L3, "v_final", {"v_bands": 1, "v_commit": 1}, P["f_v_in"], add({"st": 1, "final": 1}, sat_ref("le2")), -1.0, "the final's speed (from the slot's sightline opening)")
    for v in ("v_cruise", "v_swerve", "v_side"): L3.unit(v, {v: 1, v + "o": -1, v + "ng": 1}, note=v + " let through")
    for n in ("fin_on", "enter", "stay", "okp", "keep", "jump", "st", "col", "final", "side+", "side-", "le2+", "le2-", "turn"): L3.passthrough(n)
    L3.unit("ok", add({"okp": 1}, sat_ref("lvl7")), -1, "all of the commit's tests")

    # ------------------------------------------------------------------ layer 4: the rates gated by the choice above/below, the bits
    L4 = Layer(L3.names)
    rmax = P["roll_max"] / (P["roll_gain"] * cap)
    for s in HALF:
        m = {f"rx{s}": 1, f"over{s}": -1}        # min(|aim x| / ramp, cap |clock x|)
        gate(L4, f"roll_up{s}", m, ["up", "live"], ["fin_on"], 4.0, note=f"roll magnitude ({s}) if above, outside the dead band, not the final")
        gate(L4, f"roll_dn{s}", m, ["live"], ["up", "fin_on"], 4.0, note=f"roll magnitude ({s}) if below")
        gate(L4, f"roll_up{s}c", m, ["up", "live"], ["fin_on"], 4.0, bias=-rmax, note=f"the roll past roll_max ({s}, above)")
        gate(L4, f"roll_dn{s}c", m, ["live"], ["up", "fin_on"], 4.0, bias=-rmax, note=f"the roll past roll_max ({s}, below)")
    gate(L4, "p_up", {"p_up": 1}, [], ["fin_on"], 4.0, note="pitch up, not in the final's law")
    gate(L4, "p_dn", {"p_dn": 1}, [], ["fin_on"], 4.0, note="pitch down, not in the final's law")
    bc = P["back_cone"]
    L4.unit("b_up", add({"cy+": 1, "cy-": -1, "abscx": -bc}, {k: 2 * v for k, v in sat_ref("back").items()}, {"fin_on": -4}), -2, "far off, above: pull up hard")
    L4.unit("b_dn", add({"cy-": 1, "cy+": -1, "abscx": -bc}, {k: 2 * v for k, v in sat_ref("back").items()}, {"fin_on": -4}), -2, "far off, below: push down hard")
    L4.unit("final_s", {"enter": 1, "stay": 1}, note="enter + stay")
    L4.unit("final_t", {"enter": 1, "stay": 1}, -1, "enter AND stay (the OR's excess)")
    L4.unit("commit_s", {"ok": 1, "keep": 1}, note="ok + keep")
    L4.unit("commit_t", {"ok": 1, "keep": 1}, -1, "ok AND keep")
    L4.unit("v_final", {"v_final": 1, "v_finalo": -1, "v_finalng": 1, "crawl": -(P["f_v_near"] - P["v_crawl"])}, note="the final's speed, the crawl taken off")
    GA = add({"st": 1, "col": -1, "final": -1}, neg(sat_ref("side")), neg(sat_ref("le2")))
    vgate(L4, "v_appr", {"v_appr0": 1}, P["v_min"], GA, 0.0, "the approach's speed")
    for n in ("rf+", "rf-", "pf+", "pf-", "jump", "v_cruise", "v_swerve", "v_side"): L4.passthrough(n)
    L4.passthrough("up+", "up-")

    # ------------------------------------------------------------------ output
    OUT = Layer(L4.names, relu=False)
    g = P["roll_gain"]
    OUT.unit("alpha", {"roll_up+": -g, "roll_up+c": g, "roll_up-": g, "roll_up-c": -g, "roll_dn+": g, "roll_dn+c": -g, "roll_dn-": -g, "roll_dn-c": g, "rf+": 1, "rf-": -1},
             note="roll rate (ALPHA units)")
    OUT.unit("beta", {"p_up": P["pitch_gain"], "p_dn": -P["pitch_gain"], "b_up": P["back_gain"], "b_dn": -P["back_gain"], "pf+": 1, "pf-": -1}, note="pitch rate, + nose up")
    OUT.unit("speed", {"v_cruise": 1, "v_swerve": 1, "v_side": 1, "v_appr": 1, "v_appro": -1, "v_apprng": 1, "v_final": 1}, note="the speed wanted")
    OUT.unit("jump", {"jump": 1}, note="the in-system jump when > 0.5")
    OUT.unit("up", {"up+": 1, "up-": -1}, note="the steering's choice, for the next loop")
    OUT.unit("final", {"final_s": 1, "final_t": -1}, note="in the final (> 0.5), for the next loop")
    OUT.unit("commit", {"commit_s": 1, "commit_t": -1}, note="committed (> 0.5), for the next loop")
    return [L1, L2, L3, L4, OUT]


OUTPUTS = ["alpha", "beta", "speed", "jump", "up", "final", "commit"]
SENSE_KEYS = list(SENSES)

if __name__ == "__main__":
    from .dock_export import main
    print(main())

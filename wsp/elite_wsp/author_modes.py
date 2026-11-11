"""Two more dogfight networks, written for their mode's goal, not from the C rules' laws or numbers.

interceptor (Optimal/efficient): points a minute. Its senses are exact, so it can trust the sky's motion:
  - feed-forward: the stick is moved with the target's motion across the sky, not only towards where it is
  - a hard turn: when the target is far off the nose, full pitch while the roll comes round
  - speed as one continuous law: fast with room to close, easing by the turn still to do and by the closing speed
  - fire: the nose inside the cone, measured to the led aim point, not the target itself

camera (Curious, while the show is on): what a viewer sees. Same senses, a different program:
  - a dead zone: inside it the stick is left alone, so ships drift across the frame instead of sitting dead centre
  - hold the range at which the target is drawn big; don't fly through it
  - a pass: as it looms and closes, aim beside it, so it goes by the window, on whichever side it already is
  - a gentler stick
  - hold fire until the kill will be seen close (sooner in witchspace, where a swarm doesn't wait)

Every constant is a named parameter, chosen from the game's geometry (below) and then left to evolution.
Each builds to the 23 senses and 4 outputs of wsp_pilot.c.
"""
import math
from .features import NAMES as FEATURES
from .layers import Layer, sat_ref, neg, add

M = 6.0     # gate headroom

# rad a loop x 16 in the wx, wy senses; a rate (roll 31, pitch 8 at most) of 1 turns the view about 1/256 rad a loop
INTERCEPTOR = dict(
    lead=3.0,            # game loops ahead to aim
    orbit=0.0,           # a slow roll while on the target (the camera's: the target circles the sights, the picture turns)
    roll_gain=170.0,     # roll rate a radian of error
    roll_ramp=0.05,      # rad: inside this of the nose the roll eases with the offset
    roll_cap=1.5,        # the roll error's cap as clock-x (measured: 1.1 -> 1.5 gained a point; 2.0 and 2.5 gave it back)
    pitch_gain=110.0,    # pitch rate a radian
    pitch_cone=0.6,      # pitch eases as the sideways error grows
    ff_pitch=0.0,        # feed-forward: pitch with the target's motion (measured: it costs points, with and without a nose hold)
    ff_roll=0.0,         # feed-forward: roll with its sideways motion
    hard_th=0.22,        # off angle / pi past which it's a hard turn
    hard_gain=8.0,       # pitch rate in a hard turn
    hard_cone=0.8,       # (not when it's out on the wing: roll first)
    hyst=0.3,            # above/below kept on the side the stick is pitching
    nose_hold=0.0,       # inside this many fire cones of the nose, no roll (measured: any hold makes a limit cycle; the roll must keep going)
    fire_r=1.5,          # hit radii
    fire_range=7.5,      # range / 4096
    hot=0.9,             # laser temperature to hold fire above
    v_th=0.08,           # off angle / pi within which it flies; past it, it stops to turn (14 degrees)
    r_near=0.2, r_mid=0.45, r_far=1.0,             # range / 4096: the bands' edges (800; 1,850; 4,100)
    v_close=0.0, v_near=4.0, v_mid=31.0, v_far=31.0,   # the speed in each band, from inside r_near outwards
    v_turn=0.0,          # the speed while turning
)

CAMERA = dict(
    lead=2.0,
    dead=0.1,            # rad: inside this of the aim the stick is left alone
    orbit=5.0,           # a slow roll while on the target: the picture turns round it
    roll_gain=80.0,      # gentle
    roll_ramp=0.1,
    roll_cap=0.8,
    pitch_gain=40.0,
    pitch_cone=0.8,
    hard_th=0.5,
    hard_gain=8.0,
    hard_cone=0.8,
    hyst=0.3,
    nose_hold=0.0,
    pass_rho=1.8,        # angular radius x 16 at which it counts as looming (inside ~700 for a Cobra)
    pass_k=0.5,          # aim this far aside a unit of looming past pass_rho
    pass_close=0.4,      # (only a fast closer: 26 units a loop)
    fire_r=1.6,
    fire_range=4.0,
    hot=0.9,
    fire_rho=4.0,        # hold fire until it's drawn this big (angular radius x 16): 64 px. Fired at 20 px the beam laser ended most fights on the first approach (with the brake, 1.3 -> 4: close-up 4.3 s a fight to 6.0, longest dull stretch 17 -> 14 s)
    fire_rho_ws=0.6,     # in witchspace, sooner
    v_th=0.1,
    rho_hold=3.0,        # hold the range where it's drawn this big (48 px for a Cobra: ~430 units; measured best of 1.6, 2.2, 3.0)
    v_base=10.0,         # the speed when it's that big (it moves ~20-30: we fall behind, the gap opens, we speed up: a follow)
    v_k=25.0,            # more speed a unit of angular size short of rho_hold: 40 when it's a dot
    v_turn=0.0,
    # witchspace: a swarm of Thargoids that doesn't wait. As first written the camera flew it as any fight and never got
    # one in the sights (the lab: acquired never, 0 of 6 killed, the energy under the bail-out level in 20 s, jumped out).
    # The ws sense switches these on: the error amplified (a sharper stick), the dead zone and the pass off, the hard
    # turn from nearer the nose
    ws_gain=2.5,         # the error x this when sharp (roll 80 -> 200, pitch 40 -> 100 a radian)
    hard_th_ws=0.22,     # the hard turn from here when sharp
    # ... and the same regime when hurt: the test set had the camera, flying every fight, killed in 37 of 317 (the
    # gated one 17): a show fight is flown while the energy lasts; under hurt_e the network fights to finish it
    hurt_e=160.0,        # energy under which it's sharp
    fire_near=0.1,       # range / 4096 inside which the laser is allowed whatever the size (410: a small ship going by close)
    form_rho=1.3,        # formation: drawn this big (20 px) and no longer growing, it won't get bigger: shoot (held for 64 px, a Cobra sat at 24 px unshot for 55 s)
    form_close=0.1,      # (closing under this, units a loop / 64)
    chase_far=1.0,       # 1: a chase only while the target isn't drawn big (0: as first written)
    chase_gain=4.0,      # the error x this on a chase (measured: 2.5 and 4 level on the show meter, 4 ends the chase soonest: fights 42 s to 28): the aim held on the small cone at range (1: none)
    brake_v0=20.0,       # the brake: the speed where the closing rate is what the range asks for
    brake_kd=0.8,        # the closing rate the range asks for (units a loop / 64, a range unit of 4096): 25 units a loop at 2,000
    chase_flat=37.0,     # a chase lost flat out too: at this speed or more (the top is 40) ...
    chase_t=15.0,        # ... and closing too slowly to reach the hold size in this many seconds (16 game loops a second). A
                         # Cobra running at nearly our top speed was trailed at 1,300 for 4 minutes, closing 2 units a loop:
                         # never opening, so never a lost chase by chase_close, and never drawn big
    chase_close=-0.1,    # closing speed (units a loop / 64: opening at 6) under which a target ahead is a chase we don't win: shoot at range. (From +0.15 with the brake on, every braked approach counted as lost: on screen 6 s a fight -> 15 s)
)


def _build(P, camera=False):
    lead = P["lead"] / 16
    L1 = Layer(FEATURES)
    for k in "xy":
        L1.unit(f"a{k}+", {f"u{k}": 1, f"w{k}": lead}, note=f"the aim {k}, led, positive half")
        L1.unit(f"a{k}-", {f"u{k}": -1, f"w{k}": -lead}, note=f"the aim {k}, led, negative half")
        L1.unit(f"c{k}+", {f"c{k}": 1}, note=f"clock {k}, positive half")
        L1.unit(f"c{k}-", {f"c{k}": -1}, note=f"clock {k}, negative half")
        L1.unit(f"w{k}+", {f"w{k}": 1 / 16}, note=f"the sky's motion {k}, rad a loop, positive half")
        L1.unit(f"w{k}-", {f"w{k}": -1 / 16}, note=f"the sky's motion {k}, negative half")
    L1.sat_pair("up", {"cy": 1, "jy": -P["hyst"]}, 0.0, "target above the nose (kept while the stick pitches that way)")
    if camera:
        # sharp: witchspace, or hurt (energy under hurt_e: 10 a unit of the sense, so it's fully on 25 energy under)
        L1.sat_pair("sharp", {"ws": 10.0, "energy": -10.0}, 10.0 * P["hurt_e"] / 255, "witchspace or hurt: fight to finish it (ws weighted to win over full energy)")
    else:
        L1.sat_pair("hard", {"th": 1}, -P["hard_th"], "far off the nose: a hard turn")
    L1.sat_pair("ahead", {"uz": 1}, 0.0, "target ahead")
    L1.sat_pair("inrange", {"dist": -1}, P["fire_range"], "within laser range")
    L1.sat_pair("cool", {"hot": -1}, P["hot"], "laser cool enough")
    L1.unit("seen", {"seen": 1}, note="a target")
    L1.sat_pair("aligned", {"th": -1}, P["v_th"], "within v_th of the nose: fly, don't turn")
    if camera:
        L1.unit("gap", {"rho": -1}, P["rho_hold"], "how far short of the hold size it's drawn")
        # the brake: crossing at full speed puts 4,800 units between us by the time the 5 s turn is done. The speed that
        # keeps the closing rate to what the range asks for, so a pass is slow and the fight stays near
        L1.unit("vd", {"dist": 64 * P["brake_kd"], "close": -64.0}, P["brake_v0"], "the speed the brake allows")
    else:
        for b in ["near", "mid", "far"]:
            L1.sat_pair("d" + b, {"dist": 1}, -P["r_" + b], f"range past r_{b}")
    L1.unit("cone", {"rho": P["fire_r"] / (16 * math.pi)}, note="the fire cone, off-angle units")
    L1.unit("th", {"th": 1}, note="the off angle, carried")
    if camera:
        L1.sat_pair("big", add({"rho": 1}, {"ws": P["fire_rho"] - P["fire_rho_ws"]}), -P["fire_rho"], "drawn big enough to shoot (sooner in witchspace)")
        L1.sat_pair("cl", {"close": 1}, -P["pass_close"], "closing")
        L1.sat_pair("slow", {"close": -1}, P["chase_close"], "not closing (under chase_close)")
        L1.sat_pair("flat", {"own": 1}, -P["chase_flat"] / 64, "flat out (chase_flat or more)")
        # (closing under the range over chase_t seconds: close (units a loop / 64) < dist (/ 4096) x 4096 / (16 chase_t 64))
        L1.sat_pair("crawl", {"close": -1, "dist": 4096 / (16 * P["chase_t"] * 64)}, 0.0, "closing too slowly to get there in chase_t")
        L1.sat_pair("fbig", {"rho": 1}, -P["form_rho"], "big enough for a shot from formation")
        L1.sat_pair("fslow", {"close": -1}, P["form_close"], "no longer growing")
        L1.sat_pair("near", {"dist": -1}, P["fire_near"], "inside fire_near: a pass, whatever it's drawn")
        L1.unit("loom", {"rho": 1}, -P["pass_rho"], "looming: drawn past pass_rho")
    else:
        L1.unit("big+", {"one": 1}, note="(no size gate)")
        L1.unit("big-", {"one": 0}, note="(no size gate)")

    # ------------------------------------------------------------------ the aim point, signed, with the pass beside it
    L2 = Layer(L1.names)
    if camera:
        L2.passthrough("cl+", "cl-", "sharp+", "sharp-", "near+", "near-")
        L2.unit("formed", add(sat_ref("fbig"), sat_ref("fslow")), -1.0, "formation: drawn big and no longer growing")
        # (a chase: ahead, not closing, and not drawn big: holding beside a target at the show's distance isn't closing
        # either, and sharpening the stick on that lost the whole gentle style: validation 27 -> 19)
        L2.unit("stuck", add(sat_ref("ahead"), sat_ref("slow"), neg(sat_ref("big"), P["chase_far"])), -1.0, "a chase we don't win: ahead, not closing, not drawn big (AND: the three sum to 3 - big... - 1)")
        # (the other lost chase: flat out and crawling in. Off whenever stuck is on (slow), so the two never add)
        L2.unit("stuckf", add(sat_ref("ahead"), sat_ref("flat"), sat_ref("crawl"), neg(sat_ref("slow")), neg(sat_ref("big"))), -2.0,
                "a chase we don't win flat out: ahead, flat out, crawling in, not opening (that's stuck), not drawn big")
    L2.passthrough("cx+", "cx-", "cy+", "cy-", "wy+", "wy-", "wx+", "wx-", "up+", "up-")
    if camera:
        L2.sat_pair("hard", {"th": 1, "sharp+": P["hard_th"] - P["hard_th_ws"]}, -P["hard_th"], "far off the nose: a hard turn (from nearer the nose when sharp)")
    else:
        L2.passthrough("hard+", "hard-")
    L2.passthrough(
                   "ahead+", "ahead-", "inrange+", "inrange-", "cool+", "cool-", "seen", "aligned+", "aligned-", "big+", "big-", "th")
    if camera:
        L2.unit("band", {"gap": P["v_k"]}, P["v_base"], "the speed by how big it's drawn: v_base at the hold size, more the smaller")
        L2.unit("bover", {"gap": P["v_k"], "vd": -1, "sharp+": -200.0}, P["v_base"], "how far that's over what the brake allows (no brake when sharp)")
    else:
        vc, vn, vm, vf = P["v_close"], P["v_near"], P["v_mid"], P["v_far"]
        L2.unit("band", add(sat_ref("dnear", vn - vc), sat_ref("dmid", vm - vn), sat_ref("dfar", vf - vm)), vc, "the speed by range band")
    L2.unit("conev", {"cone": math.pi}, note="the cone, rad")
    L2.sat_pair("outhold", {"ax+": 1, "ax-": 1, "ay+": 1, "ay-": 1, "cone": -math.pi * P["nose_hold"]}, 0.0, "outside the nose hold: roll allowed", S=4000.0)
    if camera:
        k, g = P["pass_k"], sat_ref("cl", 2.0)
        L2.unit("px+", add({"loom": k, "cx+": 2.0, "sharp+": -2 * M}, g), -4.0, "a pass: aim aside, on the right, while it looms and closes (not when sharp)")
        L2.unit("px-", add({"loom": k, "cx-": 2.0, "sharp+": -2 * M}, g), -4.0, "a pass: aim aside, on the left, while it looms and closes (not when sharp)")
    for k in "xy":
        L2.unit(f"a{k}+", {f"a{k}+": 1, f"a{k}-": -1}, note=f"aim {k}, positive half")
        L2.unit(f"a{k}-", {f"a{k}-": 1, f"a{k}+": -1}, note=f"aim {k}, negative half")

    # ------------------------------------------------------------------ the error, past the dead zone; the bits' ANDs
    L3 = Layer(L2.names)
    if camera:
        L3.passthrough("cx+", "cx-", "wy+", "wy-", "wx+", "wx-", "up+", "up-", "ay+", "ay-", "aligned+", "aligned-")
        L3.unit("band", {"band": 1, "bover": -1}, note="the speed: the lesser of size's and the brake's")
        L3.unit("bandover", {"band": 1, "bover": -1}, -40.0, "the band past 40 (the game's top speed)")
    else:
        L3.passthrough("cx+", "cx-", "wy+", "wy-", "wx+", "wx-", "up+", "up-", "ay+", "ay-", "band", "aligned+", "aligned-")
        L3.unit("bandover", {"band": 1}, -40.0, "the band past 40 (the game's top speed)")
    if camera:
        dz = P["dead"]
        # a pass pushes the aim the other way from where the target is: x - px on its side
        # (no dead zone when sharp, nor on a chase: at range the fire cone is smaller than the dead zone, and the target
        # has to be centred to be shot)
        L3.unit("ex+", {"ax+": 1, "ax-": -1, "px+": -1, "px-": 1, "sharp+": dz, "stuck": dz, "stuckf": dz}, -dz, "sideways error past the dead zone (right; none when sharp or on a chase)")
        L3.unit("ex-", {"ax-": 1, "ax+": -1, "px-": -1, "px+": 1, "sharp+": dz, "stuck": dz, "stuckf": dz}, -dz, "sideways error past the dead zone (left)")
        L3.unit("ey+", {"ay+": 1, "ay-": -1, "sharp+": dz, "stuck": dz, "stuckf": dz}, -dz, "error past the dead zone (up)")
        L3.unit("ey-", {"ay-": 1, "ay+": -1, "sharp+": dz, "stuck": dz, "stuckf": dz}, -dz, "error past the dead zone (down)")
        # sharp: the error again, x (ws_gain - 1), gated: added to the error below it sharpens the stick
        kw = P["ws_gain"] - 1
        L3.unit("exw+", {"ax+": kw, "ax-": -kw, "sharp+": M}, -M, "sharp: the sideways error's extra (right)")
        L3.unit("exw-", {"ax-": kw, "ax+": -kw, "sharp+": M}, -M, "sharp: the sideways error's extra (left)")
        L3.unit("eyw+", {"ay+": kw, "ay-": -kw, "sharp+": M}, -M, "sharp: the error's extra (up)")
        L3.unit("eyw-", {"ay-": kw, "ay+": -kw, "sharp+": M}, -M, "sharp: the error's extra (down)")
        # a chase: the same extra, gated by stuck (its own units: gated by both in one, the two gates would add a
        # constant to the error when both were on). A shot at range needs the aim held on a small cone
        kc = P["chase_gain"] - 1
        L3.unit("exc+", {"ax+": kc, "ax-": -kc, "stuck": M, "stuckf": M}, -M, "a chase: the sideways error's extra (right)")
        L3.unit("exc-", {"ax-": kc, "ax+": -kc, "stuck": M, "stuckf": M}, -M, "a chase: the sideways error's extra (left)")
        L3.unit("eyc+", {"ay+": kc, "ay-": -kc, "stuck": M, "stuckf": M}, -M, "a chase: the error's extra (up)")
        L3.unit("eyc-", {"ay-": kc, "ay+": -kc, "stuck": M, "stuckf": M}, -M, "a chase: the error's extra (down)")
    else:
        L3.unit("ex+", {"ax+": 1, "ax-": -1}, note="sideways error (right)")
        L3.unit("ex-", {"ax-": 1, "ax+": -1}, note="sideways error (left)")
        L3.unit("ey+", {"ay+": 1, "ay-": -1}, note="error (up)")
        L3.unit("ey-", {"ay-": 1, "ay+": -1}, note="error (down)")
    cc = P["hard_cone"]
    L3.unit("h_up", add({"cy+": 1, "cy-": -1, "cx+": -cc, "cx-": -cc}, sat_ref("hard", 2.0)), -2.0, "far off, above: pull up hard")
    L3.unit("h_dn", add({"cy-": 1, "cy+": -1, "cx+": -cc, "cx-": -cc}, sat_ref("hard", 2.0)), -2.0, "far off, below: push down hard")
    L3.sat_pair("fin", {"conev": 1, "th": -math.pi}, 0.0, "the nose inside the fire cone (the target itself: the laser hits where the nose points now)", S=20000.0)
    L3.passthrough("ahead+", "ahead-", "inrange+", "inrange-", "cool+", "cool-", "seen", "big+", "big-", "outhold+", "outhold-")
    if camera:
        L3.passthrough("cl+", "cl-")
        L3.sat_pair("shoot", add(sat_ref("big"), sat_ref("near"), {"stuck": 1, "stuckf": 1, "formed": 1}), 0.0, "the laser allowed: drawn big, near, in formation, or a chase we don't win (shot at range rather than trailed)")

    # ------------------------------------------------------------------ the roll on the side chosen, pitch, fire
    L4 = Layer(L3.names)
    r0, cap = P["roll_ramp"], P["roll_cap"]
    W = (lambda d: d) if camera else (lambda d: {})       # (the camera's witchspace extras; none in the interceptor)
    for s in "+-":
        L4.unit("over" + s, add({"ex" + s: 1 / r0, "cx" + s: -cap}, W({"exw" + s: 1 / r0, "exc" + s: 1 / r0})), note=f"how far the sideways error / roll_ramp exceeds the clock cap ({s})")
    absx = add({"ex+": 1, "ex-": 1}, W({"exw+": 1, "exw-": 1, "exc+": 1, "exc-": 1}))
    L4.unit("p_up", add({"ey+": 1, "ey-": -1}, W({"eyw+": 1, "eyw-": -1, "eyc+": 1, "eyc-": -1}), neg(absx, P["pitch_cone"])), note="pitch up onto the aim, easing as the sideways error grows")
    L4.unit("p_dn", add({"ey-": 1, "ey+": -1}, W({"eyw-": 1, "eyw+": -1, "eyc-": 1, "eyc+": -1}), neg(absx, P["pitch_cone"])), note="pitch down onto the aim")
    if camera: L4.passthrough("exw+", "exw-", "exc+", "exc-")
    if camera:
        L4.unit("fire", add(sat_ref("fin"), sat_ref("ahead"), sat_ref("inrange"), sat_ref("cool"), sat_ref("shoot"), {"seen": 1}), -5.0,
                "AND of six: in the sights, ahead, in range, cool, drawn big or a chase, a target (no kills at a distance, unless the chase is lost)")
        L4.unit("orbit", add(sat_ref("big"), sat_ref("aligned")), -1.0, "on the target and it's drawn big: the slow roll")
    else:
        L4.unit("fire", add(sat_ref("fin"), sat_ref("ahead"), sat_ref("inrange"), sat_ref("cool"), sat_ref("big"), {"seen": 1}), -5.0,
                "AND of six: in the sights, ahead, in range, cool, big enough, a target")
    L4.unit("spd", add({"band": 1, "bandover": -1}, {k: 40 * v for k, v in sat_ref("aligned").items()}), -40.0 + P["v_turn"], "the speed: min(band, 40) if aligned, else v_turn (stop to turn)")
    L4.passthrough("ex+", "ex-", "up+", "up-", "h_up", "h_dn", "wy+", "wy-", "wx+", "wx-", "outhold+", "outhold-")

    L5 = Layer(L4.names)
    UP, OH = sat_ref("up"), sat_ref("outhold")
    for s in "+-":
        m = add({"ex" + s: 1 / r0, "over" + s: -1}, W({"exw" + s: 1 / r0, "exc" + s: 1 / r0}))      # min(|error|/ramp, cap x |clock x|)
        # gated twice: by the side, and by being outside the nose hold: v + M(up) + M(outhold) - 2M
        L5.unit("roll_up" + s, add(m, {k: M * v for k, v in UP.items()}, {k: M * v for k, v in OH.items()}), -2 * M, f"roll magnitude ({s}) if above, outside the nose hold")
        L5.unit("roll_dn" + s, add(m, {k: -M * v for k, v in UP.items()}, {k: M * v for k, v in OH.items()}), -M, f"roll magnitude ({s}) if below, outside the nose hold")
    L5.passthrough("p_up", "p_dn", "h_up", "h_dn", "wy+", "wy-", "wx+", "wx-", "fire", "spd")
    if camera: L5.passthrough("orbit")

    OUT = Layer(L5.names, relu=False)
    g = P["roll_gain"]; fr = P.get("ff_roll", 0.0) * 256; fp = P.get("ff_pitch", 0.0) * 256
    OUT.unit("alpha", add({"roll_up+": -g, "roll_up-": g, "roll_dn+": g, "roll_dn-": -g, "wx+": -fr, "wx-": fr},
                          {"orbit": P["orbit"]} if camera else {}), note="roll rate")
    OUT.unit("beta", {"p_up": P["pitch_gain"], "p_dn": -P["pitch_gain"], "h_up": P["hard_gain"], "h_dn": -P["hard_gain"], "wy+": fp, "wy-": -fp},
             note="pitch rate, + nose up, with the feed-forward")
    OUT.unit("fire", {"fire": 1.0}, note="press the laser when > 0.5")
    OUT.unit("speed", {"spd": 1.0}, note="the speed")
    return [L1, L2, L3, L4, L5, OUT]


def build_interceptor(P=None):
    return _build({**INTERCEPTOR, **(P or {})}, camera=False)


def build_camera(P=None):
    return _build({**CAMERA, **(P or {})}, camera=True)

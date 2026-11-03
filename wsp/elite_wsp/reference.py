"""The v2 law as plain arithmetic, the spec author.py writes into weights (tests check the two agree)."""
import math
from .author import PARAMS


def relu(v):
    return max(0.0, v)


def policy(f, P=None):
    P = {**PARAMS, **(P or {})}
    ux, uy, uz, wx, wy, th, cx, cy, rho, dist, close, own, hot, energy, ws, jx, jy, seen, one = [float(v) for v in f[:19]]
    hseen = float(f[19]) if len(f) > 19 else 1.0
    lead = P["lead"] / 16 * (P["lead_guess"] + (1 - P["lead_guess"]) * hseen)
    lx, ly = ux + lead * wx, uy + lead * wy
    up = cy - P["hyst"] * jy >= 0
    r0, cap = P["roll_ramp"], P["roll_cap"]
    m = min(relu(lx) / r0, cap * relu(cx)) - min(relu(-lx) / r0, cap * relu(-cx))
    alpha = -P["roll_gain"] * m if up else P["roll_gain"] * m
    if th < P["nose_hold"] * rho / (16 * math.pi):
        alpha = 0.0
    beta = P["pitch_gain"] * (relu(ly - P["pitch_cone"] * abs(lx)) - relu(-ly - P["pitch_cone"] * abs(lx)))
    if th > P["acq_th"]:
        beta += P["back_gain"] * (relu(cy - P["back_cone"] * abs(cx)) - relu(-cy - P["back_cone"] * abs(cx)))
    fr = P["fire_r"] * (P["fire_r_guess"] + (1 - P["fire_r_guess"]) * hseen)
    fire = th < fr * rho / (16 * math.pi) and uz > 0 and dist < P["fire_range"] and hot < P["hot"] and seen > 0.5
    if th >= P["turn_off"]:
        speed = P["spd_turn"]
    elif dist > P["far"]:
        speed = P["spd_far"]
    elif dist > P["mid"]:
        speed = P["spd_mid"]
    elif dist > P["near"]:
        speed = P["spd_near"]
    else:
        speed = P["spd_close"]
    return alpha, beta, float(fire), speed


def margin(f, P=None):
    """Distance to the nearest step: a ReLU step is soft within 1/S of it."""
    P = {**PARAMS, **(P or {})}
    ux, uy, uz, wx, wy, th, cx, cy, rho, dist, close, own, hot, energy, ws, jx, jy, seen, one = [float(v) for v in f[:19]]
    hseen = float(f[19]) if len(f) > 19 else 1.0
    fr = P["fire_r"] * (P["fire_r_guess"] + (1 - P["fire_r_guess"]) * hseen)
    return min(abs(cy - P["hyst"] * jy), abs(th - P["acq_th"]), abs(th - P["turn_off"]), abs(th - fr * rho / (16 * math.pi)) * 20,
               abs(th - P["nose_hold"] * rho / (16 * math.pi)) * 20,
               abs(dist - P["fire_range"]), abs(hot - P["hot"]), *(abs(dist - P[b]) for b in ["near", "mid", "far"])) * 1000

"""The senses of the docking network: what it is told about the approach, hyperspace exit to docked, the same as
cplayer/wsp_dock.c's wsp_dock_features. Coordinate changes only, as the dogfight network's (features.py): the
directions to the things a pilot steers by, as unit sightlines in ship space (x right, y up, z ahead), the station's
geometry as it is drawn, our own state, and the bits carried from the last loop. No constant here decides anything;
the marks on the slot's axis are where the C law's own were.

  0 px  1 py  2 pz     unit sightline to the planet
  3 alt                altitude / 255
  4 own                our speed / 40
  5 st                 1 with the station in sight (the rest of the station's senses are 0 without it)
  6 sx  7 sy  8 sz     unit sightline to the slot's centre (160 out from the station's centre; the centre itself inside 200)
  9 ax 10 ay 11 az     unit sightline to the approach mark: the point on the slot's axis `lead` ahead of us (further
                       when we are far off the axis, so the approach is never steeper than chi), never nearer the
                       station than k_min
 12 bx 13 by 14 bz     unit sightline to the side mark: k_side out from the station, c_side off the axis on our own side
                       (the way round from behind)
 15 ex 16 ey 17 ez     the escape heading from a collision course (4,000 to the side the threat misses on, 1,500 on)
 18 col                1 on a collision course (anything but the station). In the final, how close a ship between us
                       and the station will pass, flying as it flies and we straight for the station: 1 inside 130,
                       0 past 260 (its closest approach; the escape heading then points to the side it passes on)
 19 along              our distance out along the slot's axis / 4096 (negative behind the station)
 20 lat                our distance off the axis / 4096
 21 dist               range / 4096 (to 16)
 22 dx 23 dy           the slot's centre as drawn: x / z and y / z (to 0.5)
 24 lvl                the slot's roll off level, rad (-pi/2..pi/2)
 25 spin               the station's roll: +1 or -1
 26 nvo                the turn to face into the slot: the angle between the nose and the slot's axis / pi
 27 cen                the angle to the station's centre / pi
 28 ths                the angle to the slot's centre / pi
 29 clear              1 with nothing in the way before the slot
 30 fsh 31 ash         shields / 255
 32 jy                 the stick, -1..1 (jy < 0: nose up)
 33 up                 the steering's choice last loop: 1 above, 0 below
 34 final              in the final last loop
 35 commit             committed last loop
 36 one                the bias input
 37-46                 each of the five sightlines' clock position on the screen, (cx, cy), a unit vector, (0, 1) at
                       12 o'clock: p_cx p_cy s_cx s_cy a_cx a_cy b_cx b_cy e_cx e_cy
"""
import math
import numpy as np

NAMES = ["px", "py", "pz", "alt", "own", "st", "sx", "sy", "sz", "ax", "ay", "az", "bx", "by", "bz", "ex", "ey", "ez",
         "col", "along", "lat", "dist", "dx", "dy", "lvl", "spin", "nvo", "cen", "ths",
         "clear", "fsh", "ash", "jy", "up", "final", "commit", "one"] + [c + "_c" + k for c in "psabe" for k in "xy"]
VECS = {"p": 0, "s": 6, "a": 9, "b": 12, "e": 15}       # each sightline's first index
N = len(NAMES)
IDX = {n: i for i, n in enumerate(NAMES)}
U = 4096.0
SLOT, INSIDE = 160.0, 200.0
# the marks' geometry: the senses' own constants (SENSES in the .wsp file; wsp_dock_in_t); evolution moves them too
SENSES = dict(lead=1200.0, chi=60.0, k_min=500.0, k_side=7000.0, c_side=2500.0)


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v] if n > 1e-9 else [0.0, 0.0, 0.0]


def features(P, altitude, station, S, nv, rv, spin, own, fsh, ash, clear, col, miss, jsty, up, final, commit, G=None):
    """P: the planet (ship space); station: the station in sight, with S (its centre), nv (nose, unit), rv (roof, unit),
    spin (its roll sign); col, miss: a collision course and its miss vector; jsty: the stick; up, final, commit: the bits;
    G: the marks' geometry (SENSES)."""
    G = {**SENSES, **(G or {})}
    f = np.zeros(N, dtype=np.float32)
    f[0:3] = unit(P); f[3] = altitude / 255.0; f[4] = own / 40.0; f[5] = 1.0 if station else 0.0
    if station:
        rel = [-s for s in S]
        along = sum(r * n for r, n in zip(rel, nv))
        lv = [r - along * n for r, n in zip(rel, nv)]
        lat = math.sqrt(sum(x * x for x in lv))
        w = [x / lat for x in lv] if lat > 1e-9 else [0.0, 0.0, 0.0]
        dist = math.sqrt(sum(r * r for r in rel))
        slot = [s + (SLOT if along > INSIDE else 0.0) * n for s, n in zip(S, nv)]
        ahead = max(G["lead"], lat / math.tan(math.radians(G["chi"])))
        k = max(G["k_min"], along - ahead)
        f[6:9] = unit(slot)
        f[9:12] = unit([s + k * n for s, n in zip(S, nv)])
        f[12:15] = unit([s + G["k_side"] * n + G["c_side"] * x for s, n, x in zip(S, nv, w)])
        z = max(1.0, slot[2])
        f[19] = along / U; f[20] = lat / U; f[21] = min(dist / U, 16.0)
        f[22] = max(-0.5, min(0.5, slot[0] / z)); f[23] = max(-0.5, min(0.5, slot[1] / z))
        f[24] = math.fmod(math.atan2(rv[1], rv[0]) + math.pi / 2 + 4 * math.pi, math.pi) - math.pi / 2
        f[25] = 1.0 if spin >= 0 else -1.0
        f[26] = math.atan2(math.hypot(nv[0], nv[1]), -nv[2]) / math.pi
        f[27] = math.atan2(math.hypot(S[0], S[1]), S[2]) / math.pi
        f[28] = math.atan2(math.hypot(slot[0], slot[1]), slot[2]) / math.pi
    if col:
        m = math.hypot(miss[0], miss[1])
        mx, my = (miss[0] / m, miss[1] / m) if m > 1 else (0.0, 1.0)
        f[15:18] = unit([mx * 4000, my * 4000, 1500]); f[18] = min(1.0, float(col))
    f[29] = 1.0 if clear else 0.0; f[30] = fsh / 255.0; f[31] = ash / 255.0
    f[32] = (jsty - 128) / 128.0
    f[33] = 1.0 if up else 0.0; f[34] = 1.0 if final else 0.0; f[35] = 1.0 if commit else 0.0
    f[36] = 1.0
    for k, (c, i) in enumerate(VECS.items()):
        h = math.hypot(f[i], f[i + 1])
        f[37 + 2 * k], f[38 + 2 * k] = (f[i] / h, f[i + 1] / h) if h > 1e-6 else (0.0, 1.0)
    return f

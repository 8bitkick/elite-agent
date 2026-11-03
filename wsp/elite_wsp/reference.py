"""The spec the weights implement: the dogfight law, written as plain arithmetic.

This is cplayer/combat.c's combat_keys() + pilot.c's steer() + the fire test,
re-expressed as a piecewise-linear program so that it can be written into a
ReLU network exactly (no atan2, no division, no hypot). Where the C uses an
angle, this uses the lateral offset in ship space; where the C uses cos^3 of
the roll error to ease the pitch, this uses relu(|ay| - |ax|). The tests check
that the signs of every stick command agree with the original angle law.

All quantities are in features.py's scaled units.
"""
import numpy as np

LEAD = 4.5                      # game loops ahead (combat.c: .lead = 4.5)
LEAD_C = LEAD / 64.0            # velocity(/64) -> position(/4096) over LEAD loops
ROLL_GAIN = 593.0               # stick units per scaled lateral x   (steer: 256/1.726 rad^-1, jstx_for ~4x)
PITCH_GAIN = 1362.0             # stick units per scaled lateral y   (steer: 256/3.007 rad^-1, jsty_for ~16x)
FIRE_R = 1.431                  # hit radii (combat.c: .fire_r)
FIRE_ZMAX = 30000 / 4096.0      # combat.c: v.z < 30000
HOT = 0.85                      # laser temperature above which we hold fire
FAR, MID = 2.0, 0.5             # throttle bands (scaled z)
STICK = 127                     # the stick's full throw
ROLL_KICK, PITCH_KICK, KICK_AT = 32.0, 36.0, 0.002
BAND_GAIN = (3.0, 1.0, 0.3)


def policy(f):
    """f: features vector -> (jstx, jsty, fire, speed) as the game wants them."""
    tx, ty, tz, vx, vy, vz, own, energy, fsh, hot, r, one, seen = [float(v) for v in f]
    # 1. the aim point, LEAD loops ahead (we fly on at our own speed along +z)
    ax = tx + LEAD_C * vx
    ay = ty + LEAD_C * vy
    az = tz + LEAD_C * (vz - own)
    # 2. roll the target into our vertical plane: roll rate ∝ -ax, mirrored when it is below us
    up = 1.0 if ay >= 0 else 0.0
    alpha = -ax if up else ax
    # 3. pitch onto it, easing off while the roll error is large; behind us, pitch hard
    behind = 1.0 if az < 0 else 0.0
    beta = max(0.0, ay - abs(ax)) - max(0.0, -ay - abs(ax))
    kick = lambda v, k: k if v > KICK_AT else (-k if v < -KICK_AT else 0.0)     # the stick's dead zone (jstx_for, jsty_for)
    bg = BAND_GAIN[2] if tz > FAR else BAND_GAIN[1] if tz > MID else BAND_GAIN[0]    # the gain as 1/z, in bands
    jstx = ROLL_GAIN * bg * alpha + kick(alpha, ROLL_KICK)
    jsty = -PITCH_GAIN * bg * beta - kick(beta, PITCH_KICK) - STICK * behind * (1.0 if up else -1.0)
    jstx = 128 + np.clip(jstx, -STICK, STICK)
    jsty = 128 + np.clip(jsty, -STICK, STICK)
    # 4. fire: inside the hit cylinder, in front, in range, laser cool
    fr = FIRE_R * r / 16.0          # hit radius in position units (r is /256, positions /4096)
    fire = seen and abs(tx) < fr and abs(ty) < fr and tz > 0 and tz < FIRE_ZMAX and hot < HOT
    # 5. throttle by range (full speed with nothing to fight)
    speed = 40.0 if not seen else 15.0 + 10.0 * (tz > MID) + 15.0 * (tz > FAR)
    return float(jstx), float(jsty), float(fire), float(speed)


def original_angle_law(f):
    """steer()'s roll and pitch directions from the angle law, for the sign tests."""
    tx, ty, tz, vx, vy, vz, own = [float(v) for v in f[:7]]
    ax, ay, az = tx + LEAD_C * vx, ty + LEAD_C * vy, tz + LEAD_C * (vz - own)
    clock = np.arctan2(ax, ay)
    up = abs(clock) <= np.pi / 2
    roll_err = clock if up else clock - np.copysign(np.pi, clock)
    alpha = -roll_err
    beta = (1.0 if up else -1.0)      # off is always >= 0: pitch towards the chosen side
    return np.sign(alpha), np.sign(beta)

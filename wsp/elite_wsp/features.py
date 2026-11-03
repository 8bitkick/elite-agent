"""The input layer: Elite's RAM -> a feature vector.

This is the only non-neural code on the path from RAM to stick. It does what
cplayer/ships.c's parse_block does (decode the K% block's sign-magnitude
numbers) and nothing else: no policy lives here. Every number below is a
raw game quantity, scaled to roughly [-1, 1] so the hand-written weights can
be reasoned about in round units.

Feature layout (index: name, scale):
   0  tx     target x  (right)       / 4096   front-view ship space
   1  ty     target y  (up)          / 4096
   2  tz     target z  (forward)     / 4096
   3  vx     target velocity x       / 64     = nosev[0] * 1.5 * speed  (game units per loop)
   4  vy     target velocity y       / 64
   5  vz     target velocity z       / 64
   6  own    our speed               / 64
   7  energy energy banks            / 255
   8  fsh    front shield            / 255
   9  hot    laser temperature       / 255
  10  r      target hit radius       / 256    sqrt(blueprint area)
  11  one    constant 1.0                     (the bias every gate needs)
  12  seen   1.0 if a target exists else 0.0
"""
import numpy as np

N_FEATURES = 13
NAMES = ["tx", "ty", "tz", "vx", "vy", "vz", "own", "energy", "fsh", "hot", "r", "one", "seen"]
POS_SCALE, VEL_SCALE, R_SCALE = 4096.0, 64.0, 256.0


def _sm24(lo, hi, sign):
    v = (sign & 0x7F) << 16 | hi << 8 | lo
    return -v if sign & 0x80 else v


def _sm16(lo, hi):
    v = (hi & 0x7F) << 8 | lo
    return (-v if hi & 0x80 else v) / 24576.0


def decode_ship(block):
    """One K% block (bytes 0..36) -> dict, exactly as ships.c reads it."""
    b = block
    return dict(
        x=_sm24(b[0], b[1], b[2]), y=_sm24(b[3], b[4], b[5]), z=_sm24(b[6], b[7], b[8]),
        nosev=[_sm16(b[9 + 2 * i], b[10 + 2 * i]) for i in range(3)],
        speed=b[27], flags31=b[31], energy=b[35],
    )


def features(target, own_speed, energy, fsh, laser_temp, hit_radius):
    """target: dict from decode_ship (or None), the rest plain game bytes."""
    f = np.zeros(N_FEATURES, dtype=np.float32)
    f[11] = 1.0
    f[6] = own_speed / VEL_SCALE
    f[7] = energy / 255.0
    f[8] = fsh / 255.0
    f[9] = laser_temp / 255.0
    if target is None:
        return f
    sp = 1.5 * target["speed"]
    f[0:3] = np.clip(np.array([target["x"], target["y"], target["z"]]) / POS_SCALE, -8, 8)   # scanner range
    f[3:6] = np.array(target["nosev"]) * sp / VEL_SCALE
    f[10] = hit_radius / R_SCALE
    f[12] = 1.0
    return f

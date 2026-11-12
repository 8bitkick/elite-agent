"""The senses: what the network is told about a fight, the same as cplayer/wsp_pilot.c's wsp_features.

Coordinate changes only. The target's sightline as a pilot reads it: how far off the nose (th) and at what
clock position (cx, cy), how fast it is moving across the sky (wx, wy), how big it looks (rho), the range,
how fast it is closing; and our own state, and the stick under the hand. No constant here decides anything.

  0 ux  1 uy  2 uz    unit sightline to the target (x right, y up, z ahead)
  3 wx  4 wy          sightline rate across the view, rad a game loop x 16
  5 th                off-nose angle / pi (0 dead ahead, 1 dead astern)
  6 cx  7 cy          clock position, a unit vector on the screen ((0, 1) at 12 o'clock)
  8 rho               angular radius, hit radius / range x 16
  9 dist              range / 4096 (to 8)
 10 close             closing speed / 64
 11 own               our speed / 64
 12 hot              laser temperature / 255
 13 energy           / 255
 14 ws               1 in witchspace
 15 jx  16 jy         the stick, -1..1 (jy < 0: nose up)
 17 seen             1 with a target
 18 one              the bias input
 19 hseen            1 if the target's heading was read from its wireframe, 0 if guessed from its motion (fair mode)
 20 age              seconds since the target was last measured, to 4 (0 in efficient mode)
 21 rsd              the range's uncertainty x 4 (1.0 = a 25% range error; 0 in efficient mode)
 22 vsd              the motion estimate's uncertainty / 16 units a loop (0 in efficient mode)
 23 arm              our front laser's power / 32 (the damage a hit: pulse and beam 0.47, military 0.72)
"""
import math
import numpy as np

NAMES = ["ux", "uy", "uz", "wx", "wy", "th", "cx", "cy", "rho", "dist", "close", "own", "hot", "energy", "ws",
         "jx", "jy", "seen", "one", "hseen", "age", "rsd", "vsd", "arm"]
N = len(NAMES)
IDX = {n: i for i, n in enumerate(NAMES)}


def features(x, y, z, vx, vy, vz, own, r, laser_temp=0, energy=255, witchspace=0, jstx=128, jsty=128,
             heading_seen=1, age=0.0, range_sd=0.0, vel_sd=0.0, laser=0x97):
    f = np.zeros(N, dtype=np.float32)
    d = max(1.0, math.sqrt(x * x + y * y + z * z))
    ux, uy, uz = x / d, y / d, z / d
    rx, ry, rz = vx, vy, vz - own
    rd = rx * ux + ry * uy + rz * uz
    wx, wy = (rx - rd * ux) / d, (ry - rd * uy) / d
    h = math.sqrt(ux * ux + uy * uy)
    th = math.acos(max(-1.0, min(1.0, uz)))
    f[:] = [ux, uy, uz, wx * 16, wy * 16, th / math.pi,
            ux / h if h > 1e-6 else 0.0, uy / h if h > 1e-6 else 1.0,
            r / d * 16, min(d / 4096, 8), -rd / 64, abs(own) / 64, laser_temp / 255, energy / 255,
            1.0 if witchspace else 0.0, (jstx - 128) / 128, (jsty - 128) / 128, 1.0, 1.0,
            1.0 if heading_seen else 0.0, min(age / 50, 4.0), min(range_sd * 4, 4.0), min(vel_sd / 16, 4.0),
            (laser & 0x7F) / 32]
    return f


def from_log(line):
    """A WSPLOG line from the splice: frame, the features, | the outputs."""
    a, b = line.split("|")
    t = a.split()
    return int(t[0]), np.array([float(v) for v in t[1:]], dtype=np.float32), [int(v) for v in b.split()]

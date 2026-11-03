"""The authored network against its spec, and against the C agent's angle law."""
import numpy as np, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from elite_wsp import author, Net, policy, features, decode_ship
from elite_wsp.reference import original_angle_law

PATH = os.path.join(os.path.dirname(__file__), "..", "elite_pilot.npz")
author.save(PATH)
net = Net(PATH)
rng = np.random.default_rng(1)


def random_state():
    """A fight state as the RAM would give it: a ship somewhere in the bubble, on some heading."""
    pos = rng.integers(-30000, 30000, 3) if rng.random() < 0.7 else rng.integers(-600, 600, 3)
    n = rng.normal(size=3); n /= np.linalg.norm(n)
    tgt = dict(x=pos[0], y=pos[1], z=pos[2], nosev=list(n), speed=int(rng.integers(0, 41)), flags31=0, energy=100)
    return features(tgt, int(rng.integers(0, 41)), int(rng.integers(0, 256)), int(rng.integers(0, 256)),
                    int(rng.integers(0, 256)), int(rng.integers(30, 100)))


def margin(f):
    """Distance to the nearest decision boundary: a ReLU step is soft within 1/S of it."""
    from elite_wsp.reference import LEAD_C, FIRE_R, FIRE_ZMAX, HOT, FAR, MID, KICK_AT
    tx, ty, tz, vx, vy, vz, own, energy, fsh, hot, r = f[:11]
    ay, az, fr = ty + LEAD_C * vy, tz + LEAD_C * (vz - own), FIRE_R * r / 16
    ax = tx + LEAD_C * vx
    beta = max(0.0, ay - abs(ax)) - max(0.0, -ay - abs(ax))
    return min(abs(v) for v in [ay, az, tz, abs(ax) - KICK_AT, abs(beta) - KICK_AT, tz - FIRE_ZMAX, tz - MID, tz - FAR, hot - HOT, fr - abs(tx), fr - abs(ty)])


def test_matches_spec():
    worst = np.zeros(4)
    for _ in range(20000):
        f = random_state()
        if margin(f) < 2e-3:
            continue
        got, want = net.forward(f), np.array(policy(f))
        got[:2] = np.clip(got[:2], 1, 255)
        worst = np.maximum(worst, np.abs(got - want))
    assert worst[0] < 1.0 and worst[1] < 1.0, f"stick error {worst[:2]}"    # under one stick unit
    assert worst[2] < 0.05 and worst[3] < 0.05, f"fire/speed error {worst[2:]}"


def test_no_target():
    f = features(None, 30, 255, 255, 0, 0)
    jstx, jsty, fire, speed = net.act(f)
    assert (jstx, jsty, fire, speed) == (128, 128, False, 40)


def test_agrees_with_angle_law():
    """Roll and pitch push the same way steer() would, whenever the net pushes at all."""
    roll_ok = pitch_ok = n_roll = n_pitch = 0
    for _ in range(20000):
        f = random_state()
        if margin(f) < 2e-3:          # (a soft step: no direction to speak of)
            continue
        jstx, jsty, _, _ = net.forward(f)
        a_sign, b_sign = original_angle_law(f)
        if abs(jstx - 128) > 2:
            n_roll += 1; roll_ok += np.sign(jstx - 128) == a_sign
        if abs(jsty - 128) > 2:
            n_pitch += 1; pitch_ok += np.sign(128 - jsty) == b_sign    # nose up is jsty < 128
    assert roll_ok == n_roll, f"roll direction disagreed {n_roll - roll_ok}/{n_roll}"
    assert pitch_ok == n_pitch, f"pitch direction disagreed {n_pitch - pitch_ok}/{n_pitch}"


def test_fire_cylinder():
    ship = dict(x=0, y=0, z=5000, nosev=[0, 0, 1], speed=20, flags31=0, energy=100)
    assert net.act(features(ship, 30, 200, 200, 0, 80))[2]                  # dead ahead, cool: fire
    assert not net.act(features(ship, 30, 200, 200, 250, 80))[2]            # hot: hold
    assert not net.act(features({**ship, "x": 400}, 30, 200, 200, 0, 80))[2]   # 400 > 1.431*80: miss
    assert net.act(features({**ship, "x": 100}, 30, 200, 200, 0, 80))[2]
    assert not net.act(features({**ship, "z": -5000}, 30, 200, 200, 0, 80))[2]   # behind


def test_decode_matches_ships_c():
    blk = bytes([0x34, 0x12, 0x80 | 0x01, 0, 0, 0, 0x10, 0x27, 0] + [0] * 28)
    s = decode_ship(blk)
    assert s["x"] == -(0x01 << 16 | 0x12 << 8 | 0x34) and s["z"] == 0x2710


if __name__ == "__main__":
    for t in [v for k, v in globals().items() if k.startswith("test_")]:
        t(); print("ok", t.__name__)

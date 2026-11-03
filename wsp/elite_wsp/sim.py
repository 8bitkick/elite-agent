"""A closed-loop check on Elite's own control model: the stick turns the ship by
alpha_from_jstx / beta_from_jsty (ships.c, 1/256 rad a game loop), the target
flies straight, and we count how long the authored pilot takes to get it in the
sights and how much of the fight it keeps it there."""
import numpy as np
from .features import features
from .net import Net


def alpha_from_jstx(j):
    d = j - 128; m = abs(d) >> 2
    if m < 8: m >>= 1
    return m if d >= 0 else -m


def beta_from_jsty(j):
    d = j - 128; a = (d if d >= 0 else -d - 1) + 4; m = a >> 4
    if m < 3: m >>= 1
    return -m if d >= 0 else m


def rot(v, alpha, beta):
    """Roll by alpha then pitch by beta (radians), as the game turns the universe about us."""
    ca, sa = np.cos(alpha), np.sin(alpha)
    x, y, z = v
    x, y = ca * x - sa * y, sa * x + ca * y
    cb, sb = np.cos(beta), np.sin(beta)
    y, z = cb * y - sb * z, sb * y + cb * z
    return np.array([x, y, z])


def fight(net, rng, loops=400, own_speed=30, radius=80):
    pos = rng.uniform(-8000, 8000, 3)
    nose = rng.normal(size=3); nose /= np.linalg.norm(nose)
    speed = int(rng.integers(5, 35))
    first, held, temp = None, 0, 0
    for t in range(loops):
        tgt = dict(x=pos[0], y=pos[1], z=pos[2], nosev=list(nose), speed=speed, flags31=0, energy=100)
        jstx, jsty, fire, spd = net.act(features(tgt, own_speed, 200, 200, temp, radius))
        if fire:
            held += 1; temp = min(255, temp + 8)
            if first is None: first = t
        else:
            temp = max(0, temp - 2)
        a, b = alpha_from_jstx(jstx) / 256.0, beta_from_jsty(jsty) / 256.0
        # the world turns the opposite way to us: roll right swings it left, nose up brings +y down
        pos = rot(pos, -a, b)
        nose = rot(nose, -a, b)
        pos = pos + nose * 1.5 * speed - np.array([0, 0, own_speed])
        if np.linalg.norm(pos) > 60000:                     # lost it
            break
    return first, held / loops


def main(n=200, seed=0):
    net, rng = Net("elite_pilot.npz"), np.random.default_rng(seed)
    firsts, helds = [], []
    for _ in range(n):
        f, h = fight(net, rng)
        if f is not None: firsts.append(f)
        helds.append(h)
    print(f"{n} fights from random starts: in the sights at least once in {len(firsts)}, "
          f"median {np.median(firsts):.0f} loops to first sight, on target {100 * np.mean(helds):.0f}% of the time")


if __name__ == "__main__":
    main()

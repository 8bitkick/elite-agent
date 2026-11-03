# elite-wsp: the Elite dogfight pilot, written in weight space

![A four-pirate fight at Deadly rank, the real game on the left, the network's units lit on the right](../docs/img/weight-space.gif)

*Sidewinder, Mamba, Adder and a pirate Cobra Mk III, recorded at Deadly rank, replayed with the network flying; four kills in 4.8 seconds. Left: BBC Master Elite in the emulator. Right: the network's 91 units, lit by activation.*

A reimagining of [8bitkick/elite-agent](https://github.com/8bitkick/elite-agent)'s combat core
using the idea in MIT CSAIL's [Sparks of Weight-Space Programming](https://rikiyat.github.io/blog/weight-space-programming/):
instead of writing the pilot as C rules, **write it as the weights of a neural network, directly**.
No data, no loss, no gradients, no rollouts before it is scored. Every one of the 2,053 weights was
placed by reasoning about what each unit should compute, the way the blog's Experiment 5 writes a
Breakout policy "first layer: where the ball lands; second layer: move the paddle there".

The original agent's `combat_keys()` + `steer()` + fire test (about 300 lines of `combat.c`/`pilot.c`,
with `atan2`, `hypot`, `cos³` and a lookup table) becomes a 13 → 26 → 23 → 38 → 4 ReLU MLP that
reads the same RAM-derived quantities and writes the same outputs: the joystick's X and Y, the fire key,
and the throttle.

```
python3 -m elite_wsp.author      # writes elite_pilot.npz          (0 training steps)
python3 -m elite_wsp.inspect     # reads the weights back as a program, unit by unit
python3 tests/test_pilot.py      # against the spec and against the C agent's angle law
python3 -m elite_wsp.sim         # closed loop on Elite's own control model
python3 -m elite_wsp.export_c    # c/pilot_weights.h for the RP2350 build
```

## What the network is

| C agent (cplayer/)                                    | Weight-space pilot                                                  |
|-------------------------------------------------------|---------------------------------------------------------------------|
| `ships.c` parse_block: decode the K% block            | `features.py`: the input layer, 13 scaled numbers (tx ty tz vx vy vz own energy fsh hot r 1 seen) |
| `combat_keys`: aim = pos + vel·4.5 loops              | layer 1: `ax± ay± az±`, the lead baked into the weights (0.0703 = 4.5/64) |
| `steer`: roll target into the vertical plane          | layer 2: `roll_up± roll_dn±`, the roll rate ∝ −ax gated by "target above" |
| `steer`: pitch onto it, eased by cos³(roll error)     | layer 2: `pitch_up/dn` = relu(ay − \|ax\|), the ease as a piecewise-linear cone |
| `steer`: off > 90°, come round hardest                | layer 2: `behind_up/dn`, two ANDs                                   |
| fire: in the hit cylinder, in front, in range, cool   | layer 3: `fire` = AND of six booleans built in layers 1–2           |
| `spd_far / spd_mid / spd_near`                        | output: 15 + 10·[z > MID] + 15·[z > FAR], 40 with nothing to fight  |
| `jstx_for`, `jsty_for` (rate → stick byte)            | layer 3 + output: three range bands on the gains, a 32/36 kick past the dead zone, the stick's own saturation |

Three primitives do all of it, and they are the ones the blog's Astra reaches for:

```
relu(u), relu(-u)                    the two halves of a signed value; |u| is their sum
sat(u) = relu(S·u) − relu(S·u − 1)   a comparison as a 0..1 step (S = 10,000: decided within 0.4 game units)
relu(v + M·g − M)                    "v if g else 0": a boolean gating a value (|v| < M)
relu(a + b − 1)                      AND
```

`author.py` is written as a layer-by-layer program with every unit named and annotated, so the
weights are readable as well as writable. `inspect.py` prints that program back from the `.npz`:

```
== layer 2 (relu): 23 units
  roll_up+     = -1*ax+ +1*ax- +32*up+ -32*up- -32   # relu(-ax) if up
  pitch_up     = -1*ax+ -1*ax- +1*ay+ -1*ay-          # relu(ay - |ax|)
  behind_up    = +1*up+ -1*up- +1*behind+ -1*behind- +1*seen -2   # behind AND up AND seen
  ...
== output (linear): 4 units
  jstx         = +593*roll_up+ -593*roll_up- +593*roll_dn+ -593*roll_dn- +128   # roll stick
  speed        = -25*seen +10*mid+ -10*mid- +15*far+ -15*far- +40
```

## On the real game

`splice/` is the splice into the C agent: `elite-agent.patch` makes `combat_keys()` hand the stick, the fire key
and the throttle to `wsp_pilot()` when `WSP=1` is set, with the career loop, docking, missiles, ECM and evasion
left to the C code. The repo's `scripts/fetch-emulator.sh` fetches the BBC Master emulator, its ROMs and Mark
Moxon's flicker-free Elite; a 90-game-minute career with `FIGHTS=fights` recorded 19 real fight starts (the
whole machine at the moment each fight began), and the fight lab replayed them with each pilot. Each row is the
lab's own measure over the same 18 scored fights:

| pilot                                   | points/min | on target, of locked frames | avg off | damage | died | cleared |
|-----------------------------------------|-----------:|----------------------------:|--------:|-------:|-----:|--------:|
| C agent (`combat.c` + `steer`)          |       5.75 |                       84.7% |   0.90° |    225 |    0 |   18/18 |
| weight-space pilot, as first written    |       0.55 |                        6.6% |   6.24° |   2760 |    1 |   16/18 |
| + dead-zone kick (12 units, layer 3)    |       3.83 |                       34.8% |   4.23° |    468 |    0 |   18/18 |
| + range-banded gains (18 units, layer 3)|       4.61 |                       46.8% |   2.43° |    452 |    0 |   18/18 |

A 12-game-hour C-agent career (to Dangerous, 729 fight starts recorded) then gave a harder set: the 20 witchspace
Thargoid swarms with the most ships (3 Thargoids and 5 to 6 Thargons) and the 20 biggest pirate packs (4 to 5 ships).
The network clears all 40, with no deaths and no escape pod:

| hard set, 40 fights        | C agent pts/min | network pts/min | network damage vs C |
|----------------------------|----------------:|----------------:|--------------------:|
| 20 Thargoid swarms         |            20.3 |            13.8 |           1.75× more |
| 20 pirate packs of 4 to 5  |            13.1 |            12.5 |           1.33× more |

Both edits came from reading the lab's numbers the way the blog's Experiment 6 reads practice games: the first
pilot sat 6° off because Elite's stick has a dead zone its linear gains never crossed; the second because its
gains acted on lateral offset rather than angle, so far targets got full stick for a trifle. Each fix is a
handful of named units. The remaining gap to the C pilot is its hysteresis, its coast-then-stop on the pitch key
and its per-frame planned turns, none of which a memoryless net has.

## How it is checked

`reference.py` is the spec: the dogfight law written as plain piecewise-linear arithmetic. The tests
show the network reproduces it to under one stick unit on 20,000 random fight states (away from the
0.4-game-unit band around each comparison, where a ReLU step is soft by design), and that whenever the
network pushes the stick, it pushes the same way the C agent's `atan2` law would — roll and pitch
direction agree on every one of 20,000 states.

`sim.py` closes the loop on Elite's own control model (`alpha_from_jstx` / `beta_from_jsty`, 1/256 rad
a game loop, the target flying straight): from 200 random starts up to 8,000 units away the pilot brings
the target into the sights in 191, median 98 loops to the first sight.

`c/wsp_pilot.c` is the forward pass for the Presto (about 1.4k multiply-adds, no library), and gives the
same bytes as the numpy pass on the same state.

## What's reimagined and what isn't

This is the fight. The career loop, docking, trading, the chart cursor and witchspace chains stay as
the C agent's protothreads; the natural splice is `combat_keys()` calling `wsp_pilot()` for the stick,
fire and speed. The law itself was redesigned for weight space rather than transliterated:

- **Angles became lateral offsets.** `steer()` works in radians (`atan2(r, z)`); a ReLU net has no
  atan2, so the gains act on the aim point's x and y in ship space and the stick's saturation does the
  rest. Near the nose, where accuracy matters, the two agree; far off-axis both are at the stops anyway.
- **cos³ became a cone.** The pitch ease-off while the roll error is large is `relu(|ay| − |ax|)`: zero
  beyond 45° of roll error, full on the vertical plane. Same intent, straight lines.
- **No hysteresis.** `steer()` keeps the above/below choice for 25 frames to stop the stick flipping on a
  target near the nose. This net has no memory; a one-unit recurrent latch would restore it.
- **No rear view, missiles, ECM, bomb, pod.** Those are discrete decisions over more RAM than the
  13 features here; each is a few more gates.

The point of the exercise is the blog's: the pilot is now a thing an LLM can read and edit in place.
Want it to fire from further out? One weight (`znear`'s bias). A fatter hit radius for Thargoids? The
`fr` unit. A different lead? Six weights in layer 1. The weights are the source.

## Layout

```
elite_wsp/features.py    RAM → features (the K% decode from ships.c, nothing more)
elite_wsp/reference.py   the spec: the law as arithmetic, plus the original angle law for the tests
elite_wsp/author.py      the weights, written unit by unit → elite_pilot.npz
elite_wsp/net.py         the forward pass (numpy)
elite_wsp/inspect.py     the weights read back as a program
elite_wsp/sim.py         closed-loop check on Elite's rotation model
elite_wsp/export_c.py    → c/pilot_weights.h
c/wsp_pilot.c            the forward pass in C for the RP2350
splice/                  the patch to elite-agent, and the lab's results on the recorded fights
web/index.html           the interactive demo
tests/test_pilot.py
```

Apache-2.0, like the original.

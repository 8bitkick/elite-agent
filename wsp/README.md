# Fair mode's pilots: two AI-authored custom models

![A Deadly-rank fight flown by the first version of the network, its units lit beside the game](../docs/img/weight-space.gif)

Fair mode (`FAIR=1`) flies its dogfights and its dockings with two small neural networks, and the player sees only
what a human could (fair mode, [docs/FAIR.md](../docs/FAIR.md)): the screen, the scanner and the dashboard as drawn,
no hidden RAM. Their weights were **authored directly by an AI**, unit by unit, every one named, then **tuned by
evolution** in a lab. No gradient ever touched them. They follow MIT CSAIL's
[Sparks of Weight-Space Programming](https://rikiyat.github.io/blog/weight-space-programming/) (2 Nov 2026):
a frontier LLM writing network weights directly, as a program, rather than training them. Each is run by a forward
pass of about 60 lines of plain C, with no ML library, like the rest of the agent.

- **The dogfight network** (`wsp_pilot.c`, **2,256 weights, 8.8 KB**) scores **97.1% of the C rules' points a fight
  minute on 450 held-out fights** (90% interval 94.5-99.7%) flying every fight on its own, as it does by default. With
  the C's acquisition planner borrowed for witchspace swarms (`WSPPLAN=2`) it outscores them, 102.8%; that is off by
  default so that in fair mode every fight is the network's.
- **The docking network** (`wsp_dock.c`, **1,471 non-zero weights, about 1.5 KB**) flies from the hyperspace exit to
  docked: the cruise to the planet, the way round the station, the approach onto the slot's axis, the final with the
  roll matched to the station's spin. On 69 held-out dockings it is **2.8% cheaper than the C law it replaced** (the
  time to dock plus a penalty for ugly flying: 175.4 against 180.5), with a sixth of the C's stick reversals, no
  crawling and no deaths.

In careers to Elite on the current build, both networks flying, fair mode takes **16.7 and 17.0 game hours** on its
two seeds, with no deaths, against 16.8 and 17.5 with the C docking law on the same seeds.

| | |
|---|---|
| **Switch** | on in fair mode (`FAIR=1`); `WSP=0` / `WSPAPP=0` put the C rules back for comparisons (the dogfights / the docking), `WSP=1` / `WSPAPP=1` fly them in Optimal mode too. Optimal mode is unchanged: a career's journal is byte-identical without them |
| **What they fly** | the dogfight network: the stick, the laser and the throttle in a fight. The docking network: the stick, the throttle and the in-system jump from the hyperspace exit to docked, and when to commit. Target choice, missiles, ECM, evasion in a fight, trading and the career stay the C agent's |
| **Size** | dogfights: 23 inputs, 4 hidden layers (30, 26, 16, 14), 4 outputs: 2,256 weights, 187 non-zero. Docking: 47 inputs, 4 hidden layers (112, 179, 42, 30), 7 outputs: 1,471 non-zero weights |

## The results

### In the fight lab

Each of 450 fights recorded from two C careers (150 witchspace swarms, 150 pirate packs, 150 fights with one or
two ships), none used in tuning, flown by each pilot from the identical snapshot, each in a fresh process. Points
a fight minute (the tally is the rank: points per kill by ship type), and the ratio to the C rules in the same mode,
with a 90% paired-bootstrap interval:

| | Optimal | | Fair | |
|---|---:|---|---:|---|
| C rules | 20.32 | | 18.15 | |
| network, as written (the C's numbers) | 18.77 | 0.924 (0.901-0.948) | | |
| **network, evolved (fair mode's pilot)** | **19.76** | **0.972 (0.949-0.997)** | **17.62** | **0.971 (0.945-0.997)** |
| network + the C's planner in witchspace (`WSPPLAN=2`) | 20.03 | 0.986 (0.966-1.006) | 18.66 | 1.028 (1.006-1.052) |

By kind, fair mode with the planner: witchspace 20.50 against 20.06 (1.022), pirate packs 12.29 against 11.74 (1.047), small
fights 7.34 against 6.97 (1.052). No deaths in any of the 450, for either pilot.

### Careers to Elite, fair mode

Game hours to the rank of Elite, on the same seeds (`SKEW`) and build. On the current build (the journals are in
[../docs/careers](../docs/careers) and [careers/](careers)):

| seed | both networks (the default) | dogfight network, C docking (`WSPAPP=0`) | dogfight network + planner, C docking (`WSPPLAN=2 WSPAPP=0`) | C rules (`WSP=0 WSPAPP=0`) |
|---|---:|---:|---:|---:|
| 13 | **16.7** | 16.8 | 16.5 | 17.1 |
| 101 | **17.0** | 17.5 | 16.5 | 15.6 |
| **mean** | **16.8** | 17.1 | 16.5 | 16.4 |

The docking network gains 0.1 and 0.5 hours over the C law on its own. The planner would gain 0.3 more in the mean
and is off by default (above). A career is one path, and a seed moves it by more than any of these pilots do (the C
rules' two careers are 1.5 hours apart); the labs' held-out sets are what say which pilot is better.

On an earlier build, before Optimal's current farming strategy (the same network; the journals in [careers/](careers)):

| seed | network | C rules | |
|---|---:|---:|---|
| 0 | 27.2 | 27.9 | network 0.7 h ahead |
| 13 | 28.4 | 28.5 | network 0.1 h ahead |
| 21 | 26.0 | 26.9 | network 0.9 h ahead |
| **mean** | **27.2** | **27.8** | 2.0% faster |

A career is one path, and a seed moves it by more than either pilot does (the C rules' two careers on the current build
are 1.5 hours apart). Five careers say the network is at least the C rules' equal over a whole career; the lab's 450
paired fights are what say it's better in a fight.

## Why fair mode

Optimal mode reads exact positions, headings and energies from RAM; fair mode gets them from what's drawn, through
a tracker (fair.c), and every pilot loses about 11% to that (both: 0.893 and 0.892 of their Optimal selves). The C rules steer with the tracker's estimates as if they were exact. The network is the better tracker of a target
(below) and holds its lead over the C in normal space when the information gets worse, where the C's lead in
witchspace comes from a planner that works equally well either way. So in fair mode the network's strengths count
for more, and with the planner borrowed for the swarms it would come out ahead; on its own it is within 3%.

## How it was built

**v1** ([v1/](v1)): 2,053 weights written to steer on lateral offsets in ship space, about 72% of the C's points
a minute on the hard fights. Its two in-game fixes (a kick past the joystick's dead zone, gains banded by range)
were weight edits found by reading the lab's numbers.

**v2, written** ([elite_wsp/author.py](elite_wsp/author.py)): the C rules' steering law (pilot.c `steer`) and
throttle, re-expressed for a ReLU network over sightline senses: how far off the nose and at what clock position,
how fast the target moves across the sky, how big it looks, the range. The network has no atan2, so the senses do
the trigonometry ([elite_wsp/features.py](elite_wsp/features.py)) and the network every decision:

- the lead aim point: the sightline plus `lead` loops of its motion (layer 1)
- roll it into the vertical plane: min(|aim x| / `roll_ramp`, `roll_cap` |clock x|), on the side chosen with
  `hyst` hysteresis from the stick (layers 2-4)
- pitch onto it, easing off while the roll error is large (the cone `pitch_cone`), hard when far off the nose
- fire: an AND of six booleans, the nose inside `fire_r` hit radii among them
- the throttle: stop to turn past `turn_off`, then by range bands

Every constant is a named parameter, and the C's own tuned numbers are the defaults: as written, 92.4% of the C.

**v2, evolved** ([evolve/es.py](evolve/es.py)): CMA-ES over the 21 named parameters, scored in the fight lab on
training fights only: 97.2%. What evolution changed reads as a story: a snappier roll (gain +22%, the ramp
halved), harder pitch (+24%), less lead (3.7 loops, not 4.5), and flying on while up to 20 degrees off the nose,
not 15. It's [evolve/es_params.json](evolve/es_params.json); the weights are [elite_pilot.npz](elite_pilot.npz)
(names and notes) and [elite_pilot.wsp](elite_pilot.wsp).

**Where the last points are.** The lab's time-to-kill split, per kill in witchspace: both pilots take the same time
to slew onto a target (1.42 s), the same in the sights (0.46 s), the same between targets; the network loses it in
one phase, the capture from 10 degrees into the sights (0.58 s against 0.47). There the C flies a planner that
searches Elite's exact rotation, including the gaps in the stick's rates (pitch has no rate 2, roll none from 4 to 7).
The network is the better tracker (hold 83% against 80%, aim 1.2 degrees off against 1.9) and the slower acquirer. In
a swarm, a fight is forty acquisitions, which is where `WSPPLAN=2` would borrow the planner; by default the network
flies them itself.

**Four inputs about the senses' confidence** (fair.c's tracker): whether the heading was seen on a wireframe or
guessed from motion, how stale the sighting is, the range's and the motion's uncertainty. In Optimal mode they
read "seen, fresh, exact". The written law doesn't use them yet: gating the lead and the fire cone by them was
tried and cost points. They're there for what comes next.

## The docking network

[elite_wsp/dock_author.py](elite_wsp/dock_author.py) writes it; [elite_wsp/dock_features.py](elite_wsp/dock_features.py)
is its senses; [cplayer/wsp_dock.c](../cplayer/wsp_dock.c) runs it. The same method as the dogfight network's, and a
different lesson learned on the way.

**The first version transcribed the C law.** pilot.c's docking is seven regimes (the cruise, the plain line-up, the way
round from behind, a vector field, an approach circle, a hand-over blend, the final) with hysteresis bits, piecewise
trigonometry and special cases. Written faithfully as ReLU units it came to 12,500 weights in 10- and 13-layer heads,
85% of its units copies carrying values down the layers, and it inherited every one of the C's ugly manoeuvres: the
detours behind the station, the stick reversals in the line-up, the crawl near the marks. It worked (63 of 69 held-out
dockings) and it was thrown away.

**The second was written for the network.** Five sightlines from the senses, pure geometry: the planet, the slot, an
*approach mark* on the slot's axis a fixed lead ahead of us (further out when we are far off the axis, so the approach
is never steeper than `chi`), a *side mark* out in front of the station and off to our own side (the way round from
behind), and the escape heading from a collision course. The network's whole job is to choose between them and to
steer:

- **where to aim** (layers 1-2): a gate for each sightline, from saturating tests of where we are (behind, in front,
  nearly lined up, lined up, low over the planet, a collision course). Each gate *clips* its sightline, min(u, g),
  rather than switching it, so at a regime's edge two aims add and the steering flies their mean: no jumps
- **the steering** (layers 3-4): the dogfight network's law on the chosen aim, roll into the vertical plane (above or
  below, with hysteresis), pitch onto it; the roll held to the station's spin rate
- **the final**: the roll at the station's spin trimmed to level the slot; the pitch onto the slot's drawn centre
  through Elite's rotation (y - a x), with the sideways hold; the commit as an AND of six tests, kept with hysteresis
- **the speed**: by the room left for the turn still needed; the final's bands by range; 40 once committed
- **the bits** carried between loops: above/below, the final, the commit

As written, with the C's own numbers, it was level with the C in the lab (63 of 69 docked against 62, median
164 s against 160) and the crawl was gone. Evolution over its 45 named parameters ([evolve/es_dock.py](evolve/es_dock.py),
12 generations of 30 training starts, scored on time plus a penalty for stick reversals, crawling and swings of the
aim) took it to 175.4 against the C's 180.5 on the 69 held-out starts. What it changed: a gentler roll (gain
-33%), a longer lead on the approach mark (+22%), a tighter way round, and a stricter commit (centred within 3
degrees, level within 4.4). The parameters are [evolve/es_dock_best.json](evolve/es_dock_best.json); the weights
[elite_dock.npz](elite_dock.npz) and [elite_dock.wsp](elite_dock.wsp); [tests/test_dock.py](tests/test_dock.py) holds
the C's senses and forward pass to the Python's and checks what the written network decides in plain situations.

| 69 held-out dockings | cost | docked | median s | stick reversals | crawl s | swings |
|---|---:|---:|---:|---:|---:|---:|
| C law | 180.5 | 62 | 159.9 | 12 | 3.6 | 0.62 |
| network, as written | 183.7 | 63 | 164.1 | 12 | 0.2 | 0.90 |
| **network, evolved** | **175.4** | 62 | 161.1 | 10 | 0.2 | 0.94 |

(cost: seconds to dock, a timeout 300, a death 360, plus 0.5 a stick reversal, 1 a second of crawling far out, 3 a
swing of the aim past 60 degrees. The lab: `LABDOCK=1 cplayer/lab starts.lst`, on starts recorded by
`DOCKSNAP=dir run_sim`.)

What it does not do yet: the C's approach circle from behind (a banked turn onto the axis), which the first version
carried and this one replaced with the side mark and the turn-room speed rule; and the C's step aside from traffic
leaving the slot, which the C still applies around the network's aim.

## What didn't work

All measured on held-out fights, none kept:

- **PPO** on a learned residual over the written network: its improvements were inside the noise of 180-fight
  rollouts, and val wandered 20.0-20.4 against the evolved network's 20.3.
- **Distilling the planner** (DAgger, a regression on its roll and pitch): the planner's choices are discrete and
  bang-bang, and a regression averages them away. A gated residual kept the tracking exact but changed nothing.
- **Dithering the stick** to give the rates Elite's stick map lacks on average: Elite reads the stick once a game
  loop (two or three frames), so frame-by-frame dithering aliased into noise (-2.6%).
- **Confidence gates** (less lead when the heading is guessed, a smaller fire cone) and a **nose hold** (no roll
  near the nose): all cost points, in both modes.
- **Fair-mode tracker settings** (memory, continuity, speed filter): in the noise, or not smooth in the setting.

## Measuring it honestly

Two things in the lab bit, and are fixed in [evolve/lab.py](evolve/lab.py):

- **State carried between fights.** The lab runs a list of fights in one process, and the player keeps state from
  one fight to the next (target memory, timers, the planner). A fight's result depended on the fights before it:
  the C rules' val score moved from 18.9 to 20.8 with how the list was split. Every fight now runs in a fresh
  process (the emulator starts in a few milliseconds).
- **Small sets.** On 40 fights the same pilot's score swings about 3%, as large as the effects being chased. The
  results here are on 450 fights with paired intervals; val (40) was only for watching evolution.

(The repo's pinned test, tests/career120.sh, fails on clean `main` in this environment too: its hash was pinned on
another machine. `WSP` off, this branch writes the identical journal to `main`'s.)

## Running it

```
cplayer/buildlab.sh && cplayer/build.sh
FAIR=1 SKEW=13 cplayer/run_sim 3000 "" journal.jsonl           # a fair career, flown by the 8 KB AI pilot model
python3 -m elite_wsp.author                                     # write the weights from author.py's defaults
python3 -m elite_wsp.inspect elite_pilot.npz                    # read them back, unit by unit
python3 tests/test_pilot.py                                      # the weights against their spec, C against numpy
python3 -m elite_wsp.dock_export && python3 tests/test_dock.py   # the docking network: write it, C against numpy
cd evolve && python3 -c "import lab; print(lab.fmt(lab.score(lab.run(lab.read_set('big'), wsp='written_es14.wsp', extra_env={'FAIR':'1'})[0], by_kind=True)))"
```

The fight sets in [evolve/sets/](evolve/sets) name recorded fights: `FIGHTS=fights2 run_sim 720` and
`FIGHTS=fights3 run_sim 2400` (SKEW 0, the C rules) record them. `train` (180) was the only set evolution saw; `big`
(450) and `test` (60) are held out.

| Switch | |
|---|---|
| `WSP=0` / `WSP=1` | the C rules in fair mode / the network in Optimal mode (the default: the network in fair mode only) |
| `WSPAPP=0` / `WSPAPP=1` | the same for the docking network; `WSPAPPW=file.wsp` other docking weights, `WAPLOG=file` its senses and outputs a frame |
| `WSPPLAN=0/1/2` | the C's planner for acquisition: never (the default), always, in witchspace |
| `WSPW=file.wsp` | other weights at run time (evolution's candidates) |
| `WSPLOG=file` | each decision: the senses, the action, the score |
| `WSPN`, `WSPR`, `WSPTEACH` | exploration noise, a learned residual, the planner's labels (ppo.py, distil.py) |

## Files

```
elite_wsp/features.py   the senses: the target's sightline, as cplayer/wsp_pilot.c computes it
elite_wsp/layers.py     writing a ReLU layer unit by unit, by name
elite_wsp/author.py     the network, written: every unit named and annotated, every constant a parameter
elite_wsp/reference.py  the same law as plain arithmetic, the spec the tests hold the weights to
elite_wsp/export.py     .npz (with names), .wsp (run time), cplayer/pilot_weights.h (compiled in)
elite_wsp/dock_features.py  the docking network's senses: the sightlines to the slot, the marks, the planet; the station as drawn
elite_wsp/dock_author.py    the docking network, written: where to aim, the steering, the final, the speed, the bits
elite_wsp/dock_export.py    its .npz, .wsp (WSPAPPW=file) and cplayer/dock_weights.h
evolve/es_dock.py       CMA-ES over the docking network's parameters, in the docking lab
evolve/lab.py           the fight lab in parallel, one process a fight; scores and paired comparisons
evolve/es.py            CMA-ES over the named parameters
evolve/ppo.py           PPO on a residual (tried; see above)
evolve/distil.py        DAgger on the planner (tried; see above)
careers/                the fair careers' journals, both pilots
v1/                     the first version: its lab results, the web demo and the film scripts
ROADMAP.md              what's next
```

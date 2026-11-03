# Weight-space fair: a hand-written neural network flies Elite on what a player can see

![Careers to Elite: the efficient C pilot, the fair C pilot and the weight-space network in fair mode; each pilot's fastest career bold, its slowest faint](../docs/img/climb-wsp.svg)

`WSP=1 FAIR=1`: the dogfight is flown by a small neural network, and the player sees only what a human could
(fair mode, [docs/FAIR.md](../docs/FAIR.md)): the screen, the scanner and the dashboard as drawn, no hidden RAM.
In that mode it **outscores the C pilot: 2.8% more points a fight minute on 450 held-out fights** (90% interval
0.6-5.2%), ahead in every kind of fight, and **reaches Elite in 27.2 game hours** against the fair C's 27.8 on the
same three seeds, ahead on each.

The network is **2,256 weights (8.8 KB), run by a forward pass of about 60 lines of plain C**
([cplayer/wsp_pilot.c](../cplayer/wsp_pilot.c)), with no ML library, like the rest of the agent. Its weights were
**written by hand**, unit by unit, every one named, then **tuned by evolution** in the fight lab. No gradient ever
touched them. It follows MIT CSAIL's
[Sparks of Weight-Space Programming](https://rikiyat.github.io/blog/weight-space-programming/) (2 Nov 2026):
a frontier LLM writing network weights directly, as a program, rather than training them.

| | |
|---|---|
| **Switch** | `WSP=1` (run_sim, lab), with `FAIR=1` for fair mode. The C pilot is unchanged without it: a career's journal is byte-identical to `main`'s |
| **What it flies** | the stick, the laser and the throttle in a fight. Target choice, missiles, ECM, evasion, docking, trading and the career stay the C agent's |
| **One part it borrows** | in witchspace, the C's acquisition planner (pilot.c `plan_stick`) turns onto each new Thargoid (below). `WSPPLAN=0` turns it off |
| **Size** | 23 inputs, 4 hidden layers (30, 26, 16, 14), 4 outputs: 2,256 weights, 187 of them non-zero |

## The results

### In the fight lab

Each of 450 fights recorded from two C careers (150 witchspace swarms, 150 pirate packs, 150 fights with one or
two ships), none used in tuning, flown by each pilot from the identical snapshot, each in a fresh process. Points
a fight minute (the tally is the rank: points per kill by ship type), and the ratio to the C pilot in the same mode,
with a 90% paired-bootstrap interval:

| | efficient | | fair | |
|---|---:|---|---:|---|
| C pilot | 20.32 | | 18.15 | |
| network, as written (the C's numbers) | 18.77 | 0.924 (0.901-0.948) | | |
| network, evolved | 19.76 | 0.972 (0.949-0.997) | 17.62 | 0.971 (0.945-0.997) |
| **network + the planner in witchspace (`WSP=1`)** | **20.03** | **0.986 (0.966-1.006)** | **18.66** | **1.028 (1.006-1.052)** |

By kind, fair mode: witchspace 20.50 against 20.06 (1.022), pirate packs 12.29 against 11.74 (1.047), small
fights 7.34 against 6.97 (1.052). No deaths in any of the 450, for either pilot.

### Careers to Elite, fair mode

Seeds 0, 13 and 21 (`SKEW`), the same build, game hours when the tally reached 6,400 (the journals are in
[careers/](careers)):

| seed | network | C pilot | |
|---|---:|---:|---|
| 0 | 27.2 | 27.9 | ahead 0.7 h |
| 13 | 28.4 | 28.5 | ahead 0.1 h |
| 21 | 26.0 | 26.9 | ahead 0.9 h |
| **mean** | **27.2** | **27.8** | 2.0% faster |

Three careers each: the network ahead on every seed is consistent with the lab, but the lab's 450 paired fights
are the evidence. (Fair mode's published 29.8 hours, in the README's chart, was an earlier build; this build's fair
C takes 27.8 on these seeds.)

## Why fair mode

Efficient mode reads exact positions, headings and energies from RAM; fair mode gets them from what's drawn, through
a tracker (fair.c), and every pilot loses about 11% to that (both: 0.893 and 0.892 of their efficient selves). The
C law steers with the tracker's estimates as if they were exact. The network is the better tracker of a target
(below) and holds its lead over the C in normal space when the information gets worse, where the C's lead in
witchspace comes from a planner that works equally well either way. So in fair mode the network's strengths count
for more, and with the planner for the swarms it comes out ahead.

## How it was built

**v1** ([v1/](v1)): 2,053 weights written to steer on lateral offsets in ship space, about 72% of the C's points
a minute on the hard fights. Its two in-game fixes (a kick past the joystick's dead zone, gains banded by range)
were weight edits found by reading the lab's numbers.

**v2, written** ([elite_wsp/author.py](elite_wsp/author.py)): the C pilot's steering law (pilot.c `steer`) and
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
a swarm, a fight is forty acquisitions, so the planner flies those in witchspace and the network everything else.

**Four inputs about the senses' confidence** (fair.c's tracker): whether the heading was seen on a wireframe or
guessed from motion, how stale the sighting is, the range's and the motion's uncertainty. In efficient mode they
read "seen, fresh, exact". The written law doesn't use them yet: gating the lead and the fire cone by them was
tried and cost points. They're there for what comes next.

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
  the C pilot's val score moved from 18.9 to 20.8 with how the list was split. Every fight now runs in a fresh
  process (the emulator starts in a few milliseconds).
- **Small sets.** On 40 fights the same pilot's score swings about 3%, as large as the effects being chased. The
  results here are on 450 fights with paired intervals; val (40) was only for watching evolution.

(The repo's pinned test, tests/career120.sh, fails on clean `main` in this environment too: its hash was pinned on
another machine. `WSP` off, this branch writes the identical journal to `main`'s.)

## Running it

```
cplayer/buildlab.sh && cplayer/build.sh
WSP=1 FAIR=1 SKEW=13 cplayer/run_sim 3000 "" journal.jsonl     # a fair career, the network flying
python3 -m elite_wsp.author                                     # write the weights from author.py's defaults
python3 -m elite_wsp.inspect elite_pilot.npz                    # read them back, unit by unit
python3 tests/test_pilot.py                                      # the weights against their spec, C against numpy
cd evolve && python3 -c "import lab; print(lab.fmt(lab.score(lab.run(lab.read_set('big'), wsp='written_es14.wsp', extra_env={'FAIR':'1'})[0], by_kind=True)))"
```

The fight sets in [evolve/sets/](evolve/sets) name recorded fights: `FIGHTS=fights2 run_sim 720` and
`FIGHTS=fights3 run_sim 2400` (SKEW 0, the C pilot) record them. `train` (180) was the only set evolution saw; `big`
(450) and `test` (60) are held out.

| Switch | |
|---|---|
| `WSP=1` | the network flies the fight |
| `WSPPLAN=0/1/2` | the C's planner for acquisition: never, always, in witchspace (the default) |
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
evolve/lab.py           the fight lab in parallel, one process a fight; scores and paired comparisons
evolve/es.py            CMA-ES over the named parameters
evolve/ppo.py           PPO on a residual (tried; see above)
evolve/distil.py        DAgger on the planner (tried; see above)
careers/                the fair careers' journals, both pilots
v1/                     the first version: its lab results, the web demo and the film scripts
ROADMAP.md              what's next
```

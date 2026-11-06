# The modes' own pilots, written in weight space

Fair mode's dogfights and dockings are flown by two AI-authored networks ([README.md](README.md)). This is the same method
applied to the other two modes, each for its own goal, written from scratch rather than transcribed from the C rules:

- **Optimal: the interceptor** (`elite_wsp/author_modes.py`, `build_interceptor`; 4,019 weights, 244 non-zero, 5 hidden
  layers). Its goal is points a minute. Its senses are exact (it reads the game's RAM), so it can afford things fair mode's
  pilot can't: a hard pull onto a target far off the nose, a wide roll cap.
- **Curious: the camera** (`build_camera`; 4,839 weights, 318 non-zero). Its goal is what a viewer sees, scored by the
  show meter (`tools/show.py`): the enemy drawn big, moving, passes, the kill seen close. It flies every Curious fight,
  from the first on a pulse laser (not `show.c`'s margin, which held it back until a beam laser and a thousand credits:
  the mode is the show). Only danger hands a fight to the interceptor: the energy under 128 (under 200 in witchspace,
  above the bail-out level), or a missile homing with no ECM.
- **Docking** keeps the fair-mode network's weights in both modes, with one authored change for Optimal (below).

Every unit is named, every constant a parameter. The weights were written, measured in the labs, read where they lost,
and fixed by hand, one change at a time, as the method has it ("look at how it loses, change the part of the network
behind it, keep a change only if it wins more"). No evolution was used on them.

## Results

Held-out sets, each fight in its own process (`evolve/es_modes.py`). Points a minute; the show score (0-100).

| | fights (test set) | notes |
|---|---:|---|
| **Optimal: C rules** | 21.52 ppm (372 fights, 0 deaths) | the rules the interceptor is measured against |
| Optimal: fair mode's network, as shipped | 21.18 | |
| **Optimal: the interceptor** | **20.85** (0 deaths) | witchspace 21.5, packs 13.1, small fights 11.8 |
| **Curious: C rules + show.c** | show score 26.9 (317 fights, 19 deaths) | the show on only while clearly on top; the meter's parts: screen 0.10, motion 0.46, close kills 0.15 |
| Curious: the camera, gated as the C's show is | 23.8 (17 deaths) | last night's: motion 0.32, close kills 0.09 |
| **Curious: the camera, every fight** | **22.6** (35 deaths) | close kills 0.20, passes 0.53, motion 0.38; fights 85 s against the C's 42: it holds fire until the target is drawn 20 px, and a show fight on a pulse laser is long. Witchspace 35.9 |

Docking (`evolve/es_dock.py`'s cost: seconds to dock, a timeout or a death added, plus the look: reversals, crawling, swings):

| | 120 held-out starts | docked |
|---|---:|---:|
| Optimal: C law | 180.6 | 110/120, median 166 s |
| Optimal: fair mode's network as shipped | 190.9 | 107/120, median 169 s |
| **Optimal: the network, the jump within 20 degrees** (`elite_dock_opt.wsp`) | **179.6** | 105/120, median 158 s |
| Curious (600 s window): C law | 311.1 | 92/120, 17 deaths |
| Curious: the network as shipped | 297.5 | 95/120 |
| Curious: gentler, a wider way round | 294.8 | 97/120 (within noise: Curious keeps the shipped weights) |

(Curious mode's dockings start from wherever its wandering left it, up to 60,000 units out, so its lab needs the longer
window; a sixth of its starts still end in a death or a timeout for either pilot.)

Careers (native `run_sim`, seeds 13 and 101; Optimal to the Elite tally, Curious the tally at 25 game hours):

| | seed 13 | seed 101 |
|---|---:|---:|
| Optimal, C rules | 14.8 h, 0 deaths | 15.1 h, 0 deaths |
| Optimal, the interceptor | 17.0 h, 1 death | 15.9 h, 0 deaths |
| Curious, C rules + show.c | 1,310 pts, 26 deaths | 1,358 pts, 29 deaths |
| Curious, the camera (the interceptor off-show) | 1,404 pts, 24 deaths | 1,320 pts, 21 deaths |

The interceptor's seed-13 career lost two hours to one early death (at 1.9 h, shields down after a won witchspace fight)
and the path it took after: by 8 h it was travelling for a galactic hyperdrive while the C rules were already farming.
Witchspace points a minute were level (12.5 against 12.2). A career is one path; the lab's paired fights are the measure.

## How the interceptor was written, and what the lab said

The first draft scored 12.3 ppm against the C's 21.5. Each step below was one edit, measured on 73 validation fights:

1. **The speed law as a sum** (range term + turn term) let the range swamp the turn: it flew at 40 while turning where the
   C stops. Rewritten as an AND (aligned × a band by range): the whole set rose, witchspace most.
2. **A nose hold** (no roll inside the fire cone) looked like a fix for the roll chattering at full rate on a target a
   degree off the nose. It lifted the set to 17.5 at first, then the trajectory diff against the shipped network showed
   why it was wrong: on the nose, the shipped network keeps rolling to bring the target into the vertical plane for the
   pitch; with the hold, mine sat still until the error left the hold, then slammed the roll: a limit cycle, 4.9 degrees
   off the nose on average against 1.4. A roll doesn't move the nose off the target, so the chatter was harmless. Hold
   removed.
3. **Pitch feed-forward** (pitch with the target's motion across the sky) cost points in every setting. Removed.
4. **Firing on the led aim** instead of the target itself: the laser hits where the nose points now. Fixed.
5. **The roll cap** (how much roll the clock position allows): 1.1 → 1.5 gained a point; 2.0 and 2.5 gave it back.
6. Tried and dropped, all measured: a capture gain inside the last two degrees (every value lost), a hard turn from 54
   degrees instead of 40, a longer or shorter lead, a wider fire cone, flying on while turning.

The tool that found the nose-hold fault, and the speed fault before it: run the shipped network with `WSPLOG=`, then
compute the new network's outputs on the shipped network's own logged senses, and read where they differ. On identical
senses the two agreed except on the speed; after the speed was fixed, except on the roll near the nose.

Where it stands: 97% of the C rules and 98% of the shipped network, with no deaths, on fights recorded from Optimal
careers. Not better yet. The one place the exact senses were expected to pay (the final capture into the sights) didn't.

## The camera

The camera flies every fight, so it meets what the C's show never did: witchspace on arrival, packs on a pulse laser, the
energy running down. As first flown that way it scored 0 on every witchspace fight: its gentle stick and dead zone never
got a Thargoid in the sights, the energy fell under the bail-out level and the agent jumped out with no kills. Written
for it, one unit: `sharp`, on in witchspace or with the energy under 160, which switches the dead zone and the pass off,
amplifies the error (the stick 2.5 x sharper) and brings the hard turn in from 40 degrees. Witchspace went from 0 to 35.9
(the C: 36.5) and deaths on the test set from 37 to 19; putting the "drawn big" gate back on the laser (no kills at a
distance: the kill is the close-up) took close kills from 0.12 to 0.20 and deaths back up to 35. Curious mode is meant
to be watched, and a death now and then is part of it; the kill at a distance isn't.

The show meter wants the enemy drawn big, moving across the frame, passes, kills seen close, and no empty screen. Written
for that: a hold at the size where the target is drawn 35 pixels across, speed by how big it is drawn (a follow that
speeds up as it falls behind), a pass (aim aside) as a fast closer looms, a dead zone so the target drifts instead of
sitting dead centre, a slow roll while on it, the laser whenever in the sights (the meter counts the laser as motion).
Each of those is one or two named units.

What the lab said, on 55 validation fights (the C: 30.2): as first written 23.3; firing only on the chase 24.4; a wider
dead zone lost (22.7-23.6: the target left the frame); the C's own range, firing whenever in the sights, and a hard turn
only once the target is behind 24.9; holding closer, where it is drawn 35 pixels, 26.2; 48 pixels, 27.5. The slow roll
(orbit) was level with none. The parts still short of the C are motion (0.30 against 0.55) and close kills: the C's show
strafes while it follows and watches a flyby from the side view, which this network can't do (it has no view switch), and
a pass that the meter counts needs the enemy to cross the frame, which a steering law that keeps it in the sights
prevents. The next thing to write is a flyby: aim beside the target on purpose for a second as it closes, let it cross,
then turn after it.

## Docking: the jump cone

In Optimal mode the fair-mode docking network timed out on 5 of 60 starts the C law docked. Its trace: it asks for the
in-system jump only once the planet is within 10 degrees of the nose; by then the station is inside the game's bubble and
the jump is refused, and it flies 100,000 units at speed 40. The C law jumps earlier because its cruise steering is
faster. One parameter, `jump_cos`: the jump asked for within 20 degrees takes the network from 190.9 to 179.6 on 120 starts,
level with the C law and 11 s faster in the median. At 16 degrees the gain halves; at 26 it jumps off course and loses it
all back. That set is compiled in as Optimal mode's (`cplayer/dock_opt_weights.h`); the loader picks a set by mode.

For Curious, a gentler roll and pitch and a wider way round the station cut deaths from 16 to 11 of 120 on the network as
first written, but on the shipped (evolved) parameters the same change is within noise, so Curious flies the shipped set.

## The page

`?mode=optimal|fair|curious` ([web/](../web)), or the Mode switch. Each mode flies its pilots inside `core.wasm`; the panel
draws whichever network made the last decision (`wasm_fight_net`), seven columns for these networks. Fair mode's journal on
this build is byte-identical to the verified one. The dev page is at `/dev/` while this is on a branch.

In Curious mode the camera flies from the first fight (40 game minutes on the dev page's build: 70,410 frames the
camera's, 50 the interceptor's). The Deadly button on the options page jumps to the film career's save at Deadly rank.

## Running it

```
cd wsp && python3 -m elite_wsp.modes_export            # the .npz, .wsp and the compiled-in headers, from the defaults
cd evolve && python3 es_modes.py sets WORK             # the fight sets from WORK/rec/*/fights (run_sim FIGHTS=dir)
python3 es_modes.py score eff val [written|file.wsp]   # Optimal: points a minute; "C" for the rules
python3 es_modes.py score cam val [written|file.wsp]   # Curious: the show score
PILOTS=1 cplayer/run_sim 1500                          # a career on the mode's own pilots (CURIOUS=1 for the camera)
CAMGATE=1                                              # the lab: the camera only while show.c's margin is on (last night's gate)
WSPINTW=file.wsp WSPWS=file.wsp                        # other weights for the interceptor and the camera
```

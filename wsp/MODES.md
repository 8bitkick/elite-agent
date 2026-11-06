# The modes' own pilots, written in weight space

Fair mode's dogfights and dockings are flown by two AI-authored networks ([README.md](README.md)). This is the same method
applied to the other two modes, each for its own goal, written from scratch rather than transcribed from the C rules:

- **Optimal: the interceptor** (`elite_wsp/author_modes.py`, `build_interceptor`; 4,019 weights, 244 non-zero, 5 hidden
  layers). Its goal is points a minute. Its senses are exact (it reads the game's RAM), so it can afford things fair mode's
  pilot can't: a hard pull onto a target far off the nose, a wide roll cap.
- **Curious: the camera** (`build_camera`; 4,359 weights, 270 non-zero). Its goal is what a viewer sees, scored by the
  show meter (`tools/show.py`): the enemy drawn big, moving, passes, the kill seen close. It flies while the show is on
  (`show.c`'s margin: the agent clearly on top); the interceptor flies the rest.
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
| **Curious: C rules + show.c** | PENDING_CAM_C | |
| **Curious: the camera** | PENDING_CAM_NET | |

Docking (`evolve/es_dock.py`'s cost: seconds to dock, a timeout or a death added, plus the look: reversals, crawling, swings):

| | 120 held-out starts | docked |
|---|---:|---:|
| Optimal: C law | PENDING_DOCK_C | |
| Optimal: fair mode's network as shipped | 191.6 | 105/120 |
| **Optimal: the network, the jump within 20 degrees** | PENDING_DOCK_NET | |
| Curious (600 s window): C law | 311.1 | 92/120, 17 deaths |
| Curious: the network as shipped | 305.0 | 97/120, 16 deaths |
| **Curious: gentler, a wider way round** | 286.3 | 98/120, 11 deaths |

Careers (native `run_sim`, 25 game hours at most): PENDING_CAREERS

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

The show meter wants the enemy drawn big, moving across the frame, passes, kills seen close, and no empty screen. Written
for that: a hold at the size where the target is drawn 35 pixels across, speed by how big it is drawn (a follow that
speeds up as it falls behind), a pass (aim aside) as a fast closer looms, a dead zone so the target drifts instead of
sitting dead centre, a slow roll while on it, the laser whenever in the sights (the meter counts the laser as motion).
Each of those is one or two named units.

PENDING_CAM_STORY

## Docking: the jump cone

In Optimal mode the fair-mode docking network timed out on 5 of 60 starts the C law docked. Its trace: it asks for the
in-system jump only once the planet is within 10 degrees of the nose; by then the station is inside the game's bubble and
the jump is refused, and it flies 100,000 units at speed 40. The C law jumps earlier because its cruise steering is
faster. One parameter, `jump_cos`: the jump asked for within 20 degrees: PENDING_DOCK60. At 26 degrees it jumps off
course and loses it all back.

For Curious the same network with a gentler roll and pitch and a wider way round the station docked more and died less;
the deaths are the measure that moved.

## The page

`?mode=optimal|fair|curious` ([web/](../web)), or the Mode switch. Each mode flies its pilots inside `core.wasm`; the panel
draws whichever network made the last decision (`wasm_fight_net`), seven columns for these networks. Fair mode's journal on
this build is byte-identical to the verified one. The dev page is at `/dev/` while this is on a branch.

In Curious mode the camera appears only once the show is on, which in a career is after the first beam laser and a
thousand credits: an hour or two in.

## Running it

```
cd wsp && python3 -m elite_wsp.modes_export            # the .npz, .wsp and the compiled-in headers, from the defaults
cd evolve && python3 es_modes.py sets WORK             # the fight sets from WORK/rec/*/fights (run_sim FIGHTS=dir)
python3 es_modes.py score eff val [written|file.wsp]   # Optimal: points a minute; "C" for the rules
python3 es_modes.py score cam val [written|file.wsp]   # Curious: the show score
PILOTS=1 cplayer/run_sim 1500                          # a career on the mode's own pilots (CURIOUS=1 for the camera)
WSPINTW=file.wsp WSPWS=file.wsp                        # other weights for the interceptor and the camera
```

# Fighting, efficient mode

Goal ([FIGHTING.md](FIGHTING.md)): every fight over as soon as possible, and the fewest game hours from
Commander Jameson to Elite, with no deaths. This page: how it's measured, where the time goes, and each
experiment.

## Measures

**Careers** (the primary measure): `run_sim` from Jameson, efficient mode, `SKEW` 13, 101, 37, 71, 149,
game hours to the rank of Elite (`tools`: the journal's `rank` event). A build is deterministic: the same
build and seed give the same career to the frame. Any change in behaviour makes the careers diverge
chaotically after the first fight it touches, so one career is noisy (30.6 to 33.8 h on the baseline); the
mean of five, and how many of five get faster, are what count.

**Fights**: `cplayer/lab` on all 9327 recorded fights (`~/work/all.lst`, `tools/suite.py run all`), per
scenario (pirates by pack size and size of ship, witchspace). The lab prints an `FX` line per fight (lab.c),
which suite.py keeps in the run's json and `tools/effstats.py` sums and compares:

| Measure | What it is |
|---|---|
| clear | seconds from the fight's start to the first frame with no hostile ship about (any range). Fights that never clear (a pirate that keeps its distance) are left out of the mean, with the share cleared shown |
| per kill | seconds in `engage()` over kills: the scenario's sums |
| first kill | seconds to the first kill |
| taken | energy + shields lost, per fight (the lab's measure) |
| pts/min | points over the lab's minutes (the fight, then 3 s of quiet; in witchspace the lab chains mis-jumps as a career does, for up to 4 minutes, so this is the farming rate without the trip home) |
| engaged split | frames in `engage()` by what we're doing: slewing (target > 10 deg off), tracking (within 10), in the sights, no target, breaking off, turning a shield to a missile, dodging a ram, evading; `far`: target beyond 6000 |
| wait / count | seconds with hostiles about but not in `engage()` (regenerating, on the chart), and in a hyperspace countdown |

Pairs (effstats.py A B): the mean change in clear time over fights both cleared, with a 95% bootstrap
interval; time per kill; damage; deaths (and compare.py for points).

Why these and not only pts/min: the lab's pts/min mixes kill speed with how many enemies a fight has and when
the lab stops. Time to clear is what a career waits for: it can't travel (the in-system jump is refused with
any ship about) or chain the next mis-jump until the fight is over.

## Where career time goes (baseline, 5 careers)

| | hours | share |
|---|---|---|
| to Elite | 30.6-33.8 (mean 31.4) | |
| in fights (`engage()`, all of them) | 3.9-4.1 | 13% |
| of which witchspace | 2.3-2.5 | 8% (77% of the points) |
| witchspace sessions, dock to dock | 17.0-19.2 | 55-57% (89% of the points) |
| hunting sessions | 4.1-4.7 | 14% |

A witchspace session (seed 13 at 18 h, typical): 0.2 min docked, launch and the jump; about 1 min in
witchspace for three mis-jumps (each: the fight 0.32 min, then 0.16 min to the next arrival); then **about 5
minutes getting back to the station** from the system the way out leads to. The trip home is where most of
the session goes, and much of it is spent with the in-system jump (J) refused because a ship is about:

| J refused with the planet far ahead, why (JLOG, seeds 13, 101, 37 at 31 h) | hours per career |
|---|---|
| a hostile within 16954 but not fought: travel only fights within **9000** (a number from the first C port, never tested); they spawn ~11000 out and chase us at about our speed | 0.72-0.88 |
| a hostile beyond 16954 (keeps its distance) | 0.18-0.45 |
| an exploding ship | 0.09 |
| a non-hostile ship (a trader) | 0.08-0.10 |

## Where fight time goes (baseline, all 9327 fights)

```
scenario             n deaths pts/min |  clear (cleared) | per kill | first kill | taken | engaged: slew track in break | far
pirates 1 big     1328    2   10.60   |  4.0 s ( 94%)    | 4.1 s    | 3.9 s      |   39  | 61% 15% 21%  0%  | 92%
pirates 1 small   2920    3    8.07   |  3.8 s ( 93%)    | 3.5 s    | 3.7 s      |   38  | 63% 14% 16%  4%  | 89%
pirates 2-3 big    116    2    8.64   |  9.8 s ( 34%)    | 4.9 s    | 5.3 s      |   83  | 47% 16% 15% 19%  | 72%
pirates 2-3 small 2579    8    7.00   |  7.2 s ( 86%)    | 3.1 s    | 5.7 s      |   79  | 30% 19% 14% 35%  | 58%
pirates 4+ big      49    1    6.69   | 19.2 s ( 27%)    | 4.7 s    | 7.7 s      |  113  | 28% 27% 16% 27%  | 66%
pirates 4+ small  1066   11   10.71   | 10.6 s ( 82%)    | 2.9 s    | 6.3 s      |  147  | 34% 22% 18% 23%  | 59%
witchspace        1269    0   18.31   | 18.7 s (100%)    | 2.6 s    | 3.9 s      |  412  | 56% 21% 20%  0%  | 64%
ALL               9327   27   13.09   |  7.7 s ( 90%)    | 2.9 s    | 4.6 s      |  114  | 48% 20% 18% 12%  | 67%
```

- **Slewing is the largest share** (48%): turning from one target to the next at the pitch limit (~1.8 deg a
  game loop). Kills are mostly at range (the target beyond 6000 for two thirds of the time).
- **Breaking off** (running, hurt, from two or more) is 12% overall and a third of 2-3 small packs (the
  corpus has many hurt starts: chained fights).
- Witchspace: 6 to start (3 Thargoids, 3 Thargons, in pairs), a kill every ~2.5 s, clear in 18.7 s; then the
  lab chains. Frame trace (jameson/00599): the pairs die together at 10000+; most of the fight is two slews
  of 55-70 deg between the groups and one to the last Thargon. After the last kill of the last mis-jump the
  career waited ~6 s for full strength before leaving, though no chain was to come.

## Experiments

Tiers: **300** = `eff300` (stratified, seeded, none from `mid`: 30/40 single big/small, 25/60 2-3 big/small, 25/60 4+
big/small, 60 witchspace) to screen; **all** = the 9327 to confirm; **careers** = the five seeds. Intervals are 95%
(bootstrap, paired).

| Experiment | Result | Status |
|---|---|---|
| **Travel fights at the engage range** (FIGHTR; `fight_if_needed` fought only within 9000, a number from the first C port: pirates spawn ~11000 out and chase us at about our speed while J is refused, 0.7-0.9 h a career), with WSNOREGEN | careers: 31.46 -> **27.35 h** (13: 30.83 -> 26.70, 101: 31.29 -> 27.83, 37: 30.59 -> 27.25, 71: 30.75 -> 28.52, 149: 33.84 -> 26.46; all five faster), deaths 9 -> 3; witchspace sessions 17.0 -> 14.4 h a career (4.99 -> 6.05 pts/min); normal-space fights 1.63 -> 1.09 h (seed 13). Not visible to the lab (its fights start at the 9000 trigger) | **in** (efficient; curious keeps 9000) |
| **No regeneration before leaving witchspace when no chain is to come** (WSNOREGEN; it waited ~6 s for full strength on the last mis-jump of every session, then left anyway) | 300: witchspace pts/min +7.6% [+5.4, +10.6]; all: witchspace +9.0% [+8.4, +9.7], nothing else changed, deaths 27 -> 27 | **in** (efficient) |
| Fire at any enemy in the sights, not only the target (OPPFIRE: Elite's laser hits every ship in the crosshairs) | all: pts/min +0.2% [-0.4, +0.7], clear -0.2%, deaths 27 -> 30: the target's firing cylinder (1.43 hit radii) already covers what crosses the sights | rejected |
| Target worth counts the ships near it in our view (CLUDEG: within 5/10/20 deg, by closeness) | all: clear +1.1 / +2.0 / +1.6% (worse), packs 4+ pts/min +2.1 / +2.2 / +4.0%, witchspace pts/min -1.0 / -0.5 / -1.5%, deaths 27 -> 29 / 32 / 30. Pirates only (CLUWS=0), 300: -0.1%; weight 2: -2.1% | rejected |
| **Break off on the rear laser** (BRKREAR: the run from a pack, tail to them, flown as a rear-view attack on the nearest at full speed, instead of a blind run; the rear laser was idle while they sat astern) | all: pts/min +7.4% [+6.6, +8.4] (2-3 small +28.5%, 4+ small +18.1%, 2-3 big +12.9%, witchspace 0), clear -6.7% (2-3 small -19.4%), per kill -10.7%, damage -8%, deaths 27 -> 20 (only A 9, only B 2); careers (on the travel-reach build): 27.35 -> **26.92 h** (13: 26.89, 101: 27.34, 37: 26.15, 71: 27.68, 149: 26.52; 3 of 5 faster), deaths 3 -> 4 | **in** (both modes) |
| With it, when to break (300): brk_clear 4000 / 0 (break with them closer), brk_n 1 (also from one), sooner (energy 250, shield 150), brk_until 255 | pts/min -2.9 / -5.4 / +0.2 / -0.5 / 0.0% | rejected (the old thresholds stand) |
| Go after hostiles beyond reach that keep J refused on the way to the station (CHASE=40000: steer at the nearest at full speed until it's in reach; give up on one that opens for 10 s or after a minute) | careers: 27.35 -> 27.16 h (3 of 5 faster, -0.88 to +0.14 h), deaths 3 -> 3; cruising with J refused (planet far ahead) 0.41 -> 0.38 h a career: little left to win | rejected (noise; off) |
| Witchspace: what a visit spends (bw, 200 fights, 368 visits) | the last Thargoid dies at 12.0 s; then 4.2 s on 2.2 Thargons (0.28 pts, 3% of the visit's points); 7.5 s more to the next arrival (regen, chart, mis-jump toggle, countdown). Leaving the Thargons for the countdown would trade ~18% of visit time for 3% of points and their fire on the chart: about even | not pursued |
| The CMA-ES fight parameters (tuned before the joystick and the planner) one at a time on the current build, 300: hit_rate 0.3/0.8, t_floor 0/0.5, w_threat 0.7/2.8, lead 4/5, pitch_iters 2.5/3.5, roll_iters 1.4/2.1, spd_far 22/32, spd_mid 25, spd_near 8, far 3000/6000, turn_off 10/25, evade_shield 0/30, ram_dist 300/600 | all within -6% .. +0.6% overall: the aim's (lead, pitch_iters, roll_iters, turn_off) lose 2-6%; the rest flat. Hints only: t_floor 0 pirates +3.0% [+0.2, +6.3], evade_shield 0 pirates +1.1%; on mid (889, pirates only): -0.2% and -0.1% | the parameters stand |
| No break-off (brk_energy=0) | 300: pts/min +6.8%, clear -8.7%, deaths 1 -> 2; all: clear -9.3% (2-3 small -25%), pirates pts/min +8.5%, but deaths 27 -> 80 (only A 5, only B 58) | rejected (the break-off still saves lives) |
| Fight-parameter screens on 300: spd_turn 10 (move while turning), hot 240, fire_r 2, t_switch 1.2, witchspace rear laser at any range | pts/min -3.2 / +1.6 / -1.0 / +1.3 / +1.1%, all within noise (intervals span 0) | rejected (no signal) |

## Current best

| | baseline (c36b082) | now |
|---|---|---|
| careers to Elite, mean of 5 | 31.46 h (30.83, 31.29, 30.59, 30.75, 33.84) | **26.92 h** (26.89, 27.34, 26.15, 27.68, 26.52) |
| career deaths, 5 seeds | 9 | 4 |
| all fights: pts/min | 13.09 | 15.14 (witchspace 18.31 -> 21.09, pirates up 2-29%) |
| all fights: clear | 7.7 s | 7.2 s |
| all fights: per kill | 2.9 s | 2.5 s |
| all fights: damage taken | 114 | 99 |
| all fights: deaths | 27 | 20 |

(Seeds 13, 101, 37, 71, 149, in that order. The fight numbers are the lab's; the travel reach doesn't show
there.)

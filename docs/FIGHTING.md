# Fighting: two modes

The player has two career modes (`g_curious`): **efficient** (straight to Elite) and **curious** (explore
every galaxy). Until now both fought the same way. This page sets a separate fighting goal for each, how
it's measured, and where the work stands. The details and experiment logs are in
[fighting-efficient.md](fighting-efficient.md) and [fighting-showcase.md](fighting-showcase.md).

Both are built on the current default (joystick, lead 4.5, acquisition planner `PLAN=1`): all 9327 fights,
acquire 2.11 s, hold 79.2%, 27 deaths; careers to Elite 31.4 game hours (5 seeds).

## 1. Efficient mode: kill fastest, reach Elite soonest

**Goal:** each fight ends as soon as possible with every enemy dead; overall, the fewest game hours
from Jameson to Elite. Deaths are allowed: with a save at every dock a death costs only the trip since
and a reload, which the career hours already count, so a riskier rule that gets to Elite sooner wins.

| Metric | What it is | Why |
|---|---|---|
| **Game hours to Elite** (primary) | `run_sim` careers, the same 5 seeds, efficient mode | the end goal |
| Deaths (careers + all fights) | reported, not a constraint | their cost is already in the hours |
| Time to kill (TTK) | seconds from engage to each kill, per scenario | the per-fight goal |
| Fight clear time | seconds until no hostiles remain | whole-battle speed |
| pts/min | tally gained per minute of fight | combines kills and points |
| Damage taken | energy + shields lost a fight | the cost of speed |

A change is **in** when the career hours fall. Fight-lab numbers screen candidates; careers decide.

## 2. Curious mode: battles worth watching

**Curious mode's mission:** explore new star systems and their species (each system's inhabitants, on the
Data on System screen), meet every ship type and the rare ones (Thargoids and Thargons, the Cougar,
the missions' Constrictor), find the anomalies (witchspace mis-jumps), have cool battles,
and keep progressing (rank, ship, credits, missions) on the way to Elite. Progress matters, but the
career's hours aren't optimised the way efficient mode's are.

**Goal:** whenever the ship has the upper hand, fights are a show. Enemies are kept on screen, close
and in numbers, in a variety of views: mostly the front, with side glances as ships pass and the
occasional rear laser. The fights use varied and unusual techniques and weapons, and the ship plays
with the enemy. Fights run as long as they stay interesting and end only when they turn repetitive. Without the upper hand,
it fights as in efficient mode.

| Metric | What it is | Why |
|---|---|---|
| **Size x time on screen** (primary) | per fight: seconds with an enemy drawn 20 px or more (and 40 px), and the largest enemy's drawn size summed over frames, per fight minute | what makes a fight worth watching is how big and for how long the enemies are on screen; distance shots don't do it |
| Show score | a weighted mix of the measures below, per fight | tie-breakers once size x time is there |
| Screen presence | % of fight frames with ≥1 enemy drawn in the view on screen (front, rear, left or right); mean ships on screen | the viewer sees the fight |
| Closeness | mean distance of on-screen enemies; frames with one big on screen (near) | close is exciting |
| Technique variety | distinct techniques a fight (front/rear laser, missile, ECM, energy bomb, flyby, loop, roll-under, bait, a side glance at a pass), views used, entropy across fights | not one trick every time |
| Superiority | damage taken, near misses vs hits | skill, not luck |
| Novelty | first meeting with a ship type, rare types (Cougar, Constrictor), Thargoids, mis-jumps | the mission: new things to see |
| Pacing | no fixed time limit: a fight hands over to efficient tactics only when it turns repetitive (a long straight-line chase, the same target held unchanged, circling without closing, nothing new on screen); no dead air | long is fine, repetitive isn't |
| Repetition | similarity of technique sequences between consecutive fights | not repetitive |
| Our deaths | a risk to take for style: use the budget, up to ~5% of showcase fights (enemy deaths are the point) | losing the ship ends the show and costs a reload; the career cost is measured separately |
| Career progress | one confirmation career: reaches Elite; tally per game hour | checked, not tuned against |

**When to show off (dynamic):** a superiority margin checked throughout the fight: our weapons (laser,
missiles, energy bomb, ECM) and our current energy and shields against the threat (how many, what types,
their weapons, how many are behind or closing, Thargoids). It shows off while the margin is high and
drops to efficient as soon as it falls (low shields, more ships, a missile coming), with hysteresis so it
doesn't flap. A beam laser can show off against a lone Sidewinder; a military laser on low shields
facing a pack can't.

## Setup

- Fights: `cplayer/lab` over the 9327-fight corpus (`~/work/all.lst` on the macbox) via `tools/suite.py`.
- Careers: `run_sim` on the same seeds, efficient (`AGGR=1`) and curious (`CURIOUS=1`).
- The macbox has 6 cores: **3 for each mode** (`NPROC=3`). Working copies: `~/work/eff` and `~/work/show`.

## Progress

| Date | Mode | Change | Result | Status |
|---|---|---|---|---|
| 2026-10-18 | both | goals and metrics set; baseline = PLAN=1 default | see above | started |
| 2026-10-18 | efficient | travel fights at the engage range (not 9000), no regen before leaving witchspace without a chain | careers 31.5 -> 27.4 h (5 of 5 faster), deaths 9 -> 3; lab witchspace pts/min +9% | in |
| 2026-10-18 | both | break off on the rear laser (the run from a pack as a rear-view attack) | all fights pts/min +7.4%, clear -6.7%, deaths 27 -> 20; careers 27.35 -> 26.92 h | in |
| 2026-10-18 | efficient | no break-off (deaths a cost, not a limit) | careers 26.90 -> 26.18 h over ten seeds (7 of 10 faster), deaths 14 -> 8; lab deaths 20 -> 80 | in |
| 2026-10-18 | curious | show score defined and measured in the lab (`showmeter.c`, `tools/show.py`): any view, sizes from Elite's projection, completion-gated; baseline (curious as efficient) 16.2 on 8,727 hold-out fights, nothing bigger than a dot on screen 96% of the time | [fighting-showcase.md](fighting-showcase.md) | in |
| 2026-10-18 | curious | showcase fighting (`show.c`): hold fire until 24 px, close at full speed, dynamic superiority margin (on 2 / off 1), time budget 15 s + 10 s an enemy, flybys with a knife-edge pass and a side glance, missiles, the bomb finale, Thargoids, a close look at rare types | hold-out: show 16.2 → 35.6 (+19.4 [+19.1, +19.7]); deaths 3.0 → 6.5 per 1000 (budget 50); fight median 4.4 → 25.8 s, p90 74 s; points a fight unchanged | in |
| 2026-10-18 | curious | no show on a pulse laser; a travel leg's deadline stops while fighting | career seed 13, first 10 h: deaths 9 → 6, military laser 6.4 → 3.3 h; confirmation career (seed 13): Elite at 149.8 h, 81 deaths in 5,406 fights; careers still lose 1.5–2% of fights to the show (lab 0.65%): waves at anarchies meet a ship with a low front shield | in |
| 2026-10-18 | curious | score 2 (static screens count as dead air, a motion part); round 2: a hold-fire weave, follow a flyby into the side and rear views, predicted glances, an empty screen spends the budget double, the bomb at Thargons, witchspace margin 1.2/0.7 and Thargoids held to 24 px | hold-out (4,232, score 2): efficient 11.3, round 1 28.9, round 2 34.1 (+5.3 [+4.9, +5.6]); static runs 28% → 4% of fight frames; fight p90 70 → 48 s; deaths 21 → 14 | in |
| 2026-10-18 | curious | careers: the hunting-route ping-pong after galaxy 8 (no session scored, so no witchspace; 85 h stalled) fixed; no new leg while hurt (the recharge in place was worse) | Elite at 116.9 h (seed 13; round 1: 149.8 h) and 118.8 h (default seed); deaths 51 / 50 (1% of fights); 387 / 382 systems | in |
| 2026-10-18 | curious | round 3, judged by size x time: no shot under 32 px (Thargoids too), none at receding ships; toying (hold a 24 px+ target 12 s before the kill, 600 behind it); budget 30 s + 15 s an enemy; witchspace targets by group; the weave stops by time | hold-out (4,232): an enemy drawn 20 px+ 0.0 -> 4.2 s a fight, size-time 105 -> 469 px.s a minute; deaths 1.4% of fights; fight median 35 s | in |
| 2026-10-18 | curious | a ping-pong guard for curious careers (trips alternating between two systems: hunt at the nearest anarchy) | confirmation career (seed 13, round 3): Elite at 109.7 h (round 2 116.9 h), 104 deaths (1.9% of fights), 219 systems, 8 galaxies; showreel docs/img/curious-show-reel.mp4 | in |
| 2026-10-18 | curious | round 4: ending on repetition, not a clock (12 s with nothing new on screen hands over; the time budget a 10-minute net); the margin 1 / 0.5; no-trade means hunt (a career reloaded one dead end for 80 h) | hold-out: an enemy drawn 20 px+ 4.2 -> 6.0 s a fight, size-time 469 -> 524 px.s a minute, kills drawn 20 px+ 13 -> 17%, deaths 1.4 -> 2.7%; career: Elite at 163.9 h, deaths 3.6% of fights | in |
| 2026-10-19 | curious | round 5: shoot a target about to ram us whatever its size; no show under 1000 Cr (the round-4 poverty trap); collisions measured | tune: rams 363 -> 138 per 1000 fights, kills drawn 20 px+ 17 -> 25%, deaths 21.7 -> 8.3 per 1000; career: Elite at 134.8 h (round 4 163.9), deaths 2.1%, 271 systems in 8 galaxies | in |

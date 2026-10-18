# Fighting: two modes

The player has two career modes (`g_curious`): **efficient** (straight to Elite) and **curious** (explore
every galaxy). Until now both fought the same way. This page sets a separate fighting goal for each, how
it's measured, and where the work stands. The details and experiment logs are in
[fighting-efficient.md](fighting-efficient.md) and [fighting-showcase.md](fighting-showcase.md).

Both are built on the current default (joystick, lead 4.5, acquisition planner `PLAN=1`): all 9327 fights,
acquire 2.11 s, hold 79.2%, 27 deaths; careers to Elite 31.4 game hours (5 seeds).

## 1. Efficient mode: kill fastest, reach Elite soonest

**Goal:** each fight ends as soon as possible with every enemy dead and no damage the ship can't afford;
overall, the fewest game hours from Jameson to Elite with zero deaths.

| Metric | What it is | Why |
|---|---|---|
| **Game hours to Elite** (primary) | `run_sim` careers, the same 5 seeds, efficient mode | the end goal |
| Deaths (careers + all fights) | must not rise | a death costs a reload and a trip |
| Time to kill (TTK) | seconds from engage to each kill, per scenario | the per-fight goal |
| Fight clear time | seconds until no hostiles remain | whole-battle speed |
| pts/min | tally gained per minute of fight | combines kills and points |
| Damage taken | energy + shields lost a fight | the cost of speed |

A change is **in** when the career hours fall (or stay level with fewer deaths) and no fight scenario gets
worse beyond its noise.

## 2. Curious mode: battles worth watching

**Curious mode's mission:** explore new star systems and their species (each system's inhabitants, on the
Data on System screen), meet every ship type and the rare ones (Thargoids and Thargons, the Cougar,
the missions' Constrictor), find the anomalies (witchspace mis-jumps), have cool battles,
and keep progressing (rank, ship, credits, missions) on the way to Elite. Progress matters, but the
career's hours aren't optimised the way efficient mode's are.

**Goal:** whenever the ship has the upper hand, fights are a show. Enemies are kept on screen, close
and in numbers, in a variety of views: mostly the front, with side glances as ships pass and the
occasional rear laser. The fights use varied and unusual techniques and weapons, and the ship plays
with the enemy. Fights end within a time limit and should not repeat themselves. Without the upper hand,
it fights as in efficient mode.

| Metric | What it is | Why |
|---|---|---|
| **Show score** (primary) | a weighted mix of the measures below, per fight | one number to tune |
| Screen presence | % of fight frames with ≥1 enemy drawn in the view on screen (front, rear, left or right); mean ships on screen | the viewer sees the fight |
| Closeness | mean distance of on-screen enemies; frames with one big on screen (near) | close is exciting |
| Technique variety | distinct techniques a fight (front/rear laser, missile, ECM, energy bomb, flyby, loop, roll-under, bait, a side glance at a pass), views used, entropy across fights | not one trick every time |
| Superiority | damage taken, near misses vs hits | skill, not luck |
| Novelty | first meeting with a ship type, rare types (Cougar, Constrictor), Thargoids, mis-jumps | the mission: new things to see |
| Pacing | fight length within a band, and a show-time limit per fight (a base plus some per enemy, then finish efficiently); kill spacing; no dead air (no enemy on screen) | not tedious |
| Repetition | similarity of technique sequences between consecutive fights | not repetitive |
| Our deaths | an acceptable risk for an exciting fight: budget up to ~5% of showcase fights (enemy deaths are the point) | losing the ship ends the show and costs a reload; the career cost is measured separately |
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

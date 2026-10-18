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

**Goal:** once the ship is outfitted for superiority, fights are a show. Enemies are kept in the front
view, close and in numbers. The fights use varied and unusual techniques and weapons, and the ship plays
with the enemy while staying safe. Fights should not drag on or repeat themselves. Before the ship is
outfitted, it fights as in efficient mode.

| Metric | What it is | Why |
|---|---|---|
| **Show score** (primary) | a weighted mix of the measures below, per fight | one number to tune |
| Screen presence | % of fight frames with ≥1 enemy drawn in the front view; mean ships on screen | the viewer sees the fight |
| Closeness | mean distance of on-screen enemies; frames with one big on screen (near) | close is exciting |
| Technique variety | distinct techniques a fight (front/rear laser, missile, ECM, energy bomb, flyby, loop, roll-under, bait), entropy across fights | not one trick every time |
| Superiority | damage taken, near misses vs hits, shield never below a floor | skill, not luck |
| Pacing | fight length within a band; kill spacing; no dead air (no enemy on screen) | not tedious |
| Repetition | similarity of technique sequences between consecutive fights | not repetitive |
| Our deaths | rare: an occasional loss is an acceptable risk for a better show; budget: no more than the efficient baseline's rate on the same fights (enemy deaths are the point) | losing the ship ends the show and costs a reload |
| Career cost | game hours to Elite in curious mode | must stay reasonable |

**Readiness gate (when to show off):** fitted for superiority (military laser or better, ECM, missiles,
energy unit, energy bomb when available) and healthy. Below the gate it fights as in efficient mode.

## Setup

- Fights: `cplayer/lab` over the 9327-fight corpus (`~/work/all.lst` on the macbox) via `tools/suite.py`.
- Careers: `run_sim` on the same seeds, efficient (`AGGR=1`) and curious (`CURIOUS=1`).
- The macbox has 6 cores: **3 for each mode** (`NPROC=3`). Working copies: `~/work/eff` and `~/work/show`.

## Progress

| Date | Mode | Change | Result | Status |
|---|---|---|---|---|
| 2026-10-18 | both | goals and metrics set; baseline = PLAN=1 default | see above | started |

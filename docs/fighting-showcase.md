# Showcase fighting (curious mode)

Curious mode's mission is to explore new star systems and their species, meet every ship type and the
rare ones (Thargoids and Thargons, the Cougar, the missions' Constrictor), find the anomalies (witchspace
mis-jumps), have cool battles, and keep progressing towards Elite without efficient mode's hurry
([FIGHTING.md](FIGHTING.md)). This page covers the battles: how the show is measured, when the ship shows
off, what it does, and how each step scored. The code is `cplayer/show.c` (the behaviour; its hooks in
`combat.c` are one line each and return efficient mode's own decision unless the show is on), the meter
`cplayer/showmeter.c` (lab only), and `tools/show.py` (the score). Efficient mode is untouched: every hook
checks `g_curious`.

## The primary measure: size × time (round 3 on)

The user: "a lot of this is literally how big and for how long you can get enemies on screen. Distance shots
don't do it." From round 3 every change is judged first on these, from the meter (`showmeter.c`, the largest
enemy drawn in the view on screen, each frame):

- **seconds with an enemy drawn 20 px or more**, and **40 px or more** (on the 256-px-wide space view), per
  fight and per minute of fight;
- **size-time:** the largest enemy's drawn size (capped at 100 px) summed over the fight, in px·s per minute.

Completion (enemies must still die, within the time budget) and the death budget (up to ~5% of fights) are
constraints. Score 2's other parts (variety, pace, novelty and the rest) are kept only as tie-breakers.

| Tune set (600) | ≥20 px s a fight (s a minute) | ≥40 px s a fight | size-time px·s a minute | Deaths | Fight median / p90 |
|---|---|---|---|---|---|
| Efficient | 0.0 (0.2) | 0.0 | 104 | 1 | 4.4 / 18 s |
| Round 3 as first tuned on score 2 | 1.0 (2.7) | 0.2 | 345 | 3 | 17.5 / 41 s |
| No receding shots, fire at 24 / 32 / 48 px | 1.0 / 1.2 / 1.4 | 0.2 / 0.3 / 0.4 | 336 / 349 / 370 | 3 / 3 / 7 | ~20 / 44 s |
| + toying: hold 24 px+ for 4 / 8 s before the kill, 600 behind it | 1.7 / 1.9 | 0.4 | 393 / 402 | 11 | 20 / 41 s |
| **+ a budget of 30 s + 15 s an enemy, hold 12 s** | **3.8 (6.2)** | **0.6** | **460** | 12 (2%) | 33 / 70 s |
| same, hold 8 s / closing at full speed to 1,200 | 3.4 / 3.3 | 0.5 | 445 / 438 | 12 / 9 | 32 / 69 s |

Even holding fire to 48 px, an enemy is drawn 20 px or more for barely a second a fight. Jousting head-on, a
ship closes at ~80 units a game loop and is big only as it passes. Two things make big time:

- **Toying:** keep a big target big for a while before killing it, sitting behind it at its speed.
- **Time:** a bigger budget. The old 15 + 10 s ran out in 60% of fights.

The cost is longer fights and deaths of 2% of fights, inside the 5% budget.

## What efficient fighting looks like to a viewer

Efficient mode kills from as far away as its military laser reaches. Over the 600-fight tune set it kills
a lone pirate in 3.4 s, at a median 7,000 units. At that range a ship is 2 or 3 pixels across, a dot on the
256-pixel-wide space view. Nothing bigger than a dot is on screen for 96% of the fight. The fight is over
before there is anything to watch.

## The show score

Measured in the fight lab from RAM, every frame, as the viewer would see it (`showmeter.c`):

- **On screen:** Elite's own projection (PROJ: `x = 128 + 256 x/z`, `y = 96 - 256 y/z`) in whichever view is
  shown (PLUT turns the ships into the view: rear `(-x, y, -z)`, left `(z, y, -x)`, right `(-z, y, x)`). A
  ship is in the picture when `z > 0`, `|x| < z/2`, `|y| < 3z/8` (plus its radius).
- **Its size:** `256 r / z` pixels, with r its hit radius. A Cobra Mk III is 25 px across at 1,000 units and
  3 px at 9,000, where fights begin. It counts as *seen* from 4 px, *close* up to 48 px (a fifth of the
  screen), and a kill is a *close kill* when the ship is 20 px or more as it blows up.

A fight's score is 0 to 100. It is 100 × completion × the weighted mean of these parts:

| Part | Weight | Measure | Why |
|---|---|---|---|
| screen | 0.20 | per frame, the enemies seen over the enemies there (up to 3), averaged | the viewer sees the fight, and more ships in view is more fight |
| close | 0.15 | per frame, the biggest seen enemy's size / 48 px (at most 1) | close is exciting |
| air | 0.10 | 1 − the share of frames in gaps over 2 s with nothing seen | no dead air; short lulls are fine |
| ckill | 0.15 | close kills / kills | the explosion is seen, not a dot going out |
| passes | 0.05 | close passes (an enemy seen within 2 s then inside 800 units) / enemies, at most 1 | flybys |
| variety | 0.15 | (½ × techniques beyond the front laser / 3, at most 1, + ½ × novelty against the career's previous fight) × flick factor | not one trick every time, and not the same as last time |
| sup | 0.10 | 1 − damage taken / 384 | superiority: the ship isn't knocked about |
| pace | 0.10 | 1 inside a band of 10 + 5n to 30 + 15n s (n enemies, up to 6); half at 0 s rising to the band; falling to 0 at 3× its top | neither a blink nor a slog |
| novel | 0.10 | in fights with something new (a rare type, or a type the career hadn't met before in its corpus order), the share of those seen close up (32 px) | the mission: new things to see. Fights without one leave this part out |

- **Techniques** (`tech`): rear (a rear-laser kill or a stretch watched in the rear view, one technique
  either way, so occasional rear work counts and a rear-only fight is no better than a front one), missile
  (launched or a missile kill), ECM, energy bomb, shooting down a missile, a close pass, a loop (360° of
  pitch), a side glance (an enemy 20 px or more in the left or right view for 15 frames).
- **Flick factor:** more than 8 view changes a minute scales variety down in proportion. Flicking about
  disorients rather than shows.
- **Completion** = kills / enemies at the start (at most 1). A show that doesn't kill them is no show, so
  never shooting, or circling for ever, scores near 0 however good it looks.
- **Our death** scores 0. Deaths are a budget across fights (below), not a part.

**Why these weights:** the first three parts (45%) are what's on screen at all: without it nothing else
matters. Kills and closeness (30%) are the action, variety and novelty (25%) keep it from repeating, and
superiority and pace (20%) are the craft. Damage costs only 10%, because a fight that's a little rough and
close is better TV than a clean one at 9,000 units. Its real limit is the death budget and the
superiority margin.

### Score 2 (round 2): static screens count as dead air, motion is rewarded

The coordinator reviewed the round-1 films and found about a third of screen time empty or static. The
worst case was the target held dead centre for ~9 s while the shot was held, growing slowly with nothing
moving. The meter now also measures motion, once a game loop (positions only change then). A loop counts
as **lively** if any of these holds:

- a seen enemy moves on screen or changes size by 2 px or more since the last loop (about 20 px a second:
  across the screen in ~13 s, measured in the same view);
- our laser fires (GNTMP rising);
- an explosion is in view.

A frame with something seen but nothing lively is **static**. Static runs over 2 s count with dead air in
the air part, and a new **motion** part is the lively share of the seen frames. Weights in score 2 (score 1
in brackets):

| Part | Weight |
|---|---|
| screen | 0.15 (0.20) |
| close | 0.15 |
| air (dead + static) | 0.15 (0.10) |
| ckill | 0.10 (0.15) |
| motion | 0.10 (new) |
| passes | 0.05 |
| variety | 0.10 (0.15) |
| sup | 0.05 (0.10) |
| pace | 0.05 (0.10) |
| novel | 0.10 |

Superiority and pace gave up most: the margin and the time budget now carry them in the behaviour itself.
A side glance already counted only with a ship 20 px or more in that view (15 frames), so an empty glance
earns nothing. `SCORE=1 tools/show.py` gives the first score. Scores 1 and 2 are not comparable: the
baseline is 16.2 under score 1 and 11.1 under score 2, and round 1's defaults 35.3 and 28.7 (tune set).

**Gaming checks.** These degenerate behaviours were tried or thought through:

| Degenerate behaviour | What stops it |
|---|---|
| Never killing | completion → 0 |
| Circling until the window ends | completion and pace → 0 |
| Flicking views | the flick factor |
| One trick every fight | the novelty half of variety |
| Bombing everything at once | short fights score low on pace, a single technique, little time on screen |
| Holding off far away | seen and close stay low |

The time budget (below) also makes the behaviour itself end a show that runs long.

`tools/show.py` reports the score, its parts, deaths (by the mode at the time), fight length (median, p90,
max), time in the show, budget hits, views, techniques and the dead-air split; `-c` pairs two runs fight by
fight with a bootstrap CI; `tools/showkills.py` gives kill distances by scenario.

**Changes to the score on the way:**

1. **Size thresholds.** At first close saturated at 24 px and a close kill took 12 px. On film those kills
   were still small (a tenth of the screen's width), so they went to 48 and 20.
2. **Any view.** On the user's steer, screen presence counts whichever view is shown. View variety (side
   glances, rear) went into the techniques, with the flick factor against overuse.
3. **Novelty.** Added on the user's mission statement.
4. **Re-scoring.** All the numbers on this page are re-scored with the final `show.py`. Runs from before the
   novelty meter score 0 on that part, so those runs are slightly understated.

## When to show off: the superiority margin

The first gate was fixed: a military laser, ECM and an energy unit fitted, energy ≥ 200 and both shields
≥ 160 at the fight's start, and off for good once a shield fell under 96. On the user's steer it is now
**dynamic**: a margin recomputed every frame.

**margin = what we can lose ÷ what they'll do to us before they're all dead**

- *What we can lose:* energy over 96, plus the **weaker** shield. The show flies at them head-on, and a dead
  front shield behind a full aft one is no cover.
- *Our firepower:* each enemy costs a turn (2 s) plus its energy at our laser's rate. That rate is shots an
  iteration × power × half of them hitting × ~10 iterations a second: military ~115 a second, beam 75,
  pulse 15. Only the military laser hurts a Constrictor or Cougar, at a quarter of its power.
- *Their damage:* each enemy lands one hit (blueprint laser × 4 + 1: a Mamba 9, an Asp 20) every two
  seconds until it dies, quickest kill first. A Thargoid counts half again for its Thargons. Everyone shoots
  for the show's 6 s of holding fire on top. A missile coming at us adds 250 unless the ECM will take it.

The show runs above 2 and efficient mode takes over below 1. Either mode holds for 2 s before it may change
(no flapping), except at once when the energy is under 128 or a missile is coming and there's no ECM.

So a beam laser can show off against a lone Sidewinder, and a military laser with a dead front shield
facing a pack can't.

**Kit floor.** No show on a pulse laser (`min_laser 1`); a beam is enough. The curious confirmation career
lost 9 ships in its first 6.5 hours, most on the pulse laser, and was slower to its military laser (6.4 h
against 2.9 h without the show). On the weak-ship corpus (628 fights from pulse- and beam-laser careers) the
floor costs 1.1 points (37.4 → 36.3) and saves a death (8.0 → 6.4 per 1000). A military-laser floor costs
more (34.1) for no further gain. The margin decides moment by moment, so the tune and hold-out sets are drawn from
**all** 9,327 fights, not a pre-gated subset.

The first margin **averaged the two shields**, and that killed hurt starts. With a start that had a shield
or the energy under 160, 12 to 19 of 180 died in the show, and overall deaths were 22–47 per 1000. The
second margin (weaker shield, a reserve, the show's own exposure) brought that to 3.3 per 1000.

| Gate (tune, 600) | Show score | Deaths /1000 | In the show (time) | Fights ever in the show |
|---|---|---|---|---|
| none (efficient) | 15.8 | 1.7 | 0% | 0% |
| fixed (kit + health at the start) | 27.3 | 3.3 | 53% | 53% |
| margin v1, on 3 / off 1.5 | 34.9 | 41.7 | 72% | 86% |
| margin v2, on 2 / off 1 | **35.1** | 3.3 | 66% | 78% |
| margin v2, on 4 / off 2 | 32.4 | 1.7 | 58% | 68% |
| margin v2, on 8 / off 4 | 25.1 | 1.7 | 38% | 40% |

**Death budget.** The user's death budget is up to ~5% of showcase fights. The chosen margin loses 3.3
per 1000 on the tune set and 6.5 per 1000 on the hold-out (efficient: 3.0), about an eighth of the budget.
Looser margins buy little show (on 1 / off 0.5: +0.4, deaths 11.7 per 1000).

## The time budget

Each fight's show gets **15 s + 10 s per enemy** (the most present at once). After that, efficient mode
finishes it. A new engagement within 10 s of the last counts as the same fight, because enemies that drift
out of range and back used to restart the clock.

Why these numbers: efficient fights take a median 4.4 s (p90 20 s). A show duel with one pirate needs about
20 s: 6–8 s of approach while it's still a dot, a few seconds closing, and the kill. Each further ship adds
a turn, a close-in and a kill, about 10 s. The score's pace band (10 + 5n to 30 + 15n s) sits around it.

## Behaviours (show.c)

| Behaviour | What it does | Where |
|---|---|---|
| Close-in | full speed while the target is beyond 2,500 (the approach is dead air) | `show_speed` |
| Hold fire | no shot until the target is 24 px across (about 1,000 units for a Cobra), so the kill is seen; always fire at missiles | `show_fire` |
| Flyby | in a quarter of fights (drawn from Elite's RAND), and at every rare type, the first target is met without a shot: aimed 500 units beside it along *its own* side vector, slowing to 10 inside 2,500 | `show_aim`, `show_speed` |
| Knife-edge | in the flyby's last 900 units, roll only, bringing it to 3 or 9 o'clock so it goes past on our left or right | `show_knife` |
| Side glance | an enemy 20 px or more passing (over 40° off the nose, level, moving aft) or already in a side view: show the left or right view for up to 2 s, at least 6 s apart; eyes front if a missile comes | `show_view` |
| Missile showpiece | in 30% of fights, once: lock and launch at a target on screen between 1,200 and 6,000, and hold the laser off it while the missile flies | `show_key`, `show_fire` |
| Energy bomb finale | in half the fights where 3+ bombable ships are within 3,000 and 2+ are on screen (no Thargoids, Constrictors, Cougars or police about) | `show_key` |
| Margin and budget | as above | `show_frame` |

Notes from the source on the views:

- Our controls act on the ship whatever the view. MVEIT turns the universe in the front frame, so the
  steering flies on through a glance.
- Only the shown view's laser fires, and there are no side lasers, so a glance means no shots. Glances are
  short, and they never happen with a missile inbound.
- The rear view mirrors the steering only because the rear laser is aimed in that view's frame
  (`combat_keys`).

Why the flyby needed both fixes:

- **Its own side vector.** Offsetting the aim along *our* x axis made the ship chase the point round in an
  endless roll, because that axis rolls with us. The target's own side vector is fixed in the world.
- **The knife-edge.** Elite has no yaw: the steering rolls the aim point to 12 or 6 o'clock and pitches. So
  without the knife-edge the ship always went by above or below, and never appeared in a side view.

## Results

Tune set: 600 fights, 60 blocks of 10 consecutive fights drawn from all 9,327 (seed 11). Hold-out: the other
8,727 (`~/work/show/all_tune.lst`, `all_hold.lst`). Each fight runs in its own lab process with the career's
settings (undamped, joystick, planner). Tier: every row is the tune set unless marked hold-out.

| Step | Show | screen | close | air | ckill | variety | sup | pace | Deaths /1000 | Fight (mean s) | Status |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Baseline: curious fights as efficient | 15.8 | .015 | .025 | .045 | .001 | .140 | .756 | .655 | 1.7 | 9.0 | |
| Hold fire to 14 px + close in at 40 (fixed gate) | 33.3* | .296 | .185† | .429 | .398† | .294 | .463 | .839 | 0 | 23.5 | superseded |
| fire_px 10 / 18 / 24 / 32 / 40 / 56 | best at 24 | | | | | | | | | | **in** (24) |
| hold_max 30 / 45 / 90, far 2500 / 4000, close speed 30 / 40 | flat (±0.5) | | | | | | | | | | 40, 2500 kept |
| Health floor 64 (from 96), fixed gate, first meter | +2.4 | | | | | | | | +1.7 | | superseded by the margin |
| Wound at range, finish close (finish_e 30 / 50) | −12 / −6 | | | | | | | | | | rejected: hurt ships flee, kills end up further out |
| Witchspace show off (ws 0) | ws −7 on that scenario | | | | | | | | | | superseded by the margin (ws 1) |
| Dynamic margin v2 on 2 / off 1 + time budget | 35.1 | .295 | .101 | .367 | .343 | .384 | .475 | .868 | 3.3 | 27.0 | **in** |
| + side glances | 35.1 | .294 | .101 | .368 | .332 | .387 | .484 | .871 | 3.3 | 25.8 | **in** (2% of fights glance: few pass abeam) |
| + flyby every fight (knife-edge) | 31.6 | .263 | .091 | .367 | .085 | .481 | .502 | .914 | 3.3 | 33.9 | side 11% of fights; too costly every fight |
| + Thargoids (ws 1), missile 30%, bomb 50%, no flyby | **36.7** | .326 | .107 | .408 | .309 | .424 | .494 | .875 | 3.3 | 29.8 | |
| same + flyby 20% / 40% | 35.7 / 35.0 | | | | .257 / .224 | .447 / .456 | | | 3.3 | 31.4 / 32.6 | |
| **Final defaults** (flyby 25% + rare types) | 35.3 | .312 | .103 | .403 | .244 | .446 | .500 | .885 | 3.3 | 31.7 | **in** (hold-out below) |
| time budget 20 s + 12 s an enemy (from 15 + 10) | 35.1 | | | | .250 | .460 | .469 | .871 | 3.3 | 36.8 | rejected: longer, no better |
| kit floor: beam or better / military (weak-ship corpus, 628) | 36.3 / 34.1 vs 37.4 | | | | | | | | 6.4 / 6.4 vs 8.0 | | **in** (beam) |
| glance from 1,600 / 2,000 / 2,500 units, at ships from 20 / 12 / 8 px | 35.3 / 35.1 / 35.1 | | | | | | | | 3.3 | | rejected: side glances stay at 6% of fights; ships seldom pass abeam except in flybys |

\* With the first meter (24 px close, 12 px close kill), not re-scorable. † Not comparable with the rows below.

### Hold-out: the final defaults against curious-as-efficient (8,727 fights, tier: all but the tune set)

| | Efficient (show off) | **Showcase** |
|---|---|---|
| Show score | 16.2 | **35.6** (paired +19.4 [+19.1, +19.7]) |
| screen / close / air | .018 / .026 / .045 | .313 / .105 / .406 |
| close kills (of kills) | 0.00 | 0.14 (lone pirates 0.31) |
| variety / novelty | .147 / .060 | .450 / .322 |
| superiority (1 − damage/384) | .749 (damage 114) | .482 (damage 231) |
| pace | .667 | .886 |
| Our deaths | 26 (3.0 per 1000) | 57 (6.5 per 1000; 17 in the show, 40 after it had handed over); budget ~50 |
| Fight length (median / p90 / max) | 4.4 / 24.7 / 170 s | 25.8 / 74.4 / 240 s |
| Pirates / witchspace (median) | 4.0 / 24.0 s | 23.9 / 75.1 s |
| Time budget spent | – | 41% of fights (lone pirates 33%) |
| In the show | – | 72% of fight time; 89% of fights at some point; 2.8 show/efficient changes a fight |
| Views with an enemy seen (front / rear / left / right) | 96 / 4 / 0 / 0% | 87 / 13 / 0.2 / 0.2%; a side view in 11% of fights |
| Techniques (share of fights) | front 93, rear 9, pass 4, loop 3, missile 2 | front 87, pass 67, rear 28, missile 10, side 7, loop 6, ECM 4, bomb 1 |
| Points a fight | 3.50 | 3.53 (the same kills, taking 3½ times as long) |

The final code, with the kit floor added, scores the same on the hold-out: 35.3, paired +19.2 [+18.9, +19.5],
deaths 57.

The show more than doubles the score. Nearly every fight now has enemies on screen at some point, and the
lone-pirate duels end with a close kill a third of the time. Our deaths double, from 0.3% to 0.65% of
fights, an eighth of the user's budget. Points per fight are unchanged: the show takes longer, not less.

### Where the show still falls short

- **Dead air is the approach.** Fights start at ~9,000 units, where every ship is a dot. Even closing at full
  speed, about a third of fight frames have an enemy in the picture but still under 4 px, and 16% have
  every enemy far off. Enemies near but out of the picture are only 5%, so a "camera" that follows them
  into the rear and side views was not built: there's little for it to find.
- **Closer than 24 px the kills get no closer.** At 40–56 px the target goes past before it dies, and the
  kill happens later and further out.
- **Damage.** Holding fire through a head-on approach costs the front shield; one film shows the ECM
  destroyed. The margin and budget keep it inside the death budget, not painless.

**Efficient mode is untouched:** 200 tune fights in efficient mode, lab built from master vs this branch:
byte-identical result lines. Checked again after merging master at 123feb4, which brought efficient mode's
opportunity-fire switch; it stands down while the show is on. The show on the tune set is unchanged by that
merge (35.3, 2 deaths).

## Round 2: motion, follow-through, Thargoids, shorter tails

The coordinator reviewed the round-1 footage. A clear step up, but about a third of screen time was empty or
static. Five problems were named:

1. Empty starfield while turning back after a flyby.
2. A static stare before the kill.
3. Thargoid fights showing mostly specks, with a side glance at an empty view.
4. Long tails (p90 74 s, max 240 s).
5. Side views rare.

Each change was screened on the tune set (600 fights, score 2) and judged on film.

| Change | Problem | Tune (score 2) | Status |
|---|---|---|---|
| Round 1's behaviour | – | 28.7; static runs 29% of fight frames | baseline |
| **Weave:** while holding fire on a target under 70% of the firing size, aim 8–12° off it, circling every 3 s along its own side and roof vectors (fixed in the world), so it drifts round the screen; the planner stands down meanwhile | 2 | 8°: 33.8, static runs 10%; 12° (with the rest): 34.9 | **in** (12°) |
| **Follow and extend:** after a flyby, fly straight on 1.5 s so it goes astern, then show whichever view has it (side, then rear) for up to 4 s | 1 | 28.7 (flat); on film the pass is seen in the right view, then the rear view with rear-laser shots | **in** (footage) |
| **Predicted glances:** start a glance only if the ship will be in that side's picture 1.5 game loops on, and end an empty one at once | 3 | 28.8 (flat) | **in** |
| **Empty screen spends the budget double**, and it counts 4 enemies at most (Thargons made witchspace budgets 75 s+) | 4 | 29.6; p90 62 → 47 s | **in** |
| **Energy bomb at Thargons:** in witchspace, bunched bombable ships in view with Thargoids about (they survive, the Thargons die) | 3 | bombs in 3 of 66 witchspace fights (1 before) | **in** |
| **Witchspace thresholds:** show from margin 1.2, off below 0.7; hold fire on Thargoids to 24 px (was 14). Screened on 263 witchspace fights from the hold-out, which are left out of the hold-out numbers below | 3 | 263 witchspace fights: 32.0 → 36.2, no deaths (on 1.0: 35.0, 1 death) | **in** |
| All of the above together | – | 34.9 (weave 6° 34.2, 8° 34.6; weave stop at 50% 34.1; empty ×3 32.5) | **in** |
| More flybys (half the fights) | 5 | 33.6 vs 34.0 | rejected |
| **Camera:** in witchspace, when the front picture is empty and the target well off the nose, the view with most enemies in it | 3 | 66 witchspace fights: 38.9 → 38.0 (a little more on screen, more view changes) | rejected (left off) |
| **Tail rule:** after the budget, no rear laser beyond 1,500, so it turns and uses the front laser | 4 | on film the fight ran longer and the kills went further out | rejected (left off) |

### Hold-out (4,232 fights: every second of the 8,727, less the 263 witchspace fights used for screening)

| | Efficient | Round 1 | **Round 2** |
|---|---|---|---|
| Show score (score 2) | 11.3 | 28.9 | **34.1** |
| Paired | | | +22.8 [+22.3, +23.3] over efficient; +5.3 [+4.9, +5.6] over round 1 |
| screen / close | .016 / .025 | .308 / .104 | .271 / .101 |
| air (dead + static) | .040 | .181 | .351 |
| motion (lively share of seen frames) | .81* | .26 | **.72** |
| Static runs over 2 s (share of fight frames) | 0% | 28% | **4%** |
| ckill | .001 | .257 | .140 |
| variety / novel | .143 / .060 | .449 / .311 | .484 / .479 |
| Our deaths | 13 (3.1 per 1000) | 21 (5.0) | **14 (3.3)** |
| Fight length median / p90 / max | 4.3 / 22.1 / 162 s | 25.7 / 69.5 / 240 s | **22.2 / 47.7 / 208 s** |
| Budget spent | – | 40% | 67% |
| Views with an enemy seen (front / rear / sides) | 96 / 4 / 0% | 87 / 13 / 0% | 88 / 11 / 0.4% |
| Techniques (share of fights) | | pass 65, rear 28, missile 10, side 7 | pass 64, rear 40, loop 10, side 6, missile 6, bomb 2 |
| Witchspace (529 fights) | 19.9 | | 36.7 |

\* Efficient fights are short and mostly dots: what little is seen moves.

**What changed on screen.** The static stare is gone: the target drifts round the screen while the shot
is held, and static runs over 2 s fall from 28% of fight frames to 4%. Fights are shorter (p90 70 → 48 s)
because an empty screen spends the budget twice as fast, and our deaths are down to efficient mode's level.

The cost is close kills: 0.26 → 0.14. The weave delays the lock-on, and the budget runs out more often (67%
of fights), so efficient mode finishes more of them at range.

**Side views stay rare** (0.4% of seen frames, in 6% of fights). Only flybys put a ship abeam close, and
more flybys scored lower. The follow mostly shows the rear view (11%).

### Films (before / after, the same fights)

- **Typical fight.** `corpus/s150/fights/01168`: three pirates, the median 2–3 fight on the tune set.
  - Before: `docs/img/r1-typical.gif`.
  - After: `docs/img/r2-typical.gif` and `.mp4`. More happens mid-fight (ships drifting across, a close kill
    with the laser, rear-view shots), but it ends with ~8 s of the rear view on specks: efficient mode's
    rear beam chasing after the budget. The tail rule meant to fix that was worse.
- **Flyby.** `corpus/hunt/fights/01147`, a lone Python, `fb_odds=1`.
  - Before: `docs/img/r1-flyby.gif`.
  - After: `docs/img/r2-flyby.gif` and `.mp4`. The Python drifts round the screen on the approach, goes past
    large in the right view, then the rear view follows it while the rear laser fires and it explodes. A
    couple of empty rear-view seconds remain just after the pass: efficient mode's rear-laser choice (a 60°
    cone) is wider than the rear picture (~26°).
- **Thargoid fight.** `corpus/s132/fights/00648`.
  - Before: `docs/img/r1-thargoids.gif`.
  - After: `docs/img/r2-thargoids.gif` and `.mp4`. Thargoids come big into the front view under fire, but
    for long stretches the other Thargoids and Thargons sit abeam or behind at 2,000–10,000 units, too far
    to be more than specks in any view. This fight looks no better than round 1's.

Across the 529 witchspace hold-out fights the score rises (19.9 → 36.7), but Thargoid fights remain the
weakest footage.

## Careers (checked, not tuned against)

Curious careers, seed 13, `run_sim` capped at 100 game hours, on the defaults before the kit floor and the
travel-deadline fix:

| Seed 13, 100 h | Tally | Deadly at | Deaths | Stuck reloads | Fights | Systems | Missions |
|---|---|---|---|---|---|---|---|
| curious, show off | 5,747 (57 an hour) | 33.9 h | 8 | 12 (witchspace 7, dock 2, pod 2, to station 1) | 3,991 | 194 | both |
| curious, show on | 4,537 (45 an hour) | 54.3 h | 53 | 28 (to station 13, dock 9, witchspace 5) | 3,675 | 170 | both |

Neither reaches Elite inside 100 h: curious mode lingers on screens and explores 8 galaxies. Two things came
out of it:

- **Travel deadlines.** The "to station" and "dock" stucks were the show's long fights running a travel
  leg past its deadline. The career then reloaded the save and lost the kills. In curious mode the leg's
  clock now stops while it fights (`pilot.c`; efficient unchanged).
- **The early career.** 9 of the 53 deaths came in the first 10 hours, on the pulse laser, and the military
  laser came at 6.4 h (2.9 h without the show). With the kit floor (a 10 h run): deaths 9 → 6, the military
  laser at 3.3 h, tally at 10 h 184 → 584 (show off: 783).

Later deaths mostly start a fight already hurt (one began at energy 14, aft shield 0): show fights end with
the ship lower, and the career flies on. That is the remaining career cost; see the proposals below.

The confirmation career on the final code (seed 13, up to 200 h, `~/work/show/careers/final13` on the
macbox), compared with the two above at 65 game hours:

| Seed 13, first 65 h | Tally | Deaths | Fights | Stuck |
|---|---|---|---|---|
| show off | 4,136 | 6 | 2,767 | 12 (witchspace 7) |
| show on, before the fixes | 3,211 | 34 | 2,330 | 10 (to station 4, dock 4) |
| show on, final | 3,236 | 53 | 2,530 | 15 (dock 12) |

**The confirmation career reaches Elite at 149.8 game hours.** Seed 13, final code. Deadly at 48.8 h; 81
deaths in 5,406 fights (1.5%); 16 pods; 29 stuck reloads (dock 23); 189 systems in 8 galaxies; both
missions. That is 43 tally an hour. The same seed without the show made 57 an hour to 100 h and would reach
Elite at about 112 h (extrapolated, not run).

**Careers lose far more ships than the lab: 1.5–2.1% of fights, against 0.65% on the hold-out.** That is
still inside the 5% budget. The lab replays one fight from a recorded start. A career meets wave after wave
at anarchy stations, and the head-on show approach leaves the front shield low (fights starting at F 0–64
with A 255). The next wave then starts hurt; the margin hands it to efficient mode, which loses some of
them. The remaining "dock" stucks are those waves keeping it from the slot past the deadline. Comparing
single seeds is noisy (15 of final13's deaths fell between 10 and 20 h). The cost is about a fifth of the
tally per hour (50 against 64).

After merging master at 7b8b0ae (efficient mode's break-off on the rear laser): efficient mode is still
identical (200 lab fights, a 3 h career's journal), and round 2 on the tune set scores 35.3 (34.9 before the
merge), deaths 3.

## Round 3: close and big

**First pass, on score 2** (tune set, round 2 = 35.3):

| Change | Result | Status |
|---|---|---|
| Fire at a ship opening on us whatever its size | 37.1; ended the rear-view tail (a receding Krait sat in the rear sights from 2,000 to 4,000, held because it was small) | later reversed: a distance shot |
| Stop the weave 25 game loops before the firing range, by closing rate | close kills 0.16 → 0.21 | **in** |
| No weave on a target turning away | 34.0; static back up (5 → 12%) | rejected |
| Finishing window (the budget's last 5 / 8 s: fire from 8 px) | 35.2 / 35.1 | rejected |
| Knife-edge on any close pass of the target (not only flybys) | 35.3; side views 5 → 6% of fights | **in** |
| Witchspace: target the nearest group (nearness × enemies within 2,500) | 263 witchspace fights: 35.2 → 35.9; side views 25 → 31% of fights | **in** |

**Then judged by size × time** (table above). These went in:
- no shot under 32 px, Thargoids included, and none at receding ships (no distance shots);
- toying at 24 px for 12 s;
- a budget of 30 s + 15 s an enemy.

### Hold-out (4,232 fights; round 3's final defaults)

| | Efficient | **Round 3** |
|---|---|---|
| Enemy drawn ≥20 px | 0.0 s a fight (0.2 s a minute) | **4.2 s a fight (6.5 s a minute)** |
| ≥40 px | 0.0 s | 0.7 s a fight (1.0 s a minute) |
| Size-time | 105 px·s a minute | **469 px·s a minute** |
| Our deaths | 10 (2.4 per 1000) | 58 (13.7 per 1000: 1.4% of fights; 39 in the show, 19 after it handed over) |
| Fight length median / p90 / max | 4.3 / 18.3 / 162 s | 35.1 / 73.2 / 230 s |
| Score 2 (tie-breaker) | 11.6 | 34.2 |
| Witchspace (529) | | 38.8, 2 deaths |

Lone pirates take most of the deaths (31): toying sits a ship behind a single pirate for 12 s, and some turn
and fight. The deaths are inside the budget, and the reason to accept them is the user's: big on screen, for
long.

### Films (round 3, the same fights; size × time from the lab)

| Fight | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Typical (`s150/01168`), ≥20 px / size-time | 0.7 s / 79 px·s | 1.4 s / 150 | 1.3 s / 124 |
| Flyby (`hunt/01147`) | 2.5 s / 209 | 2.0 s / 187 | 2.0 s / 289 |
| Thargoids (`s132/00648`) | 3.2 s / 440 | 2.1 s / 165 | **8.1 s / 571** |

`docs/img/r3-typical`, `r3-flyby` and `r3-thargoids` (`.mp4`, `.gif`), against the `r1-` and `r2-` clips.
Single fights vary: the typical fight is no bigger on screen than in round 2, and the tune set's gain
(1.0 → 3.8 s at 20 px or more) is an average over 600.

- **The Thargoid clips end in an empty starfield.** That is not a static ship. After the fight the lab, like a
  career, leaves witchspace through the 15 s hyperspace countdown. The reel's best-window cut leaves it out.
- **The coordinator's frame sampling** (share of frames with nothing drawn): flyby 33 → 22 → 23%; typical
  27 → 26 → 21%; Thargoids 36 → 49 → 46% (rounds 1/2/3, round 3 as first tuned).

## Round 4: big kills, risk for style, end when it repeats

### Where the small kills come from

The meter now records each kill's size on screen and the mode at the moment (`tools/killsize.py`). On round 3's
tune run, 1,894 kills:

| Mode at the kill | Weapon | Share of kills | Drawn 20 px+ | Median distance |
|---|---|---|---|---|
| margin had handed over to efficient | front laser | 40% | 11% | 5,658 |
| budget spent | front laser | 21% | 5% | 4,625 |
| show | front laser | 15% | 50% | 719 |
| margin handed over | rear laser | 11% | 0% | 8,400 |
| budget spent | rear laser | 10% | 0% | 8,599 |
| show | missile | 1.5% | 59% | 808 |

In witchspace, 87% of kills come after the hand-over. So **the small kills are efficient mode's, after the
show hands over**. The show's own kills are mostly big; the show just isn't on when most kills happen. In
packs the margin starts under its threshold, and the show's held fire costs shields until the margin drops.

### Risk for style, and ending on repetition (the user's steer)

The user's steer: "take risks for style up to 5%. Don't worry about ending fights unless they are repetitive
(e.g. chasing in a straight line for a long time)." So:

- **The margin:** on at 1, off at 0.5 (was 2 and 1). At 0.5 / 0.25 the show gained nothing more on screen and deaths doubled: 4.3% in the lab, and a filmed career reached 5.7% of its fights (stopped at 88 h). The gain came from ending on repetition, not from risk. Witchspace keeps 1.2 / 0.7: at 0.6 / 0.3 the ship bails
  out of witchspace on low energy and completion halves (0.86 → 0.48).
- **No time budget as a fight-ender:** it is now a 10-minute safety net.
- **Repetitive, measured** (`bore_s`): a frame is boring when nothing is drawn 4 px or more in the view on
  screen, or the target's picture hasn't changed (size within 1 px, bearing within 2° of a second ago) and
  we're not firing. It is not boring while the target is closing: the approach is part of the fight. 12 s of
  continuous boredom hands the fight to efficient mode.
  - The first version counted the approach as boring and handed over before the fights began (budget spent
    in 65% of fights); witchspace completion fell to 0.17–0.38. So the approach is excluded, and witchspace
    keeps its budget (now 10 minutes).
  - 8 s: 3.0 s a fight at 20 px or more (it hands over too soon). 12 s: 5.8 s. 20 s: 6.3 s, but p90 fights of
    131 s and deaths 4.8%.

| Tune (600) | ≥20 px s a fight (a minute) | ≥40 px | Size-time px·s/min | Kills drawn 20 px+ | Deaths | Completion | Fight median / p90 |
|---|---|---|---|---|---|---|---|
| Round 3 | 3.8 (6.2) | 0.6 | 460 | 14% | 12 (2.0%) | 0.91 | 33 / 70 s |
| Margin 1 / 0.5, budget 30 + 15 | 3.9 (6.2) | 0.7 | 460 | 15% | 12 | 0.86 | 33 / 73 s |
| Margin 0.7 / 0.3 | 4.0 (6.4) | 0.7 | 471 | 17% | 23 | 0.81 | 33 / 73 s |
| Margin 0.5 / 0.25, budget off, bored 12 s | 5.8 (7.0) | 0.9 | 526 | 18% | 26 (4.3%) | 0.86 | 36 / 116 s |
| margin 0.7 / 0.35, the same | 5.8 (7.0) | 0.9 | 526 | 18% | 24 (4.0%) | 0.87 | 35 / 117 s |
| **margin 1 / 0.5, the same** | **5.7 (7.1)** | 0.9 | **526** | 17% | **13 (2.2%)** | 0.88 | 35 / 115 s |
| same, flybys in half the fights | 5.9 (7.1) | 0.8 | 515 | 17% | 29 | 0.85 | 36 / 117 s |

Witchspace (263 screening fights) with these settings: an enemy drawn 20 px or more for 13.3 s a fight, size-time
653 px·s a minute, side glances in 43% of fights, 3 deaths.

**Still small:** a third of the kills come after the margin hands over, at a median ~4,000. That's the ship
hurt: the show stops toying and efficient mode finishes fast. Making those kills big means fighting hurt for
show, and the death budget is now nearly used.

**Career deaths, time wasted:** in the round-3 confirmation career the 104 deaths cost 8.2 h of reloads out of
109.7 h (7.5%; median 3.4 min each, the trip since the last save), against 3.5 h (3%) in round 2. It's real
but modest. Each death reloads a save made at every dock.

### Hold-out (4,232 fights)

| | Round 3 | **Round 4** |
|---|---|---|
| Enemy drawn ≥20 px | 4.2 s a fight (6.5 a minute) | **6.0 s a fight (7.0 a minute)** |
| ≥40 px | 0.7 s | 0.9 s |
| Size-time | 469 px·s a minute | **524** |
| Kills drawn 20 px+ | 13% | 17% |
| Our deaths | 58 (1.4%) | 116 (2.7%) |
| Completion | 0.91 | 0.88 |
| Fight median / p90 / max | 35 / 73 / 230 s | 36 / 118 / 260 s |
| Witchspace (529) | 38.8 score 2, 2 deaths | 39.5, 4 deaths; fights 73 → 92 s |

### Round 4 careers

**A career with the first round-4 margin (0.5 / 0.25)** lost the ship in 5.7% of its fights by 88 h. It was stopped
there.

**At 1 / 0.5, a career stalled at 92 h.** It sat docked at Adien (galaxy 6) with ~700 Cr after buying a
replacement energy bomb. The trade planner found nowhere to go, the career reloaded its save, and the save
was that same dead end: it reloaded for 80 game hours.

- **The fix, curious only:** no trade worth flying means hunt bounties instead, here if this is an anarchy,
  else the nearest. From the career's own snapshot it now hunts at once and goes back to trading.
- **Efficient mode** has the same path (a stuck reload into the same state) and could loop the same way.

**The confirmation career** (filmed, seed 13, round 4 final) reaches **Elite at 163.9 game hours** (round 3: 109.7):

| | Round 4 |
|---|---|
| Deadly at | 58.5 h |
| Our deaths | 236 in 6,506 fights (3.6%, inside the 5% budget) |
| Sessions | 897 hunting, 34 witchspace |
| Exploration | 195 systems in 6 galaxies |
| Stuck reloads | 29 (dock 27) |

The no-trade fallback fired 149 times. The career became a bounty hunter (103 game hours hunting) and
witchspace almost stopped: the one-in-three rule can't keep up with that many hunt sessions. That is slower
to Elite, but the user said career hours don't matter here, and a curious career that hunts is mostly
fighting on screen.

### Round 2 careers: the stall, and setting out hurt

**The stall.** The coordinator's solid curious career (no show, master's agent.c) charted galaxy 8 by 42.8 h.
For the remaining ~85 h it flew between two systems and never scored a session, hunting or witchspace.
Fights happened in transit (2,854 of them).

The cause was a routing ping-pong:

1. At an anarchy already hunted, curious picks a fresher one by breadth-first search and flies the first
   hop.
2. At that hop, which is not an anarchy, it routes to the chosen ground with the risk-weighted `route()`.
3. When that fails, it falls back to "the nearest anarchy": the one it had just left.

With no session ever scored, the counts froze at hunting 47 and witchspace 24. The one-in-three rule
(`2 × witchspace ≤ hunting`) then never chose witchspace again either.

The fixes, curious only:

- When `route()` fails, head for the chosen ground by the same breadth-first search, a hop nearer each time.
- A backstop: after a dozen trips with no session, witchspace if it's ready.

Efficient mode has the same routing code and could hit the same ping-pong (it hunts each anarchy 3 times
before moving on); it was left unchanged.

**Setting out hurt.** The first attempt was a recharge in place after a hurt fight, in `to_station` and
`dock`. It backfired at anarchy stations, where pirates keep coming. By 65 h on seed 13: fights 2,578 → 4,323,
74 dock timeouts, and tally 3,236 → 1,299. Docking refills the ship anyway. A curious career now just doesn't
set out on a new leg hurt:

- no chained hyperspace while hurt (it docks instead);
- a hunt ends at energy 200 or a shield at 100.

| Curious career (final code of each round) | Elite at | Deadly at | Deaths (of fights) | Stuck | Systems | Sessions hunt / witch |
|---|---|---|---|---|---|---|
| Round 1, seed 13 | 149.8 h | 48.8 h | 81 (1.5%) | 29 (dock 23) | 189 | 36 / 19 by 65 h |
| **Round 2, seed 13** | **116.9 h** | 39.6 h | 51 (1.0%) | 17 (dock 9) | 387 | 157 / 81 |
| **Round 2, default seed** | **118.8 h** | 42.1 h | 50 (1.0%) | 13 (dock 6) | 382 | 145 / 73 |
| Solid run (coordinator's, no show, before the fixes) | 127.9 h | | | | | none after 42.8 h |

Sessions keep coming to the end: in hours 80–120, 52–59 hunts and 18–28 witchspace sessions. Both careers
see twice as many systems as round 1's, in all 8 galaxies, with both missions. Deaths are down to about 1% of
fights, a fifth of the budget.

### Round 3 careers and the showreel

**The filmed career** (`run_sim FILM=dir FIGHTS=dir`, seed 13, round 3's final code) reaches Elite at 167.8 game
hours, against 116.9 h for round 2. Deadly at 58.2 h; 43 deaths in 6,721 fights (0.6%); both missions (the
summary's "-T" misses mission 1, which shows in the reel and the journal).

From about 42 h it ping-ponged between two systems again: 600 "hunt chosen" trips per 40 h, but only 14 hunt
sessions in the whole career. The 12-trip backstop turned those trips into 185 witchspace sessions, which
is how it still got to Elite. It saw only 115 systems in 5 galaxies. A fresh agent started from the career's
own snapshot at 83 h moves on and hunts normally, so the stall lives in the agent's accumulated state (the
anarchies it counts as hunted, its chosen ground), not in the position.

**The fix, curious only:**
- **The guard:** when the trips alternate between two systems (A, B, A, B), drop the chosen ground and head
  for the nearest anarchy by breadth-first search, a hop nearer each time.
- **Hunting there:** it hunts at that anarchy even if it was hunted before.

**The confirmation career on the fixed code** (seed 13, round 3 final, unfilmed) reaches **Elite at 109.7
game hours**, the fastest curious career so far:

| | Round 3 |
|---|---|
| Deadly at | 44.2 h |
| Sessions | 242 hunting, 122 witchspace |
| Exploration | 219 systems in all 8 galaxies, both missions |
| Our deaths | 104 in 5,421 fights (1.9%; round 2: 51, 1.0%) |
| Stuck reloads | 10 |

The guard fired 197 times, so two-system shuttles are common in curious careers; it turns each into a hunt.
Deaths doubled with the closer show (no shot under 32 px, 12 s of toying), and are still inside the user's
5% budget.

**The showreel:** `docs/img/curious-show-reel.mp4` (3:02, 2.9 MB). It is built from the filmed career by
`tools/reel.py`, cherry-picked from solid-elite (each battle's best 12 s, chosen by `sim/film SCAN=1`'s
on-screen size score). The settings were battles only, 8 battles and 3 witchspace fights, then the kill that
made Elite:

```
DISC=rp2350-bbc-micro/discs/elite-master-flicker-free-sng47.ssd NOMISSIONS=1 NBATTLE=8 NWS=3 NCAND=30 \
  TITLE1="BBC Master Elite (flicker-free)" TITLE2="curious mode: showcase fights, close and big" \
  tools/reel.py careers/reel13 curious-show-reel.mp4
```

Big wireframes at close range, Thargoids under laser fans, rear-view work and explosions. The last clip (the
Elite kill) is mostly an empty rear view: that kill was an efficient-mode chase.

### One whole fight, uncut (the user's pick of format)

The 12 s best-stretch reel didn't work: its captions counted the whole battle's kills ("9 kills") over a clip
showing one. The user asked instead for one continuous sequence: uncut, at real speed, from first contact to
the last kill, with every kill on screen and close, and a caption true for what's shown.

**The tool.** `tools/onefight.py` ranks every fight of the filmed career with 4 or more kills (805 of them).
`sim/film SCAN=2` is the meter's view of each frame of the film: the largest enemy drawn, and each kill with
its size on screen. The window runs from first contact (a witchspace fight: from the jump in) to 2 s after
the last kill. Fights are ranked by:
- kills drawn at 20 px or more, and kills on screen;
- time with an enemy drawn 20 px or more;
- less dead air, and no kills off screen.

**Candidates** (contact sheets in `docs/img/onefight-<id>.png`, a frame every 2 s):

| Fight | Length | Kills (on screen / 20 px+) | ≥20 px | ≥40 px | Size-time | Dead air | |
|---|---|---|---|---|---|---|---|
| **4610** | 79.8 s | 9 (8 / 3) | 37.3 s | 0.3 s | 874 px·s/min | 31% | witchspace from the jump in: Thargoid swarm |
| 3138 | 80.9 s | 9 (9 / 2) | 35.1 s | 0 s | 890 | 27% | witchspace, Thargoids |
| 271 | 97.3 s | 6 (6 / 1) | 47.5 s | 0.2 s | 985 | 21% | pirates (Gecko, Mamba, Sidewinder, Worm), big wireframes and rear view |
| 3568 | 73.3 s | 6 (6 / 2) | 27.2 s | 18.5 s | 1,101 | 35% | a pack of six |

**The video:** `docs/img/curious-witchspace-fight.mp4`, fight 4610. 79.8 s at 50 fps, 1056×1024, H.264,
1.5 MB. It runs from the hyperspace jump in, through Thargoids drawn large in the front view under the laser
for most of the second half, to the last of the swarm blowing up. Its only caption, for the first 4 s:
"8 kills on screen, witchspace, uncut, real speed".

```
tools/onefight.py careers/reel13 rank 12          # the candidates
tools/onefight.py careers/reel13 sheet 4610 sheet.png
tools/onefight.py careers/reel13 render 4610 fight.mp4
```

### Round 4's best fight, uncut

From the round-4 confirmation career's 1,516 fights with 4+ kills (`tools/onefight.py careers/reel7`):

| Fight | Length | Kills (on screen / 20 px+) | ≥20 px | ≥40 px | Size-time | Dead air | |
|---|---|---|---|---|---|---|---|
| **2879** | 186 s | 10 (10 / 3) | **66.5 s** | 4.8 s | 970 px·s/min | 16% | witchspace from the jump in: a Thargoid swarm |
| 2989 | 208 s | 7 (7 / 3) | 77.4 s | 2.1 s | 936 | 25% | pirates (Krait, Mamba, Python, Worm) |
| 6376 | 143 s | 5 (5 / 4) | 40.1 s | 8.6 s | 810 | 35% | pirates: four of five kills drawn 20 px+ |

- **The video:** **`docs/img/curious-r4-fight.mp4`**, fight 2879. 186 s, uncut, real speed, H.264, 3.1 MB.
  - Content: Thargoids drawn large through most of it, under laser fans, with a rear-view kill filling the
    screen. The last third has smaller Thargons further out.
  - Caption: "10 kills on screen, witchspace, uncut, real speed".
- **Contact sheets:** `docs/img/r4-onefight-2879.png`, `-2989`, `-6376`.
- **Against round 3's best** (4610: 37 s at 20 px+): about twice the big time, and every kill on screen.

## Round 5: big kills without rams, and the career's poverty trap

### Collisions, measured

The lab runs now log collisions (`COLLOG`). `show.py` counts as a ram any sudden loss of 100 or more with a
ship inside 500 units (a ram costs 128 plus half the rammer's energy), and collision deaths from `DCAUSE`.

**Round 4 rammed in 363 of every 1,000 fights.** It held fire on a closing target until it was 32 px across,
and some came on into us. Only 1 in 600 fights ended in a collision death, but rams cost shields and
equipment.

| Tune (600) | Rams per 1000 fights | Collision deaths | Kills drawn 20 px+ | ≥20 px s a fight | Size-time | Deaths |
|---|---|---|---|---|---|---|
| Round 4 | 363 | 1 | 17% | 5.7 | 526 | 21.7 per 1000 |
| On boredom, chase it down first | 393 | 1 | 18% | 6.3 | 500 | 25.0 |
| Dodge rams 10 game loops ahead in the show (5 now) | 423 | 1 | 19% | 5.6 | 529 | 26.7 |
| **Shoot a target about to pass within 300 in 10 loops, whatever its size** | **138** | 1 | **25%** | **6.3** | **535** | **8.3** |
| both of the last two | 197 | 0 | 25% | 6.0 | 532 | 13.3 |

**Shooting a target about to ram us** is close and big already, so the kill is on screen, and dead it can't
ram us. Rams fall 62%, big kills rise from 17% to 25% and deaths fall. Dodging earlier only spoils the aim.

### The career's poverty trap

The round-4 career spent 80+ hours in galaxy 6 at 650–900 Cr:

| Trip reason (from 80 h on) | Count |
|---|---|
| "no witch: hop" (no hop for the forced mis-jump around there) | 670 |
| "saving galactic hyperdrive" (5,000 Cr, never reached) | |

Hunting there didn't climb out of it. The show's damage wrecks equipment (energy unit and ECM rebuys) and its
bombs cost 900 Cr each to replace, so the cash never grew. That, not the one-in-three rule, starved
witchspace. The forced hunts count as hunts, which if anything favours witchspace.

The fix (`frugal`, curious only): no show under 1,000 Cr, and no show bombs under 3,000 Cr. Efficient fights
cost less until the money is back.

### Round 5 career and its best fight

**The confirmation career** (filmed, seed 13, round 5 final) reaches **Elite at 134.8 game hours** (round 4: 163.9;
round 3: 109.7):

| | Round 5 |
|---|---|
| Deadly at | 49.1 h |
| Our deaths | 115 in 5,523 fights (2.1%; round 4 3.6%), 12.5 h of reloads |
| Sessions | 131 hunting, 66 witchspace (round 4: 897 / 34) |
| Exploration | 271 systems in all 8 galaxies |
| No-trade fallback | never needed |
| Docked under 1,000 Cr | 16% of docks (the show was off then) |

The remaining drag is 52 "dock" timeouts. Waves at anarchy stations keep it from the slot (round 3: 12).

**On the lab: frugal is off in those numbers.** The fight corpus comes from efficient careers that carry
little cash, so in the lab `frugal 1000` turns the show off in most fights (size-time 535 → 414). It's a
career rule.

**Its best fight, uncut:** **`docs/img/curious-r5-fight.mp4`**, fight 4993. 152 s, 2.7 MB. Rendered on the Pi from
the copied film; the macbox lost its ffmpeg. The replay checks against the career: 13 kills, all on screen,
8 drawn 20 px or more.

| Fight | Length | Kills (on screen / 20 px+) | ≥20 px | ≥40 px | Size-time | Dead air | |
|---|---|---|---|---|---|---|---|
| **4993** | 152 s | 13 (13 / **8**) | 53.0 s | **14.1 s** | 874 | 35% | pirates: 7 types |
| 5435 | 291 s | 9 (8 / 5) | 72.6 s | 3.2 s | 853 | 11% | witchspace |
| 4022 | 119 s | 7 (7 / 5) | 35.9 s | 15.1 s | 1,015 | 22% | pirates |
| (round 4's 2879) | 186 s | 10 (10 / 3) | 66.5 s | 4.8 s | 970 | 16% | witchspace |

Sheets: `docs/img/r5-onefight-4993.png`, `-5435`, `-4022`.

### Efficient mode and the reload loop (proposal, not applied)

The round-4 curious career reloaded one docked save for 80 game hours. Efficient mode has the same code path
in `agent.c`'s `trip()`. When `plan_trip_seen()` finds no trade, it fails "nowhere to go", and the career loop
reloads the last save.

**Efficient mode can loop there forever when all of these hold at a dock:**
1. **No hunting:** `farm_ready()` is false, so it has no beam or military laser, no ECM or no energy unit.
   Hits destroy equipment (OUCH), so a fitted ship can lose them.
2. **Nothing else to do:** no witchspace (that needs the military laser too) and no shopping trip it can
   afford. "Saving up for something big" only sends it back to trading.
3. **No trade:** the planner finds nothing profitable with the cash in hand. Fuel and a minimum cargo buy eat
   it all.
4. **The trap is saved:** the dock save is made before `trip()` runs, and the broke-save guard only skips
   saves under 200 Cr that are poorer than the last. With a few hundred Cr the dead end is saved. Every reload
   returns to it, and nothing differs the next time: the planner is deterministic from the same RAM.

The curious career hit it with ~700 Cr just after a replacement energy bomb. Efficient careers seldom sit
that poor once fitted, which may be why it hasn't shown, but a lost energy unit plus a death's reload could
do it.

**Proposed efficient fix** (needs the user's approval: it changes efficient careers): on "nowhere to go",
before the reload:
- If this is an anarchy, hunt with what we have (the curious fix).
- Else fly one hop toward the nearest anarchy or the cheapest-fuel neighbour.
- If the same docked state fails three times running (same system, cash and tally), sell the cargo hold's
  equipment-free extras (the cargo bay is spare) and retry.

**Test:** take a docked snapshot from any efficient career, set ECM and the energy unit to 0 and the cash to
600 Cr in RAM (as the lab's REARMIL test pokes the laser), and run `run_sim START=... 120`. Today it should loop
on "nowhere to go"; with the fix it should hunt or move on. Then the usual check: ten efficient careers'
hours to Elite unchanged within noise.

(A first try ran efficient `run_sim` from the stuck curious snapshot, but efficient mode starts a new Jameson
commander there instead of continuing, so it didn't reach the state.)

## Round 6: the board, and what the user saw watching it

All of this round came from watching curious mode live on the Presto. Each change is curious-only; a 180-minute
efficient career gives a journal byte-identical to master after every one.

### Frame cost (the juddering)

In Thargoid fights the frame hook averaged 8-25 ms with a 207 ms worst case: the acquisition planner's search
(316 rollouts of up to 50 game loops) in double precision, which the RP2350's FPU can't do. Now it runs in
single precision, with a polynomial atan2, rollouts cut once they can't beat the best plan, the hot code in RAM
(about 1.4 us a step on the board) and at most 8,000 steps a call. On the board: 0.1-2.5 ms a minute on average,
12-14 ms at worst, speed 100%. A separate 136 ms frame at mis-jumps was the jump-range table (131,072 integer
square roots); it now takes one root a system, exactly the same table. Efficient mode uses the same planner:
all 9,327 fights 15.45 -> 15.37 points a minute, deaths 80 -> 78.

### Rams

Tagging each lab collision with the show's state showed held fire wasn't the cause (3 of 220). The toy follow
had parked us in the target's path, or the ram dodge came too late. The ram shot now looks 25 game loops ahead
within 600 (was 10 within 300), and there's no toying close to a ship coming at us nose-first (that distance is
400 since the user said an occasional accident is fine).

### Strafing, and Thargons first

- Strafing: while toying and while closing to the firing size, a burst of 0.3-0.8 s every 1.5-4 s: near misses
  aimed beside the target, or grazes while its energy is high enough to survive them. Bursts in about two thirds
  of show fights; rams and deaths no worse.
- Thargons first (the user: "kill the little Thargons fast, for fun, then toy with the Thargoid"): while a
  Thargoid lives, the Thargons within 4,000 are worth ten times as much and are shot on sight; then the Thargoid
  gets the show. Its Thargons go dead when it dies, so they have to come first. Witchspace hold-out (263 fights):
  Thargons killed 6.98 -> 7.46 a fight, the last at 34.7 -> 31.4 s, a Thargoid drawn 20 px+ 2.8 -> 3.8 s a fight,
  deaths 1 -> 0. `docs/img/curious-thargons-first.mp4` is one such fight, uncut (66 s, 10 kills, all on screen):
  honest rather than pretty, as Thargons are small and much of it is empty space.

### Running away, rolling, hanging back

The user: "a lot of time speeding away from the enemy with them in the rear view; also an unnecessary amount of
rolling", and later "in pursuit but hangs too far back". Measured on three Deadly careers in the sim (RRLOG=1,
`tools/rrstats.py`):

| | before | after |
|---|---|---|
| running away (every enemy behind, speed 30+, range opening) | 11.1% of fight time | 3.1% |
| roll | 56 deg/s | 35 deg/s |
| game loops with roll on | 87% | 59% |
| the target in the toy follow, drawn | median 16 px | median 24 px (24 px+ half the time) |

Causes and fixes:
- The running was the show fighting on the rear laser: its speed rule went full speed for any target over
  2,500 out, behind us too. With the show on, no rear-laser fight beyond 1,500 (turn and face it), and no
  speeding up for a target behind.
- The rolling was the weave, circling the target 12 deg off: the steering rolled all the way round after it. It
  now weaves by pitch only, and the show rolls only as far as the shot needs (1 hit radius).
- Hanging back: between 1,000 and 2,500 efficient mode's slow speeds ran, and the toy follow sat 600 behind
  whatever the ship. Now full speed to a target ahead until 900, lead pursuit to the intercept point, and the
  follow sits where the target is drawn 35 px.

Hold-out (4,232 fights), from round 5's defaults to now: size-time 398 -> 492 px.s a minute, kills drawn 20 px+
6 -> 12%, close kills (one ship) 0.11 -> 0.21 a fight, rams 24 -> 68 per 1000, deaths 5.4 -> 7.3 per 1000 (0.7%).
The user asked for closer and bigger over ram safety. With the target ahead the median range barely moved
(about 1,800): beyond 1,000 we already fly at 37 of 40, and much of that time is approach to ships as fast as us.

### Dead-centre docking (tried, off)

Rolling with the station to keep the slot level, the slot's sideways offset is fixed in our view, and the final
approach corrects only in pitch, so the offset is never taken out. Aiming at a point on the axis ahead, tighter
line-ups, tilting the slot a little, and steering onto the axis before the roll match all cut the worst offset
further out (median 93 -> 36 inside 2,000) but not the offset at entry (13.5 -> 14), and some bumped or re-lined
up. Off (`DOCKAX=1` to try); measured with `tools/dockstats.py` on DOCKLOG runs.

## Artefacts

- `docs/img/showcase-flyby.gif` (21 s) and `showcase-flyby.png` (a frame a second): one showcase fight, a
  lone pirate Python (`corpus/hunt/fights/01147`, `SHOWP=fb_odds=1`). It closes, the shot is held, and it
  goes past on our left with the left view up. We turn back, it grows in the sights, and the military laser
  kills it close. The front shield is down to nothing on the way in, and the ECM is lost to a hit.
- `docs/img/efficient-same-fight.gif`: the same fight as efficient mode flies it, 4 s, the Python never more
  than a dot.
- `docs/img/showcase-thargoids.gif` (15 s): a witchspace fight (`corpus/s132/fights/00648`, defaults).
  Thargoids pass large, with a right-view glance at one.
- `docs/img/curious-thargons-first.mp4` (66 s): a witchspace fight, uncut, Thargons first (round 6).
- Films: the lab writes raw frames with `SHOTRAW=file` (768×640 RGB24, 50 a second); `ffmpeg -f rawvideo
  -pix_fmt rgb24 -s 768x640 -r 50 -i file out.mp4` (on the macbox: `source ~/work/viz/env.sh` first).

## Proposals (not done)

- **Efficient mode's hunting route.** Efficient mode has the routing code behind curious mode's stall (a
  breadth-first first hop, then `route()`, then "the nearest anarchy"). If it ever falls back that way, it
  ping-pongs too. Worth a check on the efficient side.
- **Close kills after the weave.** Stop weaving earlier on targets that turn away; let the planner acquire a
  second or so before the target reaches firing size.
- **Thargoid fights.** Thargoids keep their distance (2,000–10,000), too far to show in any view. Fly to the
  nearest group rather than the best-value target while the show is on in witchspace.

- **Encounter log (agent.c).** Log every ship type met, per career, and prefer systems and governments
  where unseen types appear (anarchies for pirates, mis-jumps for Thargoids). `show.c` already keeps the
  set it has met (`g_show_seen`) to give new types a close look.
- **A watched approach.** Since the approach is the dead air, fly the first seconds in the rear view with
  the aft shield to them, then turn. This might also spare the front shield.
- **Kill pacing.** Space the kills in packs: hold the next target until the last explosion has played.

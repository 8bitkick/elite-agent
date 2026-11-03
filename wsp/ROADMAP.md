# Weight-space roadmap

The `weight-space` branch replaces the C agent's dogfight core with a hand-written neural network
(`wsp/`, the pilot: `elite_wsp/author.py` writes the weights, `cplayer/wsp_pilot.c` runs them, `WSP=1`
switches the C agent onto them). It follows MIT CSAIL's
[Sparks of Weight-Space Programming](https://rikiyat.github.io/blog/weight-space-programming/)
(2 Nov 2026). Where it stands, and where it goes, with the measure that decides each step.

## Where it stands (3 Nov 2026)

13 → 26 → 23 → 38 → 4 ReLU MLP, 2,053 weights, zero training steps. On the fight lab's recorded fights
(`splice/`), against the C pilot:

| set                                   | C pts/min | network pts/min | cleared | deaths |
|---------------------------------------|----------:|----------------:|--------:|-------:|
| 18 early-career fights                |      5.75 |            4.61 |   18/18 |      0 |
| 20 witchspace Thargoid swarms (8-9)   |      20.3 |            13.8 |   20/20 |      0 |
| 20 pirate packs of 4-5                |      13.1 |            12.5 |   20/20 |      0 |

The gap is in the chase and the hold, not the aim: hold 42% of locked frames against 80%, and in a
career its fights run 1,000-4,000 frames against the C pilot's 250.

## Steps

Each step is a named set of units, checked the same way as the first three edits: `tests/test_pilot.py`
against the spec, then the lab on `splice/hard.json` (`WSP=1 cplayer/lab hard.lst`), kept only if
points a minute go up without deaths.

1. **Hysteresis on the above/below choice.** `steer()` keeps its roll side for 25 frames near the
   nose; the network flips it every frame, which is most of the lost hold. One recurrent unit: the
   previous `up` fed back as an input feature, with a gate that lets the new value through only when
   `|ay|` is past a threshold. Needs a 14th feature (last frame's `up`) from `combat.c`.
   Target: hold 42% → 60% on the hard set.
2. **Coast-then-stop on the pitch key.** `steer()` holds a pitch rate until the angle left is what
   the keys' lag turns through. Two units: `relu(error - rate × lag)` gating the kick off.
   Target: fewer stick reversals (the lab's `stick at a stop` and judder counts).
3. **The chase.** Speed bands by closing rate, not range: a target flying away at 40 is never caught at
   25. Four units on `vz − own` (already in layer 1 as `az`).
   Target: career fight length 1,000-4,000 frames → under 600.
4. **Rear view.** `combat.c` already mirrors the inputs for the rear laser; let the network choose the
   view itself: one output unit, `behind AND rear laser fitted`.
5. **Hit-radius by type.** `fr` is one weight on a single feature; make it a small lookup: the ship type
   one-hot (33 features) × a 33-weight row, so Thargoids get their 99 and a Thargon its 40.
6. **Train the rest.** With the written units frozen, fit the gains and bands by gradient descent on the
   729 recorded fights from the Dangerous career (`FIGHTS=dir` records them; the lab scores them).
   The written units stay readable; only their gains move. This is the step that tests the thesis:
   logic you write, behaviour that's learnt.
   Target: parity with the C pilot on the hard set.
7. **The Presto.** `cplayer/wsp_pilot.c` is 1,900 multiply-adds a frame; build it into the plugin
   (`presto/app/build.sh --wsp`) and run a career on the board. Target: a career to Elite on the
   network, hours against the C pilot's 23.8.
8. **The reading side.** Give the `.npz` (no names, no notes) to a frontier model and ask what game
   it plays and what each unit does, as the paper's Experiments 1-3 do. The notes in `author.py`
   are the ground truth.

## Not on the list

- Replacing the career loop, docking, trading or the chart: those are sequences over screens, not
  control laws, and nothing is gained by writing them as weights.
- Training from scratch. The point is the written network as the starting point, not PPO.

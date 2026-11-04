# Weight-space roadmap

Where the weight-space pilots stand (4 Nov 2026), and the steps that would take it further, each with the
measure that decides it: the fight lab on `evolve/sets/big.lst` (450 held-out fights, one process a fight,
a paired comparison against the C pilot), kept only if points a minute go up with no more deaths.

## Done

| | Optimal | Fair |
|---|---|---|
| v1: 2,053 AI-authored weights, lateral-offset steering | ~72% of the C rules (hard set) | |
| v2 as written: the C's steering law in angle space, every constant named | 92.4% | |
| v2 evolved: CMA-ES on the 21 named parameters | 97.2% | 97.1% |
| v2 evolved + the C's planner for witchspace acquisition (`WSPPLAN=2`, off by default) | 98.6% | 102.8% |
| fair careers to Elite, earlier build, three seeds | | **27.2 h against 27.8** |
| fair careers to Elite, current build, two seeds, the planner on | | 16.5 h against 16.4 (level) |
| the docking network: hyperspace exit to docked, written in the network's own terms, evolved | | 175.4 against the C's 180.5 (cost on 69 held-out dockings) |
| fair careers to Elite, both networks, the planner off | | 16.7 and 17.0 h against 16.8 and 17.5 |

Tried and dropped (wsp/README.md has the numbers): PPO on a residual, DAgger regression of the planner,
stick dithering, confidence gates on lead and fire, a nose hold, fair-tracker settings.

## Next

1. **The planner into the weights.** The one borrowed part. Distil `plan_stick` as a *classification* over the
   stick positions it chooses between (about 20), not a regression: a softmax can represent its bang-bang choices,
   which a regression averaged away. DAgger as before (`evolve/distil.py`, `WSPTEACH=1`), gated beyond three hit
   radii so the tracking stays exact. Done when `WSPPLAN=0` with the distilled units matches `WSPPLAN=2`.
2. **Use the confidence inputs.** Four inputs (heading seen, staleness, range and motion uncertainty) are wired
   in and read "sure" in Optimal mode; the written law ignores them. Written gates on them cost points, so
   let evolution set them jointly with the rest (the parameters exist: `lead_guess`, `fire_r_guess`).
3. **Target choice.** Still the C's (`target_value`). In fair mode a far Thargon is a dot with a guessed heading;
   a network that weighs targets by how sure it is of them, not just their points and turn time, is the kind of
   decision the paper is about.
4. **Optimal careers.** The lab says 98.6% of the C rules in Optimal mode; careers to Elite on the same seeds
   would confirm it.
5. **The Presto.** Fair mode flies the network by default, and the board has no environment to say otherwise, so
   fair mode on the Presto now flies it too. Its build includes `wsp_pilot.c` with file I/O compiled out, but it
   hasn't been built or run on the board yet: do that, and time the forward pass (about 2,000 multiply-adds a frame).
6. **The reading side.** Give the `.npz` without its names and notes to a frontier model and ask what it does,
   unit by unit, as the paper's first experiments do. `author.py`'s notes are the ground truth.

## Not on the list

- Replacing the career loop or trading: sequences over screens, not control laws. (Docking was on this list; it
  is a control law, and it's done.)
- Training from scratch. The point is the written network as the starting point.

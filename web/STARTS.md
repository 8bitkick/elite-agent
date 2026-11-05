# Page starts: fly one, log it

[The page](https://8bitkick.cc/elite-agent/) flies one career per start. This file says what a start is, which ones have
reached Elite, and how to test more of them and add your results. If you use Claude Code, the
[prompt below](#testing-starts-with-claude-code) does the work for you.

## What a start is

A start `N` is the machine's state `N` million emulated cycles after power-on, with Elite on its title screen. The page
stops the machine at the first end-of-frame at or after that cycle, finishes the instruction in progress, and snapshots it,
so the snapshot is taken at an instruction boundary. The browser keeps it in `localStorage` (key
`elite-agent-start3:<N>:...`), and the agent then starts on the next frame.

The aim is that every visitor gets the same career: nothing in the career depends on wall-clock time or on how the
browser slices up the emulation. [Determinism](#determinism-what-has-been-tested) says how far that has been tested.

| URL parameter | What it does |
|---|---|
| `?game=flicker` or `?game=solid` | Mark Moxon's flicker-free Elite (the default) or Eben Upton's Solid Elite |
| `?start=N` | use start `N` (a page's own default is in the table below) |
| `?fresh` | ignore any saved start and boot again |

Each whole `N` is 1M cycles, or 25 frames. The page checks once a frame, so a frame (40,000 cycles, `N` of 0.02) is the
finest step. Booting takes emulated time: `?start=1000` boots through 500 emulated seconds, and visitors wait for that on
a first visit, so lower starts are kinder.

## Results

Hours are game hours from Jameson at Lave to the rank of Elite. **Bold** starts are the page's defaults.

| game | start | game hours | deaths | kills | credits | stars | galaxies | docks | witchspace | gate | verified by | machine, wall clock | date | commit | wasm |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|

*No careers have finished yet. Solid 45 is flying now.*

A death is a log line containing `died` or `escape pod`, not counting buying a pod (`Fitted escape pod`). Credits are the last figure in the journal. Stars are systems
visited for the first time. Witchspace counts farming sessions. Missions aren't listed, because the web player's journal
doesn't record them. Compare rows only when their `wasm` column matches.

## Claiming a block

To avoid flying the same starts twice, claim a block by adding a row here in a pull request. You don't have to fly all of
it: fly as many as you like, in any order.

| block | game | claimed by |
|---|---|---|
| 1000-1999 | | |
| 2000-2999 | | |
| 3000-3999 | | |

## Testing starts with Claude Code

Paste this into Claude Code, with your block and game filled in:

```text
Clone github.com/8bitkick/elite-agent and follow web/STARTS.md. Test game <flicker|solid>, starts <list from my block>.
Do the prerequisites, then run web/tools/verify_start.sh for each start, at most one career per CPU core. If the
determinism gate reports a difference, stop and show me: don't fly careers. When careers finish, give me their rows for
the results table in web/STARTS.md, and open a pull request adding them, with the last lines of each career's log.
```

## Testing starts by hand

**You need:** Node 22, about 100 MB per career for its files, and one CPU core per career. A career takes many hours: at
3.6x real time on a Raspberry Pi 5, 18 game hours is 5 hours on one core.

```bash
git clone https://github.com/8bitkick/elite-agent && cd elite-agent
web/build_site.sh                                       # jsbeeb at its pinned commit, the pinned agent, the page
(cd web && npm ci && npx playwright install chromium)

curl -fL -o flicker.ssd https://raw.githubusercontent.com/markmoxon/elite-source-code-bbc-master/e6581b586b1c984142a0fee0c8009c7466a38d52/5-compiled-game-discs/elite-master-flicker-free-sng47.ssd
curl -fL -o solid.ssd https://raw.githubusercontent.com/ebenupton/solid-elite-bbc-master/262bfa61b69c3fb75ce2c004d4944bdc47c1abbd/5-compiled-game-discs/elite-master-sng47.ssd

web/tools/verify_start.sh --game flicker --start 1000 --disc flicker.ssd --out runs
```

`verify_start.sh` does three things, and prints one row for the results table at the end:

1. **The visit (about 4 minutes on the Pi):** `web/tools/start_snapshot.mjs` opens the page fresh in a browser and waits
   for 10 game minutes. It writes the snapshot (`<game>_<start>.json`) and the log (`<game>_<start>.log`).
2. **The gate (about 3 minutes):** `web/run_node.mjs` flies those 10 minutes in Node from that snapshot, and the log text
   must match the browser's exactly. If it doesn't, the script stops. Please report that, because it matters more than
   any career. Add `--throttled` to also visit with the CPU throttled 4x; the snapshot and log must match again.
3. **The career:** `run_node.mjs` flies to Elite, or to 40 game hours (`MINUTES=` to change it).

If it's interrupted, run the same command again and it picks up from the last checkpoint.

**To submit:** open a pull request that adds your row to the results table, with the run's final log lines
(`tail -n 20 runs/<game>_<start>.out`) in its description. Add the career's two files to `docs/careers/starts/`:
`<game>_<start>.journal.jsonl` and `<game>_<start>.meta.json`. The meta file records what flew it: this repo's commit, the
start, the parameters, the disc's repo, commit and hash, the `agent.wasm` and jsbeeb versions, and the machine.
`verify_start.sh` writes it, and refuses a disc that isn't the pinned one.

## Determinism: what has been tested

Same start, different conditions, on one Raspberry Pi 5 (arm64):

| Test | Start | Result |
|---|---|---|
| browser vs Node, 10 game minutes | flicker 31, 33, 35 | identical log text |
| browser vs Node, about 2 game minutes | Solid 45 | identical log text |
| `agent.wasm` vs a local clang 19 build, Node, 10 game minutes | flicker 31 | byte-identical journals |
| browser vs browser, to frame 12,000 | flicker 31 | identical log text and frames, identical snapshot |
| local page vs the live site, to frame 6,000 | Solid 45 | identical log text and frames, identical snapshot |
| first visit vs a returning visit, to frame 6,000 | flicker 31 | identical log text and frames (7 lines), identical snapshot |

**Not tested yet:** a run resumed from a checkpoint, other kinds of computer such as x86, and any comparison across a whole
career. A browser throttled 4x hasn't produced a result either: on a Raspberry Pi 5 the page stops responding at that
setting, so there was nothing to compare. So this is promising but not proven: if you
compare a same-seed run and it differs, please open an issue.

**Different starts can fly the same career.** Starts 41.24 and 287 wrote byte-identical journals for their first 10 game
minutes, though 31, 33 and 35 all differed. Nobody knows yet how many different careers the starts hold, so a new start
isn't guaranteed to be a new career. Before a long run, compare its first 10 minutes (`runs/<game>_<start>.gate.out`) with
the journals in `docs/careers/starts/`.

## Pinned versions

| What | Version |
|---|---|
| jsbeeb | [`c6ac7200d284376768458a993285192aa4d477f2`](https://github.com/mattgodbolt/jsbeeb/tree/c6ac7200d284376768458a993285192aa4d477f2) |
| flicker-free disc | `markmoxon/elite-source-code-bbc-master@e6581b586b1c984142a0fee0c8009c7466a38d52`, sha256 `3c7b7e55...` |
| Solid disc | `ebenupton/solid-elite-bbc-master@262bfa61b69c3fb75ce2c004d4944bdc47c1abbd`, sha256 `0896826f...` |
| `web/agent.wasm` | sha256 `40469dd3a338c5776c07365e1e9f8d6a1c8686c5385b2a4431e4fc2280c24b92`, built from `cplayer/` at commit `623a180` by the `pages` workflow (clang 18, Ubuntu 24.04) |

The page, `run_node.mjs` and `verify_start.sh` all fly this one file, and `build_site.sh` stops if its hash doesn't match
`web/agent.wasm.sha256`. Changes to `cplayer/` don't reach the page until a new one is pinned, and a new pin means the
careers here must be flown again. `web/build.sh` writes `agent.dev.wasm`, for development: a build on another compiler
isn't byte-identical, so don't submit results from one.

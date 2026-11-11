# Careers: fly one, log it

[The page](https://8bitkick.cc/elite-agent/) flies one career per game and skew: Elite and its Fair-mode agent on Kieran
Connell's BBC Master core, all in one WebAssembly file. This page lists which careers have reached Elite, what's been tested
about them, and how to fly more. If you use Claude Code, the [prompt below](#testing-with-claude-code) does the work for you.

## What a career is made of

The machine boots from power-on, runs to Elite's title screen, runs `skew` frames more, and then the agent starts. Nothing
else goes in: the same `core.wasm`, ROMs, disc, skew and commander name (`BITKICK`) should fly the same career, because the
emulator counts emulated cycles and the agent has its own random numbers. Neither reads a real clock.

| URL parameter | What it does |
|---|---|
| `?game=solid` | Eben Upton's Solid Elite (the default is Mark Moxon's flicker-free, wireframe Elite) |
| `?game=tube` | Eben Upton's Solid Elite for the 6502 Second Processor, on a Master with a Second Processor on its Tube (web/tube/) |
| `?skew=N` | another career: `N` frames more on the title screen |
| `?check=F` | write the journal's hash at frame `F` to the JS console (the default is 30,000: 10 game minutes) |

## Results

Hours are game hours from Jameson at Lave to the rank of Elite. **Bold** rows are the page's defaults.

| game | skew | game hours | deaths | kills | credits | stars | galaxies | docks | witchspace | verified by | machine, wall clock | date | commit | wasm |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **flicker** | **13** | **16.45** | 1 | 4,848 | 19,703 | 66 | 3 | 196 | 68 | 8bitkick | Raspberry Pi 5, 0.4 h | 2026-11-05 | `3741213` | `16783057` |
| **solid** | **13** | **18.34** | 2 | 4,610 | 19,432 | 65 | 3 | 220 | 81 | 8bitkick | Raspberry Pi 5, 0.4 h | 2026-11-05 | `3741213` | `16783057` |

A death is a `died` or `escape_pod` event in the journal. Credits are the last figure in the journal. Stars are systems visited
for the first time. Witchspace counts farming sessions. Compare rows only when their `wasm` column matches. Their journals are
in [`docs/careers/starts/`](../docs/careers/starts/), each with a `.meta.json` saying what flew it.

## Determinism: what has been tested

| Test | Result |
|---|---|
| A whole career, Chromium vs Node, flicker 13 (to frame 2,960,000 of 2,960,444) | byte-identical journals |
| A whole career, Chromium vs Node, Solid 13 (to frame 3,300,000 of 3,301,568) | byte-identical journals |
| Drawing every frame, every 7th frame, or none, flicker 13, 10 game minutes | byte-identical journals |

**Not tested yet:** any machine other than one Raspberry Pi 5 (arm64), and browsers other than Chromium. So "the same career
for everyone" is the design and hasn't been proved. To help, open the page and leave it for 10 game minutes, or a minute or so
at full speed: the JS console says whether your machine matches the reference. If it doesn't, please open an issue, because
that matters more than any career.

**Native `run_sim` isn't a reference.** It uses the same emulator and agent, but a different maths library and compiler. With the
same seed it matched for about 1.3 game hours, then split (Elite in 16.92 game hours against 17.66). Only `core.wasm` verifies
the page.

**Different skews might fly the same career.** On the page's previous emulator, two different starts wrote identical journals
for 10 game minutes. It hasn't been measured on this one. Compare your first lines with `docs/careers/starts/`.

## Claiming a block

To avoid flying the same skews twice, claim a block by adding a row here in a pull request. You don't have to fly all of it: fly
as many as you like, in any order.

| block | game | claimed by |
|---|---|---|
| 1000-1999 | | |
| 2000-2999 | | |
| 3000-3999 | | |

## Testing with Claude Code

Paste this into Claude Code, with your block and game filled in:

```text
Clone github.com/8bitkick/elite-agent and follow web/STARTS.md. Fly game <flicker|solid>, skews <list from my block>, with
web/tools/fly.sh, at most one career per CPU core. When they finish, give me their rows for the results table, and open a
pull request adding them, with the two files for each career in docs/careers/starts/.
```

## Flying by hand

You need Node 22 and curl. On a Raspberry Pi 5 a career takes about 25 minutes on one core.

```bash
git clone https://github.com/8bitkick/elite-agent && cd elite-agent
web/tools/fly.sh --game flicker --skew 1000
```

It prints the career's row at the end. **To submit:** open a pull request adding the row to the results table, and add
`runs/<game>_<skew>.journal.jsonl` and `.meta.json` to `docs/careers/starts/`, with the last lines of `runs/<game>_<skew>.out`
in its description.

## Pinned versions

| What | Version |
|---|---|
| `web/core.wasm` | sha256 `0efef07bc027b2345cc5c962ef4ab7e9f71fb82e171ec3504831d2ef49d8b383`, built by `web/build.sh` with clang 19 on arm64 (the careers above were flown on `16783057…`; the change since is the page's docking-action flag only, and both discs' 10-minute journals are unchanged) |
| Emulator | Kieran Connell's Master core, in `8bitkick/rp2350-bbc-micro@b8232984` (`src/cores/kc-beeb`), from [esp32-beeb](https://github.com/kieranhj/esp32-beeb) |
| ROMs | MOS 3.20 set, `scarybeasts/beebjit@db16aaf0` (`roms/mos3.20`) |
| flicker-free disc | `markmoxon/elite-source-code-bbc-master@e6581b58`, sha256 `3c7b7e55...` |
| Solid disc | `ebenupton/solid-elite-bbc-master@262bfa61`, sha256 `0896826f...` |

The page and `fly.sh` use this one file, and `build_site.sh` stops if its hash doesn't match `web/core.wasm.sha256`. Changes to
`cplayer/` don't reach the page until a new one is pinned, and a new pin means the careers here must be flown again.

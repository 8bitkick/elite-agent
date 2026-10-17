# Elite agent

A player for Acornsoft Elite (BBC Master, Mark Moxon's flicker-free build) that reads the
machine's RAM and presses keys, nothing else. It is plain C rules, and it runs standalone on the
Pimoroni Presto (RP2350) inside the BBC Master emulator firmware. The same code runs on a native
emulator (Pi or Mac) for development and testing.

Two modes: **efficient** goes straight for Elite and never saves (a death is the end of the
career); **curious** explores every galaxy and saves at each dock.

## The player (`cplayer/`)

| File | What it does |
|---|---|
| `player.h` | the interface: read-only RAM in, held keys and taps out, one call a frame |
| `agent.c` | the career: shopping, trading, bounty hunting, witchspace farming, missions, recovery |
| `combat.c` | fighting: threats, target choice, aim, speed, fire, evasion, ECM, missiles, bomb, pod |
| `pilot.c` | steering, the run in to the station, docking (roll-matching the slot) |
| `flight.c` | launch, hyperspace, the chart cursor, witchspace exits and chains, galactic jumps |
| `docked.c` | the docked screens: market, equipment, saving, title screens |
| `galaxy.c` | the galaxy, markets, trade plans and loops, routes, safe jump ranges |
| `ships.c` | the ships in the local bubble, and the crosshair test |
| `drv.c`, `keyq.c` | coroutines over frames (`pt.h`), keys, death detection |
| `state.h`, `elite_ram.h` | named views onto Elite's RAM (addresses from `elite/symbols.json`) |

Every helper is a protothread coroutine: it runs a little each frame and keeps its state in a
static struct. A helper that can't do its job sets `g_fail`; the career loop recovers.

## Testing

- `cplayer/run_sim` — a career on the native emulator (`run_sim [minutes] "" journal.jsonl`;
  `CURIOUS=1`, `SKEW=n` for a different career, `STOPFAIL=1` to stop at the first death or
  restart, `FIGHTS=dir` to record every fight, black box printed on every death).
- `cplayer/lab` — the fight lab: from a recorded fight's snapshot, fight as the career would.
- `tools/suite.py` — runs sets of recorded fights in parallel, per scenario, with aim and
  targeting measures and death causes; `tools/compare.py` compares two runs fight by fight.
- `tools/career_summary.py` — one line per career: ranks, deaths, systems, missions.

The lab and the suite work on fights recorded from careers (`run_sim` with `FIGHTS=dir`), kept
outside the repository (`tools/suite.py` looks in `~/work/corpus` and `~/work/sets`).

## Building

The emulator is a git submodule, [rp2350-bbc-micro](https://github.com/8bitkick/rp2350-bbc-micro)
(its `bbc-master` branch): `git clone --recursive`, or `git submodule update --init`. The ROMs and
the Elite disc aren't in either repository: `rp2350-bbc-micro/scripts/setup.sh` fetches them (and,
for the Presto, the Pico SDK and the Arm toolchain).

- **Development** (Linux; elsewhere set `ELITE_EMU` to the submodule's path): `cplayer/build.sh`
  builds `run_sim`, `cplayer/buildlab.sh` the lab, against the submodule's core. They find the ROMs
  and disc in the submodule (or `$ELITE_EMU`).
- **Presto**: `presto/build.sh` (`--curious` for curious mode) builds the submodule's firmware with
  the player in it through its `core/machine_hook.h` interface: `presto/build/presto_master.uf2`.
  Hold BOOT, tap RESET, and copy it to the RP2350 drive.

## On the Presto

The agent starts by itself a little after boot. The firmware's web panel (http://beeb.local/)
gets an Elite agent section: rank and progress to Elite, credits, where it is and what it's
doing, play time, kills, deaths, escape pods, its latest status lines, and buttons to start or
stop it and switch between efficient and curious. Over USB the same commands are `!agent start`,
`!agent stop`, `!agent mode efficient|curious`, `!agent name <NAME>`, `!agent status` and
`!agent log 0|1|2`. Saved games survive a power-off (the firmware keeps a save disc in flash).

`presto/elite_presto.c` is the Presto side; `presto/elite_splash.c` the loading screen
(`tools/make_splash.py`, from `presto/splash/8bitkick-logo.png`).

`docs/` explains how the game works, from the source: its economy, flight and combat, and game
flow.

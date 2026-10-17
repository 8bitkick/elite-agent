# Elite agent

A player for Acornsoft Elite (BBC Master, Mark Moxon's flicker-free build) that reads the
machine's RAM and plays through the keyboard and the analogue joystick (Elite's own joystick
option, on the machine's ADC), nothing else. It is plain C rules, and it runs standalone on the
Pimoroni Presto (RP2350) inside the BBC Master emulator firmware. The same code runs on a native
emulator (Pi or Mac) for development and testing.

It hooks into the machine's RAM instead of reading the screen: the game already holds everything
on the dashboard, the scanner and the market screens as bytes, so there's no need to spend cycles
recognising pixels. But it can also see bytes the screen never shows, like an enemy's energy. Is
that cheating? [`docs/IO.md`](docs/IO.md) goes through exactly what the agent reads and presses,
which of it a human player could see or work out, and what a fair mode would look like.

Two modes: **efficient** goes straight for Elite; **curious** explores every galaxy. Both save at
each dock: a save takes about 4 seconds docked, so it costs well under 1% of the time to Elite,
and a death only loses the trip since.

## How fast it gets to Elite

From a fresh Commander Jameson (100 Cr, a pulse laser) to the rank of **Elite in about 32 game
hours**: 32.1, 32.4 and 32.0 hours on three different careers (seeds), one death each, in efficient
mode on the native emulator at the real machine's speed.

![Kill tally against game hours: three careers on the current defaults reach Elite at 32.0 to 32.4 hours; without the missions 36.8 hours; with keyboard aim 57.3 hours](docs/img/climb.svg)

The early ranks pass in the first three hours. From Deadly (2,560) to Elite (6,400) takes about
14 hours of almost pure witchspace farming.

**Where the time goes** (seed 13): farming Thargoids in witchspace, by a forced mis-jump, takes
55% of the time and earns 81% of the points, at about 5 points a game minute. Bounty hunting at
anarchies makes about a quarter of that rate. The two missions take 4 hours but pay for themselves:
without them the career is ahead until about 30 hours, then reaches Elite 4.7 hours later.

![Share of game time and of points by activity: witchspace farming 55% of the time and 81% of the points](docs/img/time.svg)

**Why it is this fast:**

- **Aim.** It flies with the analogue joystick and aims 4.5 game loops ahead of its target, with
  steering the joystick's stick to an exact position each frame. Replayed over 9,327 fights recorded from careers, that scores
  38% more per fight minute than the keyboard, with 39 deaths instead of 101, and holds the target
  in the sights 82% of the time.
- **Thargoids.** Witchspace fights score about 30 points a fight minute, against about 12 in
  normal space.
- **Cheap deaths.** With a save at every dock, a lost fight costs only the trip since (the whole
  career above lost 2 game minutes to it), so the career can take risks a no-save player can't.
- **No wasted trips.** Routes avoid the systems the chart cursor can't select; the escape pod is
  bought only when rich and never holds up farming.

![Points per fight minute, deaths and time in the sights over 9,327 recorded fights, for keys and joystick with and without lead](docs/img/aim.svg)

## The player (`cplayer/`)

| File | What it does |
|---|---|
| `player.h` | the interface: read-only RAM in, held keys, taps and joystick values out, one call a frame |
| `agent.c` | the career: shopping, trading, bounty hunting, witchspace farming, missions, recovery |
| `combat.c` | fighting: threats, target choice, aim, speed, fire, evasion, ECM, missiles, bomb, pod |
| `pilot.c` | steering, the run in to the station, docking (roll-matching the slot) |
| `flight.c` | launch, hyperspace, the chart cursor, witchspace exits and chains, galactic jumps |
| `docked.c` | the docked screens: market, equipment, saving, title screens |
| `galaxy.c` | the galaxy, markets, trade plans and loops, routes, safe jump ranges |
| `ships.c` | the ships in the local bubble, and the crosshair test |
| `drv.c`, `keyq.c` | coroutines over frames (`pt.h`), keys, the joystick, death detection |
| `state.h`, `elite_ram.h` | named views onto Elite's RAM (addresses from `elite/symbols.json`) |

Every helper is a protothread coroutine: it runs a little each frame and keeps its state in a
static struct. A helper that can't do its job sets `g_fail`; the career loop recovers.

## Running it headless (no Presto)

The whole agent runs on Linux against a native build of the same emulator, about 100 times faster
than a real BBC Master, with no screen needed:

```sh
git clone --recursive https://github.com/8bitkick/elite-agent.git
cd elite-agent
rp2350-bbc-micro/scripts/setup.sh --no-build   # the MOS 3.20 ROMs and the Elite disc (plus the
                                               # Presto toolchain, which headless doesn't need)
cplayer/build.sh                               # run_sim: a career
cplayer/buildlab.sh                            # lab: the fight lab
```

A career from a fresh Commander Jameson, efficient mode, until Elite or the time runs out:

```sh
cplayer/run_sim 2400 "" journal.jsonl          # up to 2,400 game minutes (40 h); Elite in ~32 h
```

It prints the agent's log as it plays (minutes, then what it's doing) and writes one JSON line
per event to `journal.jsonl`: docks, fights and their points, sessions, deaths, ranks. Options go
in the environment:

| Option | What it does |
|---|---|
| `SKEW=n` | start n frames late: a different career (the game's randomness follows the timing) |
| `CURIOUS=1` | curious mode instead of efficient |
| `STOPFAIL=1` | stop at the first death or restart |
| `SAVE=0` | never save (a death then ends the career) |
| `FIGHTS=dir` | record every fight's starting snapshot into an existing directory (for the lab) |
| `SHOTS=from-to:step` | screenshots (PPM) every `step` frames between those frames |
| `LOAD=NAME` | load that commander from the disc when docked |
| `DISCDUMP=file.ssd` | write the disc, with the career's saves, at the end |

The fight lab replays recorded fights from their snapshots, fighting each as the career would:

```sh
mkdir -p fights && FIGHTS=fights cplayer/run_sim 600 "" journal.jsonl   # record a career's fights
ls fights/*.fight > fights.lst
UNDAMPED=1 cplayer/lab fights.lst                         # one line per fight: points, time, damage, death
```

`tools/suite.py` runs whole sets of recorded fights in parallel and reports per scenario (pirates by
pack size, witchspace) with aim and targeting measures; `tools/compare.py` compares two runs fight by
fight. They expect recorded careers under `~/work/corpus` (see the docstring of `tools/suite.py`).

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
`!agent stop`, `!agent mode efficient|curious`, `!agent name <NAME>`, `!agent load <NAME>` (load
that commander from the disc when next docked), `!agent status` and `!agent log 0|1|2`. Saved games survive a power-off (the firmware keeps a save disc in flash).

`presto/elite_presto.c` is the Presto side; `presto/elite_splash.c` the loading screen
(`tools/make_splash.py`, from `presto/splash/8bitkick-logo.png`).

`docs/` explains how the game works, from the source: its economy, flight and combat, and game
flow.


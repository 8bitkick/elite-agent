# Elite agent

A player for Acornsoft Elite (BBC Master, Mark Moxon's flicker-free build), written in pure C. It reads
the machine's RAM and plays only through the keyboard and the analogue joystick (Elite's own joystick
option). It's self-contained: no OS, no heap, no network, no model, just rules, about 100 KB of code
and 60 KB of RAM compiled for the RP2350's Cortex-M33. It runs on that microcontroller, a Pimoroni
Presto, beside the BBC Master emulator, averaging about 2.4 ms of each 20 ms frame; the same code runs on a native
build of the emulator (Pi or Mac) for development.

| | | |
|---|---|---|
| ![A pirate Python passing close in the right view](docs/img/shots/flyby-side.png) | ![The front laser on a Thargoid](docs/img/shots/thargoid-laser.png) | ![Status: rating Elite](docs/img/elite-rank.png) |
| A flyby, watched from the side | Lasers on a Thargoid in witchspace | Elite |

## Modes

| Mode | What it does | Jameson to Elite |
|---|---|---|
| **Efficient** | straight for Elite; reads any RAM it needs, even bytes the screen never shows, like an enemy's energy ([`docs/IO.md`](docs/IO.md)) | **26.9 game hours** (26.2-27.7 on five careers, 4 deaths in all) |
| **Fair** | only what a player could see: the screen, scanner and dashboard as drawn, the game's rules, its own notes; no hidden RAM ([`docs/FAIR.md`](docs/FAIR.md)) | **29.9 game hours**, efficient + fair (27.7-32.8 on five careers) |
| **Curious** | explores every galaxy, meets every ship type, both missions; fights for show ([`docs/FIGHTING.md`](docs/FIGHTING.md)) | **117.8 game hours** (two careers), show fighting in development |

All modes save at each dock (about 4 seconds), so a death loses only the trip since. Times are game
hours on the native emulator at the real machine's speed.

![Kill tally against game hours: efficient 26.2 to 27.7 hours, fair 27.7 to 32.8, curious 116.9 to 118.8, and where it started, keyboard aim, 57.3](docs/img/climb.svg)

**Careers in numbers** (the mean per career, Jameson to Elite; `tools/career_stats.py`):

| | Efficient (5) | Fair (5) | Curious (2) |
|---|---|---|---|
| Game hours to Elite | 26.9 | 29.9 | 117.8 |
| Star systems visited | 64 | 67 | 384 |
| Galaxies | 3 | 3 | 8 |
| Light years flown (dock to dock) | 636 | 672 | 4,513 |
| Docks | 352 | 361 | 1,296 |
| Fights | 1,874 | 2,570 | 5,090 |
| Kills | 5,547 | 5,583 | 9,493 |
| Witchspace sessions (Thargoids) | 191 | 197 | 77 |
| Deaths (reloaded) | 1 | 4 | 50 |
| Escape pods used | 0 | 0 | 11 |
| Most credits held | 36,357 Cr | 25,649 Cr | 25,152 Cr |

Fair counts more fights because a dot at the edge of range can turn out to be a trader once it's
drawn, ending a fight that hadn't started.

Curious mode's fights are for the camera: over 600 recorded fights an enemy is drawn 20 pixels or
bigger for 3.8 seconds a fight, against 1.0 in efficient mode, which kills from as far out as it can.

**Where efficient's time goes** (seed 13): witchspace farming of Thargoids, by a forced mis-jump, is
55% of the time and 81% of the points, about 5 a game minute; bounty hunting at anarchies makes a
quarter of that. The early ranks take three hours, Deadly to Elite about 14. The two missions take 4
hours and pay for themselves: without them it reaches Elite 4.7 hours later.

![Share of game time and of points by activity: witchspace farming 55% of the time and 81% of the points](docs/img/time.svg)

**Why it's fast:**

- **Aim.** The joystick put at an exact position each frame, aiming 4.5 game loops ahead, and each
  new target reached in one planned roll-and-pitch motion simulated from Elite's own rotation maths.
  Over 9,327 recorded fights: 38% more points a fight minute than the keyboard, 39 deaths instead
  of 101, the target in the sights 82% of the time.
- **Thargoids.** Witchspace fights score about 30 points a fight minute, normal space about 12.
- **Cheap deaths.** With a save at every dock it can take risks a no-save player can't.
- **No waiting about.** It fights pirates on the way in as far out as it engages in a hunt (ones
  hanging back used to block the in-system jump), and leaves witchspace after its last fight without
  recharging: 31.4 to 27.4 hours.
- **Running fights back.** Breaking off from a pack, it flies tail-first and shoots the nearest
  pursuer with the rear laser: 7% more points a fight minute, fewer deaths, 26.9 hours.
- **No wasted trips.** Routes avoid systems the chart cursor can't select; the escape pod waits
  until it's rich.

![Points per fight minute, deaths and time in the sights over 9,327 recorded fights, for keys and joystick with and without lead](docs/img/aim.svg)

## The player (`cplayer/`)

| File | What it does |
|---|---|
| `player.h` | the interface: read-only RAM in, held keys, taps and joystick values out, one call a frame |
| `agent.c` | the career: shopping, trading, bounty hunting, witchspace farming, missions, recovery |
| `combat.c` | fighting: threats, targets, aim, speed, fire, evasion, ECM, missiles, bomb, pod |
| `pilot.c` | steering, the run in to the station, docking (roll-matching the slot) |
| `flight.c` | launch, hyperspace, the chart cursor, witchspace exits and chains, galactic jumps |
| `docked.c` | the docked screens: market, equipment, saving, titles |
| `galaxy.c` | the galaxy, markets, trade plans and loops, routes, safe jump ranges |
| `ships.c` | the ships in the bubble, and the crosshair test |
| `drv.c`, `keyq.c` | coroutines over frames (`pt.h`), keys, the joystick, death detection |
| `state.h`, `elite_ram.h` | named views onto Elite's RAM (addresses from `elite/symbols.json`) |

Every helper is a protothread coroutine that runs a little each frame and keeps its state in a static
struct; one that fails sets `g_fail` and the career loop recovers.

## Running it headless

The emulator is a git submodule ([rp2350-bbc-micro](https://github.com/8bitkick/rp2350-bbc-micro),
`bbc-master` branch). The ROMs and the Elite disc are in neither repository; `setup.sh` fetches them.

```sh
git clone --recursive https://github.com/8bitkick/elite-agent.git && cd elite-agent
rp2350-bbc-micro/scripts/setup.sh --no-build   # ROMs and disc (and the Presto toolchain)
cplayer/build.sh                               # run_sim: a career (elsewhere than Linux: ELITE_EMU=submodule path)
cplayer/buildlab.sh                            # lab: the fight lab
cplayer/run_sim 2400 "" journal.jsonl          # a career, up to 2,400 game minutes; Elite in ~27 h
```

It runs about 100 times faster than a real BBC Master, prints its log as it plays and writes a JSON
line per event (docks, fights, sessions, deaths, ranks) to the journal. Options, in the environment:

| Option | What it does |
|---|---|
| `SKEW=n` | start n frames late: a different career |
| `CURIOUS=1` | curious mode |
| `FAIR=1` | fair mode (`FAIRHANDS=1` adds limits on the stick and keys) |
| `STOPFAIL=1` | stop at the first death or restart |
| `SAVE=0` | never save (a death ends the career) |
| `FIGHTS=dir` | record each fight's starting snapshot, for the lab |
| `SHOTS=from-to:step` | PPM screenshots every `step` frames in that range |
| `LOAD=NAME` | load that commander from the disc when docked |
| `DISCDUMP=file.ssd` | write the disc, with the career's saves, at the end |

A black box is printed on every death. **The fight lab** replays recorded fights from their
snapshots, fighting each as the career would:

```sh
mkdir -p fights && FIGHTS=fights cplayer/run_sim 600 "" journal.jsonl
ls fights/*.fight > fights.lst && UNDAMPED=1 cplayer/lab fights.lst   # a line per fight
```

`tools/suite.py` runs sets of recorded fights in parallel per scenario (pirate pack sizes,
witchspace) with aim, targeting and death-cause measures, from `~/work/corpus` and `~/work/sets`;
`tools/compare.py` compares two runs fight by fight; `tools/career_summary.py` gives a line per
career; `tools/climb_chart.py` draws the chart above from career journals.

## On the Presto

`presto/build.sh` (`--curious` for curious mode) builds the firmware with the player in it, through
the emulator's `core/machine_hook.h`: `presto/build/presto_master.uf2`. Hold BOOT, tap RESET, copy
it to the RP2350 drive. The agent starts a little after boot.

The web panel (http://beeb.local/) shows the rank and progress, credits, where it is and what it's
doing, play time, kills, deaths and pods, its latest status lines, and buttons to start or stop it
and switch modes. Over USB: `!agent start|stop`, `!agent mode efficient|curious`, `!agent fair
on|off|hands`, `!agent name <NAME>`, `!agent load <NAME>` (when next docked), `!agent status`, `!agent log
0|1|2`. Saves survive a power-off (the firmware keeps a save disc in flash).

`presto/elite_presto.c` is the Presto side, `presto/elite_splash.c` the loading screen
(`tools/make_splash.py`). `docs/` explains the game from its source: economy, flight and combat,
game flow.

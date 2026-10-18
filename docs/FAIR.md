# Fair mode: information-fair

The player normally reads Elite's RAM: every ship in the bubble, exactly, every frame, with bytes no screen
shows (a ship's energy, whether it's hostile). [IO.md](IO.md) sorts every read into visible, static, derivable
and hidden. **Fair mode is fair in terms of information**: the player knows only what a human could see on the
screen, the scanner and the dashboard, plus the game's fixed rules and its own notes. No hidden RAM. It's still
plainly a machine, with a machine's reflexes, attention and arithmetic: it reacts within the frame, watches
every ship on the scanner at once and plans its turns by simulating Elite's own maths. Fair mode doesn't claim
otherwise. It reaches Elite on every career tried: 30.2 game hours on average (27.7-34.0 on five careers),
against 26.9 with the hidden bytes (§4).

It's a switch, apart from the two career modes: efficient + fair, curious + fair. Off (the default) the player
is the same program it always was, to the byte (checked on fights and careers, §4).

| Where | How |
|---|---|
| run_sim, lab | `FAIR=1`; `FAIRHANDS=1` adds the optional hand limits (§1); `FAIRMASK=n` for single parts; `FAIRP=name=value,...` for its numbers |
| Presto | `agent fair on|off` over USB (`agent fair hands` with the hand limits); a **Fair** button on the web panel beside Efficient and Curious |
| code | `g_fair` (`cplayer/fair.h`): bits `FAIR_SEE` 1 and `FAIR_DASH` 2 (`FAIR=1`: 3); optional `FAIR_STICK` 8, `FAIR_KEYS` 16 |

## 1. What it may know

A player at a BBC Master who knows the game well (the manual, the source's rules, the galaxy charts, the ship
blueprints) and keeps careful notes, watching the screen. Precisely:

**Information.** The player may use:

* **What's on the screen now**, at the precision it's drawn. The space view: each ship in the view at its
  pixel (direction exact to 1/256 of the range), its range only as its drawn size says (or the scanner), its
  model and heading only once it's drawn as a wireframe (Elite draws a far ship as a dot: beyond z_hi = the
  blueprint's visibility distance, e.g. 20 for a Sidewinder, ~5,000 units; 50 for a Cobra Mk III, 12,800),
  its laser lines when it fires at us, its explosion. The scanner: a dot and stick for each ship within
  16,383 on every axis, x to 512 units, the stick's foot to 1,024, its height to 512 and clipped at the
  scanner's edge, in its scanner colour (yellow missiles, green the station, blue pods, plates, canisters and
  the Worm, red rocks, cyan most ships, magenta the Python, Boa and Anaconda, white the Thargoid; the Cougar
  not at all). The dashboard's bars as drawn (the energy banks to 4, the shields, laser temperature and
  altitude to 16), its indicators (S, E, missiles and their lock, the compass), and the messages (INCOMING
  MISSILE, RIGHT ON COMMANDER).
* **What a docked screen shows**, remembered: cash, cargo, equipment, fuel, legal status, markets.
* **The game's fixed rules**: blueprints (energy, speed, lasers, kill points), the galaxy and its markets'
  formulas, how the controls and the laser work, where the station's slot faces. A player could know all of
  these from the manual, the source or experience.
* **Its own controls**: where it's put the stick, what it's holding, what it fired.
* **Its own memory and arithmetic** on all of that: where a ship went when it left the screen, how hard we
  have hit it, who fired at us, how many kills at how many points.

It may not use anything else. In particular none of: a ship's energy (byte 35), its hostility flag (NEWB),
its AI byte, exact positions or headings of ships off the screen, ships beyond the scanner and out of view,
the pirate Cobra and Python's types (they are the traders' models: the screen can't tell them apart), the
exact dashboard values, the kill tally (TALLY), the missile's target slot, the mass-lock test (FRIN/JUNK).

What it still reads from RAM, and why that's fair:

| Read | Stands for |
|---|---|
| the planet, sun and station: position, and the station's axes | the planet and station are on the screen or the compass (the compass shows the planet's direction, the station's in the safe zone); the station's slot axis always points at the planet (NWSPS), so it follows from the two positions; its roll is only used in the final approach, with the slot dead ahead on the screen. Their distances aren't quantised: a simplification (§5) |
| MCNT (the main-loop counter) | the screen redrawing: each game loop draws a new frame of the ships, so a watcher counts them |
| JSTX / JSTY (only to carry tracks over a game loop) | how fast the view is turning: seen on the screen, and known from our own stick |
| QQ11, VIEW, QQ12, MJ, ECMA, MSAR, MSTG = &FF, SSPR, the hyperspace count, cash, fuel, cargo, equipment, FIST | on the screen or the dashboard, or a docked screen |
| DELTA (our speed) | the speed bar, and our own throttle presses |
| TALLY+1 (the high byte only) | the RIGHT ON COMMANDER message, printed each time it counts up |
| bytes 0-8 and 31 of a ship (position; killed/exploding), and its type | **only through the screen and scanner model above** (and to follow a ship from one frame to the next: the eye's continuity) |

**Hands: an optional extra (`FAIRHANDS=1`).** Not part of fair mode: they limit the machine's hands rather than
what it knows. The stick moves at most 32 units a frame (lock to lock in 160 ms) and misses where it's put by
Gaussian noise of sd 1 + 4% of the deflection (motor noise grows with the command: Harris & Wolpert 1998); at
most 4 keys are down at once (3 while another is tapped); each key changes at most every 80 ms; taps last at
least 4 frames down and 3 up. On top of the information limits they cost about 5% of the points a fight minute
in the lab (§4); on their own, nothing visible in a 4-hour career (§3).

**No reaction delay.** An earlier version saw everything 200 ms late and predicted over the delay with its own
stick. The user dropped it: fair mode is about what the player knows, not when. It's gone from the code (bit 4
of `g_fair` is unused). On the 300-fight screen it had cost about a fifth of the points a fight minute.

**Game options a human would play with.**

* **Saves and reloads: allowed.** A human can save at every dock and reload after a death (the game's own
  disc menu and its "Load New Commander (Y/N)?"). It's save-scumming, and an ironman human would be slower;
  the careers report deaths so the cost can be seen.
* **The forced mis-jump (X in the pause, then CTRL as the hyperspace countdown ends): allowed, flagged.**
  It's in the shipped game, a real key a player could learn about (the author credits flag, Moxon's `ee5`),
  but it's an Easter egg, not normal play, and witchspace farming through it is most of the career's points.
  `FAIRP=mj=0` turns it off: then witchspace comes only by the game's own rare mis-jumps.
* **The joystick option, keyboard damping, pausing to set them: allowed.** Ordinary options in the pause menu.

## 2. How it's done (cplayer/fair.c)

All the player's reads go through two choke points, `state.h` and `ships_read()`; fair mode works there, and
the rules themselves (combat, flight, the career) are the same code.

* **The ships as seen (FAIR_SEE), `see_ships()` in place of `ships_read()`.** Each ship is followed from frame
  to frame (the game's slot and position say which is which, as an eye follows a moving dot; nothing else of
  the truth is used). Each frame it's measured as the screen and scanner show it (above), and a Kalman filter
  per track (position and its own motion a game loop, with their full covariance) is carried each game loop by
  our own turn and speed (Elite's own rotation, MVEIT) and the ship's motion. On the screen the direction is
  the pixel's and only the range is uncertain (the measurement's error is small across the line of sight and
  the range's along it); on the scanner only, the dot's quantisation; out of both, where it should be now, for
  3 seconds. While it's drawn as a wireframe its speed along its nose is filtered separately, from how far it
  moved since it was last drawn (across the line of sight the screen measures to the pixel). The rules get
  the same `ship_t` they always had, with:
  * position: the estimate (on the screen, along the pixel's direction);
  * type: the model once drawn as a wireframe (24 as 11, 26 as 12: they look the same), else a stand-in for
    its colour (cyan a Sidewinder, magenta a Python, white a Thargoid, blue a canister, red an asteroid; in
    witchspace cyan is a Thargon);
  * heading: drawn as a wireframe, the real one (a wireframe shows it); else the direction it's been moving;
  * speed: drawn, the filtered speed along its nose; else from its motion;
  * energy: the blueprint's, less our hits on it (in our sights while our laser fired, the laser's power a
    shot), plus its recharge (1 every 8 game loops);
  * hostile: in witchspace, everything; on sight, the pirates' and Thargoids' models (Sidewinder, Mamba,
    Krait, Adder, Gecko, Cobra Mk I, Worm, Asp, Fer-de-lance, Thargoid, Thargon, Constrictor); by what it
    does: drawn firing at us, or (out of the safe zone) its nose on us while closing within 12,000 for half a
    second, hit by our laser (Elite then makes it hostile), or the only ship on the side a hit on our shield
    came from; and out of the safe zone, a dot of a pirate's colour we can't yet tell within 16,954 (cyan
    always: in pirate space a hunter goes to look; magenta only coming at us): taken for a pirate until it's
    drawn close enough to see. The police (Viper) and the Transporter (which counts as police) only when
    drawn firing at us, or once we're fugitives;
  * missiles: incoming unless it appeared just after we fired one;
  * the ship it is (`id`): fights follow the track, not the slot.
* **The dashboard (FAIR_DASH).** `energy()`, `fsh()`, `ash()`, `laser_temp()`, `altitude()` return the bar's
  middle; the tally is our count of the kills we saw (our hits on it within 4 s of it blowing up) at each
  type's points, kept inside the 256-point band the RIGHT ON COMMANDER messages give, saved and reloaded
  with the commander; the missile's lock is on the ship that was in our sights when the indicator locked;
  the in-system jump is pressed and refused by the game when mass-locked (no FRIN test).
* **The hands (FAIR_STICK, FAIR_KEYS)** in `drv.c`: the rules say where they want the stick and which keys;
  once a frame the hand moves the stick towards it at the stick speed, with the noise; the fingers press the
  most needed keys (CTRL, fire, ECM, jump, throttle first) up to the cap, and a key that changed too recently
  waits. `jstx()` and `jsty()` are where the hand has put the stick.

## 3. Why the first fair careers failed (diagnosis)

The first full fair career (seed 13, with the 200 ms delay; `car/v2fair` on the macbox) was at a tally of 8.86
after 45 game hours, still trading between nearby systems, never able to afford a better laser ("no witch:
laser" at all 452 trip decisions). Its journal: **310 deaths**, 134 docks, cash never above 477 Cr; its log:
**1,856 Vipers and 269 Transporters destroyed**. The deaths were 155 shot down and 129 collisions, most
with a Viper the nearest ship.

The trace of the first incident (game minute 9.3, run.log): inside the safe zone (`sspr 1`) on the way to the
station, a Transporter launched from the station was announced as "Enemy Sidewinder" (an unknown cyan dot's
stand-in) and destroyed. The Transporter counts as police: killing it makes us a fugitive (FIST |= 64), and
from then on the station launches Vipers at every visit. So every career step after that was a fight with the
police, ending in death and a reload of a save that was itself made as a fugitive.

Per part, one bit at a time, 4-hour careers on the same seed (macbox, `car/d*`):

| Career, 4 game hours, seed 13 | deaths | tally at 4 h | Vipers / Transporters killed |
|---|---|---|---|
| fair off | 0 | 269.7 | 0 / 0 |
| FAIR_SEE as first written | **26** | **2.0** | **218 / 24** |
| FAIR_SEE with the safe-zone and police rules (below) | 0 | 272.7 | 0 / 0 |
| FAIR_DASH alone | 0 | 272.4 | 0 / 0 |
| FAIR_STICK alone | 0 | 269.9 | 0 / 0 |
| FAIR_KEYS alone | 1 | 256.3 | 0 / 0 |
| all of it (27) | 2 | 287.1 | 0 / 0 |

So the career failure was one rule of the perception layer, not aim, not the hands, not the economy:
**hostility by behaviour misfired on the station's traffic.** Two rules did it: an unknown cyan dot closing
on us was taken for a pirate, and so was any ship whose nose pointed at us while closing for half a second.
Near the station both describe a shuttle or Transporter leaving it, or a Viper. Fixed: in the safe zone (the S
on the dashboard) neither rule applies, nor does the hit-on-our-shield rule; the police and Transporters are
hostile only when they fire at us (drawn laser lines) or we're already fugitives; our own laser hitting one
doesn't count. (Pirates in the safe zone have their aggression zeroed by the game, so leaving them alone there
costs a little score and no safety.)

**The fights.** The 300-fight screen (eff300: stratified, from careers), pts per fight minute, one part at a time:

| eff300 | pts/min | deaths |
|---|---|---|
| fair off | 15.77 | 2 |
| FAIR_SEE | 13.33 | 2 |
| FAIR_DASH | 15.24 | 2 |
| FAIR_STICK | 14.06 | 1 |
| FAIR_KEYS | 15.39 | 1 |
| all (27) | 12.64 | 5 |

The information costs about 15%, the stick about 10%, the dashboard and keys a few percent each. What the
information costs, found by giving the perceived ships back one true field at a time (`FAIRP=oracle=`, a
diagnosis switch only): the true hostility +2% (pirates alone +20% on single pirates), true energy 0, true type
+2%, true position +1%, true heading and speed +12%, true speed alone +9%. So the main cost is **knowing how fast
a far ship moves** (the lead on it): its range, and so its speed along the line of sight, comes only from a dot's
scanner position or a few pixels of drawing. Measured against the truth, a drawn ship's motion across our line of
sight is now within 3 units a game loop (of ~15); a dot's is not known better than its size (error 15 of 8),
because a dot crosses one pixel in ~4 game loops at 8,000 and the scanner's steps are 512-1,024 units.

The paired fights that lose most under FAIR_SEE are mostly ones where the lab scores nothing: a lone pirate
parked as a dot at 9,000-11,000, a pack still beyond identification, or pirates in the safe zone. The rules
don't engage what they can't yet call hostile, and the lab stops after 3 s of quiet; in a career the fight
comes a little later, when it closes. Cyan dots out of the safe zone are now taken for pirates at once (+1.4%).

## 4. Numbers

**Careers to Elite** (efficient + fair; run_sim on the native emulator at the real machine's speed, seeds as
`SKEW`, the same five as the efficient record; macbox `~/work/fair/car/`):

| Seed | efficient record | fair, information only (`FAIR=1`, the audited build) | fair + hands (`FAIRMASK=27`, the build before the master merge) |
|---|---|---|---|
| 13 | 27.0 h, 2 deaths, 1,860 fights | 30.6 h, 3 deaths, 2,525 fights | 33.6 h, 5 deaths |
| 101 | 27.5 h, 0, 1,789 | 30.8 h, 6, 2,984 | 32.7 h, 4 |
| 37 | 26.2 h, 0, 2,162 | 29.3 h, 0, 2,313 | 38.5 h, 8 |
| 71 | 27.7 h, 1, 1,706 | 30.2 h, 1, 2,542 | 34.8 h, 4 |
| 149 | 26.6 h, 1, 1,851 | 30.0 h, 6, 2,929 | 32.8 h, 7 |
| mean | **26.9 h**, 0.8 deaths | **30.2 h** (+12%), 3.2 deaths | 34.5 h (record then 27.4 h) |

(Hours: run_sim's ELITE line, the first whole game minute docked at a tally of 6,400 or more, as for the record.
Journals: macbox `~/work/fair/car/v8info/s_<seed>/journal.jsonl` for fair (the build after the leak audit, §6;
the build before it, `v6info`, also averaged 30.2 h: 34.0, 29.9, 29.7, 27.7, 29.5), `~/work/eff/car/brkrear/u_<seed>/` for
the record, `~/work/fair/car/v5fair/s_<seed>/` for fair + hands.)

Every fair career reaches Elite; information-only is 30.2 hours against 26.9 (29.3-30.8 on five seeds). **Where the gap comes from** (from the journals, the five information-only careers against
the record's five): witchspace farming makes slightly more points (5,451 a career against 5,244) in 16.3 hours
instead of 14.4 (5.58 points a minute against 6.08). Per witchspace session (seed 13), the farming itself is no
slower (0.97 minutes against 1.10: it chains as often), but the trip home to dock takes 3.60 minutes against 3.09:
travel fights against ships it has to identify before it engages, and a few seconds lost to each dot that turns
out a trader. Bounty hunting makes a fifth less (1.2 points a minute against 1.3): lone pirates parked as dots
far out, and pirate Cobras and Pythons that look like traders until they fire. Deaths 0-6 a career against 0-2;
each costs only the trip since the last save.

**Fights, splits.** The fair careers log more fights for the same kills (2,659 a career against 1,874). They're
splits more than extra fighting: a "fight" that starts and ends within a second and scores nothing, a dot taken
for a pirate that a closer look shows is a trader, or a dot's estimated range wandering across the edge of reach.
The first fair careers had 2,074 of them in seed 13 (220 in the record), and then ignored everything about for 3
minutes, 232 times. Fixed: in a fight it takes 3,000 more range to end it than to start it, and a fight over
within 10 s doesn't start the 3-minute ignore. On the merged build there are 176-1,031 such a career (192 in the
record's seed 13): fewer, but still the main reason the count is higher; they cost about a second each.

**The fight lab** (pts per fight minute, deaths; every fight from its recorded start, its own process):

| | eff300 (300 fights) | mid (889 fights) |
|---|---|---|
| fair off (master) | 16.71, 2 deaths | 15.34, 0 |
| fair, information only (`FAIR=1`) | 14.88 (-11%), 1 | 13.71 (-11%), 0 |
| fair + hands (`FAIR=1 FAIRHANDS=1`) | 13.97 (-16%), 2 | 13.08 (-15%), 0 |

(eff300: stratified from career fights, none from mid; mid: a tenth of the 9,327-fight corpus, stratified. Witchspace,
most of a career's points: 20.96 -> 18.94 and 21.06 -> 19.09 pts/min with the information limits.)

**Fair off is the same program.** Built from this branch with fair off and from master: the same lab output on
200 recorded fights (every line), and the same 5- and 10-hour careers (journal and log identical).

## 5. Simplifications still in it

* The planet, sun and station are given exactly (direction and distance), though a player sees their
  direction only (screen and compass) and their distance roughly (size, the altitude bar).
* A ship is followed from frame to frame by the game's own slot (an eye's continuity); after an explosion or
  a ship leaving, the game's slot shift is followed too.
* Thinking is free: the acquisition planner simulates 315 stick plans 50 game loops ahead each game loop.

## 6. Leak audit: is it really information-fair?

Asked after the first results ("sounds too good"): three checks, then the leaks they found, fixed, and the
careers measured again.

**1. Every read, by address and call site.** Built with `-DREAD_AUDIT`, `R()` and `R16()` (every read through
`state.h`, which is all the player's reads but three) record the address and the source line, while the player
is playing (`cplayer/audit.h`; `AUDIT` lines at the end of run_sim or the lab). Over the 300-fight set and a
10-hour fair career (seed 101), the player read 92 addresses. None is a ship's block (K% &0400-&05BB): those are
read only by `ships_read()` inside the perception layer. The rest, grouped:

| Read | Where | Verdict |
|---|---|---|
| QQ11, QQ12, VIEW, MJ, SSPR, ECMA, MSAR, the hyperspace count, QQ9/QQ10 (the chart cursor), XC/YC (the text cursor), distaway | screens and indicators | on the screen |
| MCNT, LASCT | the game loop's pace; the death sequence | the screen redrawing; the death on the screen |
| JSTX, JSTY, DELTA, the option bytes (DAMP, PATG, JSTK, the axis flags) | our own controls and settings | our own |
| ENERGY, FSH, ASH, GNTMP, ALTIT | through `energy()` etc.: the bars as drawn. Raw in three places: the perception layer quantises them itself (hits on our shields, as bars); `briefing()` tests all three at 255 (the screen after a dock, not a decision in flight); `full_strength()` in flight.c read FSH/ASH exactly | the last one a **leak** (fine detail): fixed |
| FRIN (slot 0's type only) | `briefing()`: is the planet in slot 0 | the planet on the screen |
| MSTG | only whether it's &FF (the indicator); on what it locked, from our own sights | on the dashboard |
| TALLY, TALLYL | the high byte (RIGHT ON COMMANDER); the low byte and fraction only for the log, the journal, the rank in status lines, and the tally estimate's starting value (the commander's record when the career starts) | not used by any decision: proven below (scrambled, play unchanged) |
| FIST | Fugitive or not (50+), and the log | on the Status screen |
| cash, fuel, cargo, equipment, lasers, missiles, the mission bits, the market (AVL, QQ26, QQ28, tek, QQ25, QQ29) | docked decisions | on the docked screens (QQ26 from the prices it sets) |

Reads straight from `IO->ram` instead of `R()`: the commander's name and the saved copy (docked screens),
`ships_read()` in the perception layer, and the lab's check against the truth (only with `FAIRCHK`).

**2. Precision.** In the perception layer (`perceive_update()` and `measure()` in fair.c) a ship's raw block is
used for: its position, turned into what the screen shows (its pixel, its drawn size rounded to a pixel, or the
scanner's dot and stick from the high bytes) and only that goes into the track filter; the frame-to-frame
continuity (which track is which ship, and whether it has moved yet this game loop); its type and its axes only
while it's drawn as a wireframe; byte 31's on-scanner, drawn-firing and exploding bits. The rules (aim, the lead,
the acquisition planner, target choice, collisions) get only the filtered estimate. Two places used the truth
for the estimate's own uncertainty or memory, now fixed: the range-from-size error was computed from the true
range (now from the estimated one), and a remembered ship out of sight showed the game's killed/exploding bits
(now as it was last seen; a first fix that showed it alive instead made dead ships out of sight count as threats
and split fights into ~16,000 a career, caught by the fight count and fixed). The station's block went through with its energy, AI and flags; now only its
position and axes.

**3. Scrambled bytes, the same play.** `SCRAMBLE=1` hands the player a copy of RAM each frame with every ship's
AI byte, energy, NEWB (but the scooped bit), speed, acceleration and roll and pitch counters replaced by random
numbers; `SCRAMBLE=2` also the tally's low byte and fraction. Fair off, that wrecks it (200 fights: 10.89 pts/min,
1 death -> 0.57 pts/min, 39 deaths). Fair on, nothing changes: the same output line for line on the 200 fights
(scramble 2) and the same 10-hour career journal (6,801 lines, scramble 1).

**4. Why the gap is small.** Most of the career is witchspace farming (95% of a fair career's points: 5,451 of
5,770 on average), where fair mode loses least. In witchspace every ship is a Thargoid or a Thargon, so hostility
is certain without the NEWB byte; a Thargoid is drawn as a wireframe out to z_hi 55 (14,080 units), so its type,
heading and speed along its nose are known for most of a fight; and the chaining, the way out and the docking
don't depend on what a ship is. The lab's witchspace scenario loses 10% of its points a minute, single pirates
up to 20%. Fighting is also only part of the time: 3.3 of 26.9 hours in the record (13%); a fight that's 10%
slower costs about 1% of the career. What the careers lose instead is the travel: the trip home after witchspace
(3.6 minutes a session against 3.1), and hunting (a fifth fewer points a minute), where pirates have to be seen
before they're fought.

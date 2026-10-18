# Fair mode: playing as a human could

The player normally reads Elite's RAM: every ship in the bubble, exactly, every frame, with bytes no screen
shows (a ship's energy, whether it's hostile), and it puts the joystick anywhere in one frame. [IO.md](IO.md)
sorts every read into visible, static, derivable and hidden. **Fair mode** keeps only what a human player
could see or work out, and only what a human hand could do, and still has to get to Elite.

It's a switch, apart from the two career modes: efficient + fair, curious + fair. Off (the default) the player
is the same program it always was, to the byte (checked on fights and careers, §4).

| Where | How |
|---|---|
| run_sim, lab | `FAIR=1` (all of it); `FAIRMASK=n` for parts; `FAIRP=name=value,...` for its numbers |
| Presto | `agent fair on|off` over USB; a **Fair** button on the web panel beside Efficient and Curious |
| code | `g_fair` (`cplayer/fair.h`): bits `FAIR_SEE` 1, `FAIR_DASH` 2, `FAIR_STICK` 8, `FAIR_KEYS` 16 (`FAIR=1` is 27) |

## 1. What "fair" means

A human at a BBC Master with a joystick, who knows the game well (the manual, the source's rules, the galaxy
charts, the ship blueprints) and keeps careful notes, but has a human's eyes and hands. Precisely:

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

**Inputs (human-fair).** The numbers, and where they come from:

| Limit | Value | Why |
|---|---|---|
| Stick speed | at most 32 units a frame: lock to lock (254) in 8 frames, 160 ms | fast aimed wrist movements of a joystick's travel take ~100-250 ms (Fitts' law; ballistic flicks at the short end) |
| Stick precision | where it's put, off by Gaussian noise of sd 1 + 4% of the deflection, wandering over ~10 frames | motor noise grows with the size of the command (signal-dependent noise: Harris & Wolpert 1998); endpoint errors of a few percent of the movement |
| Keys down at once | at most **4** (3 while tapping another) | one hand on the stick (with its fire button), the other on the keyboard |
| Key rate | each key changes at most once every 4 frames (80 ms): ~6 presses a second per key; taps at least 4 frames down and 3 up (~7 a second, a fast typist's 85 words a minute) | finger tapping tops out at ~5-7 Hz per finger |
| Thinking | not limited | the rules can plan and compute as much as they like between frames: fair mode limits what they see and what their hands can do, not how well they think |
| Reaction time | **none added** (keys still land 2 frames after they're asked for, as always) | see below |

**No reaction delay (the user's call).** The first version saw everything 200 ms late (8 frames of
perception delay plus the 2 every key already takes; simple visual reaction time is ~190-250 ms), and carried
what it saw forward over the delay with its own stick (a Smith predictor). The user asked for the artificial lag
to be removed: fair mode limits what the player can know and what its hands can do, not when it sees it. The
code is gone (bit 4 of `g_fair` is unused). For the record, on the 300-fight screen it cost about a fifth of
the points a fight minute on top of the information limits (12.93 -> 10.22 pts/min), mostly in aim: the
prediction over the delay put the target 0.9 deg off on average where the screen alone gives 0.13.

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

(careers to Elite: efficient + fair against the 27.4 h record; below)

## 5. Simplifications still in it

* The planet, sun and station are given exactly (direction and distance), though a player sees their
  direction only (screen and compass) and their distance roughly (size, the altitude bar).
* A ship is followed from frame to frame by the game's own slot (an eye's continuity); after an explosion or
  a ship leaving, the game's slot shift is followed too.
* Thinking is free: the acquisition planner simulates 315 stick plans 50 game loops ahead each game loop.

# Flight, combat and docking in BBC Master Elite (from the source)

Source: `vendor-src/1-source-files/main-sources/elite-source.asm` (cited as `L1234`) and
`elite-data.asm` (cited as `data L123`). Addresses: `elite/symbols.json`. Helpers: `elite/ships.py`.
All rates below are **per main-loop iteration**, not per 50 Hz frame. One iteration = one pass of
TT100 (L36704), and its length depends on how much is drawn; measure it by watching `MCNT` (&84),
which decrements once per iteration (L36721).

Order of one iteration: `M%` flight loop (L10595: damping, ALPHA/BETA, flight keys, move/draw/hit-test
every ship, recharge, altitude) -> spawning (only when MCNT wraps to 0) -> `MLOOP` (L37240: cool the
laser, LASCT -= 2, dashboard) -> `TT17`/`DOKEY` (L35315, L40678: **keys are sampled here, once per
iteration**) -> `TT102` (function keys) -> repeat. A key must be held until DOKEY has run.

## 1. Ship slots and the 37-byte ship block

* `FRIN` (&0E41, 13 bytes, L1282): ship type per slot, packed; first 0 ends the list. Slots move
  down when a ship is removed (KILLSHP, L35782).
  * slot 0 = the **planet** (type 128 or 130, SOS1 L31409); slot 1 = the **sun** (129) or the
    **space station** (2) - never both (they share the LSO line heap). Slots 2-11 = ships.
  * In witchspace there is no planet/sun: RES2 wipes FRIN and slots 0.. are Thargoids (MJP L28712).
* `K%` (&0400): slot *i*'s block is at `&0400 + 37*i` (UNIV table L14134 holds the same pointers).
  K% is **always in the front-view frame** (the view flip PLUT works on the INWK copy only; only
  bytes #31 and #35 are written back after it, L11750/L11806).
* `MANY` (&0E4E): `MANY+t` = number of ships of type t. `MANY+2` is `SSPR` (&0E50, L1301):
  non-zero = station present = **inside the safe zone**. `JUNK` (&0E70) counts junk (types 3-10 + hermit).

Block layout (INWK L910-929; ZINF L36340; NWSHP L32317):

| byte | meaning |
|---|---|
| 0-2 | x: lo, hi, sign. 24-bit **sign-magnitude**: value = ±((sign&&7F)<<16 \| hi<<8 \| lo), bit 7 of byte 2 = sign |
| 3-5 | y, same format |
| 6-8 | z, same format |
| 9-14 | nosev x,y,z: each (lo, hi) 16-bit sign-magnitude, bit 7 of hi = sign. **1.0 = &6000** (hi byte 96) (ZINF L36340) |
| 15-20 | roofv (up), same |
| 21-26 | sidev (right), same |
| 27 | speed (0..blueprint max). Moves along nosev by `|nosev_hi|*speed*4/256` = **1.5 x speed units/iteration** (MVEIT part 3, ~L46120) |
| 28 | acceleration, two's complement, added to speed once then zeroed (MVEIT part 4) |
| 29 | roll counter: bit 7 direction, bits 0-6 count. Non-zero => rotate 1/16 rad (3.6 deg) this iteration; count decrements, except 127 = forever (MVEIT part 8, L46607; MVS5 L16388) |
| 30 | pitch counter, same |
| 31 | bits 0-2 missiles left; bit 3 drawn on screen; bit 4 on scanner; bit 5 exploding; bit 6 firing laser at us (or cloud drawn); **bit 7 killed** |
| 32 | AI: bit 7 AI on, bits 1-6 aggression 0-63, bit 0 has ECM. For a missile: bit 6 = hostile (targets us), bits 1-5 = target slot (FRS1 L20239) |
| 33-34 | ship line heap pointer |
| 35 | energy |
| 36 | NEWB flags (L929, data L154): bit 0 trader, 1 bounty hunter, 2 **hostile**, 3 pirate, 4 docking, 5 innocent bystander, 6 cop, 7 scooped/docked (removed next pass) |

Byte #31 life cycle: a kill sets bit 7 (laser L11678, bomb L11190, collision L11558, missile L18387).
Next time LL9 draws it (L42613; only in a space view) it becomes bit 5=1 / bit 7=0 (exploding);
when the cloud counter overflows DOEXP sets bits 5+7 (EX2 L30947) and the main loop then pays
the bounty and removes it (L11750). Ships beyond 224 in any `hi` byte (FAROF L37692) just leave
the bubble (no credit).

### Ship types (blueprints in elite-data.asm, data L506-3441; XX21 data L116; E% data L183; KWL%/KWH% data L242/L296)

Energy/speed/laser/missiles are the blueprint maxima; bounty is in Cr; pts = kill points (KWH%+KWL%/256);
"dmg" = what one of its laser hits takes off us (blueprint byte #19 / 2, L18993). Default flags from E%.

| # | ship | energy | speed | laser | msl | bounty | radius | pts | dmg | default NEWB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Missile | 2 | 44 | 0 | 0 | 0 | 40 | 0.58 | - | - |
| 2 | Space station (Coriolis/Dodo) | 240 | 0 | 0 | 6 | 0 | 160 (Dodo 180) | 0 | - | - |
| 3 | Escape pod | 17 | 8 | 0 | 0 | 0 | 16 | 0.06 | - | trader |
| 4 | Alloy plate | 16 | 16 | 0 | 0 | 0 | 10 | 0.04 | - | - |
| 5 | Cargo canister | 17 | 15 | 0 | 0 | 0 | 20 | 0.04 | - | - |
| 6 | Boulder | 20 | 30 | 0 | 0 | 0.1 | 30 | 0.02 | - | - |
| 7 | Asteroid | 60 | 30 | 0 | 0 | 0.5 | 80 | 0.03 | - | - |
| 8 | Splinter | 20 | 10 | 0 | 0 | 0 | 16 | 0.04 | - | - |
| 9 | Shuttle | 32 | 8 | 0 | 0 | 0 | 50 | 0.06 | - | trader, innocent |
| 10 | Transporter | 32 | 10 | 0 | 0 | 0 | 50 | 0.07 | - | trader, innocent, **cop** |
| 11 | Cobra Mk III | 150 | 28 | 2 | 3 | 0 | 95 | 0.91 | 9 | innocent, pod |
| 12 | Python | 250 | 20 | 3 | 3 | 0 | 80 | 0.66 | 13 | innocent, pod |
| 13 | Boa | 250 | 24 | 3 | 4 | 0 | 70 | 0.83 | 14 | innocent, pod |
| 14 | Anaconda | 252 | 14 | 7 | 7 | 0 | 100 | 1.00 | 31 | trader, innocent, pod |
| 15 | Rock hermit | 180 | 30 | 0 | 2 | 0 | 80 | 0.33 | - | trader, innocent, pod |
| 16 | Viper (police) | 140 | 32 | 2 | 1 | 0 | 75 | 0.10 | 8 | bounty hunter, cop, pod |
| 17 | Sidewinder | 70 | 37 | 2 | 0 | 5 | 65 | 0.33 | 8 | **hostile, pirate** |
| 18 | Mamba | 90 | 30 | 2 | 2 | 15 | 70 | 0.50 | 9 | hostile, pirate, pod |
| 19 | Krait | 80 | 30 | 2 | 0 | 10 | 60 | 0.33 | 8 | hostile, pirate, pod |
| 20 | Adder | 85 | 24 | 2 | 0 | 4 | 50 | 0.35 | 8 | hostile, pirate, pod |
| 21 | Gecko | 70 | 30 | 2 | 0 | 5.5 | 99 | 0.33 | 8 | hostile, pirate |
| 22 | Cobra Mk I | 90 | 26 | 2 | 2 | 7.5 | 99 | 0.66 | 9 | hostile, pirate, pod |
| 23 | Worm | 30 | 23 | 1 | 0 | 0 | 99 | 0.20 | 4 | hostile, trader |
| 24 | Cobra Mk III (pirate) | 150 | 28 | 2 | 2 | 17.5 | 95 | 1.16 | 9 | hostile, pirate, pod |
| 25 | Asp Mk II | 150 | 40 | 5 | 1 | 20 | 60 | 1.08 | 20 | hostile, pirate, pod |
| 26 | Python (pirate) | 250 | 20 | 3 | 3 | 20 | 80 | 1.16 | 13 | hostile, pirate, pod |
| 27 | Fer-de-lance | 160 | 30 | 2 | 2 | 0 | 40 | 1.25 | 9 | bounty hunter, pod |
| 28 | Moray (never spawned, L37064) | 100 | 25 | 2 | 0 | 5 | 30 | 0.75 | 8 | hostile, pirate |
| 29 | Thargoid | 240 | 39 | 2 | 6 | 50 | 99 | 2.66 | 11 | hostile, pirate |
| 30 | Thargon | 20 | 30 | 2 | 0 | 5 | 40 | 0.13 | 8 | hostile |
| 31 | Constrictor | 252 | 36 | 6 | 4 | 0 | 65 | 5.33 | 26 | hostile |
| 32 | Cougar | 252 | 40 | 6 | 4 | 0 | 70 | 5.33 | 26 | innocent |
| 33 | Dodo (spawned as type 2) | 240 | 0 | 0 | 0 | 0 | 180 | 0 | - | - |

"radius" = sqrt(targetable area) in world units (see HITCH). Note the station's 6 "missiles" are
never fired; a hostile station launches Vipers instead.

## 2. Coordinate frame and views

* Our ship is the origin; **x right, y up, z forward**. Projection (PROJ L32746 + DVID3B2 L22854,
  which returns 256*num/den): `screen_x = 128 + 256*x/z`, `screen_y = 96 - 256*y/z` on the
  256x192 space view. So the view edge is x/z = ±0.5, y/z = ±0.375. z < 0 = behind.
* NWSPS/ZINF orientation convention: a ship facing us has nosev = (0,0,-1), roofv = (0,1,0),
  sidev = (1,0,0).
* PLUT (L47336) re-expresses each ship in the current `VIEW` (&0E77: 0 front, 1 rear, 2 left,
  3 right) before HITCH and drawing: rear `(x,y,z) -> (-x, y, -z)`; left `(z, y, -x)`;
  right `(-z, y, x)`; the same swap/flip is applied to the three vectors. `ships.to_view()`.

## 3. Our controls

Keys (IKNS L47937, key logger L1004-1100): `<` KY3 roll left, `>` KY4 roll right, `S` KY6 dive
(nose down), `X` KY5 climb (pull up), SPACE KY2 faster, `?` KY1 slower, `A` fire, `T` target,
`M` fire missile, `U` unarm, `E` ECM, `J` in-system jump, `C` docking computer, `P` docking computer
off, TAB energy bomb, ESCAPE escape pod. f0-f3 = front/rear/left/right view.

**Roll/pitch rates** JSTX (&C1, L940) and JSTY (&C2, L959): 1..255, 128 = centred. DOKEY DK152 (L40910):
* `<`: JSTX += 7 (BUMP2 L23157), `>`: JSTX -= 7 (REDU2 L23223); `S`: JSTY += 14, `X`: JSTY -= 14.
  Clamped to 1..255.
* **Auto-recentre** (`DJD` &2C57 = 0, default; L10064, djd1 L23248): pressing the key *opposite* to
  the current deflection snaps the rate to 128 on that iteration (if the result is still on the
  far side of 128). One tap of the opposite key = instant stop.
* **Damping** (`DAMP` &2C56 = 0, default; L10053; cntr L23085): each flight-loop pass moves
  JSTX 2 steps and JSTY 1 step towards 128 (part 2, L10614). Always on while the docking computer runs.
  Holding `<` therefore nets +5/iteration on JSTX, `S` +13 on JSTY.
* Toggle both while paused: COPY pauses, CAPS LOCK toggles DAMP, `A` toggles DJD, DELETE unpauses
  (DKS3 L40612, DK4).

**ALPHA/BETA** (part 2 of M%, L10614-10780): let d = JSTX-128, e = JSTY-128.
* `ALP1 = |d|>>2` if |d| >= 32 else `|d|>>3` (0..31); ALPHA (&86, sign-magnitude) has the sign of d.
  So `<` => ALPHA > 0. |d| 0-7 gives 0.
* `BET1 = (|e|+4)>>4`, halved if < 3 (0..8; for e < 0 use |e|-1, and the 6502 may add a carry of 1);
  BETA (&42) has the **opposite** sign of e. So `X` (climb) => BETA > 0, `S` => BETA < 0.
  Pitch dead zone: |e| < ~28 gives 0; then 1, 3, 4 ... 8.
* Units: 1/256 radian per iteration. Max roll 31 = 6.9 deg/iter, max pitch 8 = 1.8 deg/iter.
  `ships.alpha_from_jstx`, `beta_from_jsty`.

**How the universe moves each iteration** (MVEIT L46032; part 5 ~L46256, part 6 MV45 L46500,
part 7 MVS4 L46876), with a = ALPHA/256, b = BETA/256:
```
own motion:  pos += nosev * 1.5*speed
rotation:    K2 = y - a*x ;  z = z + b*K2 ;  y = K2 - b*z ;  x = x + a*y
our speed:   z -= DELTA
vectors:     v_y -= a*v_x ; v_x += a*v_y ; v_y -= b*v_z ; v_z += b*v_y   (nosev, roofv, sidev)
```
So **ALPHA > 0 (`<`) turns the scene clockwise on screen** (12 o'clock moves to 3), i.e. we roll
left; ALPHA < 0 (`>`) brings a 3-o'clock target up to 12. **BETA > 0 (`X`) moves things ahead down**
the screen (nose up), BETA < 0 (`S`) moves them up. Aiming recipe (ships.aim_keys): roll until the
target is at 12 o'clock (or 6), then climb (or dive) until x,y are inside the hit radius. Planet and
sun are rotated the same way (MV40). `ships.predict()` steps this model.

**Speed** `DELTA` (&75, L679): SPACE +1/iteration up to 40, `?` -1 down to 1 (L10811-10835). Our
speed is DELTA units/iteration along z. `DELT4` = DELTA*64.

## 4. Lasers, missiles, ECM, bomb, escape pod

**Firing** (L10988-11050): needs `A` held, `LASCT` (&0E78) = 0, `GNTMP` (&0E79) < 242, and a laser in
`LASER+VIEW` (&1244..). Power = bits 0-6: pulse 15, beam 143 (15, continuous), military 151 (23),
mining 50 (L122-126). Each shot (LASLI L23435): GNTMP += 8 and **ENERGY -= 1**. MLOOP cools GNTMP by 1
and LASCT by 2 per iteration (L37240). LASCT after a shot = power & &FA: pulse 10 (a shot every
5 iterations), mining 50 (every 25), beam/military 0 (every iteration). Overheat: beam/military net
+7/iteration -> locked out after ~35 iterations of continuous fire; pulse nets +3 per 5 iterations.

**Hit test HITCH** (L20107), run on each ship in the current view after PLUT, space views only:
z_sign = 0 (in front, z < 65536), not a planet/sun, not exploding (bit 5), **x_hi = y_hi = 0**,
and `x_lo^2 + y_lo^2 <= targetable area`. It is a **cylinder of fixed radius around our line of
fire** (radius = the table's "radius"), with no range limit inside 65535 - angular tolerance
shrinks with distance (radius/z). `ships.in_crosshairs()`.

**Damage** (L11635-11720): target energy -= laser power; <0 => killed (bit 7), EXNO2 kill points,
random plates/canisters (and 0-3 splinters for an asteroid killed by a mining laser). Any hit (kill
or not) calls ANGRY (L20359): hostile station if it is the station or an innocent (NEWB bit 5)
ship; ship gets AI on, accel 2, pitch 4, and hostile bit if type >= 11. The station can't be killed.
Constrictor/Cougar/Dodo (types >= 31) only take damage from a military laser, at 1/4 power.
NPCs recharge 1 energy per tactics call (L18621).

**Missiles** (L10853-10892, FRMIS L20299): `T` arms (MSAR=&FF, needs NOMSL>0, no lock yet); the first
ship that passes HITCH while armed is locked (MSTG &5B = slot, ABORT2). `M` fires: launched at
(0,-28,14) ahead, speed 2*DELTA, AI target = MSTG; target made ANGRY. `U` unarms. Our missile runs
TACTICS every iteration (L46084); it **kills the target when within 256 units on every axis**
(K3 hi bytes zero, L18318-18400), unless the target is the station (missile destroyed, TA352) or
already exploding. Each iteration the target has a 1/16 chance to fire its ECM if it has one (TA64 L18437).
Enemy missiles (SFRMIS L48735, "INCOMING MISSILE") home on us and hit when all three hi bytes are 0:
**250 damage** (TA34 L18295).

**ECM** `E` (L10944-10968): needs ECM fitted and no ECM running (ECMA &46 = 0). ECMA = 32
iterations; **every missile (ours too) is destroyed** on its next tactics pass (L18318 -> TA352);
ours drains 1 energy/iteration while on (L12348). A missile destroyed within <256 lo-units of us
does 80 damage if its x_lo, y_lo and z_lo are all 0 (a crude "near us" test, L18269).

**Energy bomb** TAB (L10894, part 5 L11148): kills every ship in the bubble except the station,
Thargoids and types >= 31 (Constrictor, Cougar, Dodo); planets/sun untouched. One use (BOMB &7F ->
0). Each victim calls EXNO2.

**Escape pod** ESCAPE (L10930, ESCAPE L17604): needs ESCP (&1262) and not witchspace. Lose all
cargo, pod used up, **FIST = 0**, fuel = 7.0, then DOENTRY: you are docked at the current system's
station with full shields/energy.

## 5. Damage to us, recharge, death

* `FSH`/`ASH`/`ENERGY` (&D5/&D6/&D7, 0..255). OOPS (L32092): the attacker's z_sign in K% (front
  frame) picks forward (z >= 0) or aft shield; damage beyond the shield comes off ENERGY;
  ENERGY <= 0 => DEATH; otherwise OUCH may destroy cargo/equipment.
* Damage sources: enemy laser = blueprint byte #19 / 2 (table "dmg"), only on that ship's tactics
  pass (every 8 iterations, L46084), when all its hi bytes < 32 (|coord| < 8192) and its nose is
  within ~13 deg of us (dot product <= -35/36, L18993-19060); missile 250; collision with a ship
  `128 + its energy/2` (MA58 L11551); station bump 5 (MA67 L11539).
* Recharge (part 13, L11878): every 8 iterations (MCNT % 8 == 0): if ENERGY >= 128, each shield
  not full +1 costing 1 energy (SHD L31816); then ENERGY += 1 + ENGY (energy unit => +2), capped 255.
  Lasers and ECM also drain ENERGY (DENGY L31843 never goes below 1).
* "ENERGY LOW" message when ENERGY < 50 (L12082).
* **DEATH** (L37850): explosion sound, RES2, dashboard hidden, QQ11 = 0, "GAME OVER", 5 canisters/
  plates spawned around a dead view, MCNT = 255, DELTA = 0, LASCT = 64 counted down once per M% pass
  (D2 L37992) - so for ~64 flight-loop passes (no keys read) FRIN holds only types 4/5 while QQ12 = 0. Then DEATH2
  (L38101): RES2 -> BR1 (L38131) title screen (QQ11 = 13, rotating Cobra) "Load New Commander (Y/N)?";
  N -> DFAULT restores the last saved/loaded commander -> second title (Cougar) "Press Space Or
  Fire" -> SPACE -> docked (QQ12 = &FF) at that commander's system. Death = everything since the
  last save is lost.

## 6. Altitude, sun heat, fuel scooping

* **Planet** (part 15, L12080, on MCNT%32 == 10): only if planet x/y/z sign bytes are all 0.
  `A = (x_hi^2 + y_hi^2 + z_hi^2) >> 8` (MAS3 L15813); if A overflows we're safe; **if A < 37
  we crash (DEATH)**; else `ALTIT` (&1221) = sqrt((A-37)*256) ~ height in 256-unit steps. Planet
  radius = 24576 (hi 96). So crash when the centre is closer than ~97*256 = 24800 units. ALTIT = 255
  when far.
* **Sun** (MCNT%32 == 20, L12182): CABTMP (&0E74) = 30 normally; if no station and sun sign bytes
  all 0: `CABTMP = 30 + 255 - A` (A as above for the sun). **Death if that exceeds 255, i.e. A < 30**
  (sun centre within ~87*256 units). Fuel scooping when CABTMP >= 224 (A <= 61, within ~125*256)
  and scoops fitted (BST &125D): QQ14 += DELTA/8 per 32 iterations (L12245), max 70. Faster =
  more fuel but less margin. Message "FUEL SCOOPS ON".
* Nothing else near the sun/planet damages you; NPCs never crash into either.

## 7. Docking

**The station** is always slot 1, type 2 (`FRIN[1] == 2`, SSPR != 0). It is spawned (part 14,
L11909-12045, every 32 iterations) once we are within the cube `|x|,|y|,|z| < 192*256` of the point
`planet + 2*planet_nosev` (one planet radius above the surface): NWSPS (L32228) copies the planet's
orientation and **negates nosev**, so the station's nosev (which points out of the slot - DOCKIT
comments; slot vertices are on the +z face, data L605) **points at the planet: the slot faces the
planet**. Tech >= 10 gives a Dodo (still type 2). Station roll counter = &FF: it spins about its
nose at 1/16 rad (3.6 deg) per iteration, forever (L32250). The safe zone ends when any hi byte of
the station exceeds 224 (FAROF): the station is replaced by the sun (KS4 L35624) and SSPR = 0.

**Docking test** (ISDK L11445, part 9): runs when the station's x/y/z hi bytes are 0 and all
lo bytes < 128 - i.e. its **centre is within 128 units on every axis** (we are inside the hull). Then:
1. Station not hostile (`K%+37+36` bit 2 clear). Hostility comes only from ANGRY (shooting the
   station or an innocent, or missiling one) and lasts until the station is respawned.
2. `nosev_z_hi >= 214`: station nosev z <= -86/96 (slot within ~26 deg of facing us head-on).
3. (omitted in this version)
4. `XX15+2 >= 89` where XX15 = unit vector (length 96, sign-magnitude) **from us to the planet**
   (SPS1 L40157). Because the sign bit is not masked, a planet *behind* us (z < 0) always passes; a
   planet ahead must be within 22 deg. Coming in through a planet-facing slot the planet is behind
   you, so this always passes in practice.
5. `|roofv_x_hi| >= 80`: the station's roof vector is within ~34 deg of our x axis, i.e. **the slot
   (its long axis is along roofv) looks horizontal on screen**.

All pass => DOENTRY (L10354): docked, shields/energy full, GNTMP 0. **No speed limit on success.**
Any fail => if DELTA < 5: bump: 5 damage, DELTA = 1 (repeats every iteration you stay inside);
else **DEATH** (MA62 L11484).

**Docking computer** (`C`, needs DKCMP &1260; `auto` &0E71; `P` cancels): DOKEY runs DOCKIT on a dummy
copy of us every iteration (L40678-40820): far from the station it heads for the planet; in the zone
it aims for a point straight out of the slot on the station nose axis (DCS1 L19926), then when pointed at the station
and roughly on the axis matches the slot roll by setting JSTX = 64 (ALPHA = -16 = exactly the
station's 3.6 deg/iter; by my reading of the vector signs the station appears to spin clockwise when seen through its slot and `>` cancels it - verify on the machine by watching the station roofv angle), accelerates,
and slows to speed 1 whenever it is misaligned. Speed capped at 22. It presses the same keys we
could (KY1/KY2/KY3/KY4/KY5/KY6). It is the standard algorithm and docks reliably, but it is slow
(it repeatedly brakes to 1).

**Manual algorithm in RAM terms** (station block S = slot 1, front frame):
1. Get into the safe zone (SSPR != 0); keep the planet ahead until the station spawns.
2. Approach point P = S.pos + k * S.nosev with k ~ 4000-8000 units (in front of the slot, on the
   planet side of the station). Steer with the aim recipe at P, any speed.
3. At P, turn to face the station: aim at S.pos until x,y ~ 0 (z > 0). Check S.nosev_z < -0.9 (we
   are on the slot axis); if not, go back to 2 with better alignment.
4. Slot angle: phi = atan2(S.roofv_y, S.roofv_x) (screen angle of the station roof). Need
   |roofv_x| >= 80/96 at the moment of entry, i.e. phi within ~34 deg of 0 or 180. The station
   turns 3.6 deg/iteration; hold JSTX ~ 64-66 (ALPHA ~ -15/-16) to freeze it, or simply time
   the run: it passes horizontal every ~50 iterations and stays good for ~18 of them.
5. Keep x,y of the station centre near 0 (the slot is 20 wide x 60 tall in station units; the
   test only looks at angles once the centre is within 128 per axis), keep S.nosev_z <= -0.9,
   and fly in. Entry from z ~ 300 to z < 128 at speed 20-40 takes a few iterations; predict
   phi forward with the station spin. Any speed works if aligned; < 5 only makes failure survivable.

## 8. In-system jump `J`

WARP (L40430), executed **every iteration J is held** (KY18, L10950):
* Refused (long beep) if `FRIN[2 + JUNK] != 0` - i.e. **any non-junk ship anywhere in the bubble**,
  regardless of distance (junk = escape pods, plates, canisters, boulders, asteroids, splinters,
  shuttles, transporters, hermits) - or SSPR != 0 (station present), or in witchspace.
* Refused if the planet is ahead (z_sign >= 0) and every planet coordinate is < 2*65536
  (`(x_sign|y_sign|z_sign)&&7F < 2`, MAS2 L15764); same for the sun.
* Otherwise planet and sun z_sign -= 1: everything else (junk included) is dragged with us; we jump
  65536 units forward. **No fuel cost.** It sets MCNT = 1 and EV = 0, so the next iteration runs
  the spawning code (a new encounter roll per jump).

## 9. Spawning, police and legal status

Spawning (main loop parts 1-4, L36576-37210) runs **only when MCNT wraps to 0 (every 256
iterations)** and never in witchspace:
1. 35/256: if JUNK < 3: 50% a trader (Cobra III/Python/Boa/Anaconda, half of them docking), else
   (not in the safe zone) an asteroid/boulder (2% canister, 1.2% rock hermit).
2. Outside the safe zone only: **cop roll**: T = 2*BAD (BAD = 2*(slaves+narcotics)+firearms tonnes,
   L37654), ORed with FIST **only if cops are already present**; spawn a Viper if random < T. So a
   clean hold => no cops from here. Random == 136 (1/256) => Thargoid+Thargon (or very rarely a Cougar).
   If any cop is present, stop.
3. `EV` (&0E7B): DEC; if still >= 0 stop. Mission 2 Thargoids (TP bits 2-3 = %10, 14%).
   Then gov (&1296) 0 (anarchy) always continues; otherwise 35% x P(rand0-7 >= gov).
4. 39%: one bounty hunter/pirate, **hostile**: Cobra III (pirate), Asp II, Python (pirate) or
   Fer-de-lance (Moray never, L37030-37064), EV += 1. 61%: a pack of 1-4 from Sidewinder..Cobra III
   pirate (small types likelier), EV = pack size-1. Spawn position (Ze L36489): hi bytes 25 on each
   axis (6400 units), in front (z > 0), random x/y signs, AI on, aggression >= 32, 4% ECM.
Also: hostile station spawns Vipers (1/16 per its tactics call, max 6, ECM, aggression 56; TN5
L18562); a friendly station launches a Shuttle/Transporter rarely; rock hermits launch hostile
pirates (22%); Anacondas launch Worms/Sidewinders (22%).

**Police and FIST** (&1268, 0 clean, 1-49 offender, 50+ fugitive):
* FIST |= BAD when launching (TT110 L28875). FIST |= 64 when a ship with NEWB bit 6 (Viper,
  Transporter) dies by any means (L11774). Nothing else raises it (attacking cops makes them hostile
  via ANGRY but does not touch FIST).
* Bounty hunters (NEWB bit 1: Vipers, Fer-de-lance; 20% of trader tactics passes also check) turn
  hostile when **FIST >= 40** (TN1 L18702).
* Pirates inside the safe zone have aggression zeroed (TN3 L18740).
* FIST decays: halved on every hyperspace arrival (SOLAR L31454); 0 on galactic hyperspace (L27796)
  and on using the escape pod.

## 10. Kill credit and bounties

`EXNO2` (L48788): TALLYL/TALLY += KWL%/KWH% of the **type in X**; "RIGHT ON COMMANDER!" on each
256-point carry. Called by:
* our laser kill (L11709, correct type);
* energy bomb, each victim (L11196, correct type);
* **our missile reaching its target** (TA87 L18402): X = the target's *slot number*, not its type -
  a bug Moxon notes: missile kills score KWL%[slot] (slot 2 = station = 0 points, slot 3 = escape
  pod, slot 11 = Cobra III ...);
* any missile (ours or an enemy's) destroyed by ECM or by hitting the station: alloy-plate points
  (0.04) (TA352 L18269).
Not credited: collisions (the other ship dies but no EXNO2), enemy missiles hitting us, ships leaving,
Thargons orphaned by a dead Thargoid (they just lose ECM and halve speed, TA21 L18674 - not killed),
planet/sun (no such kills exist). Killing the mission Constrictor adds 256 points (TALLY+1, KILLSHP).

**Bounty** (L11750-11800): when a killed ship finishes exploding (bits 5 and 7), *whatever killed
it* (laser, missile, bomb, collision), blueprint bytes #10-11 (tenths of Cr) are added to CASH
(&123D, 4 bytes big-endian, tenths) via MCASH and shown - **but only if DLY = 0 and not in
witchspace**: if an in-flight message is on screen (DLY &0E7C != 0) the bounty is silently lost.
Scooped/docked ships (NEWB bit 7) pay nothing. The explosion step needs LL9, i.e. a space view.

## 11. Witchspace

* Trigger (TT18 L28771): each hyperspace, fuel is deducted first, then **3/256** random chance, or
  forced by holding CTRL at the end of the countdown if PATG (&2C58, toggled with `X` while paused)
  is on. MJP (L28712): QQ11 = 3, RES2, `MJ` (&0E73) = &FF, spawn Thargoid+Thargon pairs (GTHG L28657,
  aggression 63, ECM) until there are **3 Thargoids (and 3 Thargons)**, dust 3, QQ1 ^= 31.
* In witchspace: no planet/sun/station, no spawning, no escape pod, no in-system jump, no bounty
  cash (kill points still count). Thargoids "fire missiles" by launching Thargons (up to 6 each, TA16 L18955).
  Thargoids are immune to the energy bomb.
* Leave by hyperspacing (H) to a system in range of the remaining fuel; the countdown has no mass
  lock. Without fuel you are stuck.

## 12. Useful RAM summary

| name | addr | use |
|---|---|---|
| K% | &0400 | slot blocks, 37 bytes each |
| FRIN / MANY / SSPR / JUNK | &0E41 / &0E4E / &0E50 / &0E70 | slots, type counts, safe zone |
| auto / ECMP / MJ / CABTMP | &0E71..&0E74 | dock computer, our ECM, witchspace, cabin temp |
| VIEW / LASCT / GNTMP / EV / DLY | &0E77 / &0E78 / &0E79 / &0E7B / &0E7C | |
| JSTX / JSTY / ALPHA / BETA / DELTA | &C1 / &C2 / &86 / &42 / &75 | controls |
| FSH / ASH / ENERGY | &D5 / &D6 / &D7 | shields/energy |
| MSTG / ECMA / MCNT / QQ11 / QQ12 | &5B / &46 / &84 / &81 / &87 | lock, ECM timer, loop counter, screen, docked |
| ALTIT / QQ14 / FIST / TALLY(L) / NOMSL | &1221 / &1241 / &1268 / &127B(&1266) / &1267 | |
| DAMP / DJD / PATG | &2C56 / &2C57 / &2C58 | config toggles |

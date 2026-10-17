# BBC Master Elite: economy, galaxy and docked-screen UI (agent reference)

Source: `vendor-src/1-source-files/main-sources/elite-source.asm` (line numbers
below are `L<n>` in that file). Addresses come from `elite/symbols.json`. The
Python port of every formula is in `elite/galaxy.py` (`python -m elite.galaxy`).

## 1. RAM map (all hex)

| Symbol | Addr | Meaning |
|---|---|---|
| QQ11 | 0081 | current view (table in §9) |
| QQ12 | 0087 | docked flag: FF = docked, 00 = in space |
| KL | 00C3 | ASCII of the key pressed in the last scan (00 = none) |
| QQ22 / QQ22+1 | 0044 / 0045 | hyperspace countdown, internal ticks / on-screen number |
| QQ9, QQ10 | 00E1, 00E2 | chart crosshair x, y (galactic coordinates) |
| QQ15 | 005E-0063 | seeds of the *selected* system (s0lo,s0hi,s1lo,s1hi,s2lo,s2hi) |
| QQ8 | 00DF-00E0 | distance to selected system in 0.1 LY (16-bit, LE) |
| QQ3..QQ7 | 00D8.. | selected system economy, gov, tech, population, productivity (16-bit) |
| QQ19 | 006D | scratch (QQ19+1 = item byte #1 during trades) |
| R | 007A | the number gnum has built so far |
| QQ0, QQ1 | 1235, 1236 | current system x, y |
| QQ21 | 1237-123C | galaxy seeds (system 0 of current galaxy) |
| CASH | 123D-1240 | cash, 4 bytes **big-endian**, in **tenths of a credit** |
| QQ14 | 1241 | fuel, 0.1 LY units (70 = full) |
| GCNT | 1243 | galaxy number 0-7 (shown +1) |
| LASER | 1244-1247 | laser power front/rear/left/right (0, 0F pulse, 8F beam, 97 military, 32 mining) |
| CRGO | 124A | hold capacity + 2 (22 standard, 37 large bay) |
| QQ20 | 124B-125B | cargo held, items 0-16 |
| ECM, BST, BOMB, ENGY, DKCMP, GHYP, ESCP | 125C-1262 | equipment flags (see §5) |
| TRIBBLE | 1264-1265 | trumble count |
| NOMSL | 1267 | missiles 0-4 |
| FIST | 1268 | legal status (0 clean, 1-49 offender, 50+ fugitive) |
| AVL | 1269-1279 | market availability items 0-16 (current system) |
| QQ26 | 127A | market random byte for this visit |
| QQ24 | 1292 | price/4 of the item last passed to TT151 |
| QQ25 | 1293 | the max gnum will accept (availability / held / top item) |
| QQ28 | 1294 | current system economy |
| QQ29 | 1295 | market item number being processed/prompted |
| gov, tek | 1296, 1297 | current system government, tech level (0-14) |
| QQ2 | 129A-129F | current system seeds |
| safehouse | 12A0-12A5 | destination seeds copied when the countdown starts |
| MJ | 0E73 | non-zero = in witchspace (no market) |
| PATG | 2C58 | FF = author display on (enables forced mis-jump) |

## 2. Galaxy and system data (port: `galaxy.py`)

* **Twist** TT54 (L24761): `s0'=s1, s1'=s2, s2'=s0+s1+s2` (16-bit, exact 6502
  carries). TT20 (L24714) = 4 twists = next system. Galaxy 0 seeds QQ21 =
  `&5A4A, &0248, &B753` (L13880). Systems are numbered 0-255 in twist order;
  256 TT20s do *not* wrap back, so after drawing a chart QQ15 is junk until
  TT111 runs again.
* **Galactic hyperdrive** Ghy (L27781): every QQ21 byte rotated left one bit
  (`ASL A / ROL QQ21,X`), `GCNT = (GCNT+1) AND &F7`, GHYP=0, FIST=0,
  countdown from 2, arrive at the system nearest (96,96) (L27839-27845).
* **TT24** (L25303): `x = s1_hi`, `y = s0_hi`;
  economy QQ3 = `s0_hi AND 7`, `OR 2` if government is 0 or 1;
  government QQ4 = `(s1_lo >> 3) AND 7`;
  tech QQ5 = `(QQ3 EOR 7) + (s1_hi AND 3) + (QQ4 >> 1) + (QQ4 AND 1)` (the
  `LSR` carry is added); population QQ6 = `QQ5*4 + QQ3 + QQ4 + 1` (0.1 bn);
  productivity QQ7 = `((QQ3 EOR 7)+3) * (QQ4+4) * QQ6 * 8` (16-bit, M CR);
  radius (TT25 L25246) = `((s2_hi AND 15) + 11)*256 + s1_hi` km.
* **Name** cpl (L29968): 4 pairs if bit 6 of s0_lo set, else 3. For each:
  `n = s2_hi AND 31`; if n != 0 print QQ16 pair n; then one TT54 twist.
  QQ16 (L13673) = AL LE XE GE ZA CE BI SO US ES AR MA IN DI RE A? ER AT EN BE
  RA LA VE TI ED OR QU AN TE IS RI ON (index 0-31; "A?" prints "A", L30754).
* **Distance** readdistnce (L27396): `dx = |x1-x0|`, `dy = |y1-y0| >> 1`,
  `QQ8 = 4 * floor(sqrt(dx^2 + dy^2))` (LL5 L42136 is an exact floor sqrt; on
  16-bit overflow only the high byte saturates to FF). So distances are
  multiples of 0.4 LY.
* **Nearest system** TT111 (L27258): the first system (galaxy order) with
  the strictly smallest `(|x-QQ9|>>1) + (|y-QQ10|>>1)`, starting threshold
  127. It copies its seeds to QQ15, **snaps QQ9/QQ10 to its x,y**, computes
  QQ8 from (QQ0,QQ1), and runs TT24 (QQ3-QQ7).
* Lave = galaxy 0 system 7 at (20,173): Rich Agricultural, Dictatorship,
  TL5, 2.5 bn, 7000 M CR, 4116 km. Within 7 LY: Leesti 3.6, Diso 3.6,
  Reorte 4.4, Zaonce 5.6, Riedquat 6.8, Orerve 6.8.

## 3. Market (TT151 L28094, var L28424, GVL L28563, QQ23 L41558)

QQ23 entries are `ITEM base_price, factor, unit, base_qty, mask`:

| # | Item | price | factor | unit | qty | mask | legal |
|---|---|---|---|---|---|---|---|
| 0 | Food | 19 | -2 | t | 6 | 01 | |
| 1 | Textiles | 20 | -1 | t | 10 | 03 | |
| 2 | Radioactives | 65 | -3 | t | 2 | 07 | |
| 3 | Slaves | 40 | -5 | t | 226 | 1F | illegal x2 |
| 4 | Liquor/Wines | 83 | -5 | t | 251 | 0F | |
| 5 | Luxuries | 196 | 8 | t | 54 | 03 | |
| 6 | Narcotics | 235 | 29 | t | 8 | 78 | illegal x2 |
| 7 | Computers | 154 | 14 | t | 56 | 03 | |
| 8 | Machinery | 117 | 6 | t | 40 | 07 | |
| 9 | Alloys | 78 | 1 | t | 17 | 1F | |
| 10 | Firearms | 124 | 13 | t | 29 | 07 | illegal x1 |
| 11 | Furs | 176 | -9 | t | 220 | 3F | |
| 12 | Minerals | 32 | -1 | t | 53 | 03 | |
| 13 | Gold | 97 | -1 | kg | 66 | 07 | |
| 14 | Platinum | 171 | -2 | kg | 55 | 1F | |
| 15 | Gem-Stones | 45 | -1 | g | 250 | 0F | |
| 16 | Alien Items | 53 | 15 | t | 192 | 07 | |

`v = economy(QQ28) * |factor|` (8-bit).
* **Price** (8-bit): `QQ24 = base + (QQ26 AND mask) + v` (factor >= 0), or
  `- v` (factor < 0). The screen shows `QQ24*4` tenths of a credit, i.e.
  **price in Cr = QQ24 * 0.4**. Trade value: GCASH (L29088)
  `qty * QQ24 * 4` tenths, **truncated to 16 bits** (wraps above 6553.5 Cr
  per transaction; avoid selling more than ~160 units of a costly item at once).
* **Availability** (GVL): `A = base_qty + (QQ26 AND mask) - v` (factor >= 0)
  or `+ v` (factor < 0), mod 256; if bit 7 set then 0; `AVL = A AND 63`.
  Only items 0-15 are computed; alien items are always 0 (set by var).
* **When**: GVL (and a new `QQ26 = DORND`) runs only on hyperspace arrival
  (TT18 -> hyp1+3 -> fall-through, L28831/L28563). Docking/launching does
  not change the market; buying lowers AVL in RAM. AVL and QQ26 are saved
  in the commander file. The default commander starts at Lave with QQ26=0
  and a *stored* AVL (L13953: 16 15 17 0 3 28 14 0 0 10 0 17 58 7 9 8 0)
  that does not match GVL, so the starting market quantities differ from
  the formula until the first jump.
* **Hold space** tnpr (L24517): items 0-12 fit if
  `1 + sum(QQ20[0..12]) + qty + TRIBBLE_hi < CRGO` (20 t or 35 t); items
  13-16 each fit if `QQ20[i] + qty < 200`.
* **Legality** BAD (L37654): on launch `FIST |= (slaves+narcotics)*2 + firearms`.

## 4. Keys and how they are read

Internal key numbers we press (verified in TRTB% L47836, row<<4|col):

| Key | No. | Key | No. | Key | No. |
|---|---|---|---|---|---|
| f0 | 20 | f5 | 74 | RETURN | 49 |
| f1 | 71 | f6 | 75 | SPACE | 62 |
| f2 | 72 | f7 | 16 | DELETE | 59 |
| f3 | 73 | f8 | 76 | ESCAPE | 70 |
| f4 | 14 | f9 | 77 | SHIFT / CTRL | 00 / 01 |
| cursor up | 39 | down | 29 | left / right | 19 / 79 |
| 0 | 27 | 1 | 30 | 2 | 31 |
| 3 | 11 | 4 | 12 | 5 | 13 |
| 6 | 34 | 7 | 24 | 8 | 15 |
| 9 | 26 | Y | 44 | N | 55 |
| H | 54 | D | 32 | F | 43 |
| O | 36 | @ | 47 | X | 42 |

All letters are returned in upper case.

**Scanning.** RDKEY (L48288) runs FILLKL, which scans internal keys &10-&7F
in ascending order. KL ends up as the TRTB% ASCII of the **highest-numbered
key held** (&00 if none), so only one non-modifier key is seen at a time.
SHIFT and CTRL are never in KL. They are read directly with DKS5 (CTRL L48132).
The f-keys come back as ASCII &80-&89, and the cursor keys as &8C left,
&8D right, &8E down, &8F up.

There are two ways the game reads keys:

1. **Main-loop keys** (f-keys, H, D, F, O, @, cursor keys). Every main-loop
   pass calls TT17 -> DOKEY -> RDKEY once (L37285), then calls TT102 with
   the key (L37323). The key is sampled, not edge-triggered: if you hold it,
   it fires again on every pass. When docked, one pass is DIALS plus a
   2-vsync DELAY (L37281), plus a further 1-vsync WSCAN on charts (TT16
   L26599), so roughly 40-60 ms. Hold a key for about 4 frames (80 ms) and
   then release it. A repeat redraws f4-f9 screens harmlessly. f1/f2/f3
   enter TT217 prompts, which wait for release, so a repeat cannot happen
   there.
2. **Prompt keys** TT217 (L41155), used by gnum, the view prompt qv, and
   line input MT26. On entry it loops `DELAY 2 vsyncs; RDKEY` **until no key
   is held**, then polls RDKEY tightly and **returns on the first press**,
   without waiting for release. It never auto-repeats, because each
   character needs a full release and then a new press. Recipe: press for
   3-4 frames, release for at least 4 frames (more than 2 vsyncs, so the
   release is seen), then press the next key.
   * A key that goes down while the previous one is still held is lost.
     TT217 waits until *all* keys are up.
   * A key pressed and released while the game is busy is also lost: for
     example during the dn/dn2 beep (BEEP plus `DELAY 25` = 0.5 s, L29570),
     a screen redraw, or the 1-second-ish delays after errors. Wait until
     the prompt is live before pressing. To detect that exactly, wait until
     the 6502 is in TT217's `t2` loop: PC is in &6E6A-&6E6E, or inside
     RDKEY/FILLKL (&7F19-&7F9x) with return address &6E6C on the stack.
     Without access to PC, wait at least 35 frames after any keystroke that
     completes a transaction or causes an error.

## 5. Docked screens, key by key

f-key dispatch is TT102 (L37373). Keys that work in both states are f0,
f4-f9, H, D, O and F. **Docked only:** f1 = Buy, f2 = Sell, f3 = Equip,
@ = disc menu. In space, f0-f3 select the front/rear/left/right views
(INSP L37447). On docking, BAY (L38250) sets QQ12=FF and forces f8.

### gnum, the number entry routine (L26066)

gnum is used by Buy, Sell and Equip. QQ25 holds the maximum allowed value.
R (&7A) accumulates the number. Each key goes through TT217:

* **Y** (only as the first key, while R=0) prints "Y" and returns R = QQ25
  (the maximum). **N** (first key) returns 0. A leading 0 keeps R=0, so Y
  and N are still accepted after it.
* **A digit** does `R = R*10 + d` and echoes the digit. gnum then waits for
  more keys (up to 12). If R was already >= 26, or the result exceeds 255
  or QQ25, it returns with **C set**, meaning "too big".
* **Any key with ASCII < '0'** (RETURN, SPACE) ends input and returns R
  (C clear). **Always finish a typed number with RETURN.** RETURN on its
  own returns 0.
* **Any key with ASCII > '9'** (letters other than a leading Y/N, f-keys,
  DELETE, cursor keys) jumps to **BAY2**. That abandons the screen and
  forces f9 (Inventory, L26012).

### Buy Cargo, f1: TT219 (L25839), QQ11=2

1. The screen shows column headers, then sets QQ29=0.
2. For each item in turn (QQ29 = 0..16), TT151 prints the row and sets
   QQ24 = price/4 and **QQ25 = AVL[QQ29]**. If AVL = 0 the item is skipped
   with no prompt. Otherwise the bottom line shows
   `Quantity of <Item>/<unit>?` and gnum runs. **QQ29 tells you which item
   is being asked.** (R=0 is set at the start of gnum.)
3. Outcomes:
   * Too big (C set): "Quantity?" plus a dn2 beep (0.5 s), then the same
     item is re-asked.
   * 0 (RETURN or N): move to the next item.
   * Not enough hold space: "Cargo?" plus a beep, and the item is re-asked.
     This only happens when R != 0.
   * Not enough cash: LCASH fails and restores CASH, "Cash?" plus a beep,
     and the item is re-asked.
   * Success: QQ20[i] += R, AVL[i] -= R, CASH -= R*QQ24*4, then **dn**
     prints the cash and beeps for 0.5 s. Then the next item.
4. Pressing **Y** buys all of the item's availability, but only if hold
   and cash allow it. Otherwise the error re-asks the item, so you must
   type a number.
5. After item 16, or on any key > '9', BAY2 forces f9 (Inventory, QQ11=8)
   and the main loop resumes. To leave early, press any letter, for
   example f9.
6. Verify the result in RAM with QQ20, AVL and CASH.

### Sell Cargo, f2: TT208 (L26236) / TT210 (L26288), QQ11=4

* The screen loops QQ29 = 0..16 over items with **QQ20[i] > 0** only. Each
  row is printed, **QQ25 = QQ20[i]** (the amount held), and the prompt
  `Sell(Y/N)?` appears on the same row. gnum runs.
* Y sells all of the item, N or RETURN keeps it, and digits followed by
  RETURN sell part. Too big gives "Quantity?" plus a beep, and the same
  item is re-asked via NWDAV4.
* On a sale: QQ20[i] -= R, and CASH += R*QQ24*4, where QQ24 is from TT151
  with printing suppressed (QQ17=FF). There is **no per-item beep or
  delay**, so the next prompt follows immediately (the row redraw takes
  a few ms).
* After the last item, dn2 beeps for 0.5 s and then BAY2 shows the
  Inventory (f9). A key > '9' exits at once in the same way.
* Illegal goods sell normally. Only launching matters for legality.

### Equip Ship, f3: EQSHP (L29146), QQ11=32

* Items listed: `n = tek+3`, or 14 when `tek+3 >= 12` (so tek >= 9 offers
  everything). Items 0..n-1 are printed numbered **1..n**, and QQ25 = n.
  Prompt: `Item?`.
* Prices are PRXS (L29909), in tenths of a credit. **Type the number shown
  (item+1) followed by RETURN.** Do not press Y first: that selects item
  n-1.

| item (type) | name | price Cr | effect in RAM |
|---|---|---|---|
| 0 ("1") | Fuel | 0.2 per 0.1 LY: PRXS = 2*(70-QQ14) | QQ14 = 70 |
| 1 ("2") | Missile | 30 | NOMSL+1. At 4 gives "All Present" and a refund |
| 2 ("3") | Large Cargo Bay | 400 | CRGO = 37. Present if already 37 |
| 3 ("4") | E.C.M. | 600 | ECM = FF |
| 4 ("5") | Pulse laser | 400 | view prompt, LASER[v] = 0F |
| 5 ("6") | Beam laser | 1000 | view prompt, LASER[v] = 8F |
| 6 ("7") | Fuel Scoops | 525 | BST = FF |
| 7 ("8") | Escape Pod | 1000 | ESCP = FF |
| 8 ("9") | Energy Bomb | 900 | BOMB = 7F |
| 9 ("10") | Energy Unit | 1500 | ENGY = 1 |
| 10 ("11") | Docking Computer | 1000 | DKCMP = FF |
| 11 ("12") | Galactic Hyperdrive | 5000 | GHYP = FF |
| 12 ("13") | Military laser | 6000 | view prompt, LASER[v] = 97 |
| 13 ("14") | Mining laser | 800 | view prompt, LASER[v] = 32 |

* Flow after the number:
  * RETURN alone, N, or a value that is too big goes to **BAY** (the
    Status screen, f8).
  * Otherwise **eq** deducts the price. If cash is short, "Cash?", a
    0.5 s beep and BAY.
  * Then the item is applied. "<Item> Present" gives a refund, a beep and
    BAY.
  * On success, dn shows the cash and beeps for 0.5 s, then **EQSHP is
    redrawn** (L29545) so you can buy more. Leave with RETURN, which shows
    the Status screen.
* **Laser view prompt** qv (L29697): if tek >= 8 the screen is cleared
  first. A menu `0 Front / 1 Rear / 2 Left / 3 Right` is shown with
  `View ?`. A single TT217 key '0'-'3' is taken with no RETURN; any other
  key re-prompts. If a laser is already in that view, its full PRXS price
  is refunded (refund L29838), and the new one is installed even if it is
  the same type.
* Fuel at a full tank costs 0 and still "succeeds".

### Charts: f4 Long-range (TT22 L25404, QQ11=64), f5 Short-range (TT23 L26871, QQ11=128)

* **Crosshair** = (QQ9, QQ10), in galactic coordinates. It persists
  between screens.
* **Moving it** (TT102 HME1 -> TT16 L26589, using TJ1 L35383): this only
  works on a chart, and only when QQ22+1 = 0 (no countdown running). On
  each main-loop pass, the key in KL moves the crosshair:

  | key | change |
  |---|---|
  | left | QQ9 -1 |
  | right | QQ9 +1 |
  | up | QQ10 -1 (up the screen) |
  | down | QQ10 +1 |

  * With **SHIFT** held the step is **4 units** per pass. Only one
    direction can move per pass (KL priority is the highest key number:
    right > up > down > left).
  * Values clamp at 0/255 (TT123 L26726 refuses to wrap).
  * Moving does **not** change the selection (QQ15/QQ8). Use a closed loop:
    read QQ9/QQ10, release when on target, then press **D**.
* **Selecting a system**: **D** (T95 L37598) calls hm, which runs TT111 on
  the crosshair. That snaps the crosshair to the nearest system and sets
  QQ15, QQ3-QQ7 and QQ8. It then prints the name and distance. Use
  `galaxy.nearest_system(g, QQ9, QQ10)` to predict which system will be
  picked. Confirm by reading QQ15 (compare with `system_data`) and QQ8.
* **O**: resets the crosshair to the current system (ping: QQ9,QQ10 =
  QQ0,QQ1). It does not reselect, so press D afterwards.
* **F** (find, HME2 L17704) works **docked and on a chart only** (L37508).
  1. The bottom line shows `Planet name?`.
  2. Type 1-9 letters with TT217. Only characters '!' to 'z' are accepted
     and the rest beep. DELETE erases a letter and ESC aborts.
  3. Press RETURN. The galaxy is searched in system order for an exact
     (case-insensitive) name.
  * Hit: QQ9,QQ10 = its coordinates, TT111 runs, and then T95 prints the
    distance. The selection is set, so no D is needed.
  * Miss: a long low beep, "Unknown planet", and the selection reverts to
    the crosshair.
  * Each letter follows the TT217 timing rules. MT26 waits 8 vsyncs and
    flushes before the first key (MT26 L38827).
* **f6 Data on System** (TT25 L24989, QQ11=1): runs TT111 on the crosshair
  first, so the selection changes. Shows distance (if not 0), economy,
  government, tech level (QQ5+1), population, species, productivity,
  radius and a description.

### Hyperspace: H (hyp L27577)

Conditions, checked in order:

1. Not docked. If QQ12 != 0 it prints "Docked" and refuses: **you cannot
   start a countdown while docked**.
2. No countdown already running (QQ22+1 = 0).
3. CTRL held means galactic hyperdrive (below).
4. The view must be space (QQ11=0) or a chart. In space it runs TT111 on
   the crosshair. On a chart it runs hm, which also calls TT111. Any other
   view does nothing.
5. QQ8 != 0. It cannot be the current system.
6. QQ8+1 = 0 and QQ14 >= QQ8. Otherwise "Hyperspace Range?". With fuel
   at 70 the maximum range is 7.0 LY.

If all pass:

* QQ15 is copied to safehouse, "HYPERSPACE -<name>" is printed, and wW
  sets **QQ22 = QQ22+1 = 15** (shown top-left).
* TT107 (L37548) ticks once per main-loop pass on which TT102 reaches it.
  Each pass does DEC QQ22. At 0, QQ22=5 and QQ22+1 is decremented.
  * That means **15 + 14*5 = 85 passes** for a normal jump, and 2+5 = 7
    for galactic.
  * Passes where an f-key, H, D or F is processed skip the tick. Do not
    hold H during the countdown.
  * Changing screens does not cancel it.
* At 0, TT18 (L28771) runs:
  1. QQ14 -= QQ8 (clamped at 0).
  2. If CTRL is held **and** PATG=FF (author display, toggled with X while
     paused), it forces a mis-jump. Otherwise there is a random mis-jump
     when DORND >= 253 (3/256). A mis-jump sets MJ=FF, spawns Thargoids,
     and does QQ1 ^= 31.
  3. Otherwise hyp1+3 sets QQ0,QQ1 = QQ9,QQ10, sets QQ2 = safehouse and
     **runs GVL (new QQ26 and AVL)**, then SOLAR (which halves FIST).
* **Galactic hyperdrive = CTRL+H** in space (internal keys 01 + 54).
  Hold CTRL while H is sampled. It needs GHYP=FF and works from any view
  (it is checked before the view test). The countdown is 2.

### Launch: f0 (TT110 L28875)

f0 when docked runs:

1. LAUN tunnel and RES2.
2. TT111, which selects the system nearest the crosshair.
3. Places the planet and station, DELTA=12.
4. FIST |= BAD.
5. QQ12=0, then the front view (QQ11=0).

In space, f0 just shows the front view.

### Status f8 (STATUS L15908) and Inventory f9 (TT213 L26481), both QQ11=8

* **f8** runs TT111 first, so it changes the selection. It shows:
  * the present and hyperspace system names
  * condition (Docked, Green, Yellow or Red)
  * fuel, cash, legal status and rating
  * the equipment list
* **f9** shows fuel, cash, and then each held cargo item with its amount
  (TT210 with no prompt). It also shows a trumble count, and "Large cargo
  bay" if CRGO >= 26.

### Market Price f7 (TT167 L28353, QQ11=16)

Read-only, and works docked or in space. It shows all 17 rows: item,
price (QQ24*4 tenths) and AVL for the *current* system. In witchspace the
rows are blank because MJ != 0.

## 6. Save (@, docked only, SVE)

@ opens the disc menu. Save returns to BAY (f8); a load restarts via QU5.
The commander file layout is at L13877-13973.

## 7. Practical agent recipes

* **Pick a destination (docked):**
  1. f4 (or f5).
  2. Either F, then the name letters, then RETURN (preferred, exact), or
     move the cursor until `nearest_system(QQ9, QQ10)` is the target and
     then press D.
  3. Verify that QQ15 equals the target seeds and QQ8 is as expected.
* **Buy n of item k:**
  1. f1.
  2. For each prompt, wait until it is live, then read QQ29. If
     QQ29 != k, press RETURN. If QQ29 == k, type the digits of n and then
     RETURN.
  3. After k, any letter (for example f9) exits.
* **Sell everything:** f2, then Y at each prompt. Prompts appear only
  for items held, and there is no delay between them.
* **Refuel:** f3, then "1" and RETURN. Then RETURN again to leave
  (Status screen).
* **Jump:**
  1. f0 to launch.
  2. f4 or f5, then H, or H from the front view. Both use the crosshair.
     hyp has no mass-lock or station-proximity check.
  3. Wait for QQ22+1 to reach 0 and QQ0/QQ1 to change.

## 8. Quirks worth knowing

* TT111 snaps the crosshair. Status (f8), f6, D, H and launch all reselect
  using the crosshair, so keep the crosshair on the target.
* The jump goes to the crosshair coordinates (QQ9,QQ10 -> QQ0,QQ1), with
  the seeds from safehouse.
* Market prices use the current system's QQ28 and QQ26. Charts and f6 do
  not show other systems' prices. Predict them with `galaxy.market`, but
  QQ26 is unknown until arrival: it is a random byte, so the price range
  is base + [0..mask].
* The dn2 comment says "1 second", but the code is DELAY 25, which is
  0.5 s (L29570-29572).
* Cash overflow in GCASH: the product is 16-bit (see §3).

## 9. QQ11 view values

| QQ11 | view |
|---|---|
| 0 | space view |
| 1 | Data on System (f6) |
| 2 | Buy (f1) |
| 3 | mis-jump/witchspace tunnel |
| 4 | Sell (f2) |
| 8 | Inventory (f9) and Status (f8) |
| 16 | Market (f7) |
| 32 | Equip (f3) |
| 64 | Long-range chart (f4) |
| 128 | Short-range chart (f5) |
| FF | transient during launch |

Bits 6/7 mean "chart" (`AND #&C0`). QQ12 is FF when docked and 00 in space.

# Game flow, saves, death and missions (BBC Master Elite, flicker-free SNG47 build)

Source: `vendor-src/1-source-files/main-sources/elite-source.asm` (line numbers below are
`src:N`), `elite-data.asm` (`data:N`), `elite-loader.asm` (`ldr:N`). This build is
`_VARIANT=1` (`_SNG47`, DFS disc), `_REMOVE_CHECKSUMS=FALSE` (`elite-build-options.asm`).
Addresses come from `elite/symbols.json`.

## 0. Keyboard model (read this first)

* **The game never uses the MOS key buffer.** Every read strobes the System VIA directly
  (`FILLKL` src:47975, `DKS5` src:48170). A key counts only if it is **physically down
  at the moment of a scan**. ESCAPE is disabled at MOS level (`OSBYTE 200,1`, ldr:306).
* `FILLKL` scans the internal key numbers &10 to &79 in increasing order. `RDKEY` (src:48288)
  translates the hit through `TRTB%` (src:47836) and stores the result in **KL (&C3)**.
  KL therefore ends up as the translated code of the **highest-numbered key held**.
  SHIFT (&00) and CTRL (&01) are never scanned here. CTRL is read separately by `CTRL`/`DKS5`:
  CTRL+H is the galactic hyperdrive, and CTRL during a jump forces a mis-jump when PATG is set.
* Flight keys go into the key logger KY17..KY13 (&C4..&D3, order per src:978-1111), which `ZEKTRAN` (src:48225)
  clears on every scan. They are **level-triggered**, so a key is "on" for each loop
  iteration in which it is held.

Internal key numbers (row<<4|col), checked against TRTB% src:47836-47930, with the
translated value the code compares against:

| key | int | KL | key | int | KL | key | int | KL |
|---|---|---|---|---|---|---|---|---|
| f0 | &20 | &80 | f1 | &71 | &81 | f2 | &72 | &82 |
| f3 | &73 | &83 | f4 | &14 | &84 | f5 | &74 | &85 |
| f6 | &75 | &86 | f7 | &16 | &87 | f8 | &76 | &88 |
| f9 | &77 | &89 | RETURN | &49 | &0D | ESCAPE | &70 | &1B |
| SPACE | &62 | &20 | DELETE | &59 | &7F | COPY | &69 | &8B |
| CAPS LOCK | &40 | &01 | TAB | &60 | &00 | @ | &47 | '@' |

Character keys (KL = the upper-case ASCII code):
Y &44, N &55, H &54, Q &10, A &41, S &51, X &42, E &22, T &23, U &35, M &65, J &45, C &52,
P &37, F &43, K &46, B &64, D &32, O &36, `,`/< &66, `.`/> &67, `/`/? &68,
0 &27, 1 &30, 2 &31, 3 &11, 4 &12, 5 &13, 6 &34, 7 &24, 8 &15, 9 &26.

Remaining letters (for names): W &21, I &25, R &33, G &53, L &56, Z &61, V &63.

**Consequence: the highest key wins.** One-shot commands (f-keys, H, @, Y/N) are read from KL.
If the agent holds SPACE (&62) or `?` (&68) and taps H (&54), KL becomes SPACE and the H is
lost. Release the higher-numbered flight keys for one iteration when issuing a command.
Keys in row 7 (f1-f3, f5, f6, f8, f9, ESC) beat everything. f0 (&20), f4 (&14) and f7 (&16)
lose to almost everything.

### Main loop and scan rate
* In flight, one iteration runs `TT100` (src:36704): `JSR M%` (the whole flight loop and
  redraw), `DEC MCNT` (src:36723), spawning, then `MLOOP` (src:37240): `DIALS`, then
  `TT17` → `DOKEY` (src:40678), then `FRCE`/`TT102` (src:37321/37373), then back to TT100.
  **The keyboard is scanned exactly once per iteration** (in DOKEY via `RDKEY-1`). M% acts
  on the logger from the previous scan.
  MCNT (&84) drops by 1 per iteration, so use it as the iteration clock. Spawn checks
  run when MCNT wraps to 0, which is every 256 iterations.
* Docked, the loop is `MLOOP` → DELAY 2 vsyncs (src:37281, skipped only if PATG is set and QQ11
  bit 0 is set) → TT17 → TT102. That is at least 2 frames per iteration.
* **Hold a key until at least one iteration has completed** (MCNT changed, or ≥3 frames
  when docked). For flight keys, the rate change applies once per iteration held.
* **One-shot keys have no edge detection in the main loop.** Each iteration re-runs TT102
  on KL:
  * f-keys redraw their screen on every iteration while held, which is harmless but slow.
  * f0 launches once. In space it then selects the front view, and LOOK1 does nothing if
    that view is already showing (src:47517).
  * H is ignored while the countdown QQ22+1 (&45) ≠ 0 (src:27583), so holding it is safe.
  * J calls WARP on every iteration (src:10944). M fires only with a lock (MSTG &5B
    positive), then needs a new T. E re-fires as soon as ECMA (&46) = 0. TAB fires once
    (BOMB bit 7).
* **Prompts are different.** `t`/`TT217` (src:41155), `YESNO` (src:35257), `PAUSE`
  (src:24012), `PAUSE2` (src:24145) and the name entry `MT26` (src:38827) first **wait for
  all keys to be released**, then wait for a press. `t` also waits 2 vsyncs first. Press a
  key, release it, and wait ≥3 frames before the next one. Holding a key never
  auto-repeats.
* `TITLE` (src:38388) is the exception: it does **not** wait for release (see §1).

## 1. Boot → title screens

Boot path: loader → `S%` (src:10207) → `COLD` (BRKV = NEWBRK, src:48876) → `BEGIN`
(src:38036). BEGIN zeroes the config bytes COMC..DISK (&2C40-&2C5E) and runs `JAMESON`
(src:38655), which copies the default commander into the "last saved" block NA%. Then
`TT170` (src:38077) → `RESET` (src:36181; this sets QQ12 = &FF and JSTGY = &FF) →
`DEATH2` (src:38101) → `RES2` → `BR1` (src:38131).

**Title 1** (`BR1`): a rotating Cobra Mk III (TYPE &85 = 11), "---- E L I T E ----",
"LOAD NEW COMMANDER (Y/N)?" (data:5782) and "(C) Acornsoft 1986". The authors' names
appear only when PATG is set.
* `TITLE` loop `TLL2`: each iteration moves the ship 1 closer (z_hi from 96 down to 1)
  and draws it, decrements MCNT, reads the joystick fire button (VIA PB4), then `RDKEY`.
  **It waits forever (no timeout or demo) and returns on any key.**
* `CPX #'Y'` (src:38147): **Y** → `DFAULT`, then `SVE` (the disc menu, §2), then falls into QU5.
  **Any other key** (N, SPACE, …) → `QU5` (src:38162) → `DFAULT` (src:38268) copies NA%
  (the last saved/loaded commander) into the live commander.
* If PB4 reads "fire", TITLE sets JSTK=&FF (joystick mode). Otherwise TITLE resets
  **JSTK (&2C5C) to 0, keyboard** (src:38462, 38487). Check that JSTK is 0 after the
  title.

**Title 2** (same TITLE routine): a Cougar (TYPE 32) and "PRESS SPACE OR FIRE, COMMANDER."
(data:5798). **Any key** continues. Then `ping`/`TT111`/`jmp` set up the current system and
fall into `BAY` (src:38250): QQ12 = &FF, `FRCE` with f8 → the docked Status screen
(QQ11 = 8).

Because TITLE does not wait for release, a key still held when BR1 runs (for example ESCAPE
from a pause-quit) answers title 1 at once. A non-Y key also passes title 2 straight
away, so you land docked with the last saved commander. Use this deliberately, or release
all keys before BR1.

**Telling screens apart from RAM** (QQ11 &81 = view type, QQ12 &87 = docked flag, src:730):

| state | signature |
|---|---|
| title 1 | QQ12=&FF, QQ11=0, distaway(&1229)=200 (TITLE stores Y there) |
| title 2 | QQ12=&FF, QQ11=0, distaway=100 |
| docked screens | QQ12=&FF, QQ11≠0: 8 Status/Inventory, 16 Market, 32 Equip, 2 Buy, 4 Sell, 64/128 charts, 1 Data/text |
| disc menu / prompts / mission text | QQ12=&FF, QQ11=1 (token "clear screen" → MT9 → TT66 with A=1, src:13270) |
| name prompt active | RLINE+2 (&6A09) = 7 (GTNMEW sets 7 and restores 9, src:38779/38793) |
| catalogue printing | CATF (&2C53) = 1 |
| mission 1 briefing (ship) | QQ11=13 |
| in flight | QQ12=0, QQ11=0 front view (VIEW &E77 = 0-3), 255 during launch |
| death animation | QQ12=0, QQ11=0, **MCNT stuck at &FF** (set at D1 src:37889; TT100 is not running), DELTA(&75)=0, LASCT(&E78) counting 64→0 |

QQ11=0 with QQ12=&FF only happens on the titles, plus a few transient instructions
inside DFAULT. The screen bitmap is in shadow RAM, so it cannot be read from the main-RAM window.

## 2. Commander save / load (`SVE`, src:39357)

`@` (int &47) works on **any docked screen**: `TT102` (src:37431, guarded by `BIT QQ12`). SVE
saves SP in `stackpt` (&6764), prints the menu (token 1, data:5629) and calls `t`, which first
waits for `@` to be released:

```
DISK ACCESS MENU
1. Load New Commander    2. Save Commander <name>   3. Catalogue Disk
4. Delete File           5. Default JAMESON         6. Exit
```
Key `1` → `loading`, `2` → `SV1`, `3` → catalogue, `4` → DELT, `5` → "ARE YOU SURE?"
(YESNO; Y → JAMESON+DFAULT). **Any other key (6, SPACE, …) → exit with C clear.**

**Name prompt** (`GTNMEW`, src:38760). It prints "COMMANDER'S NAME? " and runs
`MT26`. This is Elite's own line input (src:38827), **not OSWORD 0**, and it reads only the
hardware matrix:
* It waits 8 vsyncs, then reads one character per `TT217` (release, then press). Characters
  are the TRTB% upper-case codes, from `!` up to `z` (RLINE+3/+4, src:38968).
  **The maximum is 7 characters.** Extra characters and out-of-range keys just beep.
* RETURN (&49) ends input. DELETE (&59) rubs out a character. SHIFT is ignored in this
  variant. Use only A-Z and 0-9, because the name becomes the DFS filename.
* **Empty name (RETURN alone) → `TR1`**, which uses the last saved name from NA%
  (src:38798). **Never press ESCAPE here**: ESCAPE returns with no CR terminator, so the
  filename is left unterminated.

**"WHICH DRIVE?"** (`GTDRV`, src:39624) comes **after** the name. It reads one key via `t`,
and the disc is in drive **`0`** (int &27). Any key that is not 0-3 is "invalid".

**Save (key 2), `SV1` src:39466.** Sequence: GTNMEW → `TRNME` (name → NA%) → `LSR SVC`
(**SVC &127D is halved here**, before the copy) → copy TP..TP+NT% → NA%+8 → `CHECK` → write
CHK and CHK2 → GTDRV → `wfile` (src:39880): `OSCLI "SAVE :0.E.<NAME>  E7E +100 0 0"`, saving
256 bytes from commbuf &0E7E → `DFAULT` → RTS C clear → TT102 `JMP BAY` → Status (QQ11=8).
NA% is updated **before** the disc write. If the drive key is invalid, the save is skipped
but NA% already holds the new data.

**Load (key 1), `loading`.** Sequence: GTNMEW → GTDRV → `LOD` (src:39673) → `rfile`
`OSCLI "LOAD :0.E.<NAME>  E7E"` → if file byte #0 (TP) has bit 7 set: "ILLEGAL ELITE II
FILE", wait for a key, back to the menu. Otherwise copy into NA%+8 → `TRNME` → C set → TT102
`JMP QU5` → DFAULT → **title 2** → any key → docked Status. A catalogue is not needed
before loading. An invalid drive key returns C set too, which simply reloads NA% (the old
save) via QU5.

**Disc errors** (file not found, disc read-only, …) go through MOS BRK → `NEWBRK`
(src:37794). It restores SP from stackpt, beeps, prints the MOS error text, waits for a key
(`t`) and then shows **the SVE menu again**. Press `6` to leave. From the docked `@` path you
then get BAY. From the title Y path you get QU5, which restarts with NA% as it was before.
**Never use option 4 (Delete).** Its copy loop has a bug (src:39277, X=9) that overwrites
the `S` of `savosc`, so every later save fails until reboot.

**Checksum hang:** `DFAULT` loops forever (`doitagain`, src:38299) if CHECK(NA%) ≠ CHK.
A hand-edited or corrupt file with a bad checksum **freezes the game** on load.

### Key sequences (tap = press ≥1 iteration, release, wait ≥3 frames)
* **Save after docking:** wait for QQ12=&FF, QQ11=8. Tap `@`, `2`, `RETURN`, `0`.
  Confirm: QQ11 returns to 8, NA%+8..+75 equals TP..TP+75, and SVC has halved. To use
  a new name, type ≤7 letters before RETURN.
* **Load from docked:** `@`, `1`, `RETURN` (or the name, then RETURN), `0`, then on title 2 `SPACE`.
* **Load from title 1:** `Y`, `1`, name/`RETURN`, `0`, `6` if an error shows, then on title 2 `SPACE`.

### Commander file layout (NA% = &3468; name at NA%..+7, CR-terminated; data at NA%+8 = &3470)
The file on disc is 256 bytes: bytes 0-75 are the block below, the rest is zero. The name
is **not** in the file, because it is the DFS filename (`:0.E.NAME`). Offsets are from
src:13853-13990, and the live copy is at TP = &1234 + offset:

| # | var (live addr) | # | var |
|---|---|---|---|
| 0 | TP mission bits (&1234) | 23-39 | QQ20 cargo ×17 (&124B) |
| 1,2 | QQ0,QQ1 system x,y (&1235/6) | 40 | ECM (&125C) |
| 3-8 | QQ21 galaxy seeds | 41 | BST fuel scoops |
| 9-12 | CASH, **big-endian, in 0.1 Cr** (&123D) | 42 | BOMB (&7F fitted) |
| 13 | QQ14 fuel ×10 (&1241) | 43 | ENGY energy unit (2 = naval) |
| 14 | COK competition flags | 44,45 | DKCMP, GHYP |
| 15 | GCNT galaxy 0-7 (&1243) | 46 | ESCP (&1262) |
| 16-19 | LASER front/rear/left/right (&1244); pulse=15, beam=&8F, military=&97, mining=50 | 47-50 | unused/TRIBBLE/TALLYL |
| 20,21 | unused | 51 | NOMSL (&1267) |
| 22 | CRGO (22 or 37) | 52 | FIST (&1268) |
| 53-69 | AVL market | 70 | QQ26 |
| 71,72 | TALLY lo,hi (&127B) | 73 | SVC (&127D) |
| 74 | CHK2 (&34BA) | 75 | CHK (&34BB) |

NT% = SVC+3-TP = 76 (src:1945). CHECK (src:38580) is shown below. It was verified against
the default commander (CHK=&03, CHK2=&AA). CHK2 = CHK ^ &A9.
```python
def elite_chk(b):            # b = the 76 file bytes
    a, c = 73, 0
    for x in range(73, 0, -1):
        s = a + b[x-1] + c; c = s >> 8; a = (s & 0xFF) ^ b[x]
    return a
```

## 3. Death (`DEATH`, src:37850)

It is triggered by energy reaching 0 (src:32161), a crash into the planet (src:12164), or
docking at speed ≥5 or at a bad angle (`MA62` src:11484). Sequence:
1. Explosion noise. `RES2` clears all ships, sets MSTG=&FF and centres JSTX/JSTY.
   The dashboard is hidden (DET1 24 rows), QQ11=0, and "GAME OVER" appears in cyan at
   (12,12).
2. `D1`: five cargo canisters or alloy plates are spawned around you, about half of them
   pre-killed. MCNT=&FF, DELTA=0.
3. `D2` (src:37992): `JSR M%` then `DEC LASCT` from 64 down to 0, which is **65 runs of the
   flight loop** (a few seconds; no keys are read and M% only consumes the logger).
4. Dashboard back on, `JMP DEATH2` → SP=&FF, `RES2` → `BR1` → **title 1**.

Nothing in this path touches **NA% (&3468-&34BB)**: RES2/RESET only clear FRIN..de and the
flight state. NA% always holds the commander as of the **last successful load, the last
save (even a failed write), or BEGIN's JAMESON**. Pressing N (or any non-Y key) at title 1
runs QU5 → DFAULT, which copies the **entire** NA% into the live block. That restores the
name, mission bits, system, cash, fuel, galaxy, lasers, cargo, equipment, missiles, FIST,
market, TALLY and SVC. Then any key at title 2 → BAY: docked at the station of the saved
system, Status screen.

**Agent recipe after death:** release all keys. Wait for QQ12=&FF, QQ11=0 and distaway=200.
Tap `N`. Wait for distaway=100. Tap `SPACE`. Wait for QQ11=8. No disc access is needed.
**Reload without dying** (for example a run is going badly): tap `COPY` (pause), then
`ESCAPE` (`DK7` src:41104 → `JMP DEATH2`). This is the same title flow, so then `N`, `SPACE`.
This works docked or in flight. Use the disc load (§2) only after a reboot or to pick
another file.

## 4. Escape pod (`ESCAPE`, src:17604)

Hold ESCAPE (int &70) in flight with ESCP (&1262)=&FF and not in witchspace (MJ &E73 = 0)
(src:10930). It plays the Cobra fly-away animation, then:
* **All cargo** QQ20[0..16] = 0.
* **FIST = 0** (clean).
* **ESCP = 0** (the pod is used up).
* QQ14 = 70 (full fuel).
* Everything else is kept: cash, lasers, missiles, ECM, other equipment, TALLY, missions.

Then `JMP GOIN` → `DOENTRY` (src:10364): you are **docked at the station of the current
system** (no hyperspace). The mission checks run as for a normal dock. Without a pod, the
ESCAPE key does nothing in flight. Holding ESCAPE at a pause quits to the title instead.

## 5. Missions (checked only in `DOENTRY` on docking or escape-pod arrival, src:10388-10500)

TP (&1234): bits 0-1 hold mission 1 (00 not started, 01 hunting, 11 killed but not
debriefed, 10 done). Bits 2-3 hold mission 2 (00 not started, 01 go to Ceerdi, 10 carrying
the plans, 11 done). See src:1634. Loading a save restores TP as it was saved.

**Mission 1, the Constrictor**
* Trigger: dock with TP&3 = 0, **TALLY+1 ≥ 1 (TALLY ≥ 256)**, and **GCNT ≤ 1**
  (src:10391-10402). `BRIEF` (src:23877) sets TP bit 0 before anything is shown.
* Screens: the hangar (0.88 s), then "INCOMING MESSAGE" (BRIS, 2 s, src:23993), then the
  Constrictor animation (QQ11=13, 64 iterations plus a fly-off), then the token 10 text
  with **two key waits** ("display ship, wait for key press" twice, data:5865/5882), then
  BAY → Status.
* The hint (MT28 src:12728) says "last seen at Reesdice" in galaxy 0 or "jumped to this
  galaxy" in galaxy 1. Data on System texts for the mission systems also give hints.
* **Where:** galaxy **GCNT=1** (in-game Galaxy 2), **Orarra at QQ0,QQ1 = (144,33)**
  (`THERE` src:36078). From galaxy 0 you need the galactic hyperdrive.
* Spawn: only through the extra-ship spawn (main loop part 4, src:36934). It runs when
  MCNT wraps (every 256 iterations), outside the station safe zone (SSPR &E50 = 0), once EV
  has run down. Each check has a ~35% chance (always in anarchies), then a 39% chance of
  the "lone bounty hunter" branch. In Orarra that branch makes the Constrictor (`YESCON`
  src:37120) if TP bit 0 is set, bit 1 is clear, and no Constrictor is present (MANY+31).
  Other lone hunters in Orarra get AI &F9 (very aggressive, with ECM).
* **Only a military laser hurts it** (src:11653: if the type is ≥ CON and the laser is not
  Armlas, it takes no damage; with a military laser it takes ¼ power). It has ECM.
* Kill (src:35808): TP |= 2 and **TALLY+1 += 1** (+256). Then dock anywhere: `DEBRIEF`
  (src:23797) clears bit 0 (TP = %10) and pays **+5000 Cr** (MCASH 50000). Token 15 shows
  BRIS (2 s) and **one key wait**, then Status.

**Mission 2, the Thargoid plans** (needs mission 1 complete: TP&15 must be %0010)
* Trigger: dock with **GCNT = 2** (Galaxy 3) and **TALLY+1 ≥ 5 (TALLY ≥ 1280)**
  (src:10432-10445). `BRIEF2` sets TP bit 2. Token 11 shows BRIS (2 s), then "go to
  Ceerdi", **one key wait**, then Status.
* Dock at **Ceerdi (215,84)** with TP&15 = %0110. `BRIEF3` sets TP to %1010 (plans on
  board). Token 222 shows BRIS, then **two key waits**.
* While carrying the plans (TP&12 = %1000), each spawn check has a 14% chance of a Thargoid
  plus Thargon (src:36967).
* Dock at **Birera (63,72)** with TP&15 = %1010. `DEBRIEF2` (src:23758) sets TP |= 4,
  **ENGY = 2** (the naval energy unit, which recharges 3 per iteration), and **TALLY+1 += 1**.
  Token 223 shows BRIS and **one key wait**.
* There is no Trumble mission (TBRIEF is commented out, src:23829).

**Agent rule for all briefings:** after docking, until QQ12=&FF and QQ11=8 hold steady,
tap SPACE about every 0.5 s (press, then release). SPACE does nothing on the Status
screen, so extra taps are safe. **Save only after QQ11=8.** BRIEF/BRIEF2/BRIEF3 have
already updated TP by then, so the save records the mission state.

## 6. Rank and legal status

STATUS (src:15908, st4 src:15868) uses TALLY(1 0) = (&127C,&127B). TALLYL (&1266) is a
fraction:

| rank | TALLY |
|---|---|
| Harmless | 0-7 |
| Mostly Harmless | 8-15 |
| Poor | 16-31 |
| Average | 32-63 |
| Above Average | 64-127 |
| Competent | 128-511 |
| Dangerous | 512-2559 |
| Deadly | 2560-6399 |
| Elite | ≥ 6400 (hi ≥ 25) |

FIST (&1268) (src:1853, STATUS src:15984): 0 = Clean, 1-49 = Offender, ≥50 = Fugitive.
* Launch (TT110 src:28912) sets FIST |= BAD = (slaves + narcotics)×2 + firearms.
* Killing a cop sets FIST |= 64 (src:11774).
* FIST is halved on each arrival in a new system (SOLAR src:31483).
* It is cleared by the escape pod and by the galactic hyperdrive (src:27796).
* With FIST ≥ 40, bounty hunters turn hostile (src:18708). Cops spawn more often with
  contraband aboard.

## 7. Pause, options and keys to avoid

**COPY** pauses (DK4 src:40978 checks KL = &8B). Pausing works docked or in flight, because
DOKEY runs in both. While paused, the `FREEZE` loop (src:40985) does a WSCAN and an RDKEY
on each pass:

| key | effect | var (default) |
|---|---|---|
| CAPS LOCK | toggle damping | DAMP &2C56 (0 = **on**) |
| A | toggle keyboard auto-recentre | DJD &2C57 (0 = **on**) |
| X | author names + manual mis-jump | PATG &2C58 (0) |
| F | flashing console bars | FLH &2C59 (0) |
| Y | reverse joystick Y | JSTGY &2C5A (&FF, set by RESET) |
| J | reverse both joystick channels | JSTE &2C5B (0) |
| K | keyboard/joystick | JSTK &2C5C (0 = keyboard) |
| U, T | dead options | UPTOG, DISK |
| B | Bitstik; **also sets JSTK and JSTE** | BSTK &2C5F |
| Q / S | sound off / on | DNOIZ &2C55 (0 = on) |
| `<` / `>` | volume | VOL &2C61 (7) |
| **DELETE** | unpause | |
| **ESCAPE** | **quit to title 1** (DEATH2) | |

The toggles come from the TGINT table (src:10186) via DKS3 (src:40612). Each toggle
beeps and **waits 0.4 s, then re-toggles if the key is still held**. Hold the key for
1-15 frames only, then check the RAM byte. A toggle is one beep for off and two for on.

Options and a bot:
* Defaults: damping on, auto-recentre on. In M% part 2 (src:10642), damping (`cntr`
  src:23085) pulls the roll rate JSTX (&C1) 2 per iteration toward 128 and the pitch rate
  JSTY (&C2) 1 per iteration.
* While a key is held, it adds ±7 to roll (`<` `>`) or ±14 to pitch (S/X) per iteration
  (DOKEY src:40918). With auto-recentre on, pressing the opposite direction **snaps** the
  rate to 128 (REDU2/BUMP2 `djd1`, src:23223).
* So with the defaults, rate ≈ how long the key has been held, and it decays once you let
  go. Releasing then tapping the opposite key gives a one-iteration stop. This is the
  easiest scheme for a bot.
* Turning damping off (CAPS LOCK while paused) makes rates "sticky". Then you need
  explicit counter-presses, which only pays if you want to hold a rate without holding a
  key.
* Keep JSTK = 0. Each title screen forces it to 0 anyway.

Avoid:
* **ESCAPE** in flight: it fires the escape pod if one is fitted.
* **ESCAPE** while paused: quits.
* **CTRL+H**: galactic hyperdrive (it uses up GHYP).
* **CTRL** during a jump with PATG set: forced mis-jump.
* **J** held: repeated in-system jumps.
* **TAB**: energy bomb.
* **P**: cancels the docking computer.
* **Disc menu option 4**: breaks saving (see §2).
* **`@`** at any docked screen opens the disc menu.
* Typing in MT26 never sees SHIFT, and ESCAPE there corrupts the name.
* Hyperspace: H starts a 15-step countdown (QQ22+1 &45). Each step takes 5 main-loop
  iterations (src:37574).

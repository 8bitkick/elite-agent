# What the agent reads and what it does, and how fair each is

The agent's whole interface to the game is `player_io_t` (`cplayer/player.h`): once per emulated
50 Hz frame, between frames, it gets read-only access to the Master's 32 KB of main RAM
(&0000-&7FFF, as it stood at the end of the last frame). It can answer in two ways: key presses on
the BBC keyboard matrix, and values on the analogue (joystick) port. It never reads the screen. The
shadow screen RAM (LYNNE) isn't in what it's given, and it never writes RAM.
(The fight lab's test harness pokes two settings bytes into its fight snapshots, DAMP and JSTK at
`lab.c:205-206`, so a snapshot starts the way a career would have it. That's set-up for tests;
the agent itself doesn't do it.)

This page goes through both directions: each output, how it reaches the machine, and whether a
physical rig on a real BBC Master could do it; then each RAM location the agent reads, and whether
a human player could know the same thing. Line references are to this repository (`cplayer/`
unless a path is given). Game-side names and line numbers (`src:`) are from Mark Moxon's annotated
Master source, as in the other `docs/` pages.

Classification used for inputs:

| Tag | Meaning |
|---|---|
| **V** | **Visible**: drawn on screen for a human somewhere (dashboard, scanner, a docked screen). The precision is noted. |
| **S** | **Static**: fixed data a human can know from the manual or the source (blueprints, galaxy seeds, price formulas). |
| **D** | **Derivable**: not shown as such, but a human or a vision system can work it out at runtime from what is shown, or from their own inputs. How is noted. |
| **H** | **Hidden**: neither shown nor derivable at runtime. The use and a fair alternative are noted. |

---

## 1. Output: what the agent does to the game

### 1.1 How input gets to the machine

| | Development build (`sim/libbeeb.c`, `run_sim.c`, `lab.c`) | Presto (`presto/elite_presto.c`) |
|---|---|---|
| Keys | `keyq` (`keyq.c`) turns holds and taps into per-frame key changes, then `beeb_key()` queues each change into the emulated matrix 2000 cycles (1 ms) into the next frame (`libbeeb.c:85-88`) | the same `keyq`; then `machine_hook_io.key(row, col, down)` (`rp2350-bbc-micro/core/machine_hook.h`, called at `elite_presto.c:87-89`) |
| Joystick | `beeb_analogue()` → `machine_set_analogue()` (`libbeeb.c:211`), held from the next ADC conversion | `machine_hook_io.analogue(ch, value)` (`elite_presto.c:103`), likewise |
| Latency | every hold and tap lands `KEYQ_DELAY` = 2 frames (40 ms) after it's asked for (`keyq.h:7`; the rules were tuned over a WiFi link with that delay) | same |
| Rate | the held set can change every frame (50 Hz). Taps are queued and sent one at a time, each held `down` frames and then released for `up` frames (`keyq.c:24-48`) | same |

There are two kinds of key output (`player.h:17-21`, `drv.c:60-67`, `drv.c:123-145`):

* **hold**: exactly this set of keys down from now on. This is how flight works: roll, pitch,
  throttle, fire and ECM are held for as long as they're wanted, recomputed every frame.
* **tap**: press one key for N frames and release it for M. This is how the menus, views and
  one-shot actions work. The agent waits until the platform reports the tap done (`taps_done`).

### 1.2 How Elite reads input (the limits any player works within)

* Keys are sampled **once per main-loop iteration** (`DOKEY`, src:40678), not once a frame. An
  iteration takes about 2 frames, more in a busy scene. A key has to stay down until `DOKEY` has
  run, so taps are held 2-15 frames rather than 1.
* `FILLKL` (src ~48000) scans internal key numbers 16-127 one by one through the System VIA. Each
  key that's down and in the **flight-key table** IKNS sets its own key-logger byte. Those keys are
  E, T, U, P, A, X, J, S, C, TAB, SPACE, M, `<`, `>`, `?` and ESCAPE, and **any number of them
  register together**. **Every other key** (f0-f9, H, D, the cursor keys, digits, letters at
  prompts, COPY, DELETE, @) is only seen through `KL`, which ends up holding the
  **highest-numbered** key that's down. So a held flight key swallows f0 or f5, and the agent lets
  go of everything before pressing one (`combat.c:570-576`, `flight.c:52`, `flight.c:247-250`).
* CTRL (internal key 1) is read directly, for CTRL-H and for the forced mis-jump.
* While paused (`FREEZE`, src:40985) the game reads one key a pass with `RDKEY`. Each option
  toggle beeps and re-toggles after 0.4 s if the key is still down (see `docs/game-flow.md` §7).
* Joystick mode (JSTK set): `RDJOY` takes JSTX from the high byte of ADC channel 0 (ORed with 1)
  and JSTY from channel 1 (src:48384). The joystick's fire button would press A, but the agent
  fires with the A key.

### 1.3 Keys the agent presses

Codes are the BBC internal key numbers (row << 4 | column, `keys.h`).

| Key | Code | Used for | How it's delivered | Where |
|---|---|---|---|---|
| `<` `>` (roll) | &66 &67 | steering, in keyboard mode (in joystick mode they become stick positions, §1.4) | held, recomputed every frame | `pilot.c:84-85`, `drv.c:46` |
| S X (dive, climb) | &51 &42 | steering; X also climbs out after launch and jinks in a witchspace run | held | `pilot.c:101-103`, `flight.c:28`, `flight.c:207` |
| SPACE `?` (faster, slower) | &62 &68 | speed, toward a target value each frame | held | `pilot.c:109-112` (`speed_keys`) |
| A (fire) | &41 | laser, while the target is within `fire_r` of the sights, the laser isn't hot and no non-hostile ship is in the sights | held | `combat.c:283-288` |
| E (ECM) | &22 | an incoming missile close by, or when we're hurt | held | `combat.c:541-548` |
| J (in-system jump) | &45 | travel to the planet | held while allowed | `pilot.c:253` |
| T, M, U (target, fire, unarm missile) | &23 &65 &35 | missiles | tap 2 down, 1 up | `combat.c:553-566`, `combat.c:577` |
| TAB (energy bomb) | &60 | outnumbered and hurt | tap 2/1, repeated until BOMB clears | `combat.c:512-531` |
| ESCAPE (in flight) | &70 | escape pod: losing a fight, or pinned down short of the station | tap 12/4, up to 4 tries (6/4 for the pod-dock) | `combat.c:583-588`, `pilot.c:225` |
| f0, f1 (front, rear view) | &20 &71 | fighting with the rear laser; f0 is also launch when docked | everything released, then tap 4/1; `hold_until` taps 8/2 every 25 frames until the screen changes | `combat.c:550`, `combat.c:573-576`, `flight.c:18` |
| H | &54 | hyperspace | everything released, tap 10/3, up to 4 tries | `flight.c:50-63` |
| CTRL + H | &01 + &54 | galactic hyperdrive (CTRL held, H tapped 6/2) | CTRL in the held set | `flight.c:468-470`, `flight.c:358-361` |
| CTRL (held through a countdown) | &01 | forced mis-jump into witchspace (§1.6) | held from the start of the countdown to arrival | `flight.c:182`, `drv.c:63` |
| f5, cursor keys, D | &74, &19 &79 &39 &29, &32 | short-range chart: move the cursor to a system (taps 2/1), then D (`T95`: snaps to the nearest system and selects it) | released keys, then taps | `flight.c:243-288` |
| f4, F, letters, RETURN | &14, &43, ..., &49 | long-range chart: "find system" by typing its name | taps 3/4 per character | `docked.c:151-166` |
| f1 f2 f3 f8 (docked) | &71 &72 &73 &76 | buy, sell, equip, status | `hold_until` | `docked.c:34`, `docked.c:83`, `docked.c:115`, `docked.c:102` |
| digits, RETURN, Y, B | ... | quantities to buy, equipment numbers, laser mount (0 front, 1 rear), "Y" to sell each item, B (a letter) to leave the buy prompts | taps 3/3 or 3/4 | `docked.c:30-140` |
| @, 2, name, RETURN, 0 (SPACE, 6) | &47 ... | save the commander to disc (§1.5) | taps | `docked.c:189-218` |
| N, SPACE | &55, &62 | the title screens; SPACE also pages through mission briefings | `hold_until`, taps | `docked.c:170-185`, `docked.c:267` |
| COPY, X, CAPS LOCK, K, DELETE, ESCAPE | &69, &42, &40, &46, &59, &70 | pause-menu options (§1.4) and quitting to the title | taps 3-15 frames | `flight.c:75-90`, `flight.c:108-146`, `docked.c:230-234` |

The most keys down at once in flight is about six: a roll and a pitch key, SPACE or `?`, A, E,
and CTRL during a mis-jump countdown. With the joystick on, roll and pitch go to the stick, so it's
three or four keys plus the stick. The view, chart and hyperspace keys are always pressed alone.

### 1.4 The joystick, and the pause-menu options

**Joystick (default when the platform has an analogue port, `g_joystick = 1`, `flight.c:104`).**
When JSTK (&2C5C) is set, `hold()` turns roll and pitch into ADC values (`drv.c:34-59`).
`steer()` works out the exact stick position it wants for the roll and pitch rate it needs
(`pilot.c:19-33`, `pilot.c:79`). Any roll or pitch key from elsewhere (an evasion jink, say) moves a
virtual stick ±7 or ±14 from where JSTX/JSTY are now, as the keyboard would. The calibration is
JSTX = 3 + ch0·250/65536 and JSTY = 1 + ch1·253/65536, inverted to match the game's own JSTE and
JSTGY flags (`drv.c:50-56`). On the short-range chart in joystick mode the cursor follows the
stick, so the agent leans it toward the system and learns which way each axis moves it
(`flight.c:262-285`). In joystick mode the stick can be anywhere, and go there in one frame: there's
no keyboard ramp.

**Pause-menu options** (COPY pauses, DELETE unpauses; the table is in `docs/game-flow.md` §7):

| Option | Key in pause | What the agent does | Where |
|---|---|---|---|
| Keyboard damping (DAMP &2C56) | CAPS LOCK | **off** after each launch (sticky rates, faster turns in fights); **on** again for the final docking approach in keyboard mode, so a released roll eases off and it can hold the station's spin | `flight.c:93-146`, `flight.c:35`, `pilot.c:348-357` |
| Joystick (JSTK &2C5C) | K | **on**, in the same pause as the damping toggle (each title screen clears it, so it's set again after each launch) | `flight.c:128-132` |
| Author credits / manual mis-jump (PATG &2C58) | X | **on** before a forced mis-jump (§1.6) | `flight.c:75-90` |

The agent pauses with COPY while still holding its flight keys (COPY outranks them in `KL`). It
waits until the main-loop counter stops, which means the game is frozen, and then lets go. It
toggles, checking the RAM byte after each press, then holds DELETE for 15 frames until the loop
runs again (`flight.c:108-146`). DELETE has to be held about a quarter of a second to unpause.
No game time passes while it's paused.

### 1.5 Disc saves, reloads and the title screens

* **Save** (at every dock, `agent.c:186-203`): @ opens the disc menu, then 2 (Save Commander). If
  the commander isn't already called CLAUDE, the name is typed. Then RETURN, then 0 for the drive.
  If the write fails (a read-only disc), SPACE and 6 (Exit) get back out (`docked.c:189-218`). The
  game copies the commander to NA% before writing. The agent compares the two afterwards
  (`docked.c:215`), but only to log a mismatch.
* **Reload** (after a death, or when a step gets stuck): the agent **doesn't load from disc**. It
  uses the title screen's "Load New Commander (Y/N)?". **N** keeps the last saved commander, which
  the game holds in NA% and restores at `BR1`. Then SPACE (`docked.c:170-185`). To get to the title
  without dying, it pauses with COPY and presses ESCAPE, which quits (`docked.c:220-243`).
* That's save-scumming: a save at every dock and a reload on every death. The game allows it and
  a human can do the same, but it's worth knowing when comparing with a human's ironman run. A
  death costs only the trip since the last dock.

### 1.6 Game features the agent relies on that a casual player might not know

* **The forced mis-jump** (Moxon's `ee5`, PATG). With the author credits switched on (pause, X,
  unpause), holding CTRL as the hyperspace countdown ends drops the ship into witchspace among
  Thargoids. The agent farms Thargoids this way (`agent.c:339-372`) and chains mis-jumps
  (`flight.c:296-457`). It's in the shipped game and a human can do it, but it's an Easter egg
  rather than normal play.
* **The escape pod as a lift to the station** when pinned down for four minutes
  (`pilot.c:215-229`).
* **Missile kills score by the target's slot number**, a Master-version bug the agent exploits by
  aiming missiles at cheap ships (`combat.c:556-559`).

### 1.7 Could a real rig on a real BBC Master do this?

| Output | Physical rig | Feasible? | Notes and flags |
|---|---|---|---|
| Keyboard, menu taps | an MCU driving analogue switches (4066s, or an 8×16 crosspoint such as an MT8816) across the key contacts | **Yes, easily** | Taps of 2-15 frames are far longer than anything the hardware needs. A 50 Hz frame clock can be taken from the RGB/composite sync. |
| Keyboard, held flight keys changing every frame | same | **Yes** | The game samples once per main-loop iteration (~25 Hz), so a 50 Hz update rate is more than enough. Up to about six keys at once. **Caution:** the BBC keyboard has no per-key diodes, so three keys on the corners of a matrix rectangle might make the fourth read as pressed (ghosting). Switches across the contacts would behave the same. I haven't checked the agent's combinations for this. For example, J (row 4) + X (row 4) + SPACE (row 6, X's column) would ghost M (row 6, J's column). A rig that emulates the keyboard instead avoids it. It would answer each 7-bit key address from the VIA with the right sense bit within the ~1 µs before the 6502 reads it (an RP2040's PIO or a small CPLD can do that). |
| Keyboard timing vs a human | | **Superhuman** | The rig reacts in 40 ms and can change the held set 50 times a second. A human reacts in ~200 ms and can't re-chord keys at that rate. |
| Analogue joystick | a two-channel DAC (or digital potentiometers) into the Master's 15-pin analogue port, 0 to its VREF (about 1.8 V), on ADC channels 0 and 1 | **Yes** | The Master's MOS converts the channels in turn, so a new value can take a few tens of ms to show. That's fine, since the agent holds a value from "the next conversion" in the emulator too. **Superhuman**: the agent puts the stick at an exact 8-bit position and moves it end to end in one frame. A human on a real stick can't place it that precisely or move it that fast. |
| Pause-menu toggles | keys, as above | **Yes** | Ordinary key presses, a few frames each. |
| Disc save | keys; a real floppy drive or a Gotek | **Yes** | The game's own menu. |
| Reload | keys | **Yes** | The title screen's N, no disc access. |
| Frame sync, "between frames" | an MCU counting vsyncs | **Yes** | In the emulator the agent runs exactly between frames. A real rig would be a few ms out of phase, which doesn't matter at these press lengths. |

Nothing on the output side needs anything a physical rig couldn't provide. **The input side is
what doesn't carry over to a real machine.** Reading main RAM on a real Master means snooping the
65C12's bus: a board in the CPU socket, or on the bus, that mirrors every write into its own copy
at 2 MHz (an FPGA or an RP2040 can do it). It also has to follow ACCCON (&FE34), because &3000-&7FFF
can be switched between main RAM and the LYNNE shadow screen. NA% (&3468) and UNIV are in that
range. It's feasible, but no human gets that view.

---

## 2. Input: what the agent reads (RAM peeks)

Every read goes through `R()`/`R16()` (`state.h:8-9`), through the named accessors in `state.h`,
through `IO->ram` directly (`docked.c:215`, `agent.c:184`), or through `ships_read(IO->ram, ...)`
(`ships.c:95-105`, about 15 call sites). Addresses are from `elite_ram.h`, which is generated from
`elite/symbols.json`. Where a value has a coarse on-screen form (a dashboard bar), the agent still
reads the exact byte. The precision column says how much a human gets.

Dashboard precision (from `DIALS`/`DILX`, src:8832-9300): each bar is 16 pixels long.
`DILX` shows a byte at 1/16 (16 steps). The energy banks show ENERGY/4 across four 16-pixel banks
(64 steps). Fuel is shown on a 0-63 scale and speed on a 0-32 scale. The roll and pitch indicators
show the turn rate at about 8 positions each way. The scanner draws a ship only if x_hi, y_hi and
z_hi are all ≤ 63 (about ±16,000 on each axis: a cube, not a sphere). It draws the dot at x_hi (2-pixel
steps, so ~512 units), the stick base at z_hi/4 (~1024 units) and the stick height at y_hi/2
(~512 units) (`SCAN`, src:3516). The dot's colour depends on the ship type (`scacol`). The scanner
shows no orientation and no speed.

### 2.1 Our ship and commander

| Address | Moxon name | What it is | Used for (decisions; logging omitted) | Class |
|---|---|---|---|---|
| &00D7 | ENERGY | energy banks, 0-255 | escape pod below 40 (`combat.c:534`); bomb below 140 (`combat.c:512`); ECM below 160 (`combat.c:544`); missiles below 150 (`combat.c:559`); break off (`combat.c:450`, `:470`); missile side-turn (`combat.c:492`); witchspace bail-out (`combat.c:382`, `flight.c:405-416`, `:453`); chain when ≥ 248 (`flight.c:332`); end a hunt below 150 (`agent.c:305`); stalemate check (`pilot.c:193-201`) | **V**: the 4 bank bars, energy to ±4. That's enough for every threshold except 248, which is the top two pixels of the top bank. |
| &00D5, &00D6 | FSH, ASH | forward and aft shields, 0-255 | evade when a shield is below 13 (`combat.c:263`); break off (`combat.c:450`, `:470`); missile side-turn (`combat.c:492`); bomb (`combat.c:512`); ECM (`combat.c:544`); witchspace run vs fight (`flight.c:191`); chain at ≥ 200 (`flight.c:332`); hunt end (`agent.c:305`) | **V**: the FS and AS bars, to ±16. "Below 13" is "the bar is empty". The comparisons `ash > 2·fsh + 40` (`combat.c:450`) and `≤ 250` (`combat.c:497`) are finer than a bar shows. |
| &0075 | DELTA | our speed, 0-40 | `speed_keys` targets everywhere (4, 6, 12, 14, 22, 40...); our velocity for aim lead, collisions and missiles (`combat.c:197`, `:216`, `:495`) | **V/D**: the speed bar (~2 units a pixel). It's also exact from our own SPACE/`?` presses (±1 per iteration held). |
| &1221 | ALTIT | altitude above the planet | planet guard below 70 (`pilot.c:130`); run in to the station below 60 (`pilot.c:246`) | **V**: the AL bar, to ±16 |
| &0E79 | GNTMP | laser temperature | don't fire above 224 (`combat.c:284`) | **V**: the LT bar, to ±16 (224 is 14 pixels of 16) |
| &00C1, &00C2 | JSTX, JSTY | roll and pitch rate ("stick position"), 0-255, centre 128 | the feedback in every `steer()` call and in the turn-time model (`combat.c:112`, `:229`); the joystick baseline (`drv.c:41`); the docking spin estimate (`pilot.c:378`) | **D**: it's our own control state. With a joystick the player knows where the stick is. With keys it follows from how long each was held (±7/±14 per iteration, damping −2/−1). The dashboard's RL/DC indicators show it roughly (about 8 positions each way). |
| &1241 | QQ14 | fuel, tenths of a LY | refuel below 7 LY (`docked.c:145`); every range and route check (`flight.c:168`, `:299-379`, `agent.c:234`, `agent.c:564`, `pilot.c:169`) | **V**: exact on the Status screen; the fuel bar in flight |
| &123D (4) | CASH | credits ×10, big-endian | shopping (`agent.c:97-176`), trade plans (`agent.c:223`, `:537-564`), the save guard (`agent.c:197`), witchspace readiness (`agent.c:389`), the pod-dock (`pilot.c:221`) | **V**: exact on the Status screen |
| &124B (17) | QQ20 | cargo held, per item | sell everything (`docked.c:79`), plans (`agent.c:221`, `:563`), scooping (`agent.c:250-304`) | **V**: exact on the Inventory screen (f9) |
| &124A | CRGO | hold capacity | big bay fitted (`agent.c:84`); room to buy (`agent.c:222`, `:537`, `:543`) | **V/S**: the Status screen lists "Large Cargo Bay"; the capacity that goes with it is in the manual |
| &1244 (4) | LASER | laser per view (front, rear, left, right) | what to buy (`agent.c:66-97`, `:389-397`), the pirate "flee" rule (`pilot.c:188`), the witchspace plan (`flight.c:395-398`), laser power and the rear laser (`combat.c:111`, `:126`), extra risk with the starting ship (`agent.c:528`) | **V**: the Status screen lists each laser; its power is **S** (source) |
| &1267 | NOMSL | missiles carried | firing (`combat.c:559`); buying (`agent.c:176`) | **V**: the dashboard's missile blocks |
| &125C, &125D, &125E, &125F, &1261, &1262 | ECM, BST, BOMB, ENGY, GHYP, ESCP | equipment fitted | shopping (`agent.c:79-97`, `:152-165`); combat: ECM (`combat.c:541`), bomb (`combat.c:512`), pod (`combat.c:534`, `:584`); galactic hyperdrive way out (`flight.c:356`) | **V**: the Status screen's equipment list. (DKCMP &1260 has an accessor but is never read.) |
| &127B (2), &1266 | TALLY, TALLYL | kill points (integer and fraction) | mission 2 due at ≥ 1280 (`agent.c:61`); "a fight that scored nothing" (`pilot.c:197-199`); points per minute for witchspace vs bounty hunting (`agent.c:490`, `:512`, `:602`, which feeds `choose_witch`, `agent.c:402-411`) | **D**: only the rank band is shown (Status), plus "RIGHT ON COMMANDER!" at each multiple of 256. A player can keep the exact count from the kills they see times each type's points (KWH%/KWL%, **S**), including the Master's missile-scores-by-slot quirk. |
| &1268 | FIST | legal status | logging only (`drv.c:98`) | **V**: band (Clean, Offender, Fugitive) on Status |
| &1234 | TP | mission progress bits | mission 1 and 2 state (`agent.c:56-57`, `:61-69`) | **D**: from the briefings the player has been given |
| &1243 | GCNT | galaxy number | everything to do with the galaxy | **V**: the long-range chart's title |
| &1235, &1236 | QQ0, QQ1 | current system's coordinates | everything to do with location | **V/S**: the system is named on Status; its coordinates are in the galaxy data |
| &00E1, &00E2 | QQ9, QQ10 | the chart cursor (selected system) | steering the cursor (`flight.c:254-279`); naming the jump target (`flight.c:179`) | **V**: the cursor on the chart, exact on the short-range chart, plus the selected system's name |
| &0045 | QQ22+1 | hyperspace countdown | waiting for it (`flight.c:10`), fight on while it counts (`combat.c:382`), no flee once counting (`pilot.c:188`) | **V**: the countdown digit |
| &1294 | QQ28 | this system's economy | trade plans (`agent.c:223`, `:564`) | **V**: Data on System screen |
| &127A | QQ26 | the market's random byte for this visit | turned into local prices with the static formula (`galaxy.c:272-285`) for buying (`agent.c:223`, `:564`) | **D (exact)**: never shown, but it fixes the prices on the Market Prices screen exactly, so it can be read off any price. Prices at the destination use the mean over QQ26 (`exp_price_tenths`), so the agent doesn't foresee the next market. |
| &1269 (17) | AVL | quantities on sale | buying (`agent.c:220`, `:563`) | **V**: exact on the market screens |
| &1297 | tek | tech level | what equipment is on sale (`agent.c:157-176`, `:422`) | **V**: Data on System ("Tech Level", shown +1) |
| &122C | NAME | commander's name | whether to type the name when saving (`agent.c:184`) | **V**: Status screen |
| &3468 (+8, 73 bytes) | NA% | the last-saved commander | compared with TP onward after a save, logging only (`docked.c:215`) | n/a (no decision) |

### 2.2 Where the game is (screens, views, state)

| Address | Moxon name | What it is | Used for | Class |
|---|---|---|---|---|
| &0081 | QQ11 | which screen is up (0 space; 1, 2, 4, 8, 16, 32, 64, 128 for the menus and charts) | every menu step (`docked.c:11-21` and on); whether the stick moves the chart cursor (`drv.c:45`) | **V** |
| &0087 | QQ12 | docked | `is_docked()` (`state.h:13`), everywhere | **V** |
| &0E77 | VIEW | front, rear, left or right view | which view to fight in, and fire only in it (`combat.c:288`, `:550`) | **V**: the view's name is on screen |
| &0E73 | MJ | in witchspace | everywhere (`state.h:20`) | **V**: no planet or sun, Thargoids |
| &0E50 | SSPR (MANY+2) | the station is present: inside the safe zone | J allowed (`pilot.c:152`); run in to the station is over (`pilot.c:219`); hunting leaves the zone (`agent.c:277`) | **V**: the dashboard's S indicator |
| &0E70, &0E41+2+JUNK | JUNK, FRIN | junk count, and whether a non-junk ship is in the bubble | J allowed (`pilot.c:153`): the game's own mass-lock test | **D**: not shown, but pressing J and seeing whether it jumps gives the same answer |
| &0046 | ECMA | an ECM is running | don't trigger ECM again (`combat.c:541`) | **V**: the E indicator, and the sound |
| &005B | MSTG | missile target slot (&FF none) | lock achieved; lock on the wrong ship → unarm (`combat.c:561-564`) | **D**: the missile indicator shows when it locks, and the lock happens as a ship crosses the sights, so it's the one in the crosshairs. The slot number itself isn't shown. |
| &0E76 | MSAR | a missile is armed | arm with T only if none is (`combat.c:562`) | **V**: the missile indicator |
| &1229 | distaway | title screen 1 or 2 | `title()` (`state.h:48-53`) | **V**: the title screens |
| &0084 | MCNT | main-loop counter (−1 a pass) | death detection (`state.h:57`); pause detection (`drv.c:206-215`, used at `flight.c:120`, `:136-139`); unpaused again (`flight.c:85-87`); the station's spin per iteration (`pilot.c:373-378`) | **H** (internal), but **replaceable**: a frozen screen shows a pause, and the spin can be timed in frames |
| &0E78 | LASCT | laser pulse counter, also the death sequence's timer | death detection (`state.h:57`) | **V**: the death is on screen (debris, "GAME OVER") |
| &0010, &0012 | XC, YC | text cursor | "the screen has finished drawing" before each prompt (`drv.c:184-198`); the equip screen's result (`docked.c:108`) | **V**: the text visibly stops printing |
| &1293 | QQ25 | number of items on this screen's list | quantity cap (`docked.c:42`), equipment for sale here (`docked.c:117`) | **V**: the length of the list on screen |
| &1295 | QQ29 | the item the buy or sell prompt is on | the buy and sell loops (`docked.c:8`, `:38-91`) | **V**: the highlighted prompt line |

### 2.3 Settings the player toggles

| Address | Moxon name | What it is | Used for | Class |
|---|---|---|---|---|
| &2C56 | DAMP | keyboard damping (0 = on) | `set_damping` (`flight.c:101`); the docking final (`pilot.c:348`) | **D**: our own setting. Each toggle beeps once for off and twice for on, and the default is known. |
| &2C58 | PATG | author credits / manual mis-jump | `allow_misjumps` (`flight.c:71`) | **D**: same |
| &2C5C | JSTK | joystick mode | stick or keys (`drv.c:39`, `flight.c:105`, `:262`, `:285`, `combat.c:92`, `pilot.c:348`) | **D**: same |
| &2C5A, &2C5B | JSTGY, JSTE | joystick axis reversal | mapping the stick to the port (`drv.c:50-51`); the agent never changes them | **D/S**: the defaults are known and nothing changes them |

### 2.4 The other ships: FRIN and the K% slots

`ships_read()` (`ships.c:95-105`) reads FRIN (&0E41, the type in each slot; the first 0 ends the
list) and each slot's 37-byte block at K% (&0400 + 37·slot). The coordinates are relative to us,
in the front-view frame. Slot 0 is the planet, slot 1 the sun or the space station (in witchspace,
Thargoids). The layout is in `docs/flight-and-combat.md` §1. **The agent sees every ship in the
bubble, in every direction, exactly, every frame.** A human sees the ships in the current view and,
within scanner range, a coarse 3D dot for each.

| Bytes | Field (Moxon, INWK) | Parsed as | Used for | Class |
|---|---|---|---|---|
| FRIN+i | ship type | `type` | which ships are threats (Thargoid and Thargon always, junk types 3-8 never, `combat.c:169-170`); kill points and target value (`combat.c:54-57`); laser power vs Constrictor and Cougar (`combat.c:125-129`); finding the planet and station (`pilot.c:114-122`); what to scoop (`agent.c:257`); bomb candidates (`combat.c:515-516`) | **V** in the view (a known wireframe). **Partly V** on the scanner (colour by type; several types share a colour). Otherwise **H**, until the player turns to look. |
| 0-8 | x, y, z (24-bit sign-magnitude) | `x y z` | **everything**: aim and lead (`combat.c:213-232`); threats within 9,000 (`pilot.c:187`), 16,954 (`combat.c:378`, `agent.c:296`), 30,000 for the flee count (`pilot.c:189`), and 60,000 in witchspace (`combat.c:378`, `flight.c:395`, `:413`, `:437`); target value (`combat.c:134-151`); collision prediction (`combat.c:189-205`, `pilot.c:257`); evasion (`combat.c:264-270`); break-off (`combat.c:449-477`); missile side-turn (`combat.c:487-505`); bomb count (`combat.c:516`); docking geometry (`pilot.c:313-441`); the planet guard (`pilot.c:129-149`); scooping (`agent.c:252-317`); the witchspace run (`flight.c:197-209`) and regen hold (`flight.c:413-433`) | **V** for a ship in the current view: the screen position gives the direction exactly; the distance only roughly, from its apparent size and blueprint. **V coarse** on the scanner within ±16k on each axis (~512 to 1024 units). **H** beyond the scanner and out of the view: the 30,000 flee count and the 60,000 witchspace threat range reach well past it. The station's and planet's directions are on the compass. |
| 9-14 | nosev (heading) | `nosev[3]` | lead: where the target is going (`combat.c:216`); "its nose is on us" for threat and evasion (`combat.c:148-149`, `:269-270`); collision courses (`combat.c:197`); break-off logging; the station's slot axis (`pilot.c:313`) | **D** for a ship in the view: a vision system can fit the known wireframe and get its orientation. **H** for ships off the view (the scanner shows no heading). The station's axis is **D**: the slot is drawn when it's in view, and the station is set up with its slot toward the planet (`NWSPS`). |
| 15-20 | roofv | `roofv[3]` | the station only: how the slot is tilted, for the roll-match in the final (`pilot.c:324`, `:372`, `:427`) | **V** when the station is in view: the slot's angle on screen |
| 21-26 | sidev | `sidev[3]` | parsed, never used | n/a |
| 27 | speed | `speed` | lead (`combat.c:214-216`); collision courses (`combat.c:197`); missile closing time (`combat.c:495`) | **D, coarse**: from how it moves between frames (in view, or on the scanner); the blueprint's top speed (**S**) bounds it. Not shown as such. |
| 28 | acceleration | `accel` | parsed, never used | n/a |
| 29 | roll counter | `roll` | the station only: which way it spins (`pilot.c:398`) | **S/D**: `NWSPS` always sets 255 (full roll, one fixed direction), and it can be seen anyway |
| 30 | pitch counter | `pitch` | parsed, never used | n/a |
| 31 | bit 7 killed, bit 5 exploding (the rest unused) | `flags31` | `ship_dead()` filters everywhere; not aiming at an explosion (`ships.c:112`) | **V**: the explosion |
| 32 | AI byte; for a missile, bit 6 = launched at us | `ai` | incoming missiles: top threat (`combat.c:163-165`), the side-turn (`combat.c:490`), ECM (`combat.c:543`) | **D**: an enemy launch prints "INCOMING MISSILE" (`SFRMIS`, src:48735) and the missile shows on the scanner. Any missile we didn't fire ourselves is coming for us. |
| 33-34 | line heap pointer | (not parsed) | n/a | n/a |
| 35 | energy | `energy` | **target choice**: the time to kill = energy / (laser power × hit rate) (`combat.c:146`); otherwise logs | **H**: never shown. A player can only estimate it: the blueprint maximum (**S**) minus the hits they've landed, but ships recharge and others hit them too. |
| 36 | NEWB flags: bit 2 hostile, bit 6 cop, bit 7 scooped/docked | `newb` | **hostile**: decides what's a threat, so whether a fight is on and what's in it (`combat.c:169`; `threats()` is called from `pilot.c:187-189`, `combat.c:378`, `agent.c:296`, `flight.c:395`, `:413`, `:437`); danger nearby (`flight.c:323`); the witchspace run's "away from them" (`flight.c:203`); evasion (`combat.c:266`); **don't fire with a non-hostile ship in the sights** (`combat.c:287`). **Cop**: no energy bomb with police near (`combat.c:521`). **Bit 7**: skip scooped canisters (`agent.c:257`) | Hostile is **H**: the game's own flag. It's partly **D** from behaviour (pirate types, a ship turning nose-on and firing, our shields dropping), but late and with misses: a trader that has just turned hostile looks the same. Cop is **D/S**: Vipers and Transporters by type (E%). Bit 7 is **V**: the canister is gone. |

### 2.5 What the agent works out itself (static data, S)

None of this comes from RAM at runtime. A human can get all of it from the manual, the source, or
by playing.

* **The galaxy** (`galaxy.c`): all 8×256 systems from the seeds. That gives names, coordinates,
  economy, government, tech level, distances exactly as QQ8 computes them, nearest-system
  selection (`TT111`), routes, safe jump ranges, and traps.
* **The market** (`galaxy.c:272-500`): the price and availability formulas (QQ23 table plus QQ26),
  mean prices at a destination, equipment by tech level, and the best cargo, trip and trade loop.
* **Ship blueprints** (`ships.c:13-48`, from `elite-data.asm`): energy, speed, laser, missiles,
  bounty, targetable area (the `HITCH` crosshair test, `ships.c:107-125`), kill points
  (KWH%/KWL%), and default NEWB (E%).
* **Control model** (`ships.c:127-146`, `combat.c:63-74`, `pilot.c:19-33`): JSTX/JSTY → ALPHA/BETA,
  and turn rates by stick offset, measured in the emulator.
* **Mission targets** (`agent.c:16`): Orarra, Ceerdi, Birera, all given in the briefings.
* **Game knowledge**: 1.5 × speed units per iteration, missiles scoring by slot, the mis-jump
  landing at (x, y XOR 31), the station's spin rate (alpha 16).

### 2.6 Named but not read

COMC, JOPOS, LASX, MANY (except MANY+2 = SSPR), QQ24, SVC, UNIV, de and K% (as a name) are in
`elite_ram.h` but no decision reads them. CABTMP, and some of the reads in `run_sim.c` and `lab.c`,
are test-harness logging only. DKCMP and QQ8 have accessors that nothing calls.

---

## 3. Summary

**Docked play, trading, shopping, routes and saving** use only information that is on a screen (exact on
the Status, Market, Inventory and Data screens) or that is static. The one "hidden" byte, QQ26, is
exactly derivable from the prices it sets. This part is fair.

**Travel and docking** are fair apart from details. The geometry of the planet and station is on
screen or the compass, and the station's slot faces the planet. MCNT is an internal counter, used
for pause detection and to time the station's spin. The mass-lock check reads FRIN and JUNK where
a player would just press J. Collision avoidance sees ships behind us exactly.

**Combat** is where almost all of the hidden information is used, and it runs **in every frame of
every fight**. Rank comes only from kills, so nearly all of the agent's progress toward Elite goes
through these decisions:

1. **NEWB hostile flag** (K% byte 36, bit 2). `threats()` rebuilds the enemy list from it each frame
   (`combat.c:156-182`), so it decides whether there's a fight, who's in it, when it's over, who
   to evade, and whether a ship in the sights is safe to shoot past. A player only infers hostility
   from type and behaviour, and later.
2. **Enemy energy** (byte 35). It sets each target's kill time in `target_value()` (`combat.c:146`),
   so it drives target choice. It isn't shown and can only be estimated.
3. **All-round exact geometry**. Exact position, heading and speed of every ship in every direction:
   lead on the target, collision prediction, "nose on us" threat weights, evasion against ships
   behind, the break-off direction. Out of the current view, a human has only the scanner: coarse
   position, no heading, no speed, and nothing beyond about ±16k on each axis (and the agent counts
   threats out to 30,000 for its flee rule and 60,000 in witchspace).
4. **Exact dashboard values**, finer than the bars show: energy to 1 not 4, shields and laser
   temperature to 1 not 16, comparisons like `ash > 2·fsh + 40`. These are small advantages.
5. Smaller ones: MCNT (internal timing), the mass-lock test, and the exact TALLY (a player can keep
   it by bookkeeping).

By count of decision sites, under a tenth of the code that reads RAM uses non-derivable data:
`combat.c:146`, `:169`, `:266`, `:287`, `flight.c:203`, `:323`, the MCNT sites, and the
geometry of ships out of view wherever ships are read in combat. By play time, though, it's
**every fight**.

### Suggestions for a "fair mode"

All ship reads go through `ships_read()`, and all player-state reads through `state.h`. Those two
choke points are enough to build a fair mode without touching the rules:

* **A perception layer in place of `ships_read()`.** Report a ship only if it's in the current
  view's frustum (exact screen direction; distance from apparent size, with noise) or on the
  scanner (quantised to its pixels: x and y to 512, z to 1024, within the ±16k cube). Give heading
  and speed only for ships seen in the view, estimated from frame-to-frame motion, and speed alone
  from scanner motion. Zero bytes 32, 35 and 36.
* **Hostility from behaviour.** Treat as hostile: pirate and Thargoid types (from E%, static); any
  ship we've seen firing at us or have just hit; any ship whose shots coincide with our shields
  dropping. Treat Vipers as police by type. Treat as incoming any missile we didn't fire, cued by
  the "INCOMING MISSILE" message.
* **Enemy energy from bookkeeping.** Use the blueprint maximum minus our own hits on it (in the
  sights while firing × laser power), with an optional recharge model.
* **Dashboard quantisation in `state.h`.** ENERGY to 4, FSH/ASH/GNTMP/ALTIT to 16, speed to the
  bar, so thresholds are what a player could read off the bars.
* **Replace the internal values.** MCNT: frame counts, and "the screen stopped moving" for the
  pause. Mass-lock: just press J. TALLY: count witnessed kills × KWH%/KWL% (with the
  missile-by-slot rule). TP: the briefings received.
* **For a human-fair (not just information-fair) mode**: add ~200 ms of reaction latency, rate-limit
  stick movement and key changes, cap simultaneous keys at about four, and optionally turn off the
  X mis-jump Easter egg and the save-at-every-dock reload.

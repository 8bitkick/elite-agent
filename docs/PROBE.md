# On a real BBC Micro, through the Pi1MHz RAM probe

`cplayer/run_probe` plays disc Elite on a physical BBC Micro Model B. It runs the same player as
`run_sim` on another computer (a Pi, a Mac), and reaches the BBC over the network through the
[Pi1MHz](https://github.com/dp111/Pi1MHz) RAM probe ([PR #51](https://github.com/dp111/Pi1MHz/pull/51)).
That probe stops the BBC for a moment, reads or writes its RAM over HTTP, and lets it go on. There's no
ROM to fit and no keyboard rig: the Pi1MHz on the 1 MHz bus is the only hardware.

```sh
cplayer/build_probe.sh                       # run_probe (no emulator in it)
# on the BBC, at BASIC's prompt with the Elite disc in drive 0:  X%=23:CALL &FC88
BOOT=1 cplayer/run_probe pi1mhz.local 60 journal.jsonl
```

`BOOT=1` loads Elite from the disc; without it the player picks up a game already running (one this
program booted earlier). The other arguments are `run_sim`'s: game minutes, and the journal. `NAME=`
and `SAVE=` work as there.

## What it does to the BBC

**Booting.** Disc Elite's first loader, `ELITE2`, copies the MOS's default vectors over all of
&0200-&0235. That takes the probe's hooks off `IRQ2V` and `EVNTV`, and the loader then reads the rest of
the game through the DFS, which takes the NMI's `&0D00`. After that the probe could never stop the
machine again. So `run_probe` types `*LOAD ELITE2`, changes one instruction in it (`CPY &FFB6`, the
table's length, becomes `CPY #6`, so the copy stops after `USERV`, `BRKV` and `IRQ1V`), and types
`CALL &2F23`, its entry point. The other vectors keep what BASIC and the DFS gave them, which is the
state any program starts in. Nothing else of the loader or the game is changed.

**Stopping.** Elite's own interrupt handler (`IRQ1`) takes the vsync and the timer and returns
without passing them on, so the OS never raises the vsync event and the probe's rung 0 never answers.
Every stop is by rung 1, the Pi's nIRQ, which Elite's handler passes on to the OS and the OS to
`IRQ2V`.

**Reading.** Each pass is one stop that reads what the player looks at on this version
(`elite_ver.c`'s disc table): zero page, page 3 (the workspace and the OS's keyboard buffer), the
buffer's pointers, the ship blocks at `K%`, `LASX` to `ALTIT`, the last save at `NA%`, and the key
routine in both codes, about 1.1 KB, plus 256 bytes of the rest in turn so that the whole 32 KB is
read every 128 passes (the version check reads `&0400`, `&2000` and `&3000`).

**Keys.** Elite reads every key through one routine, `DKS4`, which asks the System VIA about one key.
Each time a launch or a dock loads the code again (`D.CODE` at `&4431`, `T.CODE` at `&3C6F`),
`run_probe` stops the machine, checks it isn't inside the routine, and writes its 25 bytes over with
ones that look the key up in a list of six held keys kept in the routine's own last bytes. Flight
keys come with bit 7 set (`KYTB`), which the keyboard ignores and the patch masks off. The routine still
returns the key number in A and X with bit 7 set if the key is down, keeps Y, and keeps its last byte
the `RTS` that `DKS1` branches to. When the held set changes, a second stop writes the list.

The one exception is the commander's name, which Elite reads through the OS (`OSWORD 0`, in `MT26`).
Each letter, digit, SPACE and RETURN the player presses is also put in the OS's keyboard buffer
(`&03E0`, MOS 1.20's layout). Elite flushes that buffer before it reads a line, so only what's typed at
the prompt is read.

**Timing.** The player counts game frames, and the BBC stands still while it's stopped, so a frame is
20 ms of wall time less the time spent stopped. Keys go through the same `keyq` as `run_sim`, timed in
those frames, and the player runs at most once a frame.

## What it costs

Measured on beebjit (Model B, MOS 1.20, DFS 0.90 on the 8271) with the probe's firmware on its test
board (beeb-remote's `tests/beebjit`):

| | |
|---|---|
| Passes | one a game frame (50 a game second) |
| Time the BBC is stopped | 41% (the game runs at 59% of its speed) |
| Pokes | only when the held keys change, a key types a character, or a code load needs the patch |
| Errors | 0 |

Over WiFi to a real Pi1MHz each request takes longer (the probe's notes: 2.3 ms for `GET /bbc`, 4 ms
for a stop and go), so expect the game to run slower again. The game itself doesn't notice: it's
stopped between instructions and goes on where it was. The screen does: a stop that lasts past the
line where Elite switches the dashboard's mode leaves that frame's split late.

## Limits

- **Model B disc Elite only** (the flicker-free version this player knows on the Model B), on MOS 1.20.
  The Master's SNG47 reads its keys differently and isn't patched.
- **The BBC's own keyboard is ignored by Elite** while the patch is in. Ctrl-Break gets it back.
- **No joystick.** Disc Elite on the Model B fires only from the stick's button, so the player plays it
  on keys anyway, as `run_sim_b` does.
- **The probe's limits apply** (its `docs/dev/ramprobe.md`): don't open the Pi1MHz's help screen or SD
  explorer while it plays.

## Testing it on beebjit

`tests/probe_beebjit.sh` does all of it without hardware: beeb-remote's `tests/beebjit` builds beebjit
with the probe's own firmware C on its test board (a Model B, MOS 1.20, DFS 0.90 on the 8271) and serves
the probe's HTTP routes on localhost, with disc Elite in its drive. From BASIC's prompt the test installs
the probe, boots Elite and plays 8 game minutes. It passes if the game was found, bought at Lave, saved
the commander to the disc (`E.CLAUDE` in its catalogue), launched and docked at Leesti, with no probe
errors. It takes about 14 minutes, and prints `SKIPPED` without beebjit, beeb-remote, the Pi1MHz
checkout or the disc.

```sh
BEEB_REMOTE=~/beeb-remote PI1MHZ=~/Pi1MHz tests/probe_beebjit.sh
```

By hand:

```sh
cp elite-disc-flicker-free-sth.ssd /tmp/elite.ssd
BEEBJIT_ARGS="-0 /tmp/elite.ssd -writeable -mutable" PI1MHZ=~/Pi1MHz tests/beebjit/run_beebjit.sh 8091 &
curl -X POST 'localhost:8091/sim/type?text=X%25%3D23%3ACALL%20%26FC88%0D'      # install the probe
BOOT=1 cplayer/run_probe 127.0.0.1:8091 30 journal.jsonl
```

`PROBELOG=1` also prints each lot of characters put in the OS's buffer.

# Solid Elite on the 6502 Second Processor

`?game=tube` on the page flies Eben Upton's [Solid Elite for the 6502 Second Processor](https://github.com/ebenupton/solid-elite-6502-second-processor)
(its disc at d52896e) on Kieran Connell's Master core with a 6502 Second Processor attached to its Tube. It works in every
mode (`?game=tube&mode=curious` to explore); the page's default is still the Wireframe game in Fair mode.

## What was added

- **The Tube and the parasite** ([tube.c](tube.c)): the Tube ULA, after [jsbeeb](https://github.com/mattgodbolt/jsbeeb)'s
  `tube.js` (with its two-byte register 3 fixed: jsbeeb indexes the wrong array there), and a 65C02 with a flat 64K, the core's
  own `cpu6502.c` compiled a second time under other names. The client ROM (2 KB, at &F800 until the first read of &FEF8) is
  jsbeeb's `6502Tube.rom`, which the page fetches from jsbeeb's repository at a pinned commit.
- **The host side** ([kc-beeb-tube.patch](kc-beeb-tube.patch), applied to a copy of the core by [web/build.sh](../build.sh)): the
  ULA at &FEE0-&FEFF in SHEILA, its interrupt in the host's IRQ level, and `machine_run()` running the host and the parasite
  in slices of 8 host cycles, the parasite at 4 MHz (the Master's internal 65C102). With no parasite attached the core is as
  it was: a 10-minute Fair career on the flicker-free disc gives the same journal as the upstream core.wasm.
- **The player's view** ([../core_wasm.c](../core_wasm.c)): the game is on the parasite, so the player's 32 KB of RAM is the
  parasite's &0000-&7FFF. Two things are put right in it:
  - Solid Elite's frame layer borrows the game's zero page (&06-&27 and &D2-&E1: energy, shields, the text cursor) while it
    draws, keeping the game's copy in `f_zsv`. Between its `BIT GMODE` and the `JSR p_flip` after it puts them back (found in
    F_FRAME's code at start-up), the player sees the game's copy.
  - The Tube version reads the joystick from JOPOS in the parasite, but its host never sends the stick across (the host's IRQ1
    keeps the ADC's readings in its own memory; on a real machine the Tube version is a keyboard game). As Solid Elite's own
    agent harness does (`tube/tools/tmachine.mjs`), the agent's stick goes straight into the parasite's JOPOS (&0CA7).
  - `core_start` waits for the whole of the parasite's code and the first title screen, which come later over the Tube.
- **The player** ([../../cplayer/elite_ver.c](../../cplayer/elite_ver.c)): `EV_TUBE`, the addresses from the parasite's
  symbols (`tube/build/psyms.txt`: WP and zero page as Solid Elite's, UP at &2800, NA% at &2F6B), recognised by two pages of
  its resident code; it takes Solid Elite's docked-screen and briefing handling (`ev_solid()`).

## Building

```sh
scripts/fetch-emulator.sh                    # the emulator's source and the Master's ROMs
web/build.sh                                 # web/core.dev.wasm (the system's clang and wasi-libc, or WASI_SDK=<dir>)
cp web/core.dev.wasm web/core.wasm && shasum -a 256 web/core.wasm > web/core.wasm.sha256    # to pin it
web/build_site.sh                            # web/_site
```

Headless: `ROMS=rp2350-bbc-micro/roms/mos3.20 DISC=solid-elite-tube.ssd TUBEROM=6502Tube.rom MODE=curious node web/run_core.mjs 20`
(10-15 times real time). [native_run.c](native_run.c) is the same machine and player built natively, for debugging.

Licences: the Tube ULA follows jsbeeb (GPL-3.0), as the core is GPL-3.0; the page as a whole is offered under the GPL.

The pinned core.wasm was built with wasi-sdk 34. Built with Ubuntu 24.04's clang 18 and wasi-libc the binary differs, but it
flies the same careers: the same journals after 10 game minutes for the flicker-free disc (src/main.js's `flicker_13`) and
for this disc (`tube_13`, Fair mode).

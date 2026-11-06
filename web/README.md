# The web page

[8bitkick.cc/elite-agent](https://8bitkick.cc/elite-agent/): the agent flying Fair mode live in the browser.

- **The machine and the agent** are one WebAssembly file, [core.wasm](core.wasm): [Kieran Connell's Master
  core](https://github.com/kieranhj/esp32-beeb) (the emulator on the Presto and in `run_sim`) and the C player from `cplayer/`,
  stepped as `run_sim`'s `step()` is: the player decides on this frame's RAM, the keys due go to the machine, and the machine
  runs 40,000 cycles ([core_wasm.c](core_wasm.c), [player_wasm.c](player_wasm.c), [core.js](core.js)).
- **Pinned:** the site ships the committed `core.wasm`, and `build_site.sh` checks its hash. `build.sh` writes `core.dev.wasm`
  (clang's wasm32-wasi target, wasi-libc, and the emulator's source from `scripts/fetch-emulator.sh`). A new pin means flying the
  careers in [STARTS.md](STARTS.md) again.
- **Elite and the ROMs** are fetched by the browser at pinned commits: Mark Moxon's flicker-free disc or Eben Upton's Solid
  Elite, and the MOS 3.20 ROMs from beebjit's repository. This site serves none of them.
- **The panel** is the film's, drawn on a canvas in the film's own 1920x1080 frame ([src/panel.js](src/panel.js)): the clock,
  rank, tally, credits, system and the tally chart; the dogfight network (yellow) in a fight, the docking network (cyan) on the
  way in; the galaxy map when docked. A phone gets the rank bar and time above the screen, and tabs: AI, Galaxy, Log and Options.
- **Speed:** one button, tapped round: **1×, 2×, 4×** hold that speed; **ACTION** (the default) is flat out, but 4× while docked, and 1× in fights and
  the docking's final and for 3 s after, the button lit in the colour of the AI flying; **MAX** is flat out, always. Flat out takes 80% of
  each display frame, the drawing included. The page shows the speed it measures. None of it changes the career.
- **Modes:** `?mode=optimal|fair|curious`, or the Mode switch: each flies its own AI pilot models ([wsp/MODES.md](../wsp/MODES.md)); the
  panel draws whichever network decided last.
- **Not on this page yet:** saving and loading a commander and "Jump to Deadly", which the jsbeeb page had. `src/save.js` and
  `chapters/` are kept for them. Sound is wired from the core's SN76489 a frame at a time, but untested by ear.

Licences: Kieran Connell's core is GPL-3.0, so the page as a whole is offered under the GPL too (`LICENSES.txt`); elite-agent's
own code stays MIT. Elite © Ian Bell and David Braben is not part of the site.

Build: `web/build_site.sh` (Node 22) writes `web/_site`; the `pages` workflow publishes it.

Headless: [run_core.mjs](run_core.mjs), or [tools/fly.sh](tools/fly.sh) for a career and its row in [STARTS.md](STARTS.md).
[tools/visit.mjs](tools/visit.mjs) flies the page in a real browser and reports its journal's hash (`?check=` for further).

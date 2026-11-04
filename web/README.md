# The web page

[8bitkick.cc/elite-agent](https://8bitkick.cc/elite-agent/): the agent flying Fair mode live in the browser.

- **The machine** is [jsbeeb](https://github.com/mattgodbolt/jsbeeb)'s BBC Master, its own page built unchanged from its
  repository at a pinned commit and run in a same-origin iframe, out of sight (`?embed&model=Master`). Its sound,
  timing and keyboard are its own.
- **Elite** is Mark Moxon's flicker-free Master Elite (SNG47), loaded by the browser from his source repository on
  GitHub (`markmoxon/elite-source-code-bbc-master`, the `flicker-free` branch at a pinned commit: the same bytes as
  elite.bbcelite.com's disc; that site doesn't let other sites' pages fetch it). This site serves none of it. On the first
  visit the page boots the disc (in turbo, behind a loading card with Elite's Cobra Mk III turning in lines, from its
  ship blueprint), takes a jsbeeb snapshot at the title screen with the Cobra turning, and keeps it in the browser;
  later visits restore it.
- **The agent** is cplayer/, the same C as `run_sim`, compiled to WebAssembly by clang's wasm32-wasi target
  ([player_wasm.c](player_wasm.c), [build.sh](build.sh)): 680 KB with both networks' weights. [agent.js](agent.js) runs it
  once a frame from jsbeeb's end-of-frame paint (where the native emulator calls the player): the 32 KB of main RAM in,
  the keys and the joystick out. It costs about 50 µs a frame against jsbeeb's ~5 ms, so it stays on the main thread.
  Fair mode, commander BITKICK.
- **The panel** is the film's, drawn on a canvas in the film's own 1920x1080 frame ([src/panel.js](src/panel.js)): the
  clock, rank, tally, credits, system and the tally chart; the dogfight network (yellow) in a fight, the docking network
  (cyan) on the way in, run again in JavaScript from the senses the player hands over (weights and layout in
  [panel.json](panel.json), from [tools/panel_data.py](tools/panel_data.py)); the galaxy map and the log when docked. A
  phone gets the numbers as a top bar, the screen full width, and the chart and network below.
- **Speed:** 1× is jsbeeb's own loop, sound and all. Turbo is the page's: jsbeeb's loop paused and the machine run from the
  page's animation frames for 80% of each, leaving the rest to the browser (jsbeeb's own turbo takes everything, ahead
  of the page). Auto (adaptive), the default, is turbo except in a fight (the dogfight network deciding) or the docking's
  final, and for 3 s after.
- **Chapters:** "Jump to Deadly" (`?chapter=deadly`) starts from a save of the film's career at 10.6 game hours, just
  after it reached Deadly (tally 2,598, at Orqulela in galaxy 3): the 256-byte commander block from the replay of that
  docking ([chapters/deadly.cmdr](chapters/deadly.cmdr), by [tools/chapter.py](tools/chapter.py), no code of the
  game), written onto the game's disc in the browser as `E.BITKICK` for the player to load when it first docks. The
  clock and the chart carry on from 10.6 h.
- **Save commander** writes an .ssd holding only the commander's file, from the disc as the game has written it (the
  player saves at each dock): load it in Elite with "Load New Commander".

Licences: jsbeeb is GPL-3.0-or-later. The site carries its licence and the commit it was built from (`jsbeeb/LICENSE`,
`jsbeeb/SOURCE.txt`), and since the page's bundle includes two of jsbeeb's modules (snapshots, the disc export), the page
as a whole is offered under the GPL too (`LICENSES.txt`); elite-agent's own code stays MIT. Elite © Ian Bell and David
Braben is not part of the site.

Build: `web/build_site.sh` (clang with the wasm32 target and wasi-libc, Node 22) writes `web/_site`; the `pages`
workflow publishes it. `LOCAL_DISC=1` puts the disc in the site for testing where GitHub can't be reached
(open it with `?disc=elite.ssd`). `?speed=1x|adaptive|turbo` sets the speed; `?fresh` boots from the disc again.

Headless, on jsbeeb in Node: `node web/run_node.mjs MINUTES journal.jsonl log.txt` (`SKEW=n`), a career to Elite as a
test that the WebAssembly player plays as the native one does (`CKPT=file` keeps a checkpoint every 10 game minutes and
resumes from it). Seed 13 (`SKEW=13`): Elite after 18.22 game hours.

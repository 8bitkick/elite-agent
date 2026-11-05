// core.wasm headless: a career, to Elite or for MINUTES of game time.
//   ROMS=dir DISC=file.ssd [SKEW=13] [DRAW=0] [SHOT=frame:file.rgba] node web/dev/run_core.mjs MINUTES [journal.jsonl]
// DRAW=n draws every nth frame through the renderer (a test that drawing changes nothing). Prints the journal's hash.
import { createHash } from "node:crypto";
import { createWriteStream, readFileSync, writeFileSync } from "node:fs";
import { Core } from "./core.js";

const here = new URL(".", import.meta.url).pathname;
const minutes = +(process.argv[2] || 10), jpath = process.argv[3];
const ROMS = process.env.ROMS, DRAW = +(process.env.DRAW || 0);
const names = ["mos", "dfs", "viewsht", "edit", "basic4", "adfs", "view", "terminal"];
const roms = names.map((n) => readFileSync(`${ROMS}/${n}.rom`));
const hash = createHash("sha256"), journal = jpath ? createWriteStream(jpath) : null;
let elite = 0, deaths = 0;
const stamp = () => (core.frameNo / 3000).toFixed(1).padStart(7);
const core = await Core.create(readFileSync(process.env.WASM || here + "core.wasm"), roms, readFileSync(process.env.DISC), {
    log: (lv, line) => { if (lv === 0) { console.log(stamp(), line); if (/died|escape pod/.test(line)) deaths++; } },
    note: (json) => {
        const l = `{"frame": ${core.frameNo}, "e": ${json}}\n`;
        hash.update(l); if (journal) journal.write(l);
        try { const e = JSON.parse(json); if (e.event === "rank" && e.rank === "Elite") elite = core.frameNo; } catch {}
    },
});
if (process.env.NAME) core.name = process.env.NAME;
if (!core.start(+(process.env.SKEW ?? 13))) { console.error("no Elite the player knows"); process.exit(1); }
const [shotAt, shotFile] = (process.env.SHOT || "0:").split(":");
const end = minutes * 3000, t0 = Date.now();
let f = 0;
for (; f < end && !elite; f++) {
    const draw = (DRAW && f % DRAW === 0) || (+shotAt && (f === +shotAt - 1 || f === +shotAt));
    core.frame(draw);
    if (+shotAt && f === +shotAt) { const a = core.rgba(); writeFileSync(shotFile, Buffer.from(a.buffer, a.byteOffset, a.byteLength)); }
    if (f % 30000 === 29999) console.log(stamp(), `[${((f / 50) / ((Date.now() - t0) / 1000)).toFixed(1)}x real time]`);
}
journal && journal.end();
const secs = (Date.now() - t0) / 1000;
console.log(elite ? `ELITE after ${(elite / 180000).toFixed(2)} game hours` : `not Elite after ${(f / 180000).toFixed(2)} game hours`);
console.log(`deaths ${deaths}, ${f} frames in ${secs.toFixed(1)} s (${(f / 50 / secs).toFixed(1)}x real time), journal sha256 ${hash.digest("hex")}`);

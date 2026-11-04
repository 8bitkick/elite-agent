// The agent on jsbeeb in Node, headless, as a test that it plays: node web/run_node.mjs MINUTES [journal.jsonl] [log.txt]
// (SKEW=n frames before it starts, as run_sim's; MODE=0/1/2 Optimal/Fair/Curious, Fair by default)
import { readFileSync, writeFileSync, renameSync, existsSync, createWriteStream } from "fs";
import { MachineSession } from "jsbeeb";
import { createSnapshot, snapshotToJSON, snapshotFromJSON } from "jsbeeb/src/snapshot.js";
import { Agent } from "./agent.js";

const here = new URL(".", import.meta.url).pathname;
const minutes = +(process.argv[2] || 10), jpath = process.argv[3], lpath = process.argv[4];
// CKPT=path: the machine, the player's memory and the frame kept there every 10 game minutes, and resumed from if it exists
const EVERY = +(process.env.CKPT_EVERY || 30000);
const CKPT = process.env.CKPT, resume = CKPT && existsSync(CKPT) ? JSON.parse(readFileSync(CKPT, "utf8")) : null;
const journal = jpath ? createWriteStream(jpath, { flags: resume ? "a" : "w" }) : null, logf = lpath ? createWriteStream(lpath, { flags: resume ? "a" : "w" }) : null;
const s = new MachineSession("Master", { discImage: here + "../rp2350-bbc-micro/discs/elite-master-flicker-free-sng47.ssd" });
await s.initialise();
const cpu = s._machine.processor, kb = s._keyboard;
let fr = 0, elite = 0;
const stamp = () => (fr / 3000).toFixed(1).padStart(7);
const agent = await Agent.create(readFileSync(here + "player.wasm"), cpu, kb, {
    log: (lv, line) => { if (lv === 0) console.log(stamp(), line); if (logf) logf.write(`${stamp()} ${line}\n`); },
    note: (json) => {
        if (journal) journal.write(`{"frame": ${agent.frameNo}, "e": ${json}}\n`);
        try { const e = JSON.parse(json); if (e.event === "rank" && e.rank === "Elite") elite = agent.frameNo; } catch {}
    },
});
const t0 = Date.now();
let fr0 = 0;
if (resume) {
    cpu.restoreState(snapshotFromJSON(resume.machine).state);
    const mem = Buffer.from(resume.wasm, "base64"), m = agent.x.memory;
    if (m.buffer.byteLength < mem.length) m.grow((mem.length - m.buffer.byteLength) / 65536);
    new Uint8Array(m.buffer).set(mem);
    fr0 = resume.fr;
    console.log(`resumed at ${(fr0 / 3000).toFixed(1)} min`);
} else {
    await s.autoboot();
    let ok = false;
    for (let i = 0; i < 80 && !ok; i++) { await s.runFrames(50); ok = agent.start(+(process.env.MODE ?? 1)); }
    if (!ok) { console.error("no Elite the player knows"); process.exit(1); }
    if (process.env.SKEW) await s.runFrames(+process.env.SKEW);
    console.log(`booted in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
}
const checkpoint = () => {
    const tmp = CKPT + ".tmp";
    writeFileSync(tmp, JSON.stringify({ fr, machine: snapshotToJSON(createSnapshot(cpu, cpu.model, {})), wasm: Buffer.from(agent.x.memory.buffer).toString("base64") }));
    renameSync(tmp, CKPT);
};
const end = minutes * 3000;
let t1 = Date.now();
for (fr = fr0; fr < end && !elite; fr++) {
    agent.frame();
    await s.runFrames(1);
    if (CKPT && fr % EVERY === EVERY - 1) { fr++; checkpoint(); fr--; }
    if (fr % 30000 === 29999) { console.log(stamp(), `[${(30000 / ((Date.now() - t1) / 1000) / 50).toFixed(1)}x real time]`); t1 = Date.now(); }
}
console.log(elite ? `ELITE after ${(elite / 180000).toFixed(2)} game hours` : `not Elite after ${(fr / 180000).toFixed(2)} game hours`);
journal && journal.end(); logf && logf.end();

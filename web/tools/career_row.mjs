#!/usr/bin/env node
// A career's row for the results table in web/STARTS.md, from run_node's output and journal:
//   career_row.mjs --game flicker --start 31 --out career.out --journal career.jsonl --wasm agent.wasm [--by name] [--machine "..."] [--wall-hours 5.2]
import { execSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

const o = {};
{ const a = process.argv.slice(2); for (let i = 0; i < a.length; i += 2) o[a[i].slice(2)] = a[i + 1]; }
const lines = readFileSync(o.out, "utf8").split("\n").filter(Boolean);
const result = lines.filter((l) => /^(ELITE|not Elite) after /.test(l)).pop() || "";
const hours = result.match(/^ELITE after ([\d.]+)/)?.[1];
// a death is a log line saying "died" or using the escape pod (buying one, "Fitted escape pod", isn't)
const deaths = lines.filter((l) => /died|escape pod/.test(l) && !/Fitted escape pod/.test(l)).length;

const c = { kills: 0, fights: 0, stars: 0, galaxy: 1, docks: 0, credits: 0, witchspace: 0, frame: 0 };
for (const l of readFileSync(o.journal, "utf8").split("\n")) {
    if (!l) continue;
    let d; try { d = JSON.parse(l); } catch { continue; }
    const e = d.e, ev = e.event;
    c.frame = d.frame;
    if (typeof e.cash === "number") c.credits = e.cash;                     // (the last seen: credits at the end)
    if (ev === "fight_end") { c.fights++; c.kills += e.kills || 0; }
    else if (ev === "new_system") { c.stars++; c.galaxy = Math.max(c.galaxy, e.galaxy || 1); }
    else if (ev === "docked") c.docks++;
    else if (ev === "session" && e.kind === "witchspace") c.witchspace++;
}
const wasm = createHash("sha256").update(readFileSync(o.wasm)).digest("hex").slice(0, 8);
let commit = "?"; try { commit = execSync("git rev-parse --short HEAD", { cwd: new URL(".", import.meta.url).pathname, stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); } catch {}
const time = hours ?? `not Elite at ${(c.frame / 180000).toFixed(1)}`;
const cr = Math.round(c.credits).toLocaleString("en");
const today = new Date().toISOString().slice(0, 10);
console.log(`| ${o.game} | ${o.start} | ${time} | ${deaths} | ${c.kills} | ${cr} | ${c.stars} | ${c.galaxy} | ${c.docks} | ${c.witchspace} | yes | ${o.by || "?"} | ${o.machine || "?"}, ${o["wall-hours"] || "?"} h | ${today} | ${commit} | ${wasm} |`);

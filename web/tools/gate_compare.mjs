#!/usr/bin/env node
// The determinism gate's comparisons (see web/STARTS.md). Exits 0 if identical, 1 with the first difference if not.
//   gate_compare.mjs node    browser.log run_node.out   the browser's log text against run_node's, in order
//   gate_compare.mjs browser a.log b.log                two browser visits' logs, frames and text
//   gate_compare.mjs snapshot a.json b.json             two snapshots, ignoring "timestamp"
import { readFileSync } from "node:fs";
import { isDeepStrictEqual } from "node:util";

const FRAMES = 30000;
const lines = (f) => readFileSync(f, "utf8").split("\n").filter((l) => l.length);
const browserLog = (f) => lines(f).map((l) => { const i = l.indexOf(" "); return { frame: +l.slice(0, i), text: l.slice(i + 1) }; });
// run_node's stdout: "    1.2 text" for the player's log lines (its "[12.3x real time]" notes aren't the player's)
const nodeLog = (f) => lines(f).map((l) => l.match(/^\s*\d+\.\d (.*)$/)).filter((m) => m && !/^\[[\d.]+x real time\]$/.test(m[1])).map((m) => m[1]);
const fail = (m) => { console.log(`DIFFERENT: ${m}`); process.exit(1); };

const [what, a, b] = process.argv.slice(2);
if (what === "node") {
    const B = browserLog(a), N = nodeLog(b);
    // run_node runs in steps of 50 frames, so it stops a little past 10 minutes: all of the browser's first 10 minutes
    // must be there, and all of run_node's lines must be the browser's, in order (the browser ran further)
    const must = B.filter((l) => l.frame <= FRAMES).length;
    if (N.length < must) fail(`run_node logged ${N.length} lines, the browser ${must} in its first ${FRAMES} frames`);
    if (N.length > B.length) fail(`run_node logged ${N.length} lines, the browser ${B.length}`);
    for (let i = 0; i < N.length; i++) if (N[i] !== B[i].text) fail(`line ${i + 1}: browser (frame ${B[i].frame}) "${B[i].text}", run_node "${N[i]}"`);
    console.log(`identical: ${N.length} log lines (${must} of them within frame ${FRAMES})`);
} else if (what === "browser") {
    const A = browserLog(a).filter((l) => l.frame <= FRAMES), B = browserLog(b).filter((l) => l.frame <= FRAMES);
    for (let i = 0; i < Math.max(A.length, B.length); i++) {
        const x = A[i], y = B[i];
        if (!x || !y || x.frame !== y.frame || x.text !== y.text) fail(`line ${i + 1}: ${x ? `${x.frame} "${x.text}"` : "(none)"} against ${y ? `${y.frame} "${y.text}"` : "(none)"}`);
    }
    console.log(`identical: ${A.length} log lines within frame ${FRAMES}, frames and text`);
} else if (what === "snapshot") {
    const load = (f) => { const j = JSON.parse(readFileSync(f, "utf8")); delete j.timestamp; return j; };
    if (!isDeepStrictEqual(load(a), load(b))) fail(`${a} and ${b}`);
    console.log("identical snapshots (ignoring timestamp)");
} else {
    console.error("usage: gate_compare.mjs node|browser|snapshot A B");
    process.exit(2);
}

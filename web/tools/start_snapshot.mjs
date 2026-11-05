#!/usr/bin/env node
// A first visit to the page, recorded: the start the browser makes, and the log of its first 10 game minutes.
//   node web/tools/start_snapshot.mjs --game flicker --start 31 --disc flicker.ssd [--out dir] [--tag name]
//        [--throttle 4] [--frames 30000] [--site web/_site] [--chromium /usr/bin/chromium] [--headed]
// Writes <game>_<start>[_<tag>].json (the snapshot from localStorage) and .log ("<frame> <text>", a line for each of
// the player's log lines). The page is served from --site and its disc request answered from --disc, so it needs no
// network; --site http(s)://... visits a published page instead, whose disc comes from wherever the page gets it.
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { extname, join, normalize, resolve } from "node:path";
import { chromium } from "playwright";

const opts = {};
{
    const a = process.argv.slice(2);
    for (let i = 0; i < a.length; i++) {
        if (!a[i].startsWith("--")) throw new Error(`unexpected argument ${a[i]}`);
        const k = a[i].slice(2);
        if (k === "headed") opts[k] = true; else opts[k] = a[++i];
    }
}
const game = opts.game, start = +opts.start;
if (!["flicker", "solid"].includes(game) || !(start > 0)) {
    console.error("usage: start_snapshot.mjs --game flicker|solid --start N --disc file.ssd [--out dir] [--tag name] [--throttle n] [--site dir|url] [--chromium path] [--headed]");
    process.exit(2);
}
const site = opts.site || new URL("../_site", import.meta.url).pathname;
const remote = /^https?:/.test(site);
if (!remote && !opts.disc) { console.error("--disc is required for a local site"); process.exit(2); }
const out = resolve(opts.out || ".");
mkdirSync(out, { recursive: true });
const base = join(out, `${game}_${start}${opts.tag ? "_" + opts.tag : ""}`);
const throttle = +(opts.throttle || 1);
const disc = opts.disc ? readFileSync(opts.disc) : null;
if (disc) console.log(`disc ${opts.disc} sha256 ${createHash("sha256").update(disc).digest("hex")}`);
const FRAMES = +(opts.frames || 30000);      // 10 game minutes by default. Then a little more: run_node stops up to 50 frames past its end, and the
const MARGIN = 100;        // comparison needs the browser's log to cover whatever run_node logged in those frames.
const TIMEOUT_MS = +(opts.timeout || 3 * 3600) * 1000;

// ---- the site, as GitHub Pages would serve it
const MIME = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css", ".json": "application/json",
    ".wasm": "application/wasm", ".png": "image/png", ".svg": "image/svg+xml", ".glsl": "text/plain", ".txt": "text/plain", ".ttf": "font/ttf", ".woff2": "font/woff2" };
const root = remote ? null : resolve(site);
const server = remote ? null : createServer((req, res) => {
    let p = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname));
    if (p.endsWith("/")) p += "index.html";
    const f = join(root, p);
    try {
        if (!f.startsWith(root) || !statSync(f).isFile()) throw new Error();
        res.writeHead(200, { "content-type": MIME[extname(f)] || "application/octet-stream", "cache-control": "no-store" });
        res.end(readFileSync(f));
    } catch { res.writeHead(404); res.end(); }
});
if (server) await new Promise((r) => server.listen(0, "127.0.0.1", r));
const url = (remote ? site.replace(/\/?$/, "/") : `http://127.0.0.1:${server.address().port}/`) + `?game=${game}&start=${start}&fresh`;

const browser = await chromium.launch({ headless: !opts.headed, executablePath: opts.chromium || process.env.CHROMIUM || undefined });
const code = await (async () => {
    const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });     // (its own, empty localStorage)
    const page = await context.newPage();
    page.on("pageerror", (e) => console.error("page error:", e.message));
    if (disc) {
        await context.route(/\.ssd(\?|$)/, (route) => route.request().resourceType() === "document" ? route.continue() : route.fulfill({ status: 200, contentType: "application/octet-stream",
            headers: { "access-control-allow-origin": "*" }, body: disc }));
    }
    const cdp = await context.newCDPSession(page);
    await cdp.send("Emulation.setCPUThrottlingRate", { rate: throttle });
    const t0 = Date.now();
    await page.goto(url, { waitUntil: "load" });
    // every log line kept (the page keeps its last 200)
    await page.evaluate(() => {
        const a = window.eliteAgent.log, push = a.push;
        window.__allLog = [];
        a.push = function (x) { window.__allLog.push(x); return push.call(this, x); };
    });
    await page.waitForFunction((n) => window.eliteAgent.frame > n, FRAMES + MARGIN, { timeout: TIMEOUT_MS, polling: 500 });
    const got = await page.evaluate((start) => {
        const keys = Object.keys(localStorage).filter((k) => k.startsWith(`elite-agent-start3:${start}:`));
        return { keys, snapshot: keys.length === 1 ? localStorage.getItem(keys[0]) : null, log: window.__allLog, frame: window.eliteAgent.frame };
    }, start);
    if (got.keys.length !== 1) { console.error(`expected one snapshot in localStorage, found: ${got.keys.join(" ") || "none"}`); return 1; }
    writeFileSync(`${base}.json`, got.snapshot);
    writeFileSync(`${base}.log`, got.log.map(([f, t]) => `${f} ${t}\n`).join(""));
    console.log(`${base}.json, ${base}.log: ${got.log.length} log lines to frame ${got.frame}, throttle ${throttle}x, ${((Date.now() - t0) / 60000).toFixed(1)} min`);
    console.log(`key ${got.keys[0]}`);
    return 0;
})().catch((e) => { console.error(e); return 1; });
await browser.close();
server?.close();
process.exit(code);

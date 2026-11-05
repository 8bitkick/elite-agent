// A visit to the dev page in a real browser, to 10 game minutes: its journal hash against the reference, a screenshot.
//   SITE=url-or-dir [ROM_DIR= DISC=] CHROMIUM=path node web/dev/visit.mjs [out.png]
import { createServer } from "node:http";
import { readFileSync, statSync } from "node:fs";
import { extname, join, resolve } from "node:path";
import { chromium } from "playwright";

const site = process.env.SITE || new URL("../_site", import.meta.url).pathname;
let url = site, server = null;
if (!/^https?:/.test(site)) {
    const root = resolve(site);
    const MIME = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".json": "application/json", ".wasm": "application/wasm", ".png": "image/png" };
    server = createServer((req, res) => {
        let p = decodeURIComponent(new URL(req.url, "http://x").pathname); if (p.endsWith("/")) p += "index.html";
        try { const f = join(root, p); if (!f.startsWith(root) || !statSync(f).isFile()) throw 0; res.writeHead(200, { "content-type": MIME[extname(f)] || "application/octet-stream" }); res.end(readFileSync(f)); }
        catch { res.writeHead(404); res.end(); }
    });
    await new Promise((r) => server.listen(0, "127.0.0.1", r));
    url = `http://127.0.0.1:${server.address().port}/dev/`;
}
url += process.env.QUERY || "";
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROMIUM || undefined });
const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
const page = await context.newPage();
page.on("pageerror", (e) => console.log("page error:", e.message));
page.on("console", (m) => { if (m.type() === "error" || /journal sha256/.test(m.text())) console.log("console:", m.text().slice(0, 220)); });
const t0 = Date.now();
await page.goto(url, { waitUntil: "load" });
if (process.argv[2]) {
    await page.waitForFunction(() => window.eliteAgent?.frame > 2000, undefined, { timeout: 300000 });
    await page.screenshot({ path: process.argv[2] });
    console.log("screenshot at", ((Date.now() - t0) / 1000).toFixed(0), "s, frame", await page.evaluate(() => window.eliteAgent.frame));
}
await page.waitForFunction(() => window.eliteAgent?.journalHash, undefined, { timeout: 3 * 3600000, polling: 2000 });
const r = await page.evaluate(() => ({ hash: window.eliteAgent.journalHash, frame: window.eliteAgent.frame, label: window.eliteAgent.speedLabel, lines: window.eliteAgent.log.length }));
console.log(`journal ${r.hash}; ${((Date.now() - t0) / 1000).toFixed(0)} s to 10 game minutes; frame ${r.frame}, ${r.label}, ${r.lines} log lines`);
await browser.close(); server?.close();

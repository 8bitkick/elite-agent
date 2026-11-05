// The dev page: the same panel, with the machine inside the WebAssembly: Kieran Connell's Master core (the Presto's and
// run_sim's) and the player in one binary (core_wasm.c), which flies exactly what the headless run_core.mjs and native
// run_sim fly. No start snapshot: the machine boots from power-on on every visit, in about a second.
import { Panel, INPUT_NOTE, COL_NOTE } from "../src/panel.js";
import { Core } from "./core.js";

const $ = (id) => document.getElementById(id);
const V = typeof ASSET_V === "string" ? ASSET_V : "dev";
const Q = new URLSearchParams(location.search);
const ROM_BASE = "https://raw.githubusercontent.com/scarybeasts/beebjit/db16aaf09df1fa0fd7ea686bc8d09b13f5674a42/roms/mos3.20/";
const ROMS = ["mos", "dfs", "viewsht", "edit", "basic4", "adfs", "view", "terminal"];
const GAMES = {
    flicker: { name: "Flicker-free Elite", skew: 13,
        disc: "https://raw.githubusercontent.com/markmoxon/elite-source-code-bbc-master/e6581b586b1c984142a0fee0c8009c7466a38d52/5-compiled-game-discs/elite-master-flicker-free-sng47.ssd" },
    solid: { name: "Solid Elite", skew: 13,
        disc: "https://raw.githubusercontent.com/ebenupton/solid-elite-bbc-master/262bfa61b69c3fb75ce2c004d4944bdc47c1abbd/5-compiled-game-discs/elite-master-sng47.ssd" },
};
// the journal's sha256 after 10 game minutes, from native run_sim (NAME=BITKICK, this skew): a visitor's machine checks itself
const REFERENCE = { flicker_13: "af1bfe517346f1266c2f84469cac0425c81cc2756b9617601126b9d215d7ac56", solid_13: "6cb32dd0b7d9c8762bf67d5d2c7a3e6a2179799f0ccf38a0d0fe20946bb375fa" };
const GAME = GAMES[Q.get("game")] ? Q.get("game") : "flicker";
const SKEW = Q.has("skew") ? +Q.get("skew") : GAMES[GAME].skew;
const CROP = { x: 128, y: 80, w: 512, h: 496 };      // the Elite screen in the core's 768x640 canvas
const HOLD = 150;                                    // adaptive: frames of real time after the action ends
const CHECK_FRAMES = +(Q.get("check") || 30000);     // the journal hashed up to here: 10 game minutes (?check=frames for further)

const s = {
    frame: 0, tally: 0, cash: 100, system: "Lave", tallyPts: [[0, 0]], log: [], visits: [[0, 20, 173]], view: "docked",
    senses: null, speedLabel: "", turbo: false, lastAction: -1e9, lastDock: 0, lastDepart: -1, started: false,
    journal: [], journalHash: null, elite: 0,
};
window.eliteAgent = s;

function layout() {
    const phone = innerWidth < 900 || innerHeight > innerWidth;
    document.body.classList.toggle("phone", phone);
    if (!phone) {
        const k = Math.min(innerWidth / 1920, innerHeight / 1080);
        $("stage").style.transform = `scale(${k})`;
        $("stage").style.left = `${(innerWidth - 1920 * k) / 2}px`;
        $("stage").style.top = `${(innerHeight - 1080 * k) / 2}px`;
    }
}
addEventListener("resize", layout);
layout();

const SPEEDS = [1, 2, 4, 8, Infinity];
const QS = Q.get("speed");
const speed = { sel: QS === "1x" ? 1 : Infinity, auto: QS !== "1x" && QS !== "turbo" };
const speedName = (n) => (n === Infinity ? "MAX" : `${n}×`);
function speedButtons() {
    for (const id of ["speedbtn", "p-speedbtn"]) { const b = $(id); if (b) b.textContent = `${speed.sel === 1 ? "▶" : "▶▶"} ${speedName(speed.sel)}`; }
    for (const id of ["auto", "p-auto"]) { const b = $(id); if (b) { b.classList.toggle("on", speed.auto); b.textContent = speed.auto ? "1× ACTION ●" : "1× ACTION ○"; b.title = "Real time in fights and docking, full speed between"; } }
}
for (const id of ["speedbtn", "p-speedbtn"]) { const b = $(id); if (b) b.onclick = () => { speed.sel = SPEEDS[(SPEEDS.indexOf(speed.sel) + 1) % SPEEDS.length]; speedButtons(); }; }
for (const id of ["auto", "p-auto"]) { const b = $(id); if (b) b.onclick = () => { speed.auto = !speed.auto; speedButtons(); }; }
speedButtons();

function loading(frac, text) {
    const card = $("loading");
    if (frac >= 1) { card.classList.remove("show"); return; }
    card.classList.add("show");
    $("lbar").style.width = `${Math.round(frac * 100)}%`;
    if (text) $("ltext").textContent = text;
}
function flash(t, ms = 4000) { const n = $("status"); n.textContent = t; n.classList.add("show"); if (ms) setTimeout(() => n.classList.remove("show"), ms); }

// ---- sound: the chip's samples, a frame at a time, only at real speed
const sound = { on: false, ctx: null, next: 0 };
function soundFrame(core) {
    if (!sound.on || !sound.ctx) return;
    const a = core.audio(), b = sound.ctx.createBuffer(1, 625, 31250), d = b.getChannelData(0);
    for (let i = 0; i < 625; i++) d[i] = a[i] / 32768;
    const src = sound.ctx.createBufferSource();
    src.buffer = b; src.connect(sound.ctx.destination);
    const now = sound.ctx.currentTime;
    if (sound.next < now || sound.next > now + 0.2) sound.next = now + 0.04;
    src.start(sound.next);
    sound.next += 0.02;
}

async function bytes(url, what) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${what}: ${r.status}`);
    return new Uint8Array(await r.arrayBuffer());
}
async function sha256(text) {
    const h = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
    return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function main() {
    for (const id of ["chapter", "load", "save", "p-chapter", "p-load", "p-save"]) { const e = $(id); if (e) e.style.display = "none"; }
    const by = `Emulated by <a href="https://github.com/kieranhj/esp32-beeb">Kieran Connell's Master core</a> (GPL-3.0) in WebAssembly · <a href="https://github.com/8bitkick/elite-agent/tree/kieran-wasm/web/dev">source</a>`;
    const elite = GAME === "solid" ? `Solid Elite by <a href="https://github.com/ebenupton/solid-elite-bbc-master">Eben Upton</a>, after Mark Moxon's source` : `flicker-free disc from <a href="https://github.com/markmoxon/elite-source-code-bbc-master">Mark Moxon</a>`;
    for (const id of ["credit", "p-credit"]) { const c = $(id); if (c) c.innerHTML = `<b>DEV PAGE</b> · Acornsoft Elite, BBC Master · Elite © Ian Bell and David Braben · ${elite}<br>${by}`; }
    for (const box of document.querySelectorAll(".games")) {
        box.innerHTML = "";
        for (const [k, g] of Object.entries(GAMES)) {
            const q = new URLSearchParams(location.search);
            if (k === "flicker") q.delete("game"); else q.set("game", k);
            q.delete("skew");
            const a = document.createElement("a");
            a.textContent = g.name; a.href = "?" + q.toString(); a.className = k === GAME ? "on" : "";
            box.appendChild(a);
        }
    }
    loading(0.05, "Loading the machine, its ROMs and Elite's disc…");
    const [panelData, galaxies, wasm, disc, ...roms] = await Promise.all([
        fetch(`../panel.json?v=${V}`).then((r) => r.json()), fetch(`../galaxies.json?v=${V}`).then((r) => r.json()),
        bytes(`core.wasm?v=${V}`, "the machine"), bytes(Q.get("disc") || GAMES[GAME].disc, "Elite's disc"),
        ...ROMS.map((n) => bytes(`${Q.get("roms") || ROM_BASE}${n}.rom`, `ROM ${n}`)),
    ]);
    const gh = new Image(); gh.src = "../gh.png";
    const panel = new Panel($("panel"), panelData, galaxies, gh);
    panelRef = panel;
    { const st = document.querySelector("#phone .strip"); if (st) st.style.display = "none"; }      // (the time is on the line under the rank bar)
    const tip = $("tip");
    $("panel").addEventListener("mousemove", (e) => {
        const c = $("panel"), r = c.getBoundingClientRect();
        const px = ((e.clientX - r.left) / r.width) * c.width + (1920 - c.width), py = ((e.clientY - r.top) / r.height) * c.height;
        const h = panel.hit(px, py);
        if (!h || !panel.net) { tip.classList.remove("show"); return; }
        const note = h.c === 0 ? INPUT_NOTE[panel.net][h.r] : COL_NOTE[h.title];
        const v = h.v === null || h.v === undefined ? "–" : (Math.abs(h.v) >= 100 ? h.v.toFixed(0) : h.v.toFixed(2));
        tip.innerHTML = `<b>${h.name}</b> <span>${h.title.toLowerCase()} · now ${v}</span>${note ? `<i>${note}</i>` : ""}`;
        tip.style.left = `${Math.min(innerWidth - 300, e.clientX + 14)}px`; tip.style.top = `${e.clientY + 16}px`;
        tip.classList.add("show");
    });
    $("panel").addEventListener("mouseleave", () => tip.classList.remove("show"));

    const core = await Core.create(wasm, roms, disc, {
        log: (level, line) => {
            if (level !== 0) return;
            const f = core.frameNo;
            s.log.push([f, line]); if (s.log.length > 200) s.log.shift();
            const m = line.match(/^(Docked at|Hyperspace to|Launched from) (.+)$/);
            if (m && m[1] === "Hyperspace to") s.arrive = [f + 600, m[2]];
            else if (m) { s.system = m[2]; s.arrive = null; }
            else if (line.startsWith("Mis-jump: trapped in witchspace")) { s.system = "Witchspace"; s.arrive = null; }
            if (m && m[1] === "Launched from") s.lastDepart = f;
        },
        note: (json) => {
            const f = core.frameNo;
            if (f < CHECK_FRAMES) s.journal.push(`{"frame": ${f}, "e": ${json}}\n`);
            let e; try { e = JSON.parse(json); } catch { return; }
            if (e.event === "docked") { s.lastDock = f; s.visits.push([e.g - 1, e.x, e.y]); }
            else if (e.event === "depart") s.lastDepart = f;
            else if (e.event === "rank" && e.rank === "Elite" && !s.elite) { s.elite = f; flash(`ELITE after ${(f / 180000).toFixed(2)} game hours`, 0); }
        },
    });
    loading(0.5, "Booting the BBC Master and loading Elite…");
    await new Promise((r) => setTimeout(r, 30));
    core.name = "BITKICK";
    if (!core.start(SKEW)) throw new Error("no Elite the player knows on this disc");
    s.started = true;
    loading(1);

    // ---- the screen
    const screen = $("screen"), sctx = screen.getContext("2d");
    screen.width = CROP.w; screen.height = CROP.h;
    const img = sctx.createImageData(CROP.w, CROP.h), out = new Uint32Array(img.data.buffer);
    const show = () => {
        const a = core.rgba(), W = core.width;
        for (let y = 0; y < CROP.h; y++) { const r = (CROP.y + y) * W + CROP.x; out.set(a.subarray(r, r + CROP.w), y * CROP.w); }
        sctx.putImageData(img, 0, 0);
        if (document.body.classList.contains("phone")) {
            const ps = $("p-screen"); if (ps.width !== CROP.w) { ps.width = CROP.w; ps.height = CROP.h; }
            ps.getContext("2d").drawImage(screen, 0, 0);
        }
    };

    // ---- the machine, from the page's animation frames: as many frames as this speed owes, at most TURBO_SHARE of the
    // time. The picture is drawn on the last two of them: the renderer needs one to find the beam after a gap
    const TURBO_SHARE = 0.8;
    let paused = false, last = performance.now(), owe = 0, lastUi = 0, checked = false;
    const frame = (draw) => { core.frame(draw); s.frameNo = core.frameNo; };
    const rate = () => (speed.auto && core.frameNo - s.lastAction < HOLD ? 1 : speed.sel);
    const run = (now) => {
        requestAnimationFrame(run);
        const dt = Math.min(100, now - last); last = now;
        if (paused) return;
        const r = rate(), t0 = performance.now(), budget = Math.min(40, Math.max(4, dt * TURBO_SHARE));
        s.turbo = r !== 1;
        if (r === Infinity) {
            let n = 0;
            do { for (let i = 0; i < 8; i++) frame(false); n += 8; note(); } while (performance.now() - t0 < budget - 2 && rate() === Infinity);
            frame(true); frame(true); note();
        } else {
            owe = Math.min(owe + (dt / 20) * r, 200);
            const n = Math.floor(owe);
            owe -= n;
            for (let i = 0; i < n; i++) {
                frame(i >= n - 2);
                if (r === 1) soundFrame(core);
                note();
                if (performance.now() - t0 > budget) { owe = 0; break; }
            }
        }
        show();
        if (now - lastUi > 50) { lastUi = now; ui(); }
    };
    // the action worth real time, each frame: the dogfight network deciding, or the docking network flying its final
    const note = () => {
        const f = core.frameNo, fw = core.x.wasm_fight_when(), dw = core.x.wasm_dock_when();
        if ((fw > 0 && f - fw < 30) || (core.action === 2 && dw > 0 && f - dw < 30)) s.lastAction = f;
        if (!checked && f >= CHECK_FRAMES) { checked = true; check(); }
    };
    const check = async () => {
        const h = await sha256(s.journal.join(""));
        s.journalHash = h; s.journal = null;
        const ref = CHECK_FRAMES === 30000 ? REFERENCE[`${GAME}_${SKEW}`] : null;
        const verdict = !ref ? "no reference for this game and skew" : h === ref ? "matches the reference: this machine flies the same career" : "DOESN'T MATCH the reference: please report it";
        console.log(`journal sha256 at frame ${CHECK_FRAMES}: ${h} (${verdict})`);
    };
    const ui = () => {
        const f = core.frameNo;
        if (s.arrive && f >= s.arrive[0]) { s.system = s.arrive[1]; s.arrive = null; }
        s.paused = paused;
        const now = performance.now();
        (s.rates ||= []).push([now, f]);
        while (s.rates.length > 2 && now - s.rates[0][0] > 2000) s.rates.shift();
        const [t0, f0] = s.rates[0];
        if (now - t0 > 500) s.rate = ((f - f0) / 50) / ((now - t0) / 1000);
        s.speedLabel = s.turbo ? `▶▶ ${(s.rate || 1).toFixed(1)}×` : "▶ 1×";
        // (at MAX, how fast MAX is on this machine)
        if (speed.sel === Infinity) for (const id of ["speedbtn", "p-speedbtn"]) { const b = $(id); if (b) b.textContent = `▶▶ MAX${s.turbo && s.rate ? ` ${Math.round(s.rate)}×` : ""}`; }
        s.frame = f; s.tally = core.tally; s.cash = core.cash;
        if (s.frame - s.tallyPts[s.tallyPts.length - 1][0] > 500) s.tallyPts.push([s.frame, s.tally]);
        const fight = core.senses("fight"), dock = core.senses("dock");
        if (s.lastDock > s.lastDepart) { s.view = "docked"; s.senses = null; }
        else if (fight.when > 0 && f - fight.when < 30) { s.view = "fight"; s.senses = fight.f; }
        else { s.view = "dock"; s.senses = dock.when > 0 && f - dock.when < 30 ? dock.f : null; }
        panel.draw(s);
        phoneStrip();
    };
    requestAnimationFrame(run);

    const toggleSound = () => {
        if (!sound.ctx) sound.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 31250 });
        sound.ctx.resume();
        sound.on = !sound.on;
        for (const id of ["sound", "p-sound"]) { $(id).classList.toggle("on", sound.on); $(id).textContent = sound.on ? "Sound on" : "🔊 Sound"; }
    };
    $("sound").onclick = toggleSound; $("p-sound").onclick = toggleSound;
    const pause = () => {
        paused = !paused;
        for (const id of ["pause", "p-pause"]) { $(id).textContent = paused ? (id === "pause" ? "▶ Resume" : "▶") : id === "pause" ? "❚❚ Pause" : "❚❚"; $(id).classList.toggle("on", paused); }
    };
    $("pause").onclick = pause; $("p-pause").onclick = pause;
}

let panelRef = null;
// the phone's galaxy chart, below the network: the stars, the route so far, the current star ringed and named
let mapKey = "";
function phoneMap() {
    if (!panelRef) return;
    const here = s.visits[s.visits.length - 1], gi = here[0], stars = panelRef.gal[gi], at = stars.find((t) => t[2] === s.system);
    const key = `${gi}/${s.visits.length}/${s.system}`;
    if (key === mapKey) return;
    mapKey = key;
    const c = $("p-map"), g = c.getContext("2d"), W = 908, TOP = 44, H = W + TOP, M = 14;     // (the map square)
    if (c.width !== W) { c.width = W; c.height = H; }
    g.fillStyle = "rgb(9,11,15)"; g.fillRect(0, 0, W, H);
    g.textBaseline = "middle"; g.textAlign = "left";
    g.font = "600 22px Inter, system-ui, sans-serif"; g.fillStyle = "rgb(110,116,128)"; g.fillText(`GALAXY ${gi + 1}`, 6, 22);
    const label = g.measureText(`GALAXY ${gi + 1}`).width;
    g.font = "600 28px Inter, system-ui, sans-serif"; g.fillStyle = "rgb(242,209,58)"; g.fillText(s.system, 6 + label + 16, 22);
    g.fillStyle = "rgb(5,7,9)"; g.fillRect(0, TOP, W, H - TOP);
    const P = (x, y) => [M + (x * (W - 2 * M)) / 256, TOP + M + (y * (H - TOP - 2 * M)) / 256];
    g.fillStyle = "rgb(60,66,78)";
    for (const [x, y] of stars) { const [px, py] = P(x, y); g.fillRect(px - 2, py - 2, 4, 4); }
    const mine = s.visits.filter((v) => v[0] === gi).map((v) => P(v[1], v[2]));
    g.strokeStyle = "rgb(27,110,80)"; g.lineWidth = 2; g.beginPath(); mine.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
    g.fillStyle = "rgb(27,175,122)"; for (const [x, y] of mine) { g.beginPath(); g.arc(x, y, 5, 0, 7); g.fill(); }
    if (at) {     // (not in witchspace, or a star of another galaxy between jumps)
        const [hx, hy] = P(at[0], at[1]);
        g.strokeStyle = "rgb(242,209,58)"; g.lineWidth = 3; g.beginPath(); g.arc(hx, hy, 12, 0, 7); g.stroke();
        g.font = "24px Inter, system-ui, sans-serif"; g.fillStyle = "rgb(242,209,58)";
        const right = hx > W - 180; g.textAlign = right ? "right" : "left"; g.fillText(at[2], hx + (right ? -18 : 18), hy);
    }
}
function phoneStrip() {
    if (!document.body.classList.contains("phone")) return;
    const secs = Math.floor(s.frame / 50), p2 = (n) => String(n).padStart(2, "0");
    $("p-time").textContent = `${p2(Math.floor(secs / 3600))}:${p2(Math.floor(secs / 60) % 60)}:${p2(secs % 60)}`;
    $("p-rank").textContent = RANKS.filter(([k]) => s.tally >= k).pop()[1];
    $("p-kills").textContent = Math.floor(s.tally).toLocaleString("en");
    $("p-cash").textContent = `${s.cash.toLocaleString("en", { maximumFractionDigits: 0 })} Cr`;
    $("p-system").textContent = s.system;
    $("p-log").textContent = s.log.slice(-3).map(([f, t]) => { const mm = Math.floor(f / 3000); return `${Math.floor(mm / 60)}:${String(mm % 60).padStart(2, "0")}  ${t}`; }).join("\n");
    for (const [id, y0, y1] of [["p-chart", 214, 300], ["p-net", 316, 972]]) {
        const c = $(id), g = c.getContext("2d");
        if (c.width !== 908) { c.width = 908; c.height = y1 - y0; }
        g.drawImage($("panel"), 0, y0, 908, y1 - y0, 0, 0, 908, y1 - y0);
    }
    phoneMap();
    // the line under the rank bar: the game's time in white, then the latest log line, larger than the panel's
    const g = $("p-chart").getContext("2d"), time = `${p2(Math.floor(secs / 3600))}:${p2(Math.floor(secs / 60) % 60)}:${p2(secs % 60)}`;
    const LINE_TOP = 44, LINE_MID = 67;
    g.fillStyle = "rgb(9,11,15)"; g.fillRect(0, LINE_TOP, 908, $("p-chart").height - LINE_TOP);
    g.textBaseline = "middle"; g.textAlign = "left";
    g.font = `600 30px ui-monospace, Menlo, Consolas, monospace`; g.fillStyle = "#fff"; g.fillText(time, 6, LINE_MID);
    const x = 6 + g.measureText(time).width + 18;
    if (panelRef && panelRef.line) {
        g.font = `22px ui-monospace, Menlo, Consolas, monospace`; g.fillStyle = panelRef.line.col;
        const room = Math.floor((908 - x - 6) / g.measureText("0").width), t = panelRef.line.text;
        g.fillText(t.length <= room ? t : t.slice(0, room - 1) + "…", x, LINE_MID);
    }
}
const RANKS = [[0, "Harmless"], [8, "Mostly Harmless"], [16, "Poor"], [32, "Average"], [64, "Above Average"], [128, "Competent"],
    [512, "Dangerous"], [2560, "Deadly"], [6400, "ELITE"]];

main().catch((e) => { console.error(e); flash(String(e), 0); });

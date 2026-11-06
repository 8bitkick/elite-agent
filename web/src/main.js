// The page: the panel, with the machine inside the WebAssembly: Kieran Connell's Master core (the Presto's and
// run_sim's) and the player in one binary (core_wasm.c), which flies exactly what the headless run_core.mjs and native
// run_sim fly. No start snapshot: the machine boots from power-on on every visit, in about a second.
import { Panel, INPUT_NOTE, COL_NOTE } from "./panel.js";
import { Core } from "../core.js";
import { addFile } from "./save.js";

const $ = (id) => document.getElementById(id);
const V = typeof ASSET_V === "string" ? ASSET_V : "dev";
const Q = new URLSearchParams(location.search);
const ROM_BASE = "https://raw.githubusercontent.com/scarybeasts/beebjit/db16aaf09df1fa0fd7ea686bc8d09b13f5674a42/roms/mos3.20/";
const ROMS = ["mos", "dfs", "viewsht", "edit", "basic4", "adfs", "view", "terminal"];
const GAMES = {
    // (both flown to Elite on this core.wasm, whole careers matching in a browser and in Node: web/STARTS.md)
    flicker: { name: "Wireframe",     // 16.45 game hours
        credit: "Flicker-free Elite by Mark Moxon, after Ian Bell and David Braben", skew: 13,
        disc: "https://raw.githubusercontent.com/markmoxon/elite-source-code-bbc-master/e6581b586b1c984142a0fee0c8009c7466a38d52/5-compiled-game-discs/elite-master-flicker-free-sng47.ssd" },
    solid: { name: "Solid",     // 18.34 game hours
        credit: "Solid Elite by Eben Upton, after Mark Moxon's source and Ian Bell and David Braben", skew: 13,
        disc: "https://raw.githubusercontent.com/ebenupton/solid-elite-bbc-master/262bfa61b69c3fb75ce2c004d4944bdc47c1abbd/5-compiled-game-discs/elite-master-sng47.ssd" },
};
// the journal's sha256 after 10 game minutes, from native run_sim (NAME=BITKICK, this skew): a visitor's machine checks itself
const REFERENCE = { flicker_13: "af1bfe517346f1266c2f84469cac0425c81cc2756b9617601126b9d215d7ac56", solid_13: "6cb32dd0b7d9c8762bf67d5d2c7a3e6a2179799f0ccf38a0d0fe20946bb375fa" };
const GAME = GAMES[Q.get("game")] ? Q.get("game") : "flicker";
// the mode (?mode=): Optimal (reads the game's RAM; its own interceptor network flies the fights), Fair (only what a player can
// see; the two AI pilot models), Curious (explores; the camera network flies the fights while the show is on)
const PMODES = { optimal: { n: 0, name: "Optimal" }, fair: { n: 1, name: "Fair" }, curious: { n: 2, name: "Curious" } };
const MODE = PMODES[Q.get("mode")] ? Q.get("mode") : "fair";
// the last save, kept in this browser: the commander block (the same for both games, at its own address in each) and the career's
// counters. A reload or a change of game carries on from it. ?fresh, ?skew or ?check: a fresh career, the save left alone
const COMMANDER = "BITKICK", SAVE_KEY = "elite-agent-save1", A_TP = { flicker: 0x1234, solid: 0x0C34 };
const KEEP = !(Q.has("fresh") || Q.has("skew") || Q.has("check"));
let saved = null;
try { saved = KEEP ? JSON.parse(localStorage.getItem(SAVE_KEY) || "null") : null; } catch {}
const OFF = saved ? saved.frame : 0;      // (the clock carries on)
const RESUMED = !!saved;
const SKEW = Q.has("skew") ? +Q.get("skew") : GAMES[GAME].skew;
const CROP = { x: 128, y: 80, w: 512, h: 496 };      // the Elite screen in the core's 768x640 canvas
const HOLD = 150;                                    // adaptive: frames of real time after the action ends
const AI_HOLD = 150;                                 // frames the network stays up after the AI last decided
const CHECK_FRAMES = +(Q.get("check") || 30000);     // the journal hashed up to here: 10 game minutes (?check=frames for further)

const s = {
    frame: 0, tally: 0, cash: 100, system: "Lave", tallyPts: [[0, 0]], log: [], visits: [[0, 20, 173]], view: "docked",
    senses: null, speedLabel: "", turbo: false, lastAction: -1e9, lastDock: 0, lastDepart: -1, started: false,
    journal: [], journalHash: null, elite: 0,
    kills: 0, stars: 0, docks: 0, deaths: 0,
};
if (saved) Object.assign(s, saved.career, { frame: OFF, tallyPts: [[OFF, saved.career.tally]] });
window.eliteAgent = s;

function layout() {
    // (a handset by its screen too: on its side it can be wider than 900, and its page may report wider still)
    const phone = innerWidth < 900 || innerHeight > innerWidth || Math.min(screen.width, screen.height) < 600;
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

// the speed: one button, tapped round. 1×, 2× and 4× hold that speed; ACTION is flat out, but 1× in fights and the docking's final
// (and for HOLD frames after) and 4× while docked; MAX is flat out, always
const DOCKED_RATE = 4;
const SPEED_BTNS = ["speedbtn", "p-speedbtn", "p-speedbtn2"];
const MODES = ["1×", "2×", "4×", "ACTION", "MAX"], FIXED = [1, 2, 4], ACTION = 3, MAX = 4;
const QS = Q.get("speed");     // (?speed=1x, 2x, 4x, action or max; the old ?speed=turbo is max)
const speed = { mode: QS === "max" || QS === "turbo" ? MAX : Math.max(0, ["1x", "2x", "4x"].indexOf(QS)) || (QS === "1x" ? 0 : ACTION) };
const setText = (b, t) => { if (b && b.textContent !== t) b.textContent = t; };      // (only when it changes)
// the buttons act when pressed, not on the click that follows it (a click alone, from a keyboard, still works)
function press(b, act) {
    if (!b) return;
    let pressed = false;
    b.addEventListener("pointerdown", (e) => { if (e.button === 0) { pressed = true; act(); } });
    b.addEventListener("click", () => { if (pressed) pressed = false; else act(); });
}
let refreshSpeed = () => {};      // (set once the machine is running)
for (const id of SPEED_BTNS) {
    press($(id), () => { speed.mode = (speed.mode + 1) % MODES.length; refreshSpeed(); });
    setText($(id), MODES[speed.mode]);
}

// the loading card: Elite's own Cobra Mk III (its ship blueprint's vertices and edges), turning in lines, and a bar
const COBRA_V = [[32,0,76],[-32,0,76],[0,26,24],[-120,-3,-8],[120,-3,-8],[-88,16,-40],[88,16,-40],[128,-8,-40],[-128,-8,-40],[0,26,-40],[-32,-24,-40],[32,-24,-40],[-36,8,-40],[-8,12,-40],[8,12,-40],[36,8,-40],[36,-12,-40],[8,-16,-40],[-8,-16,-40],[-36,-12,-40],[0,0,76],[0,0,90],[-80,-6,-40],[-80,6,-40],[-88,0,-40],[80,6,-40],[88,0,-40],[80,-6,-40]];
const COBRA_E = [[0,1],[0,4],[1,3],[3,8],[4,7],[6,7],[6,9],[5,9],[5,8],[2,5],[2,6],[3,5],[4,6],[1,2],[0,2],[8,10],[10,11],[7,11],[1,10],[0,11],[1,5],[0,6],[20,21],[12,13],[18,19],[14,15],[16,17],[15,16],[14,17],[13,18],[12,19],[2,9],[22,24],[23,24],[22,23],[25,26],[26,27],[25,27]];
let spin = null;
function loading(frac, text) {
    const card = $("loading");
    if (frac >= 1) { card.classList.remove("show"); spin = null; return; }
    card.classList.add("show");
    $("lbar").style.width = `${Math.round(frac * 100)}%`;
    if (text) $("ltext").textContent = text;
    if (spin) return;
    const c = $("cobra"), g = c.getContext("2d"), t0 = performance.now();
    const draw = (now) => {
        if (spin !== draw) return;
        const t = (now - t0) / 1000, a = t * 0.7, b = 0.35 + 0.25 * Math.sin(t * 0.4);
        const W = c.width, H = c.height, k = W / 330;
        g.clearRect(0, 0, W, H);
        const P = COBRA_V.map(([x, y, z]) => {
            const x1 = x * Math.cos(a) + z * Math.sin(a), z1 = -x * Math.sin(a) + z * Math.cos(a);
            const y2 = y * Math.cos(b) - z1 * Math.sin(b), z2 = y * Math.sin(b) + z1 * Math.cos(b);
            const p = 420 / (420 + z2);
            return [W / 2 + x1 * p * k, H / 2 - y2 * p * k];
        });
        g.strokeStyle = "rgb(56,214,232)"; g.lineWidth = 1.5 * k; g.beginPath();
        for (const [i, j] of COBRA_E) { g.moveTo(...P[i]); g.lineTo(...P[j]); }
        g.stroke();
        requestAnimationFrame(draw);
    };
    spin = draw; requestAnimationFrame(draw);
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
    const by = `Emulated by <a href="https://github.com/kieranhj/esp32-beeb">Kieran Connell's Master core</a> (GPL-3.0) in WebAssembly · <a href="https://github.com/8bitkick/elite-agent/tree/main/web">source</a>`;
    const elite = GAME === "solid" ? `Solid Elite by <a href="https://github.com/ebenupton/solid-elite-bbc-master">Eben Upton</a>, after Mark Moxon's source` : `flicker-free disc from <a href="https://github.com/markmoxon/elite-source-code-bbc-master">Mark Moxon</a>`;
    for (const id of ["credit", "p-credit"]) { const c = $(id); if (c) c.innerHTML = `<b>Acornsoft Elite</b>, BBC Master · Elite © Ian Bell and David Braben · ${elite}<br>${by}`; }
    const NOTE = {
        optimal: "Optimal mode: the agent reads the game's RAM. Its fights are flown by the interceptor, a network written for points a minute; the docking by the docking network.",
        fair: "Fair mode: the agent sees only what a player can see on the screen. Fights and docking are flown by two AI models, dogfights 8 KB, docking 1.5 KB, written after MIT CSAIL's Sparks of Weight-Space Programming.",
        curious: "Curious mode: the agent explores every galaxy. While it's on top, the camera network flies the fights for the viewer: close, drifting, a pass, the kill seen big; otherwise the interceptor.",
    };
    for (const id of ["netnote"]) { const n = $(id); if (n) n.textContent = NOTE[MODE]; }
    { const f = document.querySelector("#phone footer p"); if (f) f.textContent = NOTE[MODE]; }
    for (const box of document.querySelectorAll(".modes")) {
        box.innerHTML = "";
        for (const [k, m] of Object.entries(PMODES)) {
            const q = new URLSearchParams(location.search);
            if (k === "fair") q.delete("mode"); else q.set("mode", k);
            q.delete("skew");
            const a = document.createElement("a");
            a.textContent = m.name; a.href = "?" + q.toString(); a.className = k === MODE ? "on" : "";
            a.title = { optimal: "Reads the game's RAM; its own interceptor network flies the fights", fair: "Only what a player can see; two AI pilot models", curious: "Explores every galaxy; the camera network flies the fights for the viewer" }[k];
            box.appendChild(a);
        }
    }
    for (const box of document.querySelectorAll(".games")) {
        box.innerHTML = "";
        for (const [k, g] of Object.entries(GAMES)) {
            const q = new URLSearchParams(location.search);
            if (k === "flicker") q.delete("game"); else q.set("game", k);
            q.delete("skew");
            if (MODE !== "fair") q.set("mode", MODE);
            const a = document.createElement("a");
            a.textContent = box.closest("#p-nav") && k === "flicker" ? "Wire" : g.name; a.href = "?" + q.toString(); a.className = k === GAME ? "on" : "";
            box.appendChild(a);
        }
    }
    const shown = performance.now(), MIN_CARD_MS = 1000;      // (the card, and who made the game, for at least a second)
    const card = document.querySelector("#loading .card"), who = document.createElement("div");
    who.textContent = `${PMODES[MODE].name} mode · ${GAMES[GAME].credit}`;
    who.style.cssText = "color:var(--dim);font-size:14px;text-align:center;line-height:1.4";
    card.appendChild(who);
    loading(0.05, "Loading the machine, its ROMs and Elite's disc…");
    const [panelData, galaxies, wasm, disc, ...roms] = await Promise.all([
        fetch(`panel.json?v=${V}`).then((r) => r.json()), fetch(`galaxies.json?v=${V}`).then((r) => r.json()),
        bytes(`core.wasm?v=${V}`, "the machine"), bytes(Q.get("disc") || GAMES[GAME].disc, "Elite's disc"),
        ...ROMS.map((n) => bytes(`${Q.get("roms") || ROM_BASE}${n}.rom`, `ROM ${n}`)),
    ]);
    const gh = new Image(); gh.src = "gh.png";
    const panel = new Panel($("panel"), panelData, galaxies, gh);
    panelRef = panel;
    // the phone's controls: hidden at the top. A swipe down or a tap on them brings them in, a swipe up or a few
    // seconds without a touch takes them away; they slide in for a moment after loading
    {
        const nav = $("p-nav"); let timer = 0, y0 = 0, lastY = scrollY;
        const hide = () => nav.classList.remove("show");
        const show = (ms = 4500) => { nav.classList.add("show"); clearTimeout(timer); timer = setTimeout(hide, ms); };
        let touched = false;
        addEventListener("touchstart", (e) => { y0 = e.touches[0].clientY; touched = true; }, { passive: true });
        addEventListener("touchmove", (e) => { const d = e.touches[0].clientY - y0; if (d > 14) show(); else if (d < -14) hide(); }, { passive: true });
        // scrolling itself moves the controls only without a touch (a mouse wheel, a keyboard): an iPhone's bounce and momentum aren't
        // the finger's doing, so on a touch screen only the finger's swipe counts
        addEventListener("scroll", () => {
            const y = Math.max(0, Math.min(scrollY, document.documentElement.scrollHeight - innerHeight));
            if (!touched) { if (y < lastY - 6) show(); else if (y > lastY + 6) hide(); }
            lastY = y;
        }, { passive: true });
        nav.addEventListener("pointerdown", () => show(), { passive: true });
        $("p-chart").addEventListener("click", () => (nav.classList.contains("show") ? hide() : show()));
        showNav = show;
    }
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

    const keepSave = (f, e) => {
        const block = core.ram(A_TP[GAME], 256);
        saved = { frame: f + OFF, data: btoa(String.fromCharCode(...block)), game: GAME,
            career: { tally: e.tally, cash: e.cash, system: s.system, visits: s.visits, kills: s.kills, stars: s.stars, docks: s.docks, deaths: s.deaths } };
        try { localStorage.setItem(SAVE_KEY, JSON.stringify(saved)); } catch {}
    };
    const startDisc = RESUMED ? addFile(disc, COMMANDER, "E", Uint8Array.from(atob(saved.data), (c) => c.charCodeAt(0))) : disc;
    const core = await Core.create(wasm, roms, startDisc, {
        log: (level, line) => {
            if (level !== 0) return;
            const f = core.frameNo;
            s.log.push([f + OFF, line]); if (s.log.length > 200) s.log.shift();
            const m = line.match(/^(Docked at|Hyperspace to|Launched from) (.+)$/);
            if (m && m[1] === "Hyperspace to") s.arrive = [f + 600, m[2]];
            else if (m) { s.system = m[2]; s.arrive = null; }
            else if (line.startsWith("Mis-jump: trapped in witchspace")) { s.system = "Witchspace"; s.arrive = null; }
            if (m && m[1] === "Launched from") s.lastDepart = f;
        },
        note: (json) => {
            const f = core.frameNo;
            if (f < CHECK_FRAMES && s.journal) s.journal.push(`{"frame": ${f}, "e": ${json}}\n`);
            let e; try { e = JSON.parse(json); } catch { return; }
            if (e.event === "fight_end") s.kills += e.kills || 0;
            else if (e.event === "new_system") s.stars++;
            else if (e.event === "died" || e.event === "escape_pod") s.deaths++;
            if (e.event === "docked") { s.docks++; s.lastDock = f; s.visits.push([e.g - 1, e.x, e.y]); }
            else if (e.event === "depart") s.lastDepart = f;
            else if (e.event === "save" && KEEP && e.tally >= (saved ? saved.career.tally : 0)) keepSave(f, e);
            else if (e.event === "rank" && e.rank === "Elite" && !s.elite) { s.elite = f; flash(`ELITE after ${(f / 180000).toFixed(2)} game hours`, 0); }
        },
    });
    loading(0.5, "Booting the BBC Master and loading Elite…");
    await new Promise((r) => setTimeout(r, 30));
    core.name = COMMANDER;
    if (!core.start(SKEW, PMODES[MODE].n)) throw new Error("no Elite the player knows on this disc");
    if (RESUMED) core.load = COMMANDER;
    s.started = true;
    await new Promise((r) => setTimeout(r, Math.max(0, MIN_CARD_MS - (performance.now() - shown))));
    loading(1);
    setTimeout(() => showNav(5000), 400);      // (once the card has gone, the controls slide in, so people learn they're there)

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
    // TURBO_SHARE of each display frame, the drawing included (it's measured, and taken off the machine's time), never more
    // than a 60 Hz frame's: the rest is the browser's, for taps and scrolling. How it's sliced doesn't change the career
    const TURBO_SHARE = 0.8, FRAME_MS = 1000 / 60;
    const DRAW_MS = 30;
    let paused = false, last = performance.now(), owe = 0, lastUi = 0, lastDraw = 0, checked = false, drawCost = 4;
    const frame = (draw) => { core.frame(draw); s.frameNo = core.frameNo; };
    const inAction = () => speed.mode === ACTION && core.frameNo - s.lastAction < HOLD;
    // (ACTION, docked: 4×, so the shopping can be seen; docked as the AI window has it, from the dock until it sets off)
    const isDocked = () => s.lastDock > s.lastDepart;
    const rate = () => (inAction() ? 1 : speed.mode === ACTION && isDocked() ? DOCKED_RATE : FIXED[speed.mode] ?? Infinity);
    const run = (now) => {
        requestAnimationFrame(run);
        const dt = Math.min(100, now - last); last = now;
        if (paused) return;
        // (at speed, the picture about 30 times a second is plenty: the other display frames are all the machine's)
        const r = rate(), t0 = performance.now(), draw = r <= 2 || now - lastDraw >= DRAW_MS, budget = Math.max(1, Math.min(dt, FRAME_MS) * TURBO_SHARE - (draw ? drawCost : 0));
        if (draw) lastDraw = now;
        s.turbo = r !== 1;
        let d0 = t0;
        if (r === Infinity) {
            let n = 0;
            do { for (let i = 0; i < 4; i++) frame(false); n += 4; note(); } while (performance.now() - t0 < budget && rate() === Infinity);
            d0 = performance.now();
            if (draw) { frame(true); frame(true); note(); }
        } else {
            owe = Math.min(owe + (dt / 20) * r, 200);
            const n = Math.floor(owe);
            owe -= n;
            for (let i = 0; i < n; i++) {
                frame(draw && i >= n - 2);
                if (r === 1) soundFrame(core);
                note();
                // (over budget: stop for this display frame. Real time has first call on the CPU, and what's owed is kept, up to
                // a little backlog, so a slow frame is made up for, not lost; at higher speeds the backlog is dropped)
                if (performance.now() - t0 > (r === 1 ? Math.max(budget, FRAME_MS) : budget)) { owe = Math.min(owe + n - i - 1, r === 1 ? 4 : 1); break; }
            }
        }
        if (r !== Infinity) d0 = performance.now();
        if (draw) show();
        if (now - lastUi > 100) { lastUi = now; ui(); }
        if (draw) drawCost += (performance.now() - d0 - drawCost) * 0.1;      // (smoothed: the panel is redrawn only every few frames)
    };
    // the action worth real time, each frame: the dogfight network deciding, or the docking network flying its final
    const note = () => {
        const f = core.frameNo, fw = core.x.wasm_fight_when(), dw = core.x.wasm_dock_when();
        if (fw > 0 && f - fw < 30) { s.lastAction = f; s.actionKind = "fight"; }
        else if (core.action === 2 && dw > 0 && f - dw < 30) { s.lastAction = f; s.actionKind = "dock"; }
        if (!checked && !RESUMED && f >= CHECK_FRAMES) { checked = true; check(); }
    };
    const check = async () => {
        const h = await sha256(s.journal.join(""));
        s.journalHash = h; s.journal = null;
        const ref = CHECK_FRAMES === 30000 && MODE === "fair" ? REFERENCE[`${GAME}_${SKEW}`] : null;
        const verdict = !ref ? "no reference for this game and skew" : h === ref ? "matches the reference: this machine flies the same career" : "DOESN'T MATCH the reference: please report it";
        console.log(`journal sha256 at frame ${CHECK_FRAMES}: ${h} (${verdict})`);
    };
    const showSpeed = () => {
        const hot = inAction(), col = s.actionKind === "fight" ? "rgb(242,209,58)" : "rgb(56,214,232)";     // (lit: ACTION in force, in the colour of the AI flying)
        s.actionHot = hot ? s.actionKind : null;
        for (const id of SPEED_BTNS) {
            const b = $(id); if (!b) continue;
            setText(b, MODES[speed.mode]);
            const v = hot ? col : "";
            if (b.style.background !== v) { b.style.background = v; b.style.borderColor = v; b.style.color = hot ? "#000" : ""; }
        }
    };
    refreshSpeed = showSpeed;
    const ui = () => {
        const f = core.frameNo;
        if (s.arrive && f >= s.arrive[0]) { s.system = s.arrive[1]; s.arrive = null; }
        s.paused = paused;
        const now = performance.now();
        (s.rates ||= []).push([now, f]);
        while (s.rates.length > 2 && now - s.rates[0][0] > 2000) s.rates.shift();
        const [t0, f0] = s.rates[0];
        if (now - t0 > 500) s.rate = ((f - f0) / 50) / ((now - t0) / 1000);
        else if (s.turbo) s.rate = 0;      // (too soon after the action to say)
        s.speedLabel = s.turbo ? `▶▶ ${(s.rate || 1).toFixed(1)}×` : `▶ ${s.rate && s.rate < 0.95 ? s.rate.toFixed(1) : 1}×`;      // (never claims a 1× it isn't reaching)
        // 1× ACTION kicking in: its button lit in the colour of the AI flying (and for the seconds after, while it's still at
        // 1×), the speed button saying so. At MAX otherwise, how fast MAX is on this machine
        showSpeed();
        s.frame = f + OFF; s.tally = core.tally; s.cash = core.cash; s.fightNet = core.fightNet;
        if (s.frame - s.tallyPts[s.tallyPts.length - 1][0] > 500) s.tallyPts.push([s.frame, s.tally]);
        const fight = core.senses("fight"), dock = core.senses("dock");
        if (s.lastDock > s.lastDepart) { s.view = "docked"; s.senses = null; }
        else if (fight.when > 0 && f - fight.when < 30) { s.view = "fight"; s.senses = fight.f; s.lastAi = f; }
        else if (dock.when > 0 && f - dock.when < 30) { s.view = "dock"; s.senses = dock.f; s.lastAi = f; }
        else if (f - (s.lastAi ?? -1e9) < AI_HOLD) { s.view = "dock"; s.senses = null; }     // (a moment's gap in the AI: the network stays)
        else { s.view = "log"; s.senses = null; }
        panel.draw(s);
        phoneStrip();
    };
    requestAnimationFrame(run);

    // start again: the save forgotten, Jameson at Lave
    const reset = () => {
        if (!confirm("Start again from Jameson at Lave? This career's save will be lost.")) return;
        try { localStorage.removeItem(SAVE_KEY); } catch {}
        const q = new URLSearchParams(); if (GAME !== "flicker") q.set("game", GAME); if (MODE !== "fair") q.set("mode", MODE);
        location.search = q.toString() ? "?" + q.toString() : "";
    };
    for (const id of ["reset", "p-reset"]) { const b = $(id); if (b) b.onclick = reset; }
    // jump to Deadly: the film career's save (chapters/deadly.cmdr: Orqulela, 2,598 kills, 18,147 Cr, 10.6 game hours in),
    // kept as this browser's save, and the career carried on from it in the game and mode chosen
    const deadly = async () => {
        if (!confirm("Jump to Deadly rank? This career's save will be replaced by the film career's save at Deadly.")) return;
        const r = await fetch("chapters/deadly.cmdr"); if (!r.ok) { alert("the Deadly save didn't load"); return; }
        const block = new Uint8Array(await r.arrayBuffer());
        const jump = { frame: 1901411, data: btoa(String.fromCharCode(...block)), game: GAME,
            career: { tally: 2598.05, cash: 18147.3, system: "Orqulela", visits: [[2, 77, 242]], kills: 2598, stars: 0, docks: 0, deaths: 0 } };
        try { localStorage.setItem(SAVE_KEY, JSON.stringify(jump)); } catch { alert("this browser keeps no save"); return; }
        const q = new URLSearchParams(); if (GAME !== "flicker") q.set("game", GAME); if (MODE !== "fair") q.set("mode", MODE);
        const to = q.toString() ? "?" + q.toString() : ""; if (location.search === to) location.reload(); else location.search = to;
    };
    for (const id of ["deadly", "p-deadly"]) { const b = $(id); if (b) b.onclick = deadly; }
    const toggleSound = () => {
        if (!sound.ctx) sound.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 31250 });
        sound.ctx.resume();
        sound.on = !sound.on;
        $("sound").textContent = sound.on ? "Sound on" : "🔊 Sound";
        setText($("p-sound2"), sound.on ? "On" : "Off");
        for (const id of ["sound", "p-sound2"]) $(id).classList.toggle("on", sound.on);
    };
    press($("sound"), toggleSound); press($("p-sound2"), toggleSound);
    const pause = () => {
        paused = !paused;
        for (const id of ["pause", "p-pause"]) { $(id).textContent = paused ? (id === "pause" ? "▶ Resume" : "▶") : id === "pause" ? "❚❚ Pause" : "❚❚"; $(id).classList.toggle("on", paused); }
    };
    press($("pause"), pause); press($("p-pause"), pause);
}

let panelRef = null;
let showNav = () => {};
const paused2 = () => s.paused;
function phoneStrip() {
    if (!document.body.classList.contains("phone")) return;
    const secs = Math.floor(s.frame / 50), p2 = (n) => String(n).padStart(2, "0");
        const set = (id, v) => setText($(id), String(v));
    set("p-rank", RANKS.filter(([k]) => s.tally >= k).pop()[1]);
    set("p-tally", Math.floor(s.tally).toLocaleString("en"));
    set("p-cash", `${s.cash.toLocaleString("en", { maximumFractionDigits: 0 })} Cr`);
    set("p-system", s.system);
    set("p-galaxy", s.visits[s.visits.length - 1][0] + 1);
    set("p-stars", s.stars); set("p-kills", s.kills.toLocaleString("en")); set("p-docks", s.docks); set("p-deaths", s.deaths);
    for (const [id, y0, y1] of [["p-chart", 214, 300], ["p-net", 316, 972]]) {
        const c = $(id), g = c.getContext("2d");
        if (c.width !== 908) { c.width = 908; c.height = y1 - y0; }
        g.drawImage($("panel"), 0, y0, 908, y1 - y0, 0, 0, 908, y1 - y0);
    }
    // the line under the rank bar: the game's time in white, then the latest log line, larger than the panel's
    const g = $("p-chart").getContext("2d"), time = `${p2(Math.floor(secs / 3600))}:${p2(Math.floor(secs / 60) % 60)}:${p2(secs % 60)}`;
    const LINE_TOP = 44, LINE_MID = 67;
    g.fillStyle = "rgb(9,11,15)"; g.fillRect(0, LINE_TOP, 908, $("p-chart").height - LINE_TOP);
    g.textBaseline = "middle"; g.textAlign = "left";
    g.font = `600 30px ui-monospace, Menlo, Consolas, monospace`; g.fillStyle = "#fff"; g.fillText(time, 6, LINE_MID);
    g.font = `600 30px ui-monospace, Menlo, Consolas, monospace`;
    // the speed, beside it: lit in the colour of the AI flying while 1× ACTION is in force
    const spd = s.rate ? `${s.rate < 9.95 ? s.rate.toFixed(1) : Math.round(s.rate)}×` : "";      // (measured: what it's really doing)
    const tx = 6 + g.measureText(time).width + 14;
    g.font = `600 24px ui-monospace, Menlo, Consolas, monospace`; g.fillStyle = s.actionHot === "fight" ? "rgb(242,209,58)" : s.actionHot ? "rgb(56,214,232)" : "rgb(154,161,174)";
    g.fillText(paused2() ? "❚❚" : spd, tx, LINE_MID);
    const x = tx + Math.max(g.measureText("100×").width, g.measureText(spd).width) + 14;
    if (panelRef && panelRef.line) {
        g.font = `22px ui-monospace, Menlo, Consolas, monospace`; g.fillStyle = panelRef.line.col;
        const room = Math.floor((908 - x - 6) / g.measureText("0").width), t = panelRef.line.text;
        g.fillText(t.length <= room ? t : t.slice(0, room - 1) + "…", x, LINE_MID);
    }
}
const RANKS = [[0, "Harmless"], [8, "Mostly Harmless"], [16, "Poor"], [32, "Average"], [64, "Above Average"], [128, "Competent"],
    [512, "Dangerous"], [2560, "Deadly"], [6400, "ELITE"]];

main().catch((e) => { console.error(e); flash(String(e), 0); });

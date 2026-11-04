// The page: Acornsoft Elite on a BBC Master (jsbeeb, in a same-origin iframe), started from a snapshot of its title
// screen, flown by the elite-agent's C player as WebAssembly (fair mode: the two AI pilot models), beside the film's panel.
import { snapshotFromJSON, snapshotToJSON, createSnapshot } from "jsbeeb-src/snapshot.js";
import { toSsdOrDsd } from "jsbeeb-src/disc.js";
import { Agent } from "../agent.js";
import { Panel, INPUT_NOTE, COL_NOTE } from "./panel.js";
import { commanderSsd, addFile } from "./save.js";
import { discFor } from "jsbeeb-src/fdc.js";

const $ = (id) => document.getElementById(id);
const COMMANDER = "BITKICK";
// Elite comes from Mark Moxon's source repository on GitHub (his flicker-free Master Elite, the disc his site has, at a
// pinned commit: GitHub serves it to any page, his site to his own); this site serves none of it.
// The title screen's snapshot is made in the visitor's browser on the first visit and kept there.
const Q = new URLSearchParams(location.search);
const DISC = Q.get("disc") || "https://raw.githubusercontent.com/markmoxon/elite-source-code-bbc-master/e6581b586b1c984142a0fee0c8009c7466a38d52/5-compiled-game-discs/elite-master-flicker-free-sng47.ssd";
// the start: the machine's state START_M million cycles after it's switched on (?start= to try another; the shipped one
// is the fastest of those tried headless to Elite)
const START_M = +(Q.get("start") || 30);
const CACHE = `elite-agent-start2:${START_M}:${typeof JSBEEB_COMMIT === "string" ? JSBEEB_COMMIT : "dev"}:${DISC}`;
const CROP = { x: 264, y: 80, w: 512, h: 496 };      // the Elite screen in jsbeeb's 1024x625 frame
const HOLD = 150;                                    // adaptive: frames of real time after the action ends
// chapters: a later save of the film's career (Fair mode, seed 13), loaded at the first docking; the clock and the chart
// carry on from where it was saved
const CHAPTERS = {
    deadly: { file: "chapters/deadly.cmdr", frame: 1901411, tally: 2598.05, cash: 18147.3, system: "Orqulela", visit: [2, 77, 242] },
};
const CHAPTER = CHAPTERS[Q.get("chapter")] ? Q.get("chapter") : null, CH = CHAPTER ? CHAPTERS[CHAPTER] : null;
const OFF = CH ? CH.frame : 0;

const s = {
    frame: 0, tally: 0, cash: 100, system: "Lave", tallyPts: [[0, 0]], log: [], visits: [[0, 20, 173]], view: "docked",
    senses: null, speedLabel: "", mode: "adaptive", turbo: false, lastAction: -1e9, lastDock: 0, lastDepart: -1, started: false,
};
if (CH) Object.assign(s, { frame: OFF, tally: CH.tally, cash: CH.cash, system: CH.system, tallyPts: [[OFF, CH.tally]], visits: [CH.visit] });
window.eliteAgent = s;

// ---- the stage: the film's 1920x1080 frame, scaled to the window; a phone gets its own column
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

// ---- the speed: 1x, adaptive (turbo except in a fight or the docking's final) or turbo. 1x is jsbeeb's own loop, sound
// and all; turbo is ours: jsbeeb's loop paused and the machine run from the page's animation frames for TURBO_SHARE of
// each, so the rest of the CPU is left to the browser (jsbeeb's own turbo takes all of it, ahead of the page)
function setMode(m) {
    s.mode = m;
    document.querySelectorAll("[data-mode]").forEach((b) => b.classList.toggle("on", b.dataset.mode === m));
}
document.querySelectorAll("[data-mode]").forEach((b) => (b.onclick = () => setMode(b.dataset.mode)));
setMode(["1x", "adaptive", "turbo"].includes(new URLSearchParams(location.search).get("speed")) ? new URLSearchParams(location.search).get("speed") : "adaptive");

const TURBO_SHARE = 0.8;
const turbo = { on: false, pending: null };
function setTurbo(w, cpu, on) {
    if (on === turbo.on || turbo.pending !== null) return;
    turbo.pending = on;
    setTimeout(() => {                       // (not from inside a frame jsbeeb is running)
        turbo.pending = null;
        if (on === turbo.on) return;
        turbo.on = on;
        if (!on) { w.go(); return; }
        w.stop(false);
        let last = performance.now();
        const run = (now) => {
            if (!turbo.on) return;
            const budget = Math.min(40, Math.max(4, (now - last) * TURBO_SHARE)), t0 = performance.now();
            last = now;
            while (turbo.on && performance.now() - t0 < budget) cpu.execute(40000);    // (a frame's cycles at a time)
            requestAnimationFrame(run);
        };
        requestAnimationFrame(run);
    }, 0);
}
function altT(w) {
    for (const type of ["keydown", "keyup"]) w.document.dispatchEvent(new w.KeyboardEvent(type, { code: "KeyT", key: "t", altKey: true, bubbles: true }));
}

// ---- the game's screen: jsbeeb's frame, cropped to the Elite screen, copied at its end-of-frame paint
const screen = $("screen"), sctx = screen.getContext("2d");
screen.width = CROP.w; screen.height = CROP.h;
const img = sctx.createImageData(CROP.w, CROP.h), px = new Uint32Array(img.data.buffer);
function grab(fb32) {
    for (let y = 0; y < CROP.h; y++) {
        const row = (CROP.y + y) * 1024 + CROP.x;
        for (let x = 0; x < CROP.w; x++) px[y * CROP.w + x] = fb32[row + x] | 0xff000000;
    }
}

function cachedTitle() { try { return Q.has("fresh") ? null : localStorage.getItem(CACHE); } catch { return null; } }

async function main() {
    const title = cachedTitle();
    // jsbeeb: the Master with Elite in drive 0; booted on a first visit, or restored to its title screen after
    const frame = $("beeb");
    frame.src = `jsbeeb/index.html?embed&model=Master&disc1=${encodeURIComponent(DISC)}${title ? "" : "&autoboot"}`;
    loading(0.02, title ? "Loading the agent…" : "Loading the agent and Elite's disc from Mark Moxon's GitHub…");
    const [panelData, galaxies, wasm, cmdr] = await Promise.all([
        fetch("panel.json").then((r) => r.json()), fetch("galaxies.json").then((r) => r.json()), fetch("player.wasm").then((r) => r.arrayBuffer()),
        CH ? fetch(CH.file).then((r) => r.arrayBuffer()) : null,
    ]);
    const gh = new Image(); gh.src = "gh.png";
    const panel = new Panel($("panel"), panelData, galaxies, gh);
    // hover notes on the network's units: what it is and its value now
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
    const w = frame.contentWindow;
    for (let t = 0; !w.processor || !w.video || !w.processor.fdc?.drives?.[0]?.disc; t++) {
        loading(Math.min(0.3, 0.1 + t / 150), t >= 300 ? "Still waiting for Elite's disc from GitHub…" : title ? "Loading the agent…" : "Loading Elite's disc from Mark Moxon's GitHub…");
        await new Promise((r) => setTimeout(r, 100));
    }
    const cpu = w.processor, video = w.video;
    if (title) {
        try { cpu.restoreState(snapshotFromJSON(title).state); } catch (e) { try { localStorage.removeItem(CACHE); } catch {} location.reload(); return; }
    }

    // the agent: the C player, its log lines and journal events kept for the panel
    const agent = await Agent.create(wasm, cpu, cpu.sysvia, {
        log: (level, line) => {
            if (level !== 0) return;
            const f = agent.frameNo;
            s.log.push([f + OFF, line]); if (s.log.length > 200) s.log.shift();
            const m = line.match(/^(Docked at|Hyperspace to|Launched from) (.+)$/);
            if (m && m[1] === "Hyperspace to") s.arrive = [f + 600, m[2]];      // (the jump: ~12 s of countdown)
            else if (m) { s.system = m[2]; s.arrive = null; }
            else if (line.startsWith("Mis-jump: trapped in witchspace")) { s.system = "Witchspace"; s.arrive = null; }
            if (m && m[1] === "Launched from") s.lastDepart = f;
        },
        note: (json) => {
            let e; try { e = JSON.parse(json); } catch { return; }
            if (e.event === "docked") { s.lastDock = agent.frameNo; s.visits.push([e.g - 1, e.x, e.y]); }
            else if (e.event === "depart") s.lastDepart = agent.frameNo;
        },
    });

    // before the agent starts: the machine at Elite's title screen, the same for every visitor. The start is the
    // machine's state at START_CYCLES of emulated time after it was switched on (Elite loaded, the Cobra turning: it's
    // up by ~17 M), caught at that frame's end; it's kept in the browser, and every visit, the first too, restores it
    // and starts the agent at the next frame's end. So every visitor watches the same career, the one tested to Elite
    // headless (web/run_node.mjs START=, from this snapshot)
    const START_CYCLES = START_M * 1e6;
    const cycles = () => cpu.cycleSeconds * 2e6 + cpu.currentCycles;
    let bootF = 0, ready = !!title, pending = false;
    function booting() {
        bootF++;
        if (pending) return;
        if (!ready) {
            const c = cycles();
            if (bootF % 10 === 0) loading(0.3 + 0.7 * Math.min(1, c / START_CYCLES), c < 17e6 ? "Booting the BBC Master and loading Elite…" : "Elite is up: the Cobra turning, the agent at the controls…");
            if (c < START_CYCLES) return;
            const json = snapshotToJSON(createSnapshot(cpu, cpu.model, {}));
            try { localStorage.setItem(CACHE, json); } catch (e) { console.warn("no title snapshot kept", e); }
            pending = true;
            setTimeout(() => { cpu.restoreState(snapshotFromJSON(json).state); pending = false; ready = true; }, 0);
            return;
        }
        if (!agent.start(1)) return;          // (Elite up: always, at START_CYCLES)
        if (CH) {
            // the chapter's save, on the game's disc as E.BITKICK, for the player to load when it first docks (the start)
            const img = addFile(toSsdOrDsd(cpu.fdc.drives[0].disc, { force: true }), COMMANDER, "E", new Uint8Array(cmdr));
            cpu.fdc.loadDisc(0, discFor("elite.ssd", img));
        }
        agent.start(1);                       // (again: after the chapter's disc)
        agent.name = COMMANDER;
        if (CH) agent.load = COMMANDER;
        s.started = true;
        loading(1, "");
    }

    // the agent, once a frame, at jsbeeb's end-of-frame paint (the native player's place: between frames). The page's own
    // updates run from here too, by the clock: jsbeeb's turbo runs its ticks as user-blocking tasks, ahead of the page's
    // animation frames
    let lastUi = 0;
    const paint = video.paint_ext;
    video.paint_ext = function (...args) {
        if (!s.started) {
            booting();
            if (performance.now() - lastUi > 100) { lastUi = performance.now(); setTurbo(w, cpu, true); grab(this.fb32); s.shot = true; }
            return paint.apply(this, args);
        }
        agent.frame();
        // the action worth real time: the dogfight network deciding, or the docking network flying its final
        const f = agent.frameNo;
        if (f - agent.x.wasm_fight_when() < 30 || (agent.action === 2 && f - agent.x.wasm_dock_when() < 30)) s.lastAction = f;
        const now = performance.now();
        if (now - lastUi > 50) { lastUi = now; grab(this.fb32); s.shot = true; ui(); }
        return paint.apply(this, args);
    };
    const ui = () => {
        // the speed jsbeeb should run at; a toggle is given 20 frames to show before another
        const f = agent.frameNo;
        if (s.arrive && f >= s.arrive[0]) { s.system = s.arrive[1]; s.arrive = null; }
        const want = !s.started || s.mode === "turbo" || (s.mode === "adaptive" && f - s.lastAction >= HOLD);
        setTurbo(w, cpu, want);
        s.turbo = turbo.on;
        // the speed the game is running at, over the last 2 s: game frames (50 a second) a second of the page's clock
        const now = performance.now();
        (s.rates ||= []).push([now, f]);
        while (s.rates.length > 2 && now - s.rates[0][0] > 2000) s.rates.shift();
        const [t0, f0] = s.rates[0];
        if (now - t0 > 500) s.rate = ((f - f0) / 50) / ((now - t0) / 1000);
        s.speedLabel = s.turbo ? `▶▶ ${(s.rate || 1).toFixed(1)}×` : "▶ 1×";
        if (s.shot) {
            sctx.putImageData(img, 0, 0); s.shot = false;
            if (document.body.classList.contains("phone")) {
                const ps = $("p-screen"); if (ps.width !== CROP.w) { ps.width = CROP.w; ps.height = CROP.h; }
                ps.getContext("2d").drawImage(screen, 0, 0);
            }
        }
        if (!s.started) return;
        s.frame = f + OFF; s.tally = agent.tally; s.cash = agent.cash;
        if (s.frame - s.tallyPts[s.tallyPts.length - 1][0] > 500) s.tallyPts.push([s.frame, s.tally]);
        const fight = agent.senses("fight"), dock = agent.senses("dock");
        if (s.lastDock > s.lastDepart) { s.view = "docked"; s.senses = null; }
        else if (f - fight.when < 30) { s.view = "fight"; s.senses = fight.f; }
        else { s.view = "dock"; s.senses = f - dock.when < 30 ? dock.f : null; }
        panel.draw(s);
        phoneStrip();
        if (window.EA_DEBUG && f - (s.dbgAt || 0) >= 100) { s.dbgAt = f; console.log("EA " + JSON.stringify({ f, turbo: s.turbo, mode: s.mode, view: s.view, tag: s.tag, tally: s.tally, t: Math.round(performance.now()) })); }
    };
    // (and on the page's own frames, when jsbeeb is paused or between its frames)
    const tick = (now) => { if (now - lastUi > 200) { lastUi = now; ui(); } requestAnimationFrame(tick); };
    requestAnimationFrame(tick);

    // the commander, saved: an .ssd holding only the commander's file, from the disc as the game has written it
    $("save").onclick = () => {
        let out;
        try { out = commanderSsd(toSsdOrDsd(cpu.fdc.drives[0].disc, { force: true }), COMMANDER); } catch (e) { out = null; }
        if (!out) { flash("No save yet: the commander is saved at the first docking."); return; }
        const a = document.createElement("a");
        a.href = URL.createObjectURL(new Blob([out], { type: "application/octet-stream" }));
        a.download = `${COMMANDER.toLowerCase()}.ssd`; a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    };
    // sound: jsbeeb resumes its audio on a key in its page; Alt-T twice (turbo there and back) is one that types nothing
    const sound = () => { altT(w); altT(w); for (const id of ["sound", "p-sound"]) { $(id).classList.add("on"); $(id).textContent = "Sound on"; } };
    $("sound").onclick = sound; $("p-sound").onclick = sound;
    $("p-save").onclick = () => $("save").onclick();
    // the chapters: from the start (Jameson at Lave) or from a save at Deadly
    for (const id of ["chapter", "p-chapter"]) {
        const a = $(id), q = new URLSearchParams(location.search);
        const short = id === "p-chapter";
        if (CH) { q.delete("chapter"); a.textContent = short ? "From Jameson" : "Start again from Jameson"; }
        else { q.set("chapter", "deadly"); a.textContent = short ? "Jump to Deadly" : "Jump to Deadly (10.6 h in)"; }
        a.href = "?" + q.toString();
    }
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

function flash(t) { const n = $("status"); n.textContent = t; n.classList.add("show"); setTimeout(() => n.classList.remove("show"), 4000); }

// the phone's top bar: the panel's numbers as a strip
function phoneStrip() {
    if (!document.body.classList.contains("phone")) return;
    const secs = Math.floor(s.frame / 50), p2 = (n) => String(n).padStart(2, "0");
    $("p-time").textContent = `${p2(Math.floor(secs / 3600))}:${p2(Math.floor(secs / 60) % 60)}:${p2(secs % 60)}`;
    $("p-rank").textContent = rankName(s.tally);
    $("p-kills").textContent = Math.floor(s.tally).toLocaleString("en");
    $("p-cash").textContent = `${s.cash.toLocaleString("en", { maximumFractionDigits: 0 })} Cr`;
    $("p-system").textContent = s.system;
    // the speed, in the phone's highlighted speed button: Auto shows what it's running at
    const pb = document.querySelector("#phone [data-mode].on");
    if (pb) {
        const lab = s.turbo ? `▶▶ ${(s.rate || 1).toFixed(1)}×` : "▶ 1×";
        const name = { "1x": "", adaptive: "Auto ", turbo: "" }[s.mode];
        if (pb.textContent !== name + lab) pb.textContent = name + lab;
    }
    document.querySelectorAll("#phone [data-mode]:not(.on)").forEach((b) => {
        const t = { "1x": "▶ 1×", adaptive: "Auto", turbo: "▶▶ Turbo" }[b.dataset.mode];
        if (b.textContent !== t) b.textContent = t;
    });
    $("p-note").textContent = s.view === "dock" ? s.note || "" : "";
    $("p-log").textContent = s.log.slice(-3).map(([f, t]) => { const mm = Math.floor(f / 3000); return `${Math.floor(mm / 60)}:${String(mm % 60).padStart(2, "0")}  ${t}`; }).join("\n");
    // the chart and the network, cropped from the panel
    for (const [id, y0, y1] of [["p-chart", 200, 300], ["p-net", 316, 946]]) {
        const c = $(id), g = c.getContext("2d");
        if (c.width !== 908) { c.width = 908; c.height = y1 - y0; }
        g.drawImage($("panel"), 0, y0, 908, y1 - y0, 0, 0, 908, y1 - y0);
    }
}
const RANKS = [[0, "Harmless"], [8, "Mostly Harmless"], [16, "Poor"], [32, "Average"], [64, "Above Average"], [128, "Competent"],
    [512, "Dangerous"], [2560, "Deadly"], [6400, "ELITE"]];
const rankName = (t) => RANKS.filter(([k]) => t >= k).pop()[1];

main().catch((e) => { console.error(e); flash(String(e)); });

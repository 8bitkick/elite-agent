// The page: Acornsoft Elite on a BBC Master (jsbeeb, in a same-origin iframe), started from a snapshot of its title
// screen, flown by the elite-agent's C player as WebAssembly (fair mode: the two AI pilot models), beside the film's panel.
import { snapshotFromJSON, snapshotToJSON, createSnapshot } from "jsbeeb-src/snapshot.js";
import { toSsdOrDsd } from "jsbeeb-src/disc.js";
import { Agent } from "../agent.js";
import { Panel } from "./panel.js";
import { commanderSsd, addFile } from "./save.js";
import { discFor } from "jsbeeb-src/fdc.js";

const $ = (id) => document.getElementById(id);
const COMMANDER = "BITKICK";
// Elite comes from Mark Moxon's site, as his own "Play online" link loads it into jsbeeb; this site serves none of it.
// The title screen's snapshot is made in the visitor's browser on the first visit and kept there.
const Q = new URLSearchParams(location.search);
const DISC = Q.get("disc") || "https://elite.bbcelite.com/versions/flicker_free_elite/elite-master-flicker-free-sng47.ssd";
const CACHE = `elite-agent-title:${typeof JSBEEB_COMMIT === "string" ? JSBEEB_COMMIT : "dev"}:${DISC}`;
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
    $("loading").classList.toggle("show", true);
    const [panelData, galaxies, wasm, cmdr] = await Promise.all([
        fetch("panel.json").then((r) => r.json()), fetch("galaxies.json").then((r) => r.json()), fetch("player.wasm").then((r) => r.arrayBuffer()),
        CH ? fetch(CH.file).then((r) => r.arrayBuffer()) : null,
    ]);
    const gh = new Image(); gh.src = "gh.png";
    const panel = new Panel($("panel"), panelData, galaxies, gh);
    const w = frame.contentWindow;
    for (let t = 0; !w.processor || !w.video || !w.processor.fdc?.drives?.[0]?.disc; t++) {
        if (t === 300) $("loading").textContent = "Still waiting for Elite's disc from elite.bbcelite.com…";
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

    // before the agent starts: the machine at Elite's title screen. On a first visit it boots there (the player knows
    // Elite once it's loaded; another 3 s and the spinning Cobra is up), and the moment is kept for the next visit
    let bootF = 0, seen = -1;
    function booting() {
        bootF++;
        if (seen < 0) { if ((title || bootF % 25 === 0) && agent.start(1)) seen = bootF; return; }
        if (!title && bootF - seen < 150) return;
        if (!title) setTimeout(() => { try { localStorage.setItem(CACHE, snapshotToJSON(createSnapshot(cpu, cpu.model, {}))); } catch (e) { console.warn("no title snapshot kept", e); } }, 0);
        if (CH) {
            // the chapter's save, on the game's disc as E.BITKICK, for the player to load when it first docks (the start)
            const img = addFile(toSsdOrDsd(cpu.fdc.drives[0].disc, { force: true }), COMMANDER, "E", new Uint8Array(cmdr));
            cpu.fdc.loadDisc(0, discFor("elite.ssd", img));
        }
        agent.start(1);
        agent.name = COMMANDER;
        if (CH) agent.load = COMMANDER;
        s.started = true;
        $("loading").classList.remove("show");
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
    $("p-tag").textContent = s.tag || "";
    $("p-note").textContent = s.view === "dock" ? s.note || "" : "";
    $("p-log").textContent = s.log.slice(-3).map(([f, t]) => { const mm = Math.floor(f / 3000); return `${Math.floor(mm / 60)}:${String(mm % 60).padStart(2, "0")}  ${t}`; }).join("\n");
    // the chart and the network, cropped from the panel
    for (const [id, y0, y1] of [["p-chart", 228, 404], ["p-net", 416, 940]]) {
        const c = $(id), g = c.getContext("2d");
        if (c.width !== 824) { c.width = 824; c.height = y1 - y0; }
        g.drawImage($("panel"), 0, y0, 824, y1 - y0, 0, 0, 824, y1 - y0);
    }
}
const RANKS = [[0, "Harmless"], [8, "Mostly Harmless"], [16, "Poor"], [32, "Average"], [64, "Above Average"], [128, "Competent"],
    [512, "Dangerous"], [2560, "Deadly"], [6400, "ELITE"]];
const rankName = (t) => RANKS.filter(([k]) => t >= k).pop()[1];

main().catch((e) => { console.error(e); flash(String(e)); });

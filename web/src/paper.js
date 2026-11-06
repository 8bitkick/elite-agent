// The paper's diagrams: the sim's own Panel, its own forward pass, replaying the senses each network really saw (tools/paper_senses.py).
import { Panel, acts, fightCol, CYAN } from "./panel.js";

const NETS = {
    camera: ["Curious: the camera", (f) => `Target drawn ${Math.round(f[8] * 16)} px, ${Math.round(f[9] * 4096).toLocaleString()} units away`],
    interceptor: ["Optimal: the interceptor", (f) => `Target drawn ${Math.round(f[8] * 16)} px, ${Math.round(f[9] * 4096).toLocaleString()} units away`],
    fight: ["Fair: the dogfight network", (f) => `Target drawn ${Math.round(f[8] * 16)} px, about ${Math.round(f[9] * 4096).toLocaleString()} units away, as judged from the screen`],
    dock: ["Docking", null],
};
const [panelData, senses] = await Promise.all(["panel.json", "paper_senses.json"].map((u) => fetch(u).then((r) => r.json())));
const canvas = document.getElementById("net"), cap = document.getElementById("netcap"), tabs = document.getElementById("nettabs"), playBtn = document.getElementById("netplay");
const panel = new Panel(canvas, panelData, null, null);
let name = "camera", i = 0, last = 0, seen = false, playing = !matchMedia("(prefers-reduced-motion: reduce)").matches;

function draw() {
    const g = panel.ctx, tr = senses[name], f = tr[i % tr.length];
    g.setTransform(1, 0, 0, 1, -1012, -312);
    g.fillStyle = "rgb(9,11,15)"; g.fillRect(1012, 312, 908, canvas.height);
    let note;
    if (name === "dock") {
        panel.dockActs = acts(panelData.dock.layers, f);
        const phase = panel.dockPhase(f, panel.dockActs);
        panel.drawDock({});
        note = `Phase: ${phase}`;
    } else {
        panel.drawFight({ fightNet: name, senses: f });
        note = NETS[name][1](f);
    }
    cap.textContent = note;
}
function tick(t) {
    const hz = name === "dock" ? 5 : 10;
    if (playing && seen && t - last >= 1000 / hz) { last = t; i++; draw(); }
    requestAnimationFrame(tick);
}
// the diagram's height: the panel's rows for this network (its fitRows), all of them
function heightOf(n) {
    const D = panelData[n], k = n === "dock" ? 2 : 1;
    const rows = Math.max(...D.cols.map((c, j) => (n === "dock" ? c[2] : c[1]).length * (j === D.cols.length - 1 ? 2 : 1)));
    return 22 + 24 + rows * (Math.max(14, Math.floor(625 / rows) - 2) + 2) + 10;
}
function pick(n) {
    name = n; i = 0;
    canvas.height = heightOf(n);
    tabs.querySelectorAll("button").forEach((b) => {
        const on = b.dataset.net === n, col = n === "dock" ? CYAN : fightCol(n);
        b.classList.toggle("on", on);
        b.style.color = on ? `rgb(${col.join(",")})` : "";
    });
    draw();
}
for (const n of Object.keys(NETS)) {
    const b = document.createElement("button"); b.dataset.net = n; b.textContent = NETS[n][0]; b.onclick = () => pick(n); tabs.append(b);
}
playBtn.onclick = () => { playing = !playing; playBtn.textContent = playing ? "❚❚ Pause" : "▶ Play"; };
playBtn.textContent = playing ? "❚❚ Pause" : "▶ Play";
new IntersectionObserver((e) => { seen = e[0].isIntersecting; }).observe(canvas);
pick("camera");
requestAnimationFrame(tick);

// ---- what is fair: one fight three ways. Each frame's ships: slot, true type, x, y, z, energy, hostile, wire, on screen, identified, seen type, est x, y, z, est energy, est hostile, range error, age
const NAMES = { 1: "Missile", 2: "Coriolis", 3: "Pod", 4: "Plate", 5: "Canister", 6: "Boulder", 7: "Asteroid", 8: "Splinter", 9: "Shuttle", 10: "Transporter", 11: "Cobra Mk III", 12: "Python", 13: "Boa", 14: "Anaconda", 15: "Rock hermit", 16: "Viper", 17: "Sidewinder", 18: "Mamba", 19: "Krait", 20: "Adder", 21: "Gecko", 22: "Cobra Mk I", 23: "Worm", 24: "Cobra Mk III", 25: "Asp Mk II", 26: "Python", 27: "Fer-de-lance", 28: "Moray", 29: "Thargoid", 30: "Thargon", 31: "Constrictor", 32: "Cougar", 33: "Dodo" };
const fair = await fetch("paper_fair.json").then((r) => r.json());
const fairC = ["fram", "fscr", "ffil"].map((id) => document.getElementById(id));
const RED = "rgb(240,72,72)", YELC = "rgb(242,209,58)", FGC = "rgb(232,234,238)", DIMC = "rgb(110,116,128)", LINEC = "rgb(40,45,56)", CYANC = "rgb(56,214,232)";
const SCALE = 16000;
let fi = 0, fairSeen = false;
const q = (v, s) => Math.round(v / s) * s;
function base(c) {
    const g = c.getContext("2d"), W = c.width, H = c.height;
    g.fillStyle = "rgb(16,19,25)"; g.fillRect(0, 0, W, H);
    g.strokeStyle = LINEC; g.lineWidth = 1;
    for (const r of [0.5, 1]) { g.beginPath(); g.arc(W / 2, H * 0.72, r * H * 0.68, 0, 7); g.stroke(); }
    return { g, sx: (x) => W / 2 + (x / SCALE) * H * 0.68, sy: (z) => H * 0.72 - (z / SCALE) * H * 0.68, W, H };
}
function me(a) { const { g, W, H } = a; g.fillStyle = FGC; g.beginPath(); g.moveTo(W / 2, H * 0.72 - 7); g.lineTo(W / 2 - 5, H * 0.72 + 5); g.lineTo(W / 2 + 5, H * 0.72 + 5); g.fill(); }
const onC = (s) => Math.hypot(s[2], s[4]) < SCALE * 1.5;
function drawFair() {
    const rows = fair[fi % fair.length].filter((s) => s[1] > 2 && s[1] !== 8);
    const a = base(fairC[0]), b = base(fairC[1]), c = base(fairC[2]);
    // the view's cone, on the screen panel
    { const { g, sx, sy } = b; g.fillStyle = "rgba(56,214,232,.07)"; g.beginPath(); g.moveTo(sx(0), sy(0)); g.lineTo(sx(-SCALE / 2), sy(SCALE)); g.lineTo(sx(SCALE / 2), sy(SCALE)); g.fill(); }
    for (const s of rows) {
        const [, , x, y, z, , hostile, wire, scr, , , ex, , ez, , ehost, rsd] = s;
        // RAM: exactly where it is, and what it is
        { const { g, sx, sy } = a; g.fillStyle = hostile ? RED : CYANC; g.beginPath(); g.arc(sx(x), sy(z), 4, 0, 7); g.fill(); }
        // the screen: a sightline if drawn, a coarse scanner blip if within its range
        { const { g, sx, sy } = b;
          if (scr) { const d = Math.hypot(x, z) || 1, k = SCALE * 1.5 / d; g.strokeStyle = wire ? FGC : DIMC; g.lineWidth = wire ? 1.5 : 1; g.beginPath(); g.moveTo(sx(0), sy(0)); g.lineTo(sx(x * k), sy(z * k)); g.stroke(); }
          if (Math.abs(x) < 16383 && Math.abs(z) < 16383 && Math.abs(y) < 16383) { g.fillStyle = YELC; const X = q(x, 512), Z = q(z, 1024); g.fillRect(sx(X - 256), sy(Z + 512), Math.max(3, sx(X + 256) - sx(X - 256)), Math.max(3, sy(Z - 512) - sy(Z + 512))); } }
        // the filter: its estimate, its doubt along the sightline, and the truth as a ghost
        { const { g, sx, sy } = c;
          const d = Math.hypot(ex, ez) || 1, e = rsd * d, ux = ex / d, uz = ez / d;
          g.strokeStyle = ehost ? RED : CYANC; g.lineWidth = 2.5; g.beginPath(); g.moveTo(sx(ex - ux * e * 2), sy(ez - uz * e * 2)); g.lineTo(sx(ex + ux * e * 2), sy(ez + uz * e * 2)); g.stroke();
          g.fillStyle = ehost ? RED : CYANC; g.beginPath(); g.arc(sx(ex), sy(ez), 3, 0, 7); g.fill();
          g.strokeStyle = DIMC; g.lineWidth = 1; g.beginPath(); g.moveTo(sx(x) - 3, sy(z) - 3); g.lineTo(sx(x) + 3, sy(z) + 3); g.moveTo(sx(x) + 3, sy(z) - 3); g.lineTo(sx(x) - 3, sy(z) + 3); g.stroke(); }
    }
    me(a); me(b); me(c);
    // the nearest ship, read out three ways
    const n = rows.filter((s) => s[1] > 8).sort((p, r) => Math.hypot(p[2], p[3], p[4]) - Math.hypot(r[2], r[3], r[4]))[0];
    if (n) {
        const [, ty, x, y, z, E, hostile, wire, scr, ident, ety, ex, ey, ez, eE, eh, rsd] = n;
        const d = Math.hypot(x, y, z), ed = Math.hypot(ex, ey, ez);
        const seen = !scr ? "off screen" : wire ? "wireframe" : "a dot";
        const T = (id, v) => { document.getElementById(id).textContent = v; };
        T("r-t", NAMES[ty] || "?"); T("r-r", Math.round(d).toLocaleString()); T("r-e", String(E)); T("r-h", hostile ? "yes" : "no");
        T("s-t", wire ? NAMES[ty] : seen); T("s-r", scr ? (Math.hypot(x, z) < 16383 ? "from size or scanner" : "from size") : "scanner only"); T("s-e", "not shown"); T("s-h", "not shown");
        T("f-t", ident ? NAMES[ety] : "a colour, guessed"); T("f-r", `${Math.round(ed).toLocaleString()} ± ${Math.round(rsd * ed).toLocaleString()}`); T("f-e", `about ${eE}`); T("f-h", eh ? "assumed yes" : "assumed no");
    }
}
for (const c of fairC) { c.width = 520; c.height = 390; }
new IntersectionObserver((e) => { fairSeen = e[0].isIntersecting; }).observe(fairC[0]);
drawFair();
setInterval(() => { if (fairSeen && !matchMedia("(prefers-reduced-motion: reduce)").matches) { fi++; drawFair(); } }, 200);

const COBRA_V = [[32,0,76],[-32,0,76],[0,26,24],[-120,-3,-8],[120,-3,-8],[-88,16,-40],[88,16,-40],[128,-8,-40],[-128,-8,-40],[0,26,-40],[-32,-24,-40],[32,-24,-40],[-36,8,-40],[-8,12,-40],[8,12,-40],[36,8,-40],[36,-12,-40],[8,-16,-40],[-8,-16,-40],[-36,-12,-40],[0,0,76],[0,0,90],[-80,-6,-40],[-80,6,-40],[-88,0,-40],[80,6,-40],[88,0,-40],[80,-6,-40],[0,0,224],[0,48,48],[96,0,-16],[-96,0,-16],[0,48,-32],[0,24,-112],[-48,0,-112],[48,0,-112],[0,-48,48],[0,-48,-32],[0,-24,-112]], COBRA_E = [[0,1],[0,4],[1,3],[3,8],[4,7],[6,7],[6,9],[5,9],[5,8],[2,5],[2,6],[3,5],[4,6],[1,2],[0,2],[8,10],[10,11],[7,11],[1,10],[0,11],[1,5],[0,6],[20,21],[12,13],[18,19],[14,15],[16,17],[15,16],[14,17],[13,18],[12,19],[2,9],[22,24],[23,24],[22,23],[25,26],[26,27],[25,27],[0,8],[0,3],[0,2],[0,1],[2,4],[1,2],[2,8],[1,3],[3,8],[2,9],[3,4],[3,9],[3,5],[3,10],[2,5],[2,10],[2,7],[3,6],[5,6],[5,7],[7,10],[6,10],[4,5],[9,10],[1,4],[8,9]];
// ---- the hero: Elite's Cobra Mk III, turning as the camera network's real outputs say, each control pulsing with its value
{
    const cv = document.getElementById("cobra"), g = cv.getContext("2d"), W = (cv.width = 1040), H = (cv.height = 520);
    const tr = senses.camera, L = panelData.camera.layers, COL = { roll: "56,214,232", pitch: "242,209,58", throttle: "27,175,122", fire: "240,72,72" };
    const out = tr.map((f) => { const a = acts(L, f), o = a[a.length - 1]; return { roll: Math.max(-31, Math.min(31, o[0])), pitch: Math.max(-8, Math.min(8, o[1])), fire: o[2] > 0.5 ? 1 : 0, throttle: Math.max(0, Math.min(40, o[3])) }; });
    let t = 0, turn = 0.9, rollA = 0, pitchA = 0, cur = { roll: 0, pitch: 0, fire: 0, throttle: 0 }, heroSeen = true;
    const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
    const rot = (p) => {
        let [x, y, z] = p, c = Math.cos(rollA), s = Math.sin(rollA);           // the ship's own roll, about its nose
        [x, y] = [x * c - y * s, x * s + y * c];
        c = Math.cos(pitchA); s = Math.sin(pitchA); [y, z] = [y * c - z * s, y * s + z * c];      // its pitch
        c = Math.cos(turn); s = Math.sin(turn); [x, z] = [x * c + z * s, -x * s + z * c];         // the slow turntable
        c = Math.cos(0.32); s = Math.sin(0.32); [y, z] = [y * c - z * s, y * s + z * c];          // the viewer, a little above
        const k = 2.5 * (900 / (900 - z));
        return [W / 2 + x * k, H / 2 - y * k - 24];
    };
    const line = (pts, col, a, w) => { g.strokeStyle = `rgba(${col},${a})`; g.lineWidth = w; g.lineCap = "round"; g.beginPath(); pts.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.stroke(); };
    const arrow = (pts3, col, v) => {
        const a = 0.5 + 0.5 * v, w = 3 + 5 * v, q = pts3.map(rot);
        line(q, col, a, w);
        const [p0, p1] = [q[q.length - 2], q[q.length - 1]], ang = Math.atan2(p1[1] - p0[1], p1[0] - p0[0]), h = 15 + 10 * v;
        g.fillStyle = `rgba(${col},${a})`; g.beginPath(); g.moveTo(p1[0] + Math.cos(ang) * h * 0.7, p1[1] + Math.sin(ang) * h * 0.7);
        g.lineTo(p1[0] + Math.cos(ang + 2.5) * h, p1[1] + Math.sin(ang + 2.5) * h); g.lineTo(p1[0] + Math.cos(ang - 2.5) * h, p1[1] + Math.sin(ang - 2.5) * h); g.fill();
    };
    const arc = (fn, sign) => { /* the arrow runs the way the value says */ const pts = []; for (let i = 0; i <= 30; i++) pts.push(fn((-110 + (220 * i) / 30) * (Math.PI / 180) * (sign < 0 ? -1 : 1))); return pts; };
    const label = (i, name, val, col, on) => {
        const x = 16 + i * 254, y = H - 62;
        g.fillStyle = "rgb(9,11,15)"; g.fillRect(x, y, 246, 50); g.fillStyle = `rgba(${col},${0.08 + 0.3 * on})`; g.fillRect(x, y, 246 * Math.max(0.02, on), 50); g.strokeStyle = `rgba(${col},${0.4 + 0.6 * on})`; g.lineWidth = 1 + 2 * on; g.strokeRect(x + 0.5, y + 0.5, 246, 50);
        g.font = "700 22px 'JetBrains Mono', monospace"; g.textBaseline = "middle"; g.textAlign = "left"; g.fillStyle = `rgb(${col})`; g.fillText(name, x + 12, y + 26);
        g.textAlign = "right"; g.fillStyle = "rgb(232,234,238)"; g.fillText(val, x + 234, y + 26);
    };
    function frame() {
        g.fillStyle = "rgb(16,19,25)"; g.fillRect(0, 0, W, H);
        const r = Math.abs(cur.roll) / 31, p = Math.abs(cur.pitch) / 8, th = cur.throttle / 40, pulse = 0.5 + 0.5 * Math.sin(t * 0.25);
        // the ship, from its blueprint's vertices and edges
        g.strokeStyle = "rgba(232,234,238,.9)"; g.lineWidth = 1.3; g.beginPath();
        for (const [a, b] of COBRA_E) { const A = rot(COBRA_V[a]), B = rot(COBRA_V[b]); g.moveTo(A[0], A[1]); g.lineTo(B[0], B[1]); }
        g.stroke();
        // the controls, each brighter and thicker with its value, each its direction by its sign
        arrow(arc((a) => [42 * Math.cos(a + 1.57), 42 * Math.sin(a + 1.57), 118], cur.roll), COL.roll, Math.min(1, r * (0.75 + 0.25 * pulse)));
        arrow(arc((a) => [-165, 58 * Math.sin(a), 58 * Math.cos(a) - 8], cur.pitch), COL.pitch, Math.min(1, p * (0.75 + 0.25 * pulse)));
        arrow([[0, -56, -70], [0, -56, -70 + 50 + 110 * th]], COL.throttle, th * (0.75 + 0.25 * pulse));
        if (cur.fire) line([rot([0, 0, 92]), rot([0, 0, 330])], COL.fire, 0.95, 4);
        label(0, "roll", (cur.roll > 0 ? "+" : "") + Math.round(cur.roll), COL.roll, r);
        label(1, "pitch", (cur.pitch > 0 ? "+" : "") + Math.round(cur.pitch), COL.pitch, p);
        label(2, "throttle", String(Math.round(cur.throttle)), COL.throttle, th);
        label(3, "fire", cur.fire ? "FIRE" : "-", COL.fire, cur.fire);
    }
    let last = 0;
    function run(ms) {
        if (heroSeen && !still && ms - last >= 33) {
            last = ms; t++;
            const o = out[Math.floor(t / 3) % out.length];     // (the trace is 10 a second, this 30)
            cur = o; turn += 0.006;
            rollA += (o.roll / 256) * 0.55; pitchA = pitchA * 0.985 + (o.pitch / 256) * 0.55;
            frame();
        }
        requestAnimationFrame(run);
    }
    cur = out.find((o) => o.fire) || out[0];
    frame();
    new IntersectionObserver((e) => { heroSeen = e[0].isIntersecting; }).observe(cv);
    requestAnimationFrame(run);
}

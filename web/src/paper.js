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

// The right-hand panel, as the film draws it (the film scripts' render.py and render_app.py), on a canvas in the film's
// own coordinates: the frame is 1920x1080 and the panel is its right part, from x = 1012 (the page's game a touch smaller than the film's, the network wider). The clock, rank, tally,
// credits and system; the tally chart; then the dogfight network (yellow) in a fight, the docking network (cyan) on the
// way in, or the galaxy map and the agent's log when docked; the status lines and the credits.
export const BG = "rgb(9,11,15)", PANEL = [16, 19, 25], LINE = "rgb(40,45,56)", DIM = "rgb(110,116,128)", FG = "rgb(232,234,238)";
const IDLE_TEXT = "rgb(150,156,168)", IDLE_EDGE = "rgb(58,64,78)";      // (the network's idle units, readable on a phone)
export const YEL = [242, 209, 58], CYAN = [56, 214, 232], GREEN = "rgb(27,175,122)";
// each dogfight network's colour: fair mode's yellow, Optimal's interceptor red, Curious's camera purple
export const FIGHT_COL = { fight: YEL, interceptor: [240, 72, 72], camera: [178, 112, 255] };
export const fightCol = (net) => FIGHT_COL[net] || YEL;
const SANS = "Inter, system-ui, sans-serif", MONO = "'JetBrains Mono', ui-monospace, monospace";
const PX = 1016, PW = 880, NET_Y = 322;
// the columns: a network's layers, inputs and outputs included (fair mode's dogfight network 6, the mode networks 7)
let NCOL = 6, GAP = 62, CW = Math.floor((PW - 5 * GAP) / 6);
function setCols(n) { NCOL = n; GAP = n > 6 ? 40 : 62; CW = Math.floor((PW - (n - 1) * GAP) / n); }
// the pilot by network, as the status line names it
const PILOT = { fight: "Dogfight AI", interceptor: "Interceptor AI", camera: "Camera AI" };
// the units' height: each network's tallest column fills the space down to the game screen's bottom
let CH = 20;
const fitRows = (n) => { CH = Math.max(14, Math.floor((BOT - NET_Y - 24 + 2) / n) - 2); };
const GY = 36, GH = 1014, H = 1080, X0 = 1012;   // (GY, GH: the panel's own top and bottom, as the film's game)
const CH_X = PX + 46, CH_Y = 236, CH_W = PW - 60, CH_H = 64, HMAX = 17, STATX = [0, 250, 410, 610];
const BOT = 969;   // (the game screen's bottom: the galaxy map's too)
const RANKS = [[0, "Harmless"], [8, "Mostly Harmless"], [16, "Poor"], [32, "Average"], [64, "Above Average"], [128, "Competent"],
    [512, "Dangerous"], [2560, "Deadly"], [6400, "ELITE"]];
const NOTE = {
    CRUISE: "At the planet, full speed; the jump once it is within 10° of the nose.",
    ROUND: "Behind or beside the station: round it, by a mark out in front on our side.",
    APPROACH: "At a mark on the slot's axis ahead of us: the path settles onto the axis.",
    "LINE-UP": "Nearly on the axis: the aim slides from the mark onto the slot itself.",
    FINAL: "Rolling with the station, the slot trimmed level and pitched onto the nose.",
    SWERVE: "A collision course: the escape heading, out to the side it will miss on.",
};
// what each input says, and each column does (the panel's hover notes)
export const INPUT_NOTE = {
    fight: ["left or right of the nose: where the target is drawn", "above or below the nose", "ahead or behind: the view it is drawn in",
        "moving across the view, frame to frame", "moving up or down the view", "how far off the nose",
        "clock position round the crosshairs, across", "clock position round the crosshairs, up", "how big it is drawn",
        "range: from its drawn size or its scanner dot", "closing speed: the range changing", "our speed: the speed bar",
        "laser temperature: the LT bar", "our energy: the four energy banks", "witchspace or not",
        "our own stick, left and right: the roll indicator", "our own stick, up and down: the pitch indicator",
        "a target or none", "a constant: the bias", "its heading read from its wireframe, or guessed from how it moves",
        "seconds since it was last measured", "how uncertain the range is", "how uncertain its motion is"],
    dock: ["the planet ahead (+) or behind (−)", "our height above the planet", "1 with the station in sight",
        "the slot's direction: right (+) or left (−)", "the slot's direction: up (+) or down (−)", "the slot ahead (+) or behind (−)",
        "the approach mark on the slot's axis, ahead of us: across", "the approach mark: up or down",
        "the side mark, to go round the station by: across", "the side mark: up or down",
        "on a collision course, the escape heading: across", "1 on a collision course (anything but the station)",
        "our distance out along the slot's axis (negative behind the station)", "our distance off the slot's axis",
        "range to the station", "the slot's centre as drawn: across", "the slot's centre as drawn: up or down",
        "the slot's roll off level", "the station's roll: one way or the other", "the turn to face into the slot",
        "the angle to the station's centre", "the angle to the slot's centre", "1 with nothing in the way before the slot",
        "our own stick, up and down", "last loop: steering from above", "last loop: in the final", "last loop: committed"],
};
INPUT_NOTE.interceptor = INPUT_NOTE.camera = INPUT_NOTE.fight;      // (the same senses)
export const COL_NOTE = {
    INPUTS: "what the network is told, read off the screen",
    TESTS: "yes-or-no tests on the inputs, each made of ReLU units",
    REGIME: "which part of the approach applies now",
    "THE AIM": "the point to steer for, for that part",
    RATES: "how hard to roll and pitch onto the aim",
    OUTPUT: "what goes to the game: the stick, the throttle and the switches",
    "LAYER 1": "hidden units, written by hand: features of the target's motion",
    "LAYER 2": "hidden units: combinations of layer 1",
    "LAYER 3": "hidden units: the choice of manoeuvre",
    "LAYER 4": "hidden units: the stick commands, shaped",
};
export const rankOf = (t) => RANKS.filter(([k]) => t >= k).pop()[1];
const rgb = (c) => `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`;
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const colX = (c) => PX + c * (CW + GAP);
const rowY = (c, r) => NET_Y + 24 + r * (CH + 2) * (c === NCOL - 1 ? 2 : 1);
const rowH = (c) => (c === NCOL - 1 ? CH * 2 + 2 : CH);
const cx = (h) => CH_X + (h / HMAX) * CH_W;
const cy = (t) => CH_Y + CH_H - (Math.min(t, 7000) / 7000) * CH_H;

/** a forward pass, every layer's activations (the senses first) */
export function acts(layers, f) {
    const out = [Float64Array.from(f)];
    let h = out[0];
    layers.forEach((L, i) => {
        const y = new Float64Array(L.b.length);
        for (let o = 0; o < y.length; o++) {
            let s = L.b[o]; const row = L.W[o];
            for (let j = 0; j < row.length; j++) if (row[j]) s += row[j] * h[j];
            y[o] = i < layers.length - 1 && s < 0 ? 0 : s;
        }
        out.push(y); h = y;
    });
    return out;
}

function trace(lanes, c, a, b) {
    const x0 = colX(c) + CW, x1 = colX(c + 1), lx = lanes[c].get(a), k = 4;
    const ys = rowY(c, a) + rowH(c) / 2, yd = rowY(c + 1, b) + rowH(c + 1) / 2;
    if (Math.abs(yd - ys) < 2 * k) return [[x0, ys], [lx, ys], [lx, yd], [x1, yd]];
    const sg = yd > ys ? 1 : -1;
    return [[x0, ys], [lx - k, ys], [lx, ys + sg * k], [lx, yd - sg * k], [lx + k, yd], [x1, yd]];
}

function lanesFor(links) {
    const lanes = [];
    for (let c = 0; c < NCOL - 1; c++) {
        const srcs = [...new Set(links.filter((l) => l[0] === c).map((l) => l[1]))].sort((a, b) => a - b);
        const x0 = colX(c) + CW;
        lanes.push(new Map(srcs.map((a, k) => [a, x0 + 10 + (k * (GAP - 20)) / Math.max(1, srcs.length - 1)])));
    }
    return lanes;
}

export class Panel {
    constructor(canvas, data, galaxies, gh) {
        this.c = canvas; this.d = data; this.gal = galaxies; this.gh = gh;
        canvas.width = 1920 - X0; canvas.height = H;
        this.ctx = canvas.getContext("2d");
        // the dogfight network's routes: every non-zero weight, column i to i + 1
        this.fnets = {};      // (each dogfight-type network's links and lanes, made when first drawn)
        this.dockLanes = lanesFor(data.dock.links);
    }

    text(s, x, y, font, fill, align = "left", base = "top") {
        const g = this.ctx; g.font = font; g.fillStyle = fill; g.textAlign = align; g.textBaseline = base; g.fillText(s, x, y);
    }

    /** s: the state the page keeps (frame, tally, cash, system, tallyPts, log, visits, docked, which, senses, phase...) */
    draw(s) {
        const g = this.ctx;
        // what's flying: the docking network's phase from its own units, or a fight, or docked
        if (s.view === "dock") {
            this.dockActs = s.senses ? acts(this.d.dock.layers, s.senses) : null;
            s.phase = this.dockPhase(s.senses, this.dockActs);
            s.note = NOTE[s.phase];
        } else s.phase = s.view === "fight" ? "FIGHT" : s.view === "docked" ? "DOCKED" : "CRUISE";
        s.tag = `${s.phase}  ${s.speedLabel}`;
        g.setTransform(1, 0, 0, 1, -X0, 0);
        g.fillStyle = BG; g.fillRect(X0, 0, 1920 - X0, H);
        this.drawTop(s);
        if (s.view === "docked") this.drawDocked(s);
        else if (s.view === "log") this.drawLog(s);
        else if (s.view === "fight") this.drawFight(s);
        else this.drawDock(s);
    }

    drawTop(s) {
        const secs = Math.floor(s.frame / 50), p2 = (n) => String(n).padStart(2, "0");
        this.text("GAME TIME", PX, 36, `600 14px ${SANS}`, DIM);
        this.text(`${p2(Math.floor(secs / 3600))}:${p2(Math.floor(secs / 60) % 60)}:${p2(secs % 60)}`, PX - 3, 52, `700 60px ${MONO}`, FG);
        ["RANK", "KILL TALLY", "CREDITS", "SYSTEM"].forEach((lab, k) => this.text(lab, PX + STATX[k], 156, `600 14px ${SANS}`, DIM));
        this.text(rankOf(s.tally), PX, 176, `600 26px ${SANS}`, s.tally >= 6400 ? rgb(YEL) : FG);
        this.text(Math.floor(s.tally).toLocaleString("en"), PX + STATX[1], 176, `600 26px ${SANS}`, FG);
        this.text(`${s.cash.toLocaleString("en", { minimumFractionDigits: 1, maximumFractionDigits: 1 })} Cr`, PX + STATX[2], 176, `600 26px ${SANS}`, FG);
        this.text(s.system, PX + STATX[3], 176, `600 26px ${SANS}`, s.system === "Witchspace" ? rgb(CYAN) : FG);
        // the ranks: a meter from Harmless to Elite, a section a rank (equal widths: the low ranks are a few kills
        // each), filled to the kill tally
        const g = this.ctx, MY = 222, MH = 12, seg = PW / (RANKS.length - 1);
        for (let i = 0; i < RANKS.length - 1; i++) {
            const lo = RANKS[i][0], hi = RANKS[i + 1][0], x = PX + i * seg;
            const fr = Math.max(0, Math.min(1, (s.tally - lo) / (hi - lo)));
            g.fillStyle = "rgb(26,30,38)"; g.fillRect(x + 1, MY, seg - 2, MH);
            if (fr > 0) { g.fillStyle = fr >= 1 ? "rgb(27,140,100)" : GREEN; g.fillRect(x + 1, MY, (seg - 2) * fr, MH); }
            const here = s.tally >= lo && s.tally < hi;
            this.text(RANKS[i][1], x + 2, MY + MH + 8, `${here ? 600 : 400} 12px ${SANS}`, here ? FG : DIM);
            this.text(lo.toLocaleString("en"), x + 2, MY + MH + 25, `11px ${MONO}`, "rgb(80,86,98)");
        }
        this.text("ELITE", PX + PW, MY + MH + 8, `600 12px ${SANS}`, s.tally >= 6400 ? rgb(YEL) : DIM, "right");
        this.text("6,400", PX + PW, MY + MH + 25, `11px ${MONO}`, "rgb(80,86,98)", "right");
        // one status line, from whoever is flying: the docking model (its phase and what it's doing), the dogfight
        // model (with the fight's latest log line), or else the agent's own latest log line
        const last = s.log[s.log.length - 1], cut = (t, n) => (t.length <= n ? t : t.slice(0, n - 1) + "…");
        let at = last ? last[0] : s.frame, text = last ? `Agent: ${last[1]}` : "", tcol = GREEN;
        if (s.view === "dock" && s.senses) { at = s.frame; text = `Docking AI: ${s.note || ""}`; tcol = rgb(CYAN); }
        else if (s.view === "fight" && s.senses) { text = `${PILOT[s.fightNet] || PILOT.fight}: ${last ? last[1] : ""}`; tcol = rgb(fightCol(s.fightNet)); }
        if (text) {
            const mm = Math.floor(at / 3000), hh = Math.floor(mm / 60);
            this.text(`${hh}:${String(mm % 60).padStart(2, "0")}`, PX, 282, `14px ${MONO}`, DIM);
            this.text(cut(text, 96), PX + 50, 282, `14px ${MONO}`, tcol);
            this.line = { text, col: tcol };      // (the phone's top line draws it again, larger)
        }
        g.strokeStyle = LINE; g.lineWidth = 1; g.beginPath(); g.moveTo(PX, 304.5); g.lineTo(PX + PW, 304.5); g.stroke();
        // the speed, as big as the clock: a play mark at real time, a fast-forward mark and how many times faster in
        // turbo; what's flying above it
        const net = s.view === "dock", fight = s.view === "fight";
        const col = net ? rgb(CYAN) : fight ? rgb(fightCol(s.fightNet)) : FG, right = PX + PW;
        let cyc, h;
        const n = s.turbo ? Math.max(1, s.rate || 1) : 1, txt = s.paused ? "PAUSED" : `${n < 10 ? n.toFixed(n === 1 ? 0 : 1) : Math.round(n)}×`;
        this.text(txt, right, 52, `700 60px ${MONO}`, col, "right");
        // the mark centred on the digits as this browser draws them (fonts differ: measured, not assumed)
        g.font = `700 60px ${MONO}`; g.textBaseline = "top";
        const m = g.measureText(txt), top = 52 - m.actualBoundingBoxAscent, bot = 52 + m.actualBoundingBoxDescent;
        cyc = (top + bot) / 2; h = (bot - top) * 0.9;
        const tx = right - m.width - 18;
        g.fillStyle = col; g.beginPath();
        if (s.paused) { g.rect(tx - 30, cyc - h / 2, h * 0.28, h); g.rect(tx - 30 + h * 0.45, cyc - h / 2, h * 0.28, h); }
        else if (!s.turbo) { g.moveTo(tx - 34, cyc - h / 2); g.lineTo(tx, cyc); g.lineTo(tx - 34, cyc + h / 2); }
        else for (const k of [0, 1]) { g.moveTo(tx - 30 * k - 30, cyc - h / 2); g.lineTo(tx - 30 * k, cyc); g.lineTo(tx - 30 * k - 30, cyc + h / 2); }
        g.fill();
    }

    drawCells(cols, vals, lit, color, outRows, valOf) {
        this.hits = [];
        cols.forEach(([title, names], c) => {
            const x = colX(c);
            this.text(title, x, NET_Y, `600 14px ${SANS}`, IDLE_TEXT);
            names.forEach((name, r) => {
                const y = rowY(c, r);
                this.hits.push({ x, y, w: CW, h: rowH(c), c, r, name, title, v: valOf ? valOf(c, r) : null });
                if (c === NCOL - 1) { outRows(x, y, r, name); return; }
                const L = lit(c, r);
                const g = this.ctx;
                g.fillStyle = L > 0.02 ? rgb(mix(PANEL, color(c, r), 0.12 + 0.88 * L)) : rgb(PANEL);
                g.fillRect(x, y, CW, CH); g.strokeStyle = IDLE_EDGE; g.strokeRect(x + 0.5, y + 0.5, CW, CH);
                this.text(name.slice(0, 12), x + 5, y + CH / 2 + 1, `12px ${MONO}`, L > 0.55 ? "rgb(6,18,22)" : L > 0.02 ? FG : IDLE_TEXT, "left", "middle");
            });
        });
    }

    drawOut(x, y, name, v, sc, centred, color, txt, bar = true) {
        const g = this.ctx;
        g.fillStyle = rgb(PANEL); g.fillRect(x, y, CW, CH * 2 + 2); g.strokeStyle = LINE; g.strokeRect(x + 0.5, y + 0.5, CW, CH * 2 + 2);
        if (bar) {
            const fr = Math.max(-1, Math.min(1, v / sc)), mid = centred ? x + CW / 2 : x, end = mid + fr * (centred ? CW / 2 : CW);
            g.fillStyle = rgb(color); g.fillRect(Math.min(mid, end), y + CH + 6, Math.abs(end - mid), CH - 8);
        }
        this.text(name, x + 5, y + CH / 2 + 1, `12px ${MONO}`, FG, "left", "middle");
        this.text(txt, x + CW - 5, y + CH / 2 + 1, `12px ${MONO}`, rgb(color), "right", "middle");
    }

    drawRoutes(segs) {
        const g = this.ctx;
        segs.sort((a, b) => a[0] - b[0]);
        for (const [L, c3, pts] of segs) {
            g.strokeStyle = c3; g.lineWidth = L > 0.4 ? 2 : 1; g.lineJoin = "round";
            g.beginPath(); pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
            const [vx, vy] = pts[pts.length - 2]; g.fillStyle = c3; g.beginPath(); g.arc(vx, vy, 1.5, 0, 7); g.fill();
        }
    }

    /** a dogfight-type network's links and lanes, by name (fight: fair mode's; interceptor; camera) */
    fnet(name) {
        if (!this.fnets[name]) {
            const fl = [];
            this.d[name].layers.forEach((L, i) => L.W.forEach((row, o) => row.forEach((w, j) => { if (w) fl.push([i, j, o, w]); })));
            setCols(this.d[name].cols.length);
            this.fnets[name] = { links: fl, lanes: lanesFor(fl.map(([i, j, o]) => [i, j, o])) };
        }
        return this.fnets[name];
    }

    drawFight(s) {
        const name = s.fightNet && this.d[s.fightNet] ? s.fightNet : "fight", F = this.d[name], f = s.senses, COL = fightCol(name);
        setCols(F.cols.length);
        const { links, lanes } = this.fnet(name);
        fitRows(Math.max(...F.cols.map(([, names], c) => names.length * (c === NCOL - 1 ? 2 : 1))));
        const vals = f ? acts(F.layers, f) : null;
        const base = [44, 49, 64];
        const segs = links.map(([i, j, o, w]) => {
            const flow = vals ? (w * vals[i][j]) / (i === F.layers.length - 1 ? F.outscale[o] : F.scale[i + 1][o]) : 0;
            const L = Math.min(1, Math.abs(flow));
            const c3 = rgb(mix(base, flow >= 0 ? COL : CYAN, L * 0.6));
            return [L, c3, trace(lanes, i, j, o)];
        });
        this.drawRoutes(segs);
        const lit = (c, r) => (vals ? Math.min(1, Math.abs(vals[c][r]) / F.scale[c][r]) : 0);
        this.drawCells(F.cols, vals, lit, (c, r) => (c === 0 && vals && vals[0][r] < 0 ? CYAN : COL), (x, y, r, name) => {
            const v = vals ? Math.max(-F.outscale[r], Math.min(F.outscale[r], vals[F.layers.length][r])) : 0;
            const txt = r < 2 ? (v >= 0 ? "+" : "") + Math.round(v) : r === 2 ? (v > 0.5 ? "FIRE" : "") : String(Math.round(v));
            this.drawOut(x, y, name, v, F.outscale[r], r < 2, COL, txt);
        }, (c, r) => (vals ? vals[c][r] : null));
        this.net = name;
    }

    dockPhase(f, A) {
        if (!f || !A) return "CRUISE";
        const out = A[A.length - 1], D = this.d.dock;
        if (f[18] > 0.5 && out[5] < 0.5) return "SWERVE";
        if (f[5] < 0.5) return "CRUISE";
        if (out[5] > 0.5) return "FINAL";
        const [, li, rows] = D.cols[2];
        let best = 0, bv = -1e9;
        for (let r = 0; r < 6; r++) { const c = rows[r], v = A[li][c[2]] - A[li][c[0]] + A[li][c[3]]; if (v > bv) { bv = v; best = r; } }
        return ["CRUISE", "CRUISE", "SWERVE", "LINE-UP", "APPROACH", "ROUND"][best];
    }

    drawDock(s) {
        const D = this.d.dock, f = s.senses;
        setCols(6);
        fitRows(Math.max(...D.cols.map(([, , rows], c) => rows.length * (c === 5 ? 2 : 1))));
        const A = this.dockActs;
        // each shown cell's value and scale: (column, row)
        const V = (c, r) => {
            const [, li, rows] = D.cols[c], cell = rows[r];
            if (!A) return [0, 1];
            if (c === 2 && cell.length > 2) return [A[li][cell[2]] - A[li][cell[0]] + A[li][cell[3]], 1];     // an aim let through
            return [A[li][cell[0]], c === 5 ? D.outscale[r] : D.scale[li][cell[0]]];
        };
        const lit = (c, r) => { const [v, sc] = V(c, r); return Math.min(1, Math.abs(v) / sc); };
        const base = [44, 49, 64];
        const segs = D.links.map(([c, a, b]) => {
            const L = lit(c, a) * (0.4 + 0.6 * lit(c + 1, b));
            return [L, rgb(mix(base, CYAN, L * 0.7)), trace(this.dockLanes, c, a, b)];
        });
        this.drawRoutes(segs);
        this.drawCells(D.cols.map(([t, , rows]) => [t, rows.map((cell) => cell[1])]), A, lit, () => CYAN, (x, y, r, name) => {
            const [v0] = V(5, r), sc = D.outscale[r], v = Math.max(-sc, Math.min(sc, v0));
            const txt = r < 2 ? (v >= 0 ? "+" : "") + Math.round(v) : r === 2 ? String(Math.round(v)) : v > 0.5 ? "YES" : "";
            this.drawOut(x, y, name, v, sc, r < 2, CYAN, txt, r < 3 || v > 0.5);
        }, (c, r) => (A ? V(c, r)[0] : null));
        this.net = "dock";
    }

    drawDocked(s) {
        const g = this.ctx;
        // the galaxy chart
        const box = [PX, NET_Y + 24, PX + PW, BOT];
        const here = s.visits[s.visits.length - 1], gi = here[0];
        g.fillStyle = "rgb(5,7,9)"; g.fillRect(box[0], box[1], box[2] - box[0], box[3] - box[1]);
        g.strokeStyle = LINE; g.strokeRect(box[0] + 0.5, box[1] + 0.5, box[2] - box[0], box[3] - box[1]);
        this.text(`GALAXY ${gi + 1}`, box[0], box[1] - 18, `600 14px ${SANS}`, DIM);
        const sx = (box[2] - box[0] - 16) / 256, sy = (box[3] - box[1] - 16) / 256, P = (x, y) => [box[0] + 8 + x * sx, box[1] + 8 + y * sy];
        g.fillStyle = "rgb(60,66,78)";
        for (const [x, y] of this.gal[gi]) { const [px, py] = P(x, y); g.fillRect(px - 1, py - 1, 3, 3); }
        const mine = s.visits.filter((v) => v[0] === gi).map((v) => P(v[1], v[2]));
        g.strokeStyle = "rgb(27,110,80)"; g.beginPath(); mine.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
        g.fillStyle = GREEN; for (const [x, y] of mine) { g.beginPath(); g.arc(x, y, 3, 0, 7); g.fill(); }
        const [hx, hy] = P(here[1], here[2]);
        g.strokeStyle = rgb(YEL); g.lineWidth = 2; g.beginPath(); g.arc(hx, hy, 7, 0, 7); g.stroke(); g.lineWidth = 1;
        const nm = (this.gal[gi].find(([x, y]) => x === here[1] && y === here[2]) || [0, 0, ""])[2];
        this.text(nm, hx + 11, hy - 7, `14px ${SANS}`, rgb(YEL));
        this.hits = []; this.net = null;
    }

    // the AI not flying: the agent's log in the network's place, the latest at the bottom
    drawLog(s) {
        const LH = 34, N = Math.floor((BOT - NET_Y - 24) / LH), p2 = (n) => String(n).padStart(2, "0");
        this.text("AGENT LOG", PX, NET_Y, `600 14px ${SANS}`, IDLE_TEXT);
        const lines = s.log.slice(-N), font = `24px ${MONO}`;
        lines.forEach(([f, t], i) => {
            const mm = Math.floor(f / 3000), y = NET_Y + 24 + i * LH;
            this.text(`${Math.floor(mm / 60)}:${p2(mm % 60)}`, PX, y, font, IDLE_TEXT);
            this.text(t.length <= 54 ? t : t.slice(0, 53) + "…", PX + 92, y, font, i === lines.length - 1 ? "rgb(52,204,148)" : GREEN);
        });
        this.hits = []; this.net = null;
    }

    /** the cell under a point in the panel's coordinates (the film's frame), or null */
    hit(px, py) {
        return (this.hits || []).find((h) => px >= h.x && px <= h.x + h.w && py >= h.y && py <= h.y + h.h) || null;
    }
}

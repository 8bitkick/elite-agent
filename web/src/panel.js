// The right-hand panel, as the film draws it (the film scripts' render.py and render_app.py), on a canvas in the film's
// own coordinates: the frame is 1920x1080 and the panel is its right part, from x = 1096. The clock, rank, tally,
// credits and system; the tally chart; then the dogfight network (yellow) in a fight, the docking network (cyan) on the
// way in, or the galaxy map and the agent's log when docked; the status lines and the credits.
export const BG = "rgb(9,11,15)", PANEL = [16, 19, 25], LINE = "rgb(40,45,56)", DIM = "rgb(110,116,128)", FG = "rgb(232,234,238)";
export const YEL = [242, 209, 58], CYAN = [56, 214, 232], GREEN = "rgb(27,175,122)";
const SANS = "Inter, system-ui, sans-serif", MONO = "'JetBrains Mono', ui-monospace, monospace";
const PX = 1100, PW = 796, NET_Y = 428, GAP = 62, CH = 16, CW = Math.floor((PW - 5 * GAP) / 6);
const GY = 36, GH = 1014, H = 1080, X0 = 1096;
const CH_X = PX + 46, CH_Y = 262, CH_W = PW - 60, CH_H = 120, HMAX = 17, STATX = [0, 250, 410, 610];
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
export const rankOf = (t) => RANKS.filter(([k]) => t >= k).pop()[1];
const rgb = (c) => `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`;
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const colX = (c) => PX + c * (CW + GAP);
const rowY = (c, r) => NET_Y + 24 + r * (CH + 2) * (c === 5 ? 2 : 1);
const rowH = (c) => (c === 5 ? CH * 2 + 2 : CH);
const cx = (h) => CH_X + (h / HMAX) * CH_W;
const cy = (t) => CH_Y + CH_H - (Math.min(t, 7000) / 7000) * CH_H;

/** a forward pass, every layer's activations (the senses first) */
function acts(layers, f) {
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
    for (let c = 0; c < 5; c++) {
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
        const fl = [];
        data.fight.layers.forEach((L, i) => L.W.forEach((row, o) => row.forEach((w, j) => { if (w) fl.push([i, j, o, w]); })));
        this.fightLinks = fl;
        this.fightLanes = lanesFor(fl.map(([i, j, o]) => [i, j, o]));
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
        else if (s.view === "fight") this.drawFight(s);
        else this.drawDock(s);
        this.drawCaption(s);
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
        // the chart
        const g = this.ctx;
        this.text("KILL TALLY AGAINST GAME HOURS", PX, 236, `600 14px ${SANS}`, DIM);
        g.strokeStyle = "rgb(26,30,38)"; g.lineWidth = 1;
        for (let t = 0; t <= 7000; t += 1000) { g.beginPath(); g.moveTo(CH_X, cy(t) + 0.5); g.lineTo(CH_X + CH_W, cy(t) + 0.5); g.stroke(); }
        for (let hh = 0; hh < 18; hh += 2) {
            g.beginPath(); g.moveTo(cx(hh) + 0.5, CH_Y); g.lineTo(cx(hh) + 0.5, CH_Y + CH_H); g.stroke();
            this.text(String(hh), cx(hh), CH_Y + CH_H + 6, `11px ${MONO}`, DIM, "center");
        }
        g.strokeStyle = "rgb(90,94,104)"; g.setLineDash([4, 4]);
        for (const [t, n] of [[2560, "Deadly"], [6400, "Elite"]]) {
            g.beginPath(); g.moveTo(CH_X, cy(t) + 0.5); g.lineTo(CH_X + CH_W, cy(t) + 0.5); g.stroke();
            this.text(n, CH_X - 6, cy(t), `11px ${MONO}`, FG, "right", "middle");
        }
        g.setLineDash([]);
        const pts = s.tallyPts.map(([f, t]) => [cx(f / 180000), cy(t)]).concat([[cx(s.frame / 180000), cy(s.tally)]]);
        g.strokeStyle = GREEN; g.lineWidth = 3; g.beginPath();
        pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
        const [lx, ly] = pts[pts.length - 1];
        g.fillStyle = GREEN; g.beginPath(); g.arc(lx, ly, 5, 0, 7); g.fill();
        g.strokeStyle = LINE; g.lineWidth = 1; g.beginPath(); g.moveTo(PX, 412.5); g.lineTo(PX + PW, 412.5); g.stroke();
        // the speed, as big as the clock: a play mark at real time, a fast-forward mark and how many times faster in
        // turbo; what's flying above it
        const net = s.view === "dock", fight = s.view === "fight";
        const col = net ? rgb(CYAN) : fight ? rgb(YEL) : FG, right = PX + PW, cyc = 88, h = 40;
        this.text(`SPEED  ·  ${s.phase}`, right, 36, `600 14px ${SANS}`, DIM, "right");
        const n = s.turbo ? Math.max(1, s.rate || 1) : 1, txt = `${n < 10 ? n.toFixed(n === 1 ? 0 : 1) : Math.round(n)}×`;
        this.text(txt, right, 52, `700 60px ${MONO}`, col, "right");
        g.font = `700 60px ${MONO}`;
        const tx = right - g.measureText(txt).width - 18;
        g.fillStyle = col; g.beginPath();
        if (!s.turbo) { g.moveTo(tx - 34, cyc - h / 2); g.lineTo(tx, cyc); g.lineTo(tx - 34, cyc + h / 2); }
        else for (const k of [0, 1]) { g.moveTo(tx - 30 * k - 30, cyc - h / 2); g.lineTo(tx - 30 * k, cyc); g.lineTo(tx - 30 * k - 30, cyc + h / 2); }
        g.fill();
    }

    drawCells(cols, vals, lit, color, outRows) {
        cols.forEach(([title, names], c) => {
            const x = colX(c);
            this.text(title, x, NET_Y, `600 14px ${SANS}`, DIM);
            names.forEach((name, r) => {
                const y = rowY(c, r);
                if (c === 5) { outRows(x, y, r, name); return; }
                const L = lit(c, r);
                const g = this.ctx;
                g.fillStyle = L > 0.02 ? rgb(mix(PANEL, color(c, r), 0.12 + 0.88 * L)) : rgb(PANEL);
                g.fillRect(x, y, CW, CH); g.strokeStyle = LINE; g.strokeRect(x + 0.5, y + 0.5, CW, CH);
                this.text(name.slice(0, 12), x + 4, y + 2, `11px ${MONO}`, L > 0.55 ? "rgb(6,18,22)" : L > 0.02 ? FG : DIM);
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
        this.text(name, x + 4, y + 2, `11px ${MONO}`, FG);
        this.text(txt, x + CW - 4, y + 2, `11px ${MONO}`, rgb(color), "right");
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

    drawFight(s) {
        const F = this.d.fight, f = s.senses;
        const vals = f ? acts(F.layers, f) : null;
        const base = [30, 34, 44];
        const segs = this.fightLinks.map(([i, j, o, w]) => {
            const flow = vals ? (w * vals[i][j]) / (i === F.layers.length - 1 ? F.outscale[o] : F.scale[i + 1][o]) : 0;
            const L = Math.min(1, Math.abs(flow));
            const c3 = rgb(mix(base, flow >= 0 ? YEL : CYAN, L * 0.6));
            return [L, c3, trace(this.fightLanes, i, j, o)];
        });
        this.drawRoutes(segs);
        const lit = (c, r) => (vals ? Math.min(1, Math.abs(vals[c][r]) / F.scale[c][r]) : 0);
        this.drawCells(F.cols, vals, lit, (c, r) => (c === 0 && vals && vals[0][r] < 0 ? CYAN : YEL), (x, y, r, name) => {
            const v = vals ? Math.max(-F.outscale[r], Math.min(F.outscale[r], vals[5][r])) : 0;
            const txt = r < 2 ? (v >= 0 ? "+" : "") + Math.round(v) : r === 2 ? (v > 0.5 ? "FIRE" : "") : String(Math.round(v));
            this.drawOut(x, y, name, v, F.outscale[r], r < 2, YEL, txt);
        });
        this.status(s, f ? null : "No target: the C agent is flying. The model takes over in a fight.", DIM);
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
        const A = this.dockActs;
        // each shown cell's value and scale: (column, row)
        const V = (c, r) => {
            const [, li, rows] = D.cols[c], cell = rows[r];
            if (!A) return [0, 1];
            if (c === 2 && cell.length > 2) return [A[li][cell[2]] - A[li][cell[0]] + A[li][cell[3]], 1];     // an aim let through
            return [A[li][cell[0]], c === 5 ? D.outscale[r] : D.scale[li][cell[0]]];
        };
        const lit = (c, r) => { const [v, sc] = V(c, r); return Math.min(1, Math.abs(v) / sc); };
        const base = [30, 34, 44];
        const segs = D.links.map(([c, a, b]) => {
            const L = lit(c, a) * (0.4 + 0.6 * lit(c + 1, b));
            return [L, rgb(mix(base, CYAN, L * 0.7)), trace(this.dockLanes, c, a, b)];
        });
        this.drawRoutes(segs);
        this.drawCells(D.cols.map(([t, , rows]) => [t, rows.map((cell) => cell[1])]), A, lit, () => CYAN, (x, y, r, name) => {
            const [v0] = V(5, r), sc = D.outscale[r], v = Math.max(-sc, Math.min(sc, v0));
            const txt = r < 2 ? (v >= 0 ? "+" : "") + Math.round(v) : r === 2 ? String(Math.round(v)) : v > 0.5 ? "YES" : "";
            this.drawOut(x, y, name, v, sc, r < 2, CYAN, txt, r < 3 || v > 0.5);
        });
        this.status(s, s.note, rgb(CYAN));
    }

    status(s, note, color) {
        const yb = GY + GH, lines = s.log.slice(-2);
        lines.forEach(([f, t], k) => {
            const mm = Math.floor(f / 3000), hh = Math.floor(mm / 60);
            this.text(`${String(hh).padStart(2)}:${String(mm % 60).padStart(2, "0")}  ${t.slice(0, 56)}`, PX, yb - (lines.length - 1 - k) * 20,
                `15px ${MONO}`, k === lines.length - 1 ? GREEN : "rgb(150,156,166)", "left", "bottom");
        });
        if (note) this.text(note, PX, yb - lines.length * 20 - 4, `14px ${SANS}`, color, "left", "bottom");
    }

    drawDocked(s) {
        const capTop = GY + GH - 152, g = this.ctx;
        // the galaxy chart
        const box = [PX, NET_Y + 24, PX + PW, capTop - 50];
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
        // the log
        this.text("DOCKED: THE AGENT LOG", PX, capTop - 24, `600 14px ${SANS}`, DIM);
        const lb = [PX, capTop, colX(4) - 24, H - 30];
        g.fillStyle = "rgb(5,7,9)"; g.fillRect(lb[0], lb[1], lb[2] - lb[0], lb[3] - lb[1]);
        g.strokeStyle = LINE; g.strokeRect(lb[0] + 0.5, lb[1] + 0.5, lb[2] - lb[0], lb[3] - lb[1]);
        const n = Math.floor((lb[3] - lb[1] - 20) / 21), lines = s.log.slice(-n);
        let y = lb[3] - 14 - 21 * lines.length;
        lines.forEach(([f, t], k) => {
            const mm = Math.floor(f / 3000), hh = Math.floor(mm / 60);
            this.text(`${String(hh).padStart(2)}:${String(mm % 60).padStart(2, "0")}`, lb[0] + 12, y, `15px ${MONO}`, DIM);
            this.text(t.length <= 50 ? t : t.slice(0, 49) + "…", lb[0] + 72, y, `15px ${MONO}`, k === lines.length - 1 ? GREEN : "rgb(170,176,186)");
            y += 21;
        });
    }

    drawCaption() {
        const bx = colX(4), by = GY + GH - 152;
        this.text("Acornsoft Elite, BBC Master", bx, by, `600 16px ${SANS}`, FG);
        ["Fair mode: only what a player", "can see on the screen.", "Flown by two AI models:", "dogfights 8 KB, docking 1.5 KB."]
            .forEach((t, k) => this.text(t, bx, by + 30 + k * 20, `14px ${SANS}`, DIM));
        if (this.gh.complete) this.ctx.drawImage(this.gh, bx, by + 128, 24, 24);
        this.text("8bitkick/elite-agent", bx + 32, by + 140, `15px ${MONO}`, FG, "left", "middle");
    }
}

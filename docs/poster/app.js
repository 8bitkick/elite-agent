(function () {
  const D = window.ELITE;
  const NS = 'http://www.w3.org/2000/svg';
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const C = { ink: css('--ink'), muted: css('--muted'), faint: css('--faint'), rule: css('--rule'), cyan: css('--cyan'),
              yellow: css('--yellow'), red: css('--red'), s1: css('--s1'), s2: css('--s2'), s3: css('--s3') };
  const SHIPC = ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#008300', '#9085e9'];
  const fmt = n => n.toLocaleString('en-GB', { maximumFractionDigits: 0 });
  const mmss = s => Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0');
  const RANKS = D.ranks;   // [[threshold, name]]
  const rankOf = t => { let r = RANKS[0][1]; for (const [th, n] of RANKS) if (t >= th) r = n; return r; };

  function el(tag, attrs, parent, text) {
    const e = document.createElementNS(NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (text != null) e.textContent = text;
    if (parent) parent.appendChild(e);
    return e;
  }
  const tip = document.getElementById('tip');
  function showTip(evt, html) {
    tip.innerHTML = html; tip.hidden = false;
    const w = tip.offsetWidth, h = tip.offsetHeight;
    let x = evt.clientX + 14, y = evt.clientY + 14;
    if (x + w > innerWidth - 8) x = evt.clientX - w - 14;
    if (y + h > innerHeight - 8) y = evt.clientY - h - 14;
    tip.style.left = x + 'px'; tip.style.top = y + 'px';
  }
  const hideTip = () => { tip.hidden = true; };
  function svgPoint(svg, evt) {
    const p = svg.createSVGPoint(); p.x = evt.clientX; p.y = evt.clientY;
    return p.matrixTransform(svg.getScreenCTM().inverse());
  }

  // ---- images
  const I = D.img;
  const setImg = (id, k) => { const e = document.getElementById(id); if (e) e.src = I[k]; };
  setImg('img-elite', 'elite'); setImg('img-elite2', 'elite'); setImg('img-title', 'title'); setImg('img-deadly', 'deadly');
  setImg('img-c1', 'fight-0197'); setImg('img-c2', 'fight-0609'); setImg('img-c3', 'fight-2340'); setImg('img-c4', 'fight-2358');

  // ---- stat tiles
  const T = D.totals;
  const tiles = [[fmt(T.final_tally), 'kill points'], ['≈' + Math.round(T.game_hours), 'game hours'], [T.systems, 'systems docked at'],
                 [fmt(T.docks), 'dockings'], [T.sessions, 'bounty hunts'], ['2 of 2', 'missions'], [T.deaths, 'deaths (back to a save)'],
                 [T.pods, 'escape pods used']];
  const st = document.getElementById('stats');
  for (const [v, l] of tiles) { const d = document.createElement('div'); d.className = 'stat'; d.innerHTML = `<b>${v}</b><span>${l}</span>`; st.appendChild(d); }

  // ---- rank chart: log tally against game hours, rank bands
  (function () {
    const svg = document.getElementById('rank');
    const W = 1100, H = 420, L = 150, R = 24, Tp = 26, B = 40;
    const S = D.series, hmax = Math.ceil(T.game_hours / 10) * 10;
    const x = h => L + (W - L - R) * h / hmax;
    const ymax = Math.log10(6500 + 1);
    const y = t => Tp + (H - Tp - B) * (1 - Math.log10(Math.max(0, t) + 1) / ymax);
    // bands
    RANKS.forEach(([th, name], i) => {
      const top = i + 1 < RANKS.length ? RANKS[i + 1][0] : 6500;
      if (i % 2 === 0) el('rect', { class: 'band', x: L, y: y(top), width: W - L - R, height: y(th) - y(top) }, svg);
      const mid = name === 'Elite' ? y(6400) - 6 : (y(th) + y(top)) / 2 + 4;
      el('text', { x: L - 10, y: mid, 'text-anchor': 'end', class: name === 'Elite' ? 'label-ink' : '' }, svg, name);
    });
    el('line', { class: 'grid', x1: L, x2: W - R, y1: y(6400), y2: y(6400), stroke: C.yellow, 'stroke-dasharray': '4 4' }, svg);
    for (let h = 0; h <= hmax; h += 10) {
      el('line', { class: 'axis', x1: x(h), x2: x(h), y1: H - B, y2: H - B + 5 }, svg);
      el('text', { x: x(h), y: H - B + 19, 'text-anchor': 'middle' }, svg, h === 0 ? '0 h' : h);
    }
    el('line', { class: 'axis', x1: L, x2: W - R, y1: H - B, y2: H - B }, svg);
    el('text', { x: W - R, y: H - 6, 'text-anchor': 'end' }, svg, 'game hours (hunting measured, hops at 3.5 min)');
    const d = S.map((p, i) => (i ? 'L' : 'M') + x(p.h).toFixed(1) + ',' + y(p.tally).toFixed(1)).join('');
    el('path', { d, fill: 'none', stroke: C.cyan, 'stroke-width': 2, 'stroke-linejoin': 'round' }, svg);
    // milestones
    const ms = [];
    const eq = (it) => (D.equip.find(e => e.item === it) || {}).h;
    ms.push([eq('military'), 'military laser']);
    const gal = D.events.filter(e => e.kind === 'galaxy');
    gal.forEach(g => ms.push([g.h, g.label]));
    for (let i = 1; i < S.length; i++) if (S[i].tally - S[i - 1].tally > 200) ms.push([S[i].h, '+256 mission']);
    ms.push([D.ranks_at['Elite'], 'Elite']);
    const at = h => { let best = S[0]; for (const p of S) if (Math.abs(p.h - h) < Math.abs(best.h - h)) best = p; return best; };
    ms.sort((a, b) => a[0] - b[0]);
    const seen = new Set();
    const ms2 = ms.filter(([h, l]) => h != null && !(l.startsWith('Galaxy') && seen.has(l)) && (seen.add(l) || true));
    // labels in a column to the right of the cluster, joined to their points by leader lines
    const colX = x(44), y0 = y(1000) + 2;
    // label rows in the order of the points' heights, so no leader lines cross
    const rows = ms2.filter(m => m[1] !== 'Elite').map(m => m.concat([at(m[0]).tally])).sort((a, b) => b[2] - a[2]);
    const rowOf = new Map(rows.map((m, i) => [m[1] + m[0], i]));
    ms2.forEach(([h, label]) => {
      const i = rowOf.get(label + h);
      const p = at(h), px = x(h), py = y(p.tally);
      if (label === 'Elite') {
        el('circle', { cx: px, cy: py, r: 5, fill: C.yellow, stroke: '#0c0f17', 'stroke-width': 2 }, svg);
        el('text', { x: px - 8, y: py + 20, 'text-anchor': 'end', class: 'label-ink' }, svg, `Elite at ${p.h.toFixed(0)} game hours`);
        return;
      }
      const ly = y0 + i * 19;
      el('path', { d: `M${px},${py}L${colX - 8},${ly - 4}`, stroke: C.faint, fill: 'none' }, svg);
      el('circle', { cx: px, cy: py, r: 4.5, fill: C.yellow, stroke: '#0c0f17', 'stroke-width': 2 }, svg);
      el('text', { x: colX, y: ly, class: 'label-ink' }, svg, `${label}`);
      el('text', { x: colX + 130, y: ly }, svg, `${p.h.toFixed(1)} h · tally ${fmt(p.tally)}`);
    });
    // hover
    const cross = el('line', { x1: 0, x2: 0, y1: Tp, y2: H - B, stroke: C.faint, opacity: 0 }, svg);
    const dot = el('circle', { r: 4, fill: C.cyan, opacity: 0 }, svg);
    const hit = el('rect', { x: L, y: Tp, width: W - L - R, height: H - Tp - B, fill: 'transparent' }, svg);
    hit.addEventListener('pointermove', evt => {
      const pt = svgPoint(svg, evt), h = (pt.x - L) / (W - L - R) * hmax, p = at(h);
      cross.setAttribute('x1', x(p.h)); cross.setAttribute('x2', x(p.h)); cross.setAttribute('opacity', 1);
      dot.setAttribute('cx', x(p.h)); dot.setAttribute('cy', y(p.tally)); dot.setAttribute('opacity', 1);
      showTip(evt, `${p.h.toFixed(1)} game h<br>tally <b>${fmt(p.tally)}</b> · ${rankOf(p.tally)}`);
    });
    hit.addEventListener('pointerleave', () => { cross.setAttribute('opacity', 0); dot.setAttribute('opacity', 0); hideTip(); });
  })();

  // ---- cash
  (function () {
    const svg = document.getElementById('cash');
    const W = 540, H = 300, L = 54, R = 12, Tp = 14, B = 34, S = D.series;
    const hmax = Math.ceil(T.game_hours / 10) * 10, ymax = 8000;
    const x = h => L + (W - L - R) * h / hmax, y = c => Tp + (H - Tp - B) * (1 - c / ymax);
    for (let c = 0; c <= ymax; c += 2000) {
      el('line', { class: 'grid', x1: L, x2: W - R, y1: y(c), y2: y(c) }, svg);
      el('text', { x: L - 8, y: y(c) + 4, 'text-anchor': 'end' }, svg, c === 0 ? '0' : fmt(c));
    }
    for (let h = 0; h <= hmax; h += 20) el('text', { x: x(h), y: H - B + 18, 'text-anchor': 'middle' }, svg, h === 0 ? '0 h' : h);
    el('text', { x: L, y: Tp - 2, class: 'label-ink' }, svg, 'Cr');
    // the highest cash in each game hour (between saves it swings as cargo is bought and sold)
    const bin = new Map();
    S.filter(p => p.cash != null).forEach(p => { const k = Math.floor(p.h); if (!bin.has(k) || bin.get(k).cash < p.cash) bin.set(k, { h: k + .5, cash: p.cash }); });
    const pts = [...bin.values()].sort((a, b) => a.h - b.h);
    const d = pts.map((p, i) => (i ? 'L' : 'M') + x(p.h).toFixed(1) + ',' + y(p.cash).toFixed(1)).join('');
    el('path', { d: d + `L${x(pts[pts.length - 1].h)},${y(0)}L${x(pts[0].h)},${y(0)}Z`, fill: C.cyan, opacity: .08 }, svg);
    el('path', { d, fill: 'none', stroke: C.cyan, 'stroke-width': 1.5 }, svg);
    const pk = pts.reduce((a, b) => (b.cash > a.cash ? b : a));
    el('circle', { cx: x(pk.h), cy: y(pk.cash), r: 4.5, fill: C.yellow, stroke: '#0c0f17', 'stroke-width': 2 }, svg);
    el('text', { x: x(pk.h) + 8, y: y(pk.cash) + 4, class: 'label-ink' }, svg, fmt(pk.cash) + ' Cr, then the military laser');
    const hit = el('rect', { x: L, y: Tp, width: W - L - R, height: H - Tp - B, fill: 'transparent' }, svg);
    const dot = el('circle', { r: 3.5, fill: C.cyan, opacity: 0 }, svg);
    hit.addEventListener('pointermove', evt => {
      const h = (svgPoint(svg, evt).x - L) / (W - L - R) * hmax;
      let p = pts[0]; for (const q of pts) if (Math.abs(q.h - h) < Math.abs(p.h - h)) p = q;
      dot.setAttribute('cx', x(p.h)); dot.setAttribute('cy', y(p.cash)); dot.setAttribute('opacity', 1);
      showTip(evt, `${p.h.toFixed(1)} game h<br><b>${fmt(p.cash)} Cr</b>`);
    });
    hit.addEventListener('pointerleave', () => { dot.setAttribute('opacity', 0); hideTip(); });
    document.getElementById('tradeline').textContent =
      `Cargo bought for ${fmt(D.trade.spent)} Cr and sold for ${fmt(D.trade.income)} Cr: ${fmt(D.trade.income - D.trade.spent)} Cr of trading profit. The rest came from bounties and the 5,000 Cr Constrictor reward.`;
  })();

  // ---- horizontal bars
  function bars(id, rows, unit, opts = {}) {
    const svg = document.getElementById(id);
    const W = 540, L = opts.left || 150, R = 64, rowH = opts.rowH || 22, Tp = 4;
    const H = Tp + rows.length * rowH + 4;
    svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    const max = Math.max(...rows.map(r => r[1]));
    rows.forEach(([name, v, color], i) => {
      const y0 = Tp + i * rowH, w = (W - L - R) * v / max;
      el('text', { x: L - 10, y: y0 + rowH / 2 + 4, 'text-anchor': 'end', class: 'label-ink' }, svg, name);
      const r = el('rect', { x: L, y: y0 + 4, width: Math.max(2, w), height: rowH - 8, rx: 2, fill: color || C.s1 }, svg);
      el('text', { x: L + w + 8, y: y0 + rowH / 2 + 4 }, svg, fmt(v));
      const hit = el('rect', { x: 0, y: y0, width: W, height: rowH, fill: 'transparent' }, svg);
      hit.addEventListener('pointermove', evt => showTip(evt, `${name}<br><b>${fmt(v)}</b> ${unit}`));
      hit.addEventListener('pointerleave', hideTip);
    });
  }
  const units = { Gold: 'kg', Platinum: 'kg', 'Gem-Stones': 'g' };
  bars('goods', Object.entries(D.bought).filter(([, v]) => v > 0).sort((a, b) => b[1] - a[1])
    .map(([k, v]) => [k + (units[k] ? ` (${units[k]})` : ''), v]), 'units bought');
  bars('foes', Object.entries(D.engaged).slice(0, 14).map(([k, v]) => [k, v, k === 'Cargo canister' ? C.red : C.s1]),
    'times targeted', { left: 170 });

  // ---- star maps
  (function () {
    const host = document.getElementById('maps');
    const named = { 0: ['Lave', 'Ararus', 'Isinor', 'Ensoreus'], 1: ['Orarra'], 2: ['Ceerdi', 'Birera', 'Ersodi'] };
    const below = { Birera: 1, Ararus: 1 }, leftOf = { Ersodi: 1 };
    Object.keys(D.maps).forEach(g => {
      const div = document.createElement('div'); div.className = 'map';
      const docks = D.docks.filter(d => String(d.g) === String(g));
      const vis = D.visited.filter(v => String(v.g) === String(g));
      div.innerHTML = `<h3>Galaxy ${+g + 1} · ${vis.length} systems · ${docks.length} dockings</h3>`;
      const svg = el('svg', { class: 'chart', viewBox: '-6 -6 268 140', role: 'img', 'aria-label': `Galaxy ${+g + 1} chart` }, div);
      el('rect', { x: -6, y: -6, width: 268, height: 140, fill: '#04050a', rx: 3 }, svg);
      D.maps[g].forEach(s => el('circle', { cx: s.x, cy: s.y / 2, r: .8, fill: C.faint }, svg));
      if (docks.length > 1) {
        const d = docks.map((p, i) => (i ? 'L' : 'M') + p.x + ',' + (p.y / 2)).join('');
        el('path', { d, fill: 'none', stroke: C.cyan, 'stroke-width': .5, opacity: .45, 'stroke-linejoin': 'round' }, svg);
      }
      const max = Math.max(...vis.map(v => v.n));
      vis.forEach(v => {
        const s = D.maps[g].find(q => q.name === v.name); if (!s) return;
        const r = 1.4 + 3.4 * Math.sqrt(v.n / max);
        if (s.gov === 0) el('circle', { cx: s.x, cy: s.y / 2, r: r + 1.6, fill: 'none', stroke: C.red, 'stroke-width': .7 }, svg);
        const c = el('circle', { cx: s.x, cy: s.y / 2, r, fill: C.cyan, stroke: '#04050a', 'stroke-width': .6 }, svg);
        const govs = ['Anarchy', 'Feudal', 'Multi-government', 'Dictatorship', 'Communist', 'Confederacy', 'Democracy', 'Corporate State'];
        const hit = el('circle', { cx: s.x, cy: s.y / 2, r: Math.max(r + 2, 5), fill: 'transparent' }, svg);
        hit.addEventListener('pointermove', evt => showTip(evt, `${v.name} (${s.x}, ${s.y})<br>${govs[s.gov]} · <b>${v.n}</b> dockings`));
        hit.addEventListener('pointerleave', hideTip);
        if ((named[g] || []).includes(v.name)) {
          const t = el('text', { x: leftOf[v.name] ? s.x - r - 2.5 : s.x + r + 2.5, y: s.y / 2 + (below[v.name] ? 7 : 2.5), 'text-anchor': leftOf[v.name] ? 'end' : 'start', class: 'label-ink' }, svg, v.name);
          t.style.fontSize = '7px';
        }
      });
      host.appendChild(div);
    });
  })();

  // ---- the fight: our energy and shields, kills, laser
  const FT = D.fight;
  const end = FT.end / 50;
  (function () {
    const svg = document.getElementById('fight');
    const W = 540, H = 320, L = 40, R = 12, Tp = 58, B = 40;
    const x = s => L + (W - L - R) * s / end, y = v => Tp + (H - Tp - B) * (1 - v / 255);
    [0, 64, 128, 192, 255].forEach(v => { el('line', { class: 'grid', x1: L, x2: W - R, y1: y(v), y2: y(v) }, svg); el('text', { x: L - 8, y: y(v) + 4, 'text-anchor': 'end' }, svg, v); });
    for (let s = 0; s <= end; s += 15) el('text', { x: x(s), y: H - B + 30, 'text-anchor': 'middle' }, svg, mmss(s));
    const series = [['E', 'energy', C.s1], ['fs', 'front shield', C.s2], ['as_', 'aft shield', C.s3]];
    const lg = document.getElementById('fightlegend');
    series.forEach(([k, name, col]) => {
      const d = FT.us.map((p, i) => (i ? 'L' : 'M') + x(p.f / 50).toFixed(1) + ',' + y(p[k]).toFixed(1)).join('');
      el('path', { d, fill: 'none', stroke: col, 'stroke-width': 2, 'stroke-linejoin': 'round' }, svg);
      lg.insertAdjacentHTML('beforeend', `<span><i style="background:${col}"></i>${name}</span>`);
    });
    FT.us.forEach(p => { if (p.fire) el('rect', { x: x(p.f / 50), y: H - B + 4, width: 1.2, height: 8, fill: C.yellow, opacity: .8 }, svg); });
    // kills along the top, two rows
    const kills = FT.tracks.filter(t => t.dead).map(t => [t.dead / 50, t.name]).sort((a, b) => a[0] - b[0]);
    kills.forEach(([s, name], i) => {
      el('line', { x1: x(s), x2: x(s), y1: Tp - 6, y2: H - B, stroke: C.faint, 'stroke-dasharray': '2 3' }, svg);
      el('path', { d: `M${x(s)},${Tp - 6}l-4,-7h8z`, fill: C.yellow }, svg);
      const t = el('text', { x: x(s), y: i % 2 ? 12 : 26, 'text-anchor': 'middle', class: 'label-ink' }, svg, name.replace(' (pirate)', ''));
    });
    const hit = el('rect', { x: L, y: Tp, width: W - L - R, height: H - Tp - B, fill: 'transparent' }, svg);
    const cross = el('line', { y1: Tp, y2: H - B, stroke: C.faint, opacity: 0 }, svg);
    hit.addEventListener('pointermove', evt => {
      const s = (svgPoint(svg, evt).x - L) / (W - L - R) * end;
      let p = FT.us[0]; for (const q of FT.us) if (Math.abs(q.f / 50 - s) < Math.abs(p.f / 50 - s)) p = q;
      cross.setAttribute('x1', x(p.f / 50)); cross.setAttribute('x2', x(p.f / 50)); cross.setAttribute('opacity', 1);
      showTip(evt, `${mmss(p.f / 50)}<br>energy <b>${p.E}</b> · front ${p.fs} · aft ${p.as_}${p.fire ? '<br>firing' : ''}`);
    });
    hit.addEventListener('pointerleave', () => { cross.setAttribute('opacity', 0); hideTip(); });
  })();

  // ---- 3D: each enemy's path in CLAUDE's frame (x right, y up, z ahead)
  (function () {
    const cv = document.getElementById('space'), ctx = cv.getContext('2d');
    const tl = document.getElementById('tracklegend');
    FT.tracks.forEach((t, i) => tl.insertAdjacentHTML('beforeend', `<span><i style="background:${SHIPC[i % 7]}"></i>${t.name.replace(' (pirate)', '')}</span>`));
    let yaw = -0.7, pitch = 0.5, t = 1, playing = false, dragging = null;   // at rest: the whole fight drawn; Play replays it
    const slider = document.getElementById('t'), label = document.getElementById('tlabel'), btn = document.getElementById('play');
    function size() { const r = cv.getBoundingClientRect(), dpr = devicePixelRatio || 1; cv.width = r.width * dpr; cv.height = r.height * dpr; ctx.setTransform(dpr, 0, 0, dpr, 0, 0); return r; }
    let box = size();
    addEventListener('resize', () => { box = size(); draw(); });
    function proj(x, y, z) {
      // our frame -> view: yaw about y, then pitch about x; z goes into the screen
      const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
      let X = x * cy - z * sy, Z = x * sy + z * cy, Y = y;
      const Y2 = Y * cp - Z * sp, Z2 = Y * sp + Z * cp;
      const f = 1 / (1 + Z2 / 30000), s = Math.min(box.width, box.height) / 17000;
      return [box.width / 2 + X * s * f, box.height / 2 - Y2 * s * f];
    }
    function line(a, b, col, w) { const p = proj(...a), q = proj(...b); ctx.strokeStyle = col; ctx.lineWidth = w; ctx.beginPath(); ctx.moveTo(p[0], p[1]); ctx.lineTo(q[0], q[1]); ctx.stroke(); }
    function draw() {
      const W = box.width, H = box.height;
      ctx.fillStyle = '#03040a'; ctx.fillRect(0, 0, W, H);
      // grid on the y = 0 plane, 2,000-unit squares
      ctx.globalAlpha = .55;
      for (let g = -10000; g <= 10000; g += 2000) { line([g, 0, -10000], [g, 0, 10000], '#1c2232', 1); line([-10000, 0, g], [10000, 0, g], '#1c2232', 1); }
      ctx.globalAlpha = 1;
      // CLAUDE: a Cobra-ish wireframe at the origin, nose along +z
      const S = 380, hull = [[0, 0, 1.6], [-1.6, 0, -1], [1.6, 0, -1], [0, .45, -1], [0, -.3, -1]].map(v => v.map(c => c * S));
      [[0, 1], [0, 2], [1, 2], [0, 3], [1, 3], [2, 3], [1, 4], [2, 4], [0, 4]].forEach(([a, b]) => line(hull[a], hull[b], '#e7ebf3', 1.2));
      line([0, 0, 0], [0, 0, 4200], '#e7ebf3', .8);
      const now = t * FT.end;
      FT.tracks.forEach((tr, i) => {
        const col = SHIPC[i % 7];
        const pts = tr.pts.filter(p => p[0] <= now);
        if (pts.length < 2) return;
        ctx.strokeStyle = col; ctx.lineWidth = 1.6; ctx.globalAlpha = .9; ctx.beginPath();
        pts.forEach((p, k) => { const q = proj(p[1], p[2], p[3]); k ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1]); });
        ctx.stroke(); ctx.globalAlpha = 1;
        const last = pts[pts.length - 1], q = proj(last[1], last[2], last[3]);
        if (tr.dead && now >= tr.dead) {
          ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(q[0], q[1], 7, 0, 7); ctx.stroke();
        } else {
          ctx.fillStyle = col; ctx.beginPath(); ctx.arc(q[0], q[1], 4, 0, 7); ctx.fill();
        }
      });
      label.textContent = mmss(now / 50);
      slider.value = Math.round(t * 1000);
    }
    function tick() {
      if (playing && !dragging) { t += 1 / (60 * 14); if (t > 1.06) t = 0; draw(); }
      requestAnimationFrame(tick);
    }
    slider.addEventListener('input', () => { t = slider.value / 1000; playing = false; btn.textContent = 'Play'; draw(); });
    btn.addEventListener('click', () => { playing = !playing; btn.textContent = playing ? 'Pause' : 'Play'; if (playing && t >= 1) t = 0; });
    btn.textContent = playing ? 'Pause' : 'Play';
    cv.addEventListener('pointerdown', e => { dragging = [e.clientX, e.clientY, yaw, pitch]; cv.setPointerCapture(e.pointerId); cv.style.cursor = 'grabbing'; });
    cv.addEventListener('pointermove', e => { if (!dragging) return; yaw = dragging[2] + (e.clientX - dragging[0]) * .008; pitch = Math.max(-1.4, Math.min(1.4, dragging[3] + (e.clientY - dragging[1]) * .008)); draw(); });
    cv.addEventListener('pointerup', () => { dragging = null; cv.style.cursor = 'grab'; });
    draw(); requestAnimationFrame(tick);
  })();
})();

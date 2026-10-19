// Showcase fighting (curious mode only): while we're clearly on top, fights are flown to be watched
// (docs/fighting-showcase.md); the moment we aren't, efficient mode's rules fly them, and back again once we
// are. combat.c calls the hooks below; each returns efficient mode's own decision when the show is off.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pilot.h"
#include "show.h"

extern int g_curious;
int g_show = 1;                     // (SHOW=0 in the lab or run_sim: curious fights as efficient)
show_params_t g_sp = {
    // hold fire until it's 24 px across (a Cobra at ~1000), closing at full speed beyond 2500 (lab, 600 fights, first
    // meter: 10 / 14 / 18 / 24 / 32 / 40 / 56 px: 31.8 / 34.0 / 34.6 / 35.5 / 32.1 / 31.5 / 30.5; closer than 24 the
    // kills get no closer, they go past us instead). Thargoids too, through the margin (ws 1: +2.0, no more deaths)
    .fire_px = 32, .hold_max = 0, .safe_shield = 96, .safe_energy = 200, .close_speed = 40, .far = 2500, .ws = 1, .ws_fire_px = 32,
    // a missile in 30% of fights, the bomb in half of those where a pack is in close (variety: rare by design)
    .msl_odds = 0.3f, .msl_near = 1200, .msl_far = 6000, .bomb_odds = 0.5f, .bomb_n = 3, .bomb_dist = 3000, .finish_e = 0,
    .glance = 1, .glance_d = 1200, .glance_max = 2.0f, .glance_gap = 6.0f, .glance_px = 20,
    // the margin: show above 2, efficient below 1 (on 2 / 4 / 8: show 35.1 / 32.4 / 25.1, deaths 2 / 1 / 1 in 600)
    .gate = 1, .on_m = 1, .off_m = 0.5f, .dwell = 100, .hit_s = 0.5f, .budget_base = 600, .budget_per = 0, .hold_s = 6, .reserve = 96,
    // a flyby in a quarter of fights (and at every rare type): side glances and variety, at some cost in close kills.
    // No show on a pulse laser (min_laser 1; weak-ship corpus, 628 fights: show 37.4 -> 36.3, deaths 5 -> 4; the
    // curious career's early deaths were on the pulse laser); a beam laser is enough
    .fb_odds = 0.25f, .fb_side = 500, .fb_start = 4000, .knife_d = 900, .fb_speed = 10, .novel_look = 1, .min_laser = 1,
    // round 2 (lab, tune 600, score 2; round 1's behaviour 28.7): a 12 deg weave while holding fire (33.8 alone: static
    // runs over 2 s 29% -> 10% of fight frames), follow a flyby into the side and rear views, glances on predicted
    // positions, an empty screen spends the budget double and it counts 4 enemies at most (p90 fight 62 -> 44 s), the
    // bomb at Thargons; witchspace: show from margin 1.2, off below 0.7, Thargoids held to 24 px (263 witchspace
    // fights: 32.0 -> 36.2, no deaths). Together 34.9
    .weave_deg = 12, .weave_s = 3.0f, .follow_s = 4, .extend_s = 1.5f, .empty_x = 2, .bomb_ws = 1, .glance_pred = 1, .ws_on_m = 1.2f, .ws_off_m = 0.7f, .weave_stop = 0.7f, .budget_nmax = 4, .cam = 0, .tail_rear = 0,
    // round 5: shoot a target about to pass within 300 in the next 10 game loops whatever its size (tune 600: rams
    // 363 -> 138 per 1000 fights, kills drawn 20 px+ 17 -> 25%, deaths 21.7 -> 8.3 per 1000, an enemy 20 px+ 5.7 -> 6.3 s
    // a fight; dodging rams further ahead in the show: worse, 423 rams); no show under 1000 Cr (the round-4 career's
    // poverty trap: equipment the show's damage wrecked and bombs, 80 h at 650-900 Cr); chasing on boredom: little
    // (6.3 s, more rams), off
    // round 4 (the user: "take risks for style up to 5%"; "don't worry about ending fights unless they are
    // repetitive"): the margin on at 1, off at 0.5 (0.5 / 0.25 gave the same size x time with twice the deaths, 4.3%
    // in the lab and 5.7% of fights in a career: 1 / 0.5 is 2.2%); at first on at 0.5, off at 0.25 (witchspace keeps 1.2 / 0.7: lower, the ship bails out of
    // witchspace and half the Thargoids live); the time budget a 10-minute safety net; the show hands over after 12 s
    // with nothing new on screen (tune 600: an enemy 20 px+ 3.8 -> 5.8 s a fight, size-time 460 -> 526 px.s a minute,
    // kills drawn 20 px+ 14 -> 18%, deaths 12 -> 26 of 600 (4.3%), completion 0.91 -> 0.86; witchspace, 263: 13.3 s a
    // fight at 20 px+, 3 deaths)
    // round 3, judged first by size x time (the user: "how big and for how long you can get enemies on screen.
    // Distance shots don't do it"): no shot under 32 px, Thargoids too, and no shots at receding ships (round 3's first
    // fire_recede was a distance shot); toying: hold a target 24 px or more for 12 s of big time before the kill, sitting
    // 600 behind it; a budget of 30 s + 15 s an enemy (tune 600: an enemy 20 px or more 1.0 -> 3.8 s a fight, 2.7 -> 6.2 s
    // a minute; size-time 345 -> 460 px.s a minute; deaths 3 -> 12 of 600, 2%, inside the 5% budget). Below: the round-3
    // first pass (tune 600, score 2; round 2 on the merged code 35.3): fire at a target opening on us whatever its size
    // (37.1 alone: the rear-view tail, a receding ship held because it was small); stop the weave 25 game loops
    // before the firing range (close kills 0.16 -> 0.21); the knife-edge on any close pass; together 36.7. Witchspace:
    // target by group (263 fights: 35.2 -> 35.9, side views 25 -> 31% of fights). Not taken: the finishing window
    // (flat), no weave on targets turning away (34.0: static back up)
    .ws_group = 1, .group_d = 2500, .finish_s = 0, .finish_px = 8, .weave_fac = -2, .pass_knife = 1, .pass_cpa = 600, .fire_recede = 0, .weave_t = 25, .toy_px = 24, .toy_s = 12, .follow_d = 600, .bore_s = 12, .frugal = 1000, .pursue = 0, .col_fire = 1, .col_t = 25, .col_r = 600, .ram_iters = 0, .noplan = 0, .toy_safe = 1000, .strafe = 2, .strafe_miss = 0.6f, .strafe_off = 3, .strafe_k = 5, .strafe_temp = 120, .strafe_e = 80, .strafe_px = 8,
};
static struct { int on, was_on, fights, n_max, spent, empty, left, bore, bview, bslot, pursuing, want_msl, want_bomb, msl_target, msl_fired, our_msl; uint32_t t0, since, last; double margin, bpx, boff; } S;
static struct { int view; uint32_t until, min_until, next; } G;
// toying (toy_px): big time on the target before the kill: frames it's been toy_px or more, by target
static struct { int type, frames; double x, y, z; } TY;
static struct { int view; uint32_t since; } C;      // the camera
// the flyby: one target a fight, met on its first close approach without a shot, aimed beside so it goes past
// close on one side (where we glance at it), then fought as usual once it's by. state 0 none yet, 1 on, 2 done
static struct { int want, state, type, side; double x, y, z, dmin; uint32_t t0, passed; } FB;
// novelty: the rare ships (Thargoid, Thargon, Constrictor, Cougar) always; and in a career, a type met for the
// first time (g_show_seen: every type met so far; the lab has no career behind it and sets g_show_lab)
static uint64_t g_show_seen;
int g_show_lab;
int show_novel(int type) {
    if (type >= 29 && type <= 32) return 1;
    return !g_show_lab && type > 8 && type < 64 && !(g_show_seen >> type & 1);
}
int g_show_spent;                   // (the lab: this fight's show budget ran out)
int g_show_mode = -1;               // (the lab's meter: 1 show, 0 efficient, -1 not fighting or not curious)

int show_set_param(const char *name, double v) {
#define F(f) if (!strcmp(name, #f)) { g_sp.f = (__typeof__(g_sp.f)) v; return 1; }
    F(fire_px) F(hold_max) F(safe_shield) F(safe_energy) F(close_speed) F(far) F(ws) F(ws_fire_px)
    F(msl_odds) F(msl_near) F(msl_far) F(bomb_odds) F(bomb_n) F(bomb_dist) F(finish_e) F(glance) F(glance_d) F(glance_max) F(glance_gap) F(glance_px)
    F(gate) F(on_m) F(off_m) F(dwell) F(hit_s) F(budget_base) F(budget_per) F(hold_s) F(reserve) F(fb_odds) F(fb_side) F(fb_start) F(knife_d) F(fb_speed) F(novel_look) F(min_laser) F(weave_deg) F(weave_s) F(follow_s) F(extend_s) F(empty_x) F(bomb_ws) F(glance_pred) F(ws_on_m) F(ws_off_m) F(weave_stop) F(budget_nmax) F(cam) F(tail_rear) F(ws_group) F(group_d) F(finish_s) F(finish_px) F(weave_fac) F(pass_knife) F(pass_cpa) F(fire_recede) F(weave_t) F(toy_px) F(toy_s) F(follow_d) F(bore_s) F(frugal) F(pursue) F(col_fire) F(col_t) F(col_r) F(ram_iters) F(noplan) F(toy_safe) F(strafe) F(strafe_miss) F(strafe_off) F(strafe_k) F(strafe_temp) F(strafe_e) F(strafe_px)
#undef F
    return 0;
}

static double size_px(const ship_t *s, double d);
static int in_view(const ship_t *s, int view);
static double dist_of(const ship_t *s) { return sqrt((double) s->x * s->x + (double) s->y * s->y + (double) s->z * s->z); }
// its range's rate, units a game loop (+ opening): its motion along the line to us, less ours along z
static double range_rate(const ship_t *t) {
    double d = dist_of(t) + 1, sp = 1.5 * t->speed;
    return (t->x * t->nosev[0] * sp + t->y * t->nosev[1] * sp + t->z * (t->nosev[2] * sp - speed())) / d;
}

// The superiority margin: what we can lose before the reserve (energy over `reserve`, and the weaker
// shield: the show flies at them head-on, and a dead front shield behind a full aft one is no cover; the
// first version averaged the two and the hurt starts died, 12-19 of 180) over the damage they'll do before
// we've killed them all, holding fire for the show (hold_s seconds of all of them shooting) included. Ours: each enemy takes a turn (2 s) and its energy at our laser's rate (shots an iteration x
// power x half of them hitting x ~10 iterations a second: military ~115 a second, beam 75, pulse 15; only
// the military laser touches a Constrictor or Cougar, at a quarter). Theirs: one hit (blueprint laser x 4
// + 1: a Mamba 9, an Asp 20) every two seconds each (hit_s a second: a tactics pass every 8 iterations,
// lined up on us part of the time), Thargoids half again for their Thargons, until each dies, quickest
// kill first. A missile coming for us is 250 more unless the ECM will take it. Above on_m, show; below
// off_m, efficient; and either holds dwell frames before it may change (no flapping).
static double margin(const ship_t *sh, int n) {
    int l = laser(0), p = l & 0x7F;
    double rate = l == 0 ? 0 : (l & 0x80) ? 1.0 : p == 15 ? 0.2 : 0.04;
    double t_kill[NOSH], dmg[NOSH];
    int k = 0;
    double missiles_in = 0;
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_in_space(s) || ship_dead(s)) continue;
        if (s->type == 1) { if (s->ai & 0x40) missiles_in += has_ecm() ? 0 : 250; continue; }
        if (!((s->newb & NEWB_HOSTILE) || s->type == 29 || s->type == 30) || (s->type >= 3 && s->type <= 8)) continue;
        if (dist_of(s) > 16000) continue;
        const shiptype_t *ty = ship_type(s->type);
        double pw = s->type >= 31 ? (l == 0x97 ? p / 4.0 : 0) : p;
        double dps = rate * pw * 0.5 * 10;
        t_kill[k] = 2 + (dps > 0 ? s->energy / dps : 120);
        dmg[k] = (ty ? 4 * ty->laser + 1 : 9) * g_sp.hit_s * (s->type == 29 ? 1.5 : 1);
        k++;
    }
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && t_kill[j] < t_kill[j - 1]; j--) {
            double a = t_kill[j]; t_kill[j] = t_kill[j - 1]; t_kill[j - 1] = a;
            a = dmg[j]; dmg[j] = dmg[j - 1]; dmg[j - 1] = a;
        }
    double t = 0, d = missiles_in;
    for (int i = 0; i < k; i++) t += t_kill[i], d += dmg[i] * (t + g_sp.hold_s);
    double have = energy() - g_sp.reserve + fmin(fsh(), ash());
    return have / fmax(d, 1.0);
}

// the old fixed gate (gate 0): a military laser ahead, ECM and an energy unit fitted, energy and both
// shields up at the start; off for the rest of the fight once a shield or the energy falls under its floor
static int fixed_ready(void) {
    return laser(0) == 0x97 && has_ecm() && has_energy_unit() && energy() >= 200 && fsh() >= 160 && ash() >= 160;
}
static int healthy(void) { return energy() >= g_sp.safe_energy && fsh() >= g_sp.safe_shield && ash() >= g_sp.safe_shield; }

int show_ready(void) { return S.on; }

void show_begin(void) {
    // (the same fight if the last engagement ended under 10 s ago: they went out of range and came back; the
    // budget runs on)
    if (now() - S.last > 500) S.t0 = now(), S.n_max = 0, S.spent = g_show_spent = 0, S.empty = 0, S.bore = 0, S.pursuing = 0;
    S.since = now();
    S.on = 0;
    S.msl_target = -1, S.msl_fired = 0, S.our_msl = 0;
    G.view = 0, G.next = 0;
    // the tricks this fight: by Elite's own random numbers (RAND, &02-&05), so they vary from fight to fight
    // (mixed: the plain bytes ORed gave 10 bits, and the flyby's draw from bits 6 up was always yes)
    uint32_t r = (uint32_t) R(0x02) | (uint32_t) R(0x03) << 8 | (uint32_t) R(0x04) << 16 | (uint32_t) R(0x05) << 24;
    r ^= r >> 16, r *= 0x7feb352dU, r ^= r >> 15, r *= 0x846ca68bU, r ^= r >> 16;
    S.want_msl = (r & 255) < g_sp.msl_odds * 256;
    S.want_bomb = (r >> 8 & 255) < g_sp.bomb_odds * 256;
    FB.want = (r >> 16 & 255) < g_sp.fb_odds * 256, FB.state = 0, FB.side = (r >> 24 & 1) ? 1 : -1;
    if (g_sp.gate == 0) S.on = g_show && g_curious && fixed_ready() && (g_sp.ws || !witchspace());
    S.was_on = S.on;
    if (S.on) S.fights++;
}
// once a frame, from engage(): show or efficient, now
void show_frame(const ship_t *sh, int n) {
    S.last = now();
    if (!g_show || !g_curious || (!g_sp.ws && witchspace())) { S.on = 0, g_show_mode = -1; return; }
    // (frugal, round 5: no show under `frugal` credits. The show's damage wrecks equipment (energy unit, ECM) and its
    // bombs need replacing: the round-4 career sat at 650-900 Cr in galaxy 6 for 80+ hours, unable to afford the
    // hyperdrive or the witchspace hop, hunting. Efficient fights cost less until the money's back)
    if (g_sp.frugal && cash() < g_sp.frugal * 10) { S.on = 0, g_show_mode = 0; return; }
    // the show's time budget: budget_base seconds and budget_per for each enemy (the most at once); spent,
    // efficient mode finishes the fight
    int ne = 0;
    for (int i = 0; i < n; i++)
        if (ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && sh[i].type != 1 && !(sh[i].type >= 3 && sh[i].type <= 8) &&
            ((sh[i].newb & NEWB_HOSTILE) || sh[i].type >= 29) && dist_of(&sh[i]) < 16000) ne++;
    if (ne > S.n_max) S.n_max = ne;
    for (int i = 0; i < n; i++)          // (met: once it's been close enough to see, 2000)
        if (ship_in_space(&sh[i]) && sh[i].type > 8 && sh[i].type < 64 && dist_of(&sh[i]) < 2000) g_show_seen |= 1ull << sh[i].type;
    {   // (an empty screen spends it empty_x times as fast: dead air is the tedious part. Nothing 4 px or more in the
        // view on screen this frame counts as empty)
        int seen = 0, v = space_view();
        for (int i = 0; i < n; i++)
            if (ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && sh[i].type != 1 && in_view(&sh[i], v) && size_px(&sh[i], dist_of(&sh[i])) >= 4) seen = 1;
        if (!seen && g_sp.empty_x > 1) S.empty += (int) g_sp.empty_x - 1;
    }
    // (budget_nmax caps the enemies it counts: a Thargoid's Thargons made witchspace budgets 75 s and more)
    int nb = g_sp.budget_nmax && S.n_max > g_sp.budget_nmax ? g_sp.budget_nmax : S.n_max;
    {
        int total = (int) ((g_sp.budget_base + g_sp.budget_per * nb) * 50), used = (int) (now() - S.t0 + S.empty);
        S.left = total - used;          // (frames of the budget left: the finishing window)
        if (!S.spent && used > total) S.spent = g_show_spent = 1;
    }
    // repetitive (bore_s, round 4: the user's "don't worry about ending fights unless they are repetitive"): a frame
    // is boring when nothing is drawn 4 px or more in the view on screen, or our target is drawn but hasn't changed
    // (its size within 1 px and its bearing within 2 deg of a second ago) and we're not firing: a long straight
    // chase, the same picture held. A boring stretch of bore_s seconds hands the fight over, as the budget did
    if (g_sp.bore_s && !S.spent && !witchspace()) {
        extern int combat_target(void);
        int v = space_view(), seen = 0, tg = combat_target();
        const ship_t *t = NULL;
        for (int i = 0; i < n; i++) {
            if (!ship_in_space(&sh[i]) || ship_dead(&sh[i]) || sh[i].type == 1) continue;
            if (in_view(&sh[i], v) && size_px(&sh[i], dist_of(&sh[i])) >= 4) seen = 1;
            if (sh[i].slot == tg) t = &sh[i];
        }
        // (not while the target is closing: the approach is part of the fight, though it's dots at first)
        int closing = t && range_rate(t) < -8;
        int boring = !seen && !closing;
        if (seen && t && (now() % 50) == 0) {           // (once a second: against a second ago)
            double px = in_view(t, v) ? size_px(t, dist_of(t)) : 0, d = dist_of(t) + 1;
            double off = acos(fmax(-1.0, fmin(1.0, (v == 1 ? -t->z : t->z) / d))) * 180 / M_PI;
            int same = t->slot == S.bslot && v == S.bview && fabs(px - S.bpx) < 1 && fabs(off - S.boff) < 2 && R(A_GNTMP) == 0;
            S.bpx = px, S.boff = off, S.bslot = t->slot, S.bview = v;
            if (same && !closing) S.bore += 49;       // (the second it covered)
            else S.bore = 0;
        } else if (boring) S.bore++;
        else if (!t) S.bore = 0;
        if (seen && !t) S.bore = 0;
        if (S.bore > g_sp.bore_s * 50) {
            // (pursue: the first time, chase it down at full speed instead: a runner is killed big from behind, not
            // by efficient mode at 5,000-9,000 (62% of round 3's kills were after the hand-over, nearly all small)
            if (g_sp.pursue && !S.pursuing) { S.pursuing = 1, S.bore = 0; status_("The show: chasing it down"); }
            else { S.spent = g_show_spent = 1; status_("The show: nothing new on screen, finishing it"); }
        }
    }
    if (S.spent) { S.on = 0, g_show_mode = 0; return; }
    if (g_sp.gate == 0) {
        if (S.on && !healthy()) S.on = 0;       // (once off, off for the fight)
    } else {
        S.margin = margin(sh, n);
        // (witchspace's own thresholds if set: three Thargoids put the margin near 1.2 at the start, under on_m, and
        // the Thargoid fights were efficient ones at range: specks)
        double on_m = witchspace() && g_sp.ws_on_m ? g_sp.ws_on_m : g_sp.on_m, off_m = witchspace() && g_sp.ws_off_m ? g_sp.ws_off_m : g_sp.off_m;
        int want = S.on ? S.margin >= off_m : S.margin >= on_m;
        // (the kit floor: min_laser 1, a beam laser or better ahead (bit 7: continuous fire); 2, military)
        if (g_sp.min_laser && !(laser(0) & 0x80)) want = 0;
        if (g_sp.min_laser >= 2 && laser(0) != 0x97) want = 0;
        // (a missile coming for us without an ECM, or the energy under 128: now, whatever the dwell)
        int urgent = !want && (energy() < 128 || (!has_ecm() && S.margin < off_m));
        if (want != S.on && (now() - S.since >= (uint32_t) g_sp.dwell || urgent)) {
            S.on = want, S.since = now();
            if (want) S.was_on = 1, S.fights++;
        }
    }
    g_show_mode = S.on;
}
int show_on(void) { return S.on; }

static double size_px(const ship_t *s, double d) {
    const shiptype_t *t = ship_type(s->type);
    return 256 * (t && t->area ? sqrt((double) t->area) : 80) / (d + 1);
}
// in that view's picture (PLUT turns the ship into the view: rear (-x, y, -z), left (z, y, -x), right (-z, y, x);
// PROJ: |x| < z/2, |y| < 3z/8)
static int in_view(const ship_t *s, int view) {
    double x = s->x, z = s->z;
    if (view == 1) x = -x, z = -z;
    else if (view == 2) { double t = x; x = z, z = -t; }
    else if (view == 3) { double t = x; x = -z, z = t; }
    return z > 0 && fabs(x) < z / 2 && fabs((double) s->y) < z * 0.375;
}

// the view to show: a glance left or right as a ship goes past close, then back. Our controls act on the ship
// whatever the view (MVEIT turns the universe in the front frame), so the steering flies on through it; but
// only the view's own laser fires, and there are none to the sides: no shots while we look
// where it'll be k iterations on: its own motion, and ours along z
static ship_t ahead(const ship_t *s, double k) {
    ship_t q = *s;
    double sp = 1.5 * s->speed;
    q.x = (int32_t) (s->x + s->nosev[0] * sp * k), q.y = (int32_t) (s->y + s->nosev[1] * sp * k), q.z = (int32_t) (s->z + (s->nosev[2] * sp - speed()) * k);
    return q;
}
static int seen_in(const ship_t *s, int view) { return in_view(s, view) && size_px(s, dist_of(s)) >= 4; }
int show_view(int view, const ship_t *sh, int n, const ship_t *t) {
    if (!show_on() || !g_sp.glance) { G.view = 0; return view; }
    for (int i = 0; i < n; i++)         // (an incoming missile: eyes front)
        if (sh[i].type == 1 && (sh[i].ai & 0x40) && !ship_dead(&sh[i])) { G.view = 0; return view; }
    if (G.view) {
        int still = 0;
        for (int i = 0; i < n; i++)
            if (ship_is_ship(&sh[i]) && ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && in_view(&sh[i], G.view) && dist_of(&sh[i]) < 2 * g_sp.glance_d) still = 1;
        // (glance_pred: an empty view ends at once, not after a minimum: the side glance that showed nothing)
        if (now() < G.until && (still || (!g_sp.glance_pred && now() < G.min_until))) return G.view;
        G.view = 0;
    }
    // follow: after a flyby, for follow_s, whichever view has it (side, then rear as it goes astern), until it's
    // in the front picture again
    if (g_sp.follow_s && FB.state == 2 && t && t->type == FB.type && now() - FB.passed < (uint32_t) (g_sp.follow_s * 50) && view == 0) {
        if (seen_in(t, 0)) return 0;
        for (int v = 1; v <= 3; v++) if (seen_in(t, v)) return v;
        if (space_view() && seen_in(t, space_view())) return space_view();
    }
    // the camera (cam 1; witchspace only with cam 2... cam 1 is witchspace, 2 everywhere): when the front picture
    // has no enemy and the target is well off the nose (no shot coming), the view with the most enemies in it,
    // each choice held 1.5 s. (Thargoid fights: the others sat abeam and behind while we turned on one)
    if (g_sp.cam && (witchspace() || g_sp.cam >= 2) && view == 0 && t) {
        double td = dist_of(t), toff = acos(fmax(-1.0, fmin(1.0, t->z / (td + 1)))) * 180 / M_PI;
        int cnt[4] = {0};
        for (int i = 0; i < n; i++)
            if (ship_is_ship(&sh[i]) && ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && sh[i].type != 1 && ((sh[i].newb & NEWB_HOSTILE) || sh[i].type >= 29))
                for (int v = 0; v < 4; v++) cnt[v] += seen_in(&sh[i], v);
        if (C.view && now() - C.since < 75 && cnt[C.view]) return C.view;
        int best = 0;
        if (!cnt[0] && toff > 30) for (int v = 1; v < 4; v++) if (cnt[v] > cnt[best]) best = v;
        if (best != C.view) C.view = best, C.since = now();
        if (best) return best;
    }
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_is_ship(s) || !ship_in_space(s) || ship_dead(s) || !(s->newb & NEWB_HOSTILE) || s->type == 1) continue;
        double d = dist_of(s);
        if (d > g_sp.glance_d) continue;
        int side = s->x < 0 ? 2 : 3;
        if (s->z < -d / 2 || now() < G.next || view != 0 || size_px(s, d) < g_sp.glance_px) continue;
        // (in that side's picture, or on its way there: over 40 deg off the nose, level enough (the side view is
        // 3/8 as tall as it is deep) and coming past: the view key takes a few frames, and a close pass is quick)
        double off = acos(fmax(-1.0, fmin(1.0, s->z / (d + 1)))) * 180 / M_PI;
        int coming = off > 40 && fabs((double) s->y) < fabs((double) s->x) * 0.5 && s->nosev[2] * 1.5 * s->speed - speed() < 0;
        // (glance_pred: coming means in that view's picture a game loop and a half on (the view key takes ~6 frames),
        // not just heading that way: the Thargoid glance showed an empty view)
        if (g_sp.glance_pred) { ship_t q = ahead(s, 1.5); coming = in_view(&q, side); }
        if (!in_view(s, side) && !coming) continue;
        G.view = side, G.until = now() + (uint32_t) (g_sp.glance_max * 50), G.min_until = now() + 60, G.next = now() + (uint32_t) (g_sp.glance_gap * 50);
        status_("A look %s", side == 2 ? "left" : "right");
        return side;
    }
    return view;
}

// fire at it? Not until it's big on screen (the kill seen, not a dot going out), unless it's taking too long;
// and not at a ship our missile is on its way to
long g_show_why[6];          // (lab: frames fired by why: show off at the start, gone off since, missile, timeout, big enough; held)
// strafing (strafe): toying, the user's steer: "the occasional strafe with a laser without killing, or even a near
// miss, is more interesting to watch than just following". Bursts of 0.3-0.8 s every 1.5-4 s: near misses (aimed beside
// it, along its own side vector, fired while it's a few radii off the line of fire) and grazing hits (on it, only
// while its energy is over a floor, so the kill still comes at the big close moment); none with the laser warm, so
// the kill isn't held up by the heat. Only while toying is allowed (toy_safe keeps it off a ship coming at us)
static struct { uint32_t next, until; int miss, side; long bursts, shots; } SF;
static uint32_t sf_rand(void) {
    uint32_t r = (uint32_t) now() * 2654435761u ^ ((uint32_t) R(0x02) << 8 | R(0x03));
    r ^= r >> 15, r *= 0x846ca68bU, r ^= r >> 16;
    return r;
}
static int head_on(const ship_t *t, double dist);
// (col_fire: it'll pass within col_r in the next col_t game loops)
static int about_to_ram(const ship_t *t) {
    if (!g_sp.col_fire) return 0;
    double sp = 1.5 * t->speed, vx = t->nosev[0] * sp, vy = t->nosev[1] * sp, vz = t->nosev[2] * sp - speed();
    double vv = vx * vx + vy * vy + vz * vz, tc = vv > 1e-6 ? -(t->x * vx + t->y * vy + t->z * vz) / vv : 0;
    if (tc <= 0 || tc >= g_sp.col_t) return 0;
    double cx = t->x + vx * tc, cy = t->y + vy * tc, cz = t->z + vz * tc;
    return sqrt(cx * cx + cy * cy + cz * cz) < g_sp.col_r;
}
static int toy_now(const ship_t *t, double dist) {      // toying: holding fire to keep it big on screen
    return g_sp.toy_px && t->type != 1 && !head_on(t, dist) && t->type == TY.type && size_px(t, dist) >= g_sp.toy_px && TY.frames < g_sp.toy_s * 50;
}
// strafing's window: toying; or (strafe 2) holding fire while it closes to the firing size, from strafe_px, ahead
static int sf_window(const ship_t *t, double dist) {
    if (FB.state == 1 || head_on(t, dist)) return 0;
    if (toy_now(t, dist)) return 1;
    if (g_sp.strafe < 2 || t->z <= 0) return 0;
    double px = size_px(t, dist), fire_px = witchspace() ? g_sp.ws_fire_px : g_sp.fire_px;
    return px >= g_sp.strafe_px && px < fire_px;
}
static double radius_of(const ship_t *t) { const shiptype_t *ty = ship_type(t->type); return ty && ty->area ? sqrt((double) ty->area) : 80; }
static void sf_aim(const ship_t *t, double *ax, double *ay, double *az) {
    if (!g_sp.strafe || !sf_window(t, dist_of(t))) { SF.until = 0; return; }
    if (now() >= SF.until && now() >= SF.next) {
        uint32_t r = sf_rand();
        SF.until = now() + 15 + r % 26, SF.miss = (r >> 8 & 255) < g_sp.strafe_miss * 256, SF.side = (r >> 16 & 1) ? 1 : -1;
        SF.next = SF.until + 75 + (r >> 17) % 126;
        SF.bursts++;
    }
    if (now() < SF.until && SF.miss) {
        double o = SF.side * g_sp.strafe_off * radius_of(t);
        *ax += o * t->sidev[0], *ay += o * t->sidev[1], *az += o * t->sidev[2];
    }
}
// this frame's strafing shot, or -1: none (toying holds fire as before)
static int sf_fire(const ship_t *t, double dist, int fire) {
    if (!g_sp.strafe || now() >= SF.until || !sf_window(t, dist) || laser_temp() >= g_sp.strafe_temp || about_to_ram(t)) return -1;
    if (S.our_msl && t->slot == S.msl_target) return -1;
    int v = space_view();
    if (v > 1) return -1;
    double x = v ? -t->x : t->x, y = t->y, z = v ? -t->z : t->z, off = hypot(x, y), r = radius_of(t);
    if (z <= 0) return 0;
    int on = off < g_cpa->fire_r * r;
    if (on && t->energy <= g_sp.strafe_e) return 0;          // (a hit now might kill it: hold)
    int shot = SF.miss ? off < g_sp.strafe_k * r : (fire && on);
    SF.shots += shot;
    return shot;
}
// (toy_safe: closing on us nose-first within toy_safe: no toying, no follow speed: a tail chase is what they're for,
// and sat still in front of an oncoming ship (the follow speed goes to 0 when it comes at us) it rams us)
static int head_on(const ship_t *t, double dist) {
    if (!g_sp.toy_safe || dist > g_sp.toy_safe) return 0;
    return -(t->nosev[0] * t->x + t->nosev[1] * t->y + t->nosev[2] * t->z) / (dist + 1) > 0.5;
}
int g_show_hold;             // (lab, the collision log: this frame's show_fire: 0 show off, 1 not aimed, 2 fired, 3 held for
                             // our missile, 4 held for the flyby, 5 held toying, 6 held below the firing size)
int g_show_toyspd;           // (lab: show_speed's toy follow set our speed this frame)
int show_fb_state(void) { return FB.state; }
static int show_fire_(const ship_t *t, double dist, int fire);
int show_fire(const ship_t *t, double dist, int fire) {
    if (show_on() && t->type != 1) {
        int sf = sf_fire(t, dist, fire);
        if (sf >= 0) { if (toy_now(t, dist)) TY.frames++; g_show_hold = sf ? 7 : 5; return sf; }
    }
    int r = show_fire_(t, dist, fire);
    g_show_hold = !show_on() ? 0 : !fire ? 1 : r ? 2 : g_show_hold;
    return r;
}
static int show_fire_(const ship_t *t, double dist, int fire) {
    if (!fire) return fire;
    if (!show_on()) { g_show_why[S.was_on ? 1 : 0]++; return fire; }
    if (t->type == 1) { g_show_why[2]++; return fire; }                  // (missiles: always)
    if (S.our_msl && t->slot == S.msl_target) { g_show_hold = 3; return 0; }
    // (col_fire, round 5: it'll pass within col_r in the next col_t game loops: shoot now, whatever its size. It's
    // close and big already, and dead it can't ram us; held, it rammed: 363 rams in 1000 fights)
    if (about_to_ram(t)) { g_show_why[4]++; return fire; }
    if (FB.state == 1 && t->type == FB.type) { g_show_hold = 4; return 0; }
    double fire_px = witchspace() ? g_sp.ws_fire_px : g_sp.fire_px;
    // (toying: hold it big on screen for toy_s before the kill: the user's measure is how big and how long)
    if (g_sp.toy_px && t->type != 1 && !head_on(t, dist)) {
        double dx = t->x - TY.x, dy = t->y - TY.y, dz = t->z - TY.z;
        if (t->type != TY.type || sqrt(dx * dx + dy * dy + dz * dz) > 2500) TY.type = t->type, TY.frames = 0;
        TY.x = t->x, TY.y = t->y, TY.z = t->z;
        if (size_px(t, dist) >= g_sp.toy_px && TY.frames < g_sp.toy_s * 50) { TY.frames++; g_show_hold = 5; return 0; }
    }
    // (the finishing window: in the budget's last finish_s, fire from finish_px, while they're still close, rather
    // than leave efficient mode a long chase after the hand-over)
    if (g_sp.finish_s && S.left < g_sp.finish_s * 50) fire_px = g_sp.finish_px;
    // (fire_recede: opening on us, it'll only get smaller: shoot now. The tail: a Krait sat in the rear sights from
    // 2,000 out to 4,000, held at 7 px, and efficient mode killed it at 4,000+ once the budget ran out)
    if (g_sp.fire_recede && range_rate(t) > 2) fire_px = 0;
    int ok = size_px(t, dist) >= fire_px || (g_sp.finish_e && t->energy > g_sp.finish_e);
    g_show_why[ok ? 4 : 5]++;
    g_show_hold = 6;
    return ok;
}

// speed: close in fast while they're far (no dead air), efficient mode's speeds inside
int show_speed(const ship_t *t, double dist, int want) {
    g_show_toyspd = 0;
    if (!show_on() || t->type == 1) return want;
    if (FB.state == 1 && t->type == FB.type && dist < 2500) return g_sp.fb_speed;     // (a slow pass: it doesn't open the range)
    // (toying: close and ahead of us, sit follow_d behind it: its speed, plus or minus the range error. A tail chase
    // keeps it big; jousting head-on it was big for a second)
    if (g_sp.toy_px && !head_on(t, dist) && t->z > 0 && dist < 3 * g_sp.follow_d && atan2(hypot((double) t->x, (double) t->y), (double) t->z) < 0.5) {
        double ts = 1.5 * t->speed * t->nosev[2];          // (its speed away from us along our nose)
        int w = (int) (ts + (dist - g_sp.follow_d) / 25);
        g_show_toyspd = 1;
        return w < 0 ? 0 : w > 40 ? 40 : w;
    }
    if (S.pursuing && dist > 1500) return 40;          // (the chase: full speed until it's in close)
    if (dist > g_sp.far) return g_sp.close_speed;
    return want;
}

// a special key this frame (0: none): the missile showpiece, the energy bomb finale
int show_key(const ship_t *sh, int n, const ship_t *const *th, int nt, const ship_t *t, double off) {
    S.our_msl = 0;
    for (int i = 0; i < n; i++) if (sh[i].type == 1 && !(sh[i].ai & 0x40) && !ship_dead(&sh[i])) S.our_msl = 1;
    if (!show_on() || !t) return 0;
    // the energy bomb: a pack in close, on screen, every one of them a ship it destroys (not Thargoids, not
    // the Constrictor or Cougar), no police about
    if ((S.want_bomb || (g_sp.bomb_ws && witchspace())) && R(A_BOMB) && (!witchspace() || g_sp.bomb_ws) &&
        (!g_sp.frugal || cash() >= 3 * g_sp.frugal * 10)) {      // (frugal: a bomb costs 900 Cr to replace)
        int close = 0, seen = 0, immune = 0;
        for (int i = 0; i < nt; i++) {
            int ty = th[i]->type;
            if (ty == 1) continue;
            if (ty == 29 || ty >= 31) { immune++; continue; }
            double d = dist_of(th[i]);
            if (d < g_sp.bomb_dist) close++, seen += in_view(th[i], space_view() == 1) && size_px(th[i], d) >= 4;
        }
        int cops = 0;
        for (int i = 0; i < n; i++) if ((sh[i].newb & NEWB_COP) && !ship_dead(&sh[i]) && ship_in_space(&sh[i]) && dist_of(&sh[i]) < 12000) cops++;
        // (bomb_ws: immune ones about don't stop it; it's for the ones it does destroy: Thargons round a Thargoid)
        if (close >= g_sp.bomb_n && seen >= 2 && (!immune || g_sp.bomb_ws) && !cops) { status_("Energy bomb!"); return K_TAB; }
    }
    // the missile: once a fight, at a ship on screen at mid range, near the sights; the laser holds off it
    if (S.want_msl && !S.msl_fired && missiles() && t->type != 1 && !witchspace()) {
        double d = dist_of(t);
        int mstg = R(A_MSTG);
        if (d > g_sp.msl_near && d < g_sp.msl_far && off < 10) {
            if (mstg == 0xFF && R(A_MSAR) == 0) return K_TARGET;
            if (mstg == t->slot) { S.msl_fired = 1, S.msl_target = t->slot; status_("Missile away"); return K_MISSILE; }
            if (mstg != 0xFF) return K_UNARM;
        } else if (mstg != 0xFF && mstg != t->slot) return K_UNARM;
    }
    return 0;
}

// the flyby's aim: the target's lead point, moved fb_side to one side of it (across our view), while the
// flyby is on. Started on the target when it's inside fb_start and coming at us; done once it's gone by
// (behind us, or opening again past its nearest), or after 10 s
void show_aim(const ship_t *t, double *ax, double *ay, double *az) {
    if (show_on() && t && t->type != 1) sf_aim(t, ax, ay, az);
    if (!show_on() || FB.state == 2 || !t || t->type == 1) return;
    double d = dist_of(t);
    if (FB.state == 0) {
        if (d > g_sp.fb_start || t->z < d / 2) return;
        if (!FB.want && !(g_sp.novel_look && show_novel(t->type))) return;
        FB.state = 1, FB.type = t->type, FB.dmin = d, FB.t0 = now();
    } else {
        if (t->type != FB.type) return;
        double dx = t->x - FB.x, dy = t->y - FB.y, dz = t->z - FB.z;
        if (sqrt(dx * dx + dy * dy + dz * dz) > 2500) return;         // (another ship of the type)
    }
    FB.x = t->x, FB.y = t->y, FB.z = t->z;
    if (d < FB.dmin) FB.dmin = d;
    if (t->z < 0 || d > FB.dmin + 300 || now() - FB.t0 > 500) { FB.state = 2, FB.passed = now(); status_("Flyby"); return; }
    // (along its own side vector: a point fixed beside it. Along our x it rolled with us and we chased it
    // round in a roll for ever; scaled down near so the nose doesn't swing far off it as it closes)
    double side = FB.side * g_sp.fb_side * fmin(1.0, d / 1500.0 + 0.3);
    *ax += side * t->sidev[0], *ay += side * t->sidev[1], *az += side * t->sidev[2];
}
int show_flyby(void) { return FB.state == 1 && show_on(); }

// motion before the kill: while we hold fire on a target still under 70% of the firing size, aim weave_deg off it,
// circling every weave_s (along its own side and roof vectors, fixed in the world), so it drifts round the screen
// instead of sitting dead centre growing slowly. After a flyby, extend_s straight on so it goes astern
int show_weave(const ship_t *t, int rear, double *ax, double *ay, double *az) {
    if (!show_on() || !t || t->type == 1) return 0;
    if (g_sp.extend_s && FB.state == 2 && t->type == FB.type && now() - FB.passed < (uint32_t) (g_sp.extend_s * 50)) {
        *ax = 0, *ay = 0, *az = 10000;       // (straight on: the nose where it is)
        return 1;
    }
    if (!g_sp.weave_deg || FB.state == 1) return 0;
    double d = dist_of(t), fire = witchspace() ? g_sp.ws_fire_px : g_sp.fire_px;
    if (size_px(t, d) >= g_sp.weave_stop * fire || t->z < d / 2) return 0;
    // (weave_t: stop that many game loops before it reaches the firing size, by its closing rate: off the nose at
    // 900 a head-on Krait went by at 150 without a shot)
    if (g_sp.weave_t) {
        double r = 256 * sqrt((double) (ship_type(t->type) && ship_type(t->type)->area ? ship_type(t->type)->area : 6400)) / fire;   // (the firing range)
        double closing = -range_rate(t);
        if (closing > 1 && (d - r) / closing < g_sp.weave_t) return 0;
    }
    // (not on one turning away, its nose off us: the weave held the lock-on off and close kills fell 0.26 -> 0.14)
    if (-(t->nosev[0] * t->x + t->nosev[1] * t->y + t->nosev[2] * t->z) / (d + 1) < g_sp.weave_fac) return 0;
    double a = 2 * M_PI * now() / (g_sp.weave_s * 50), r = d * tan(g_sp.weave_deg * M_PI / 180);
    double c = cos(a) * r, s = sin(a) * r, m = rear ? -1 : 1;      // (the rear view's frame: x and z mirrored)
    *ax += m * (c * t->sidev[0] + s * t->roofv[0]), *ay += c * t->sidev[1] + s * t->roofv[1], *az += m * (c * t->sidev[2] + s * t->roofv[2]);
    return 1;
}

// the knife-edge: in the flyby's last knife_d, roll only (no pitch) to bring it to 3 or 9 o'clock, whichever is
// nearer, so it goes past on our left or right (Elite has no yaw: aimed beside it, the steering rolls the aim
// point to 12 or 6 o'clock and pitches, and it went by above or below, out of every side view)
void show_knife(const ship_t *t, keyset_t *keys) {
    if (!show_on() || !t || t->type == 1 || !g_sp.knife_d) return;
    double d = dist_of(t);
    if (d > g_sp.knife_d || t->z < 0) return;
    if (!(show_flyby() && t->type == FB.type)) {
        // (pass_knife: any close pass of the target, not just a flyby: when it will go by within pass_cpa in the next
        // 10 game loops and isn't in the sights (a shot would end it first), roll it to the side we'll look out of)
        if (!g_sp.pass_knife || in_crosshairs(t, 0)) return;
        double sp = 1.5 * t->speed, vx = t->nosev[0] * sp, vy = t->nosev[1] * sp, vz = t->nosev[2] * sp - speed();
        double vv = vx * vx + vy * vy + vz * vz, tc = vv > 1e-6 ? -(t->x * vx + t->y * vy + t->z * vz) / vv : 0;
        if (tc <= 0 || tc > 10) return;
        double cx = t->x + vx * tc, cy = t->y + vy * tc, cz = t->z + vz * tc;
        if (sqrt(cx * cx + cy * cy + cz * cz) > g_sp.pass_cpa) return;
    }
    double c = atan2((double) t->x, (double) t->y) * 180 / M_PI;     // (0 straight up the screen, 90 right)
    double err = (c >= 0 ? 90 : -90) - c;          // (ALPHA > 0, the stick right of centre: the scene turns clockwise)
    ks_del(keys, K_ROLL_LEFT), ks_del(keys, K_ROLL_RIGHT), ks_del(keys, K_CLIMB), ks_del(keys, K_DIVE);
    if (IO->analogue && R(0x2C5C)) g_want_jx = fabs(err) < 3 ? 128 : 128 + (int) fmax(-125, fmin(125, err * 3)), g_want_jy = 128;
    else if (fabs(err) >= 3) ks_add(keys, err > 0 ? K_ROLL_LEFT : K_ROLL_RIGHT);
}

// the tail: once the budget's spent (curious), no rear laser beyond tail_rear: turn and finish it with the front
// laser, not a long rear-view stare at a speck (efficient mode already does this in witchspace, rear_max 1500)
int show_rear_ok(double dist) {
    extern int g_curious;
    return !(g_curious && g_sp.tail_rear && S.spent && dist > g_sp.tail_rear);
}

// witchspace, the show on: the target by its group: nearness times the enemies within group_d of it, so we fly at the
// swarm, not at the best-value ship on its own out at 10,000 (Thargoids hang back; flown at, they fill the screen)
double show_value(const ship_t *s, double v, const ship_t *const *th, int nt) {
    if (!g_sp.ws_group || !show_on() || !witchspace() || s->type == 1) return v;
    double d = dist_of(s);
    int near = 1;
    for (int i = 0; i < nt; i++) {
        if (th[i] == s || th[i]->type == 1) continue;
        double dx = th[i]->x - s->x, dy = th[i]->y - s->y, dz = th[i]->z - s->z;
        if (sqrt(dx * dx + dy * dy + dz * dz) < g_sp.group_d) near++;
    }
    return near * 10000.0 / (d + 1000);
}

// the ram dodge's look-ahead while the show is on (ram_iters 0: efficient mode's own)
double show_ram_iters(double iters) { return g_sp.ram_iters && show_on() ? g_sp.ram_iters : iters; }

// noplan (off): curious mode without the acquisition planner. The stopgap for the Presto's juddering (the double
// planner took the frame hook to 200-500 ms there); the single-precision, capped planner (pilot.c, plan_rates_f)
// fixed the cost instead, and the planner is back on in curious mode
int show_noplan(void) { extern int g_curious; return g_curious && g_sp.noplan; }

// (lab: strafing bursts and frames fired, since the start)
void show_strafe_count(long *bursts, long *shots) { *bursts = SF.bursts, *shots = SF.shots; }

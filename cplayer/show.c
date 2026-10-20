// Showcase fighting (curious mode only): while we're clearly on top, fights are flown to be watched
// (docs/fighting-showcase.md); the moment we aren't, efficient mode's rules fly them, and back again once we are.
// combat.c calls the hooks below; each returns efficient mode's own decision when the show is off.
//
// The show, by state (each section below):
//   MODE       show or efficient, moment by moment: the superiority margin, the time budget, boredom, frugality
//   CLOSING IN full speed at a target ahead, aimed where we'd meet it; near targets first; no rear-view fights at range
//   FIRING     hold fire for a kill that's seen (fire_px); a target about to ram, a stalled fight: sooner
//   TOY/FOLLOW sit behind it where it's drawn big, hold fire toy_s seconds, strafing now and then; then the kill
//   FLYBY      one target a fight met without a shot: aimed beside it, the knife-edge, a glance out of the side, then
//              followed round in whichever view has it
//   SHOWPIECES the missile, the energy bomb
//   WITCHSPACE the nearest group first; while a Thargoid lives, its Thargons first, once they've homed in
//   EVADE      a collision coming from any ship: pitch away from it, ahead of everything else (the show on or off)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pilot.h"
#include "show.h"

extern int g_curious;
int g_show = 1;                     // (SHOW=0 in the lab or run_sim: curious fights as efficient)

// The numbers, with why (lab: the 600-fight tune set; careers: Deadly-rank sim careers; the docs have the history)
show_params_t g_sp = {
    // MODE. The margin on at 1, off at 0.5 (0.5 / 0.25 gave the same size x time with twice the deaths: 2.2% of fights
    // in a career at 1 / 0.5); witchspace 1.2 / 0.7 (lower, it bails out and half the Thargoids live). A 10-minute
    // budget as a safety net; hand over after 12 s with nothing new on screen (fights end only when they
    // repeat). No show on a pulse laser (the early career deaths). No show under 1000 Cr (the round-4 poverty trap)
    .on_m = 1, .off_m = 0.5f, .ws_on_m = 1.2f, .ws_off_m = 0.7f, .dwell = 100, .hit_s = 0.5f, .hold_s = 6, .reserve = 96,
    .min_laser = 1, .frugal = 1000, .budget_base = 600, .empty_x = 2, .bore_s = 12,
    // CLOSING IN. Full speed to a target ahead until 900 (between 1000 and 2500 efficient mode's speeds left it a
    // speck), aimed where we'd meet it (close kills 0.11 -> 0.21 a fight); near targets first (size-time 433 -> 498:
    // the show had chased the best-value ship 10,000 out with the pack at 1,000); one opening far out worth a third;
    // no rear-view fight beyond 1500 (a fight on the rear laser flew away from it half the time: 17% of fight time)
    .close_speed = 40, .far = 2500, .close_d = 900, .intercept = 1, .near_pref = 1, .run_value = 0.33f, .rear_all = 1, .rear_close = 1500,
    // FIRING. Hold to 32 px (the measure: how big and how long; distance shots don't count), Thargoids too;
    // after 45 s with no kill, from 12 px; the show off, no shot at a speck under 8 px (kills seen 24 -> 45%); a target
    // about to pass within 600 in the next 25 game loops: shoot now, whatever its size (it can't ram us dead)
    .fire_px = 48, .ws_fire_px = 32, .stall_s = 45, .stall_px = 12, .eff_px = 8, .eff_e = 180, .eff_close = 8, .slow_s = 5, .col_fire = 1, .col_t = 25, .col_r = 600,
    // TOY / FOLLOW. 40 s at 24 px or more before the kill, sat where it's drawn 50 px; none within 400 of one coming
    // nose-first (the follow parked us in its path: rams). Strafing near misses every 0.2-0.8 s while toying (big ships
    // fought on screen, not just followed): a big ship centred or under fire 1.40 -> 1.89 s a fight minute in
    // three Deadly careers, stick and roll reversals down, deaths 9 -> 2. (Grazes too killed them early: no gain)
    .toy_px = 24, .toy_s = 40, .follow_px = 50, .follow_d = 600, .toy_safe = 400,
    .strafe = 1, .strafe_miss = 1, .strafe_off = 1.4f, .strafe_k = 5, .nm_lo = 0.3f, .nm_hi = 1, .nm_side = 1, .strafe_temp = 120, .strafe_e = 80, .strafe_px = 24, .strafe_gap0 = 10, .strafe_gapr = 30,
    // FLYBY. In a quarter of fights and at every rare type: side glances and variety, at some cost in close kills; the
    // knife-edge on any close pass; followed into the side and rear views for 4 s, after 1.5 s straight on
    .fb_odds = 0.25f, .fb_side = 500, .fb_start = 4000, .fb_speed = 10, .novel_look = 1, .knife_d = 900, .pass_knife = 1, .pass_cpa = 600,
    .follow_s = 4, .extend_s = 1.5f, .glance = 1, .glance_d = 1200, .glance_max = 2.0f, .glance_gap = 6.0f, .glance_px = 20,
    // SHOWPIECES. A missile in 30% of fights, the bomb in half of those with a pack in close (rare by design)
    .msl_odds = 0.3f, .msl_near = 1200, .msl_far = 6000, .bomb_odds = 0.5f, .bomb_n = 3, .bomb_dist = 3000, .bomb_ws = 1,
    // WITCHSPACE. The nearest group (side views 25 -> 31% of fights); the Thargons first (cleared fast, then
    // the Thargoid toyed with), shot once drawn 10 px (they home in: the rear view 11 -> 30%)
    .ws_group = 1, .group_d = 2500, .thg_first = 1, .thg_d = 4000, .thg_px = 10,
    // EVADE. A ship passing within its radius + 150 inside 12 game loops: rams 90 -> 35 per 1000 fights, size-time up
    .ev = 1, .ev_t = 12, .ev_r = 150, .ev_out = 1.5f,
};

int show_set_param(const char *name, double v) {
#define F(f) if (!strcmp(name, #f)) { g_sp.f = (__typeof__(g_sp.f)) v; return 1; }
    F(on_m) F(off_m) F(ws_on_m) F(ws_off_m) F(dwell) F(hit_s) F(hold_s) F(reserve) F(min_laser) F(frugal) F(budget_base) F(empty_x) F(bore_s)
    F(close_speed) F(far) F(close_d) F(intercept) F(near_pref) F(run_value) F(rear_all) F(rear_close)
    F(fire_px) F(ws_fire_px) F(stall_s) F(stall_px) F(eff_px) F(eff_e) F(eff_close) F(slow_s) F(col_fire) F(col_t) F(col_r)
    F(toy_px) F(toy_s) F(follow_px) F(follow_d) F(toy_safe) F(strafe) F(strafe_miss) F(strafe_off) F(strafe_k) F(strafe_temp) F(strafe_e) F(strafe_px) F(strafe_gap0) F(strafe_gapr) F(nm_lo) F(nm_hi) F(nm_side)
    F(fb_odds) F(fb_side) F(fb_start) F(fb_speed) F(novel_look) F(knife_d) F(pass_knife) F(pass_cpa) F(follow_s) F(extend_s)
    F(glance) F(glance_d) F(glance_max) F(glance_gap) F(glance_px)
    F(msl_odds) F(msl_near) F(msl_far) F(bomb_odds) F(bomb_n) F(bomb_dist) F(bomb_ws)
    F(ws_group) F(group_d) F(thg_first) F(thg_d) F(thg_px) F(ev) F(ev_t) F(ev_r) F(ev_out)
#undef F
    return 0;
}

// ---- state
static struct { int on, was_on, fights, spent, empty, left, bore, bview, bslot, want_msl, want_bomb, msl_target, msl_fired, our_msl; uint32_t t0, since, last; double margin, bpx, boff; } S;
static struct { int view; uint32_t until, next; } G;          // a glance out of a side view
static struct { int type, frames; double x, y, z; } TY;      // toying: frames the target's been toy_px or more
// the flyby: state 0 none yet, 1 on, 2 done
static struct { int want, state, type, side; double x, y, z, dmin; uint32_t t0, passed; } FB;
static struct { uint32_t next, until; int miss, side; long bursts, shots; } SF;      // strafing
long g_sf_hist[16];      // (lab: near-miss shots by the gap from the sights to its outline over its drawn radius: 0 inside, then 0.2 steps)
static uint32_t s_last_kill;        // the last kill, or the fight's start (stall_s)
static double s_kill_tally;
int g_show_spent;                   // (the lab: this fight's show budget ran out)
int g_show_mode = -1;               // (the lab's meter: 1 show, 0 efficient, -1 not fighting or not curious)
int g_show_lab;                     // (the lab: no career behind it, no first meetings)
long g_show_why[6];                 // (lab: frames fired by why: show off at the start, gone off since, missile, -, big enough; held)
int g_show_hold;                    // (lab, the collision log: show_fire's last word: 0 show off, 1 not aimed, 2 fired, 3 held
                                    // for our missile, 4 for the flyby, 5 toying, 6 below the firing size, 7 a strafing shot)
int g_show_toyspd;                  // (lab: the toy follow set our speed this frame)
long g_ev_frames, g_ev_starts;      // (lab: the collision evasion)

// ---- geometry
static double dist_of(const ship_t *s) { return sqrt((double) s->x * s->x + (double) s->y * s->y + (double) s->z * s->z); }
static double radius_of(const ship_t *t) { const shiptype_t *ty = ship_type(t->type); return ty && ty->area ? sqrt((double) ty->area) : 80; }
static double size_px(const ship_t *s, double d) {
    const shiptype_t *t = ship_type(s->type);
    return 256 * (t && t->area ? sqrt((double) t->area) : 80) / (d + 1);
}
// its range's rate, units a game loop (+ opening): its motion along the line to us, less ours along z
static double range_rate(const ship_t *t) {
    double d = dist_of(t) + 1, sp = 1.5 * t->speed;
    return (t->x * t->nosev[0] * sp + t->y * t->nosev[1] * sp + t->z * (t->nosev[2] * sp - speed())) / d;
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
static int seen_in(const ship_t *s, int view) { return in_view(s, view) && size_px(s, dist_of(s)) >= 4; }
// where it'll be k game loops on: its own motion, and ours along z
static ship_t ahead(const ship_t *s, double k) {
    ship_t q = *s;
    double sp = 1.5 * s->speed;
    q.x = (int32_t) (s->x + s->nosev[0] * sp * k), q.y = (int32_t) (s->y + s->nosev[1] * sp * k), q.z = (int32_t) (s->z + (s->nosev[2] * sp - speed()) * k);
    return q;
}
// coming at us nose-first within toy_safe: no toying, no follow speed (sat in front of an oncoming ship, it rams us)
static int head_on(const ship_t *t, double dist) {
    if (!g_sp.toy_safe || dist > g_sp.toy_safe) return 0;
    return -(t->nosev[0] * t->x + t->nosev[1] * t->y + t->nosev[2] * t->z) / (dist + 1) > 0.5;
}
// it'll pass within col_r in the next col_t game loops
static int about_to_ram(const ship_t *t) {
    if (!g_sp.col_fire) return 0;
    double sp = 1.5 * t->speed, vx = t->nosev[0] * sp, vy = t->nosev[1] * sp, vz = t->nosev[2] * sp - speed();
    double vv = vx * vx + vy * vy + vz * vz, tc = vv > 1e-6 ? -(t->x * vx + t->y * vy + t->z * vz) / vv : 0;
    if (tc <= 0 || tc >= g_sp.col_t) return 0;
    double cx = t->x + vx * tc, cy = t->y + vy * tc, cz = t->z + vz * tc;
    return sqrt(cx * cx + cy * cy + cz * cz) < g_sp.col_r;
}

// novelty: the rare ships (Thargoid, Thargon, Constrictor, Cougar) always; and in a career, a type met for the
// first time (every type met so far: g_show_seen)
static uint64_t g_show_seen;
int show_novel(int type) {
    if (type >= 29 && type <= 32) return 1;
    return !g_show_lab && type > 8 && type < 64 && !(g_show_seen >> type & 1);
}

// ==== MODE
// The superiority margin: what we can lose before the reserve (energy over `reserve`, and the weaker shield: the show
// flies at them head-on, and a dead front shield behind a full aft one is no cover) over the damage they'll do before
// we've killed them all, holding fire for the show (hold_s seconds of all of them shooting) included. Ours: each enemy
// takes a turn (2 s) and its energy at our laser's rate (military ~115 a second, beam 75, pulse 15; only the military
// laser touches a Constrictor or Cougar, at a quarter). Theirs: one hit (blueprint laser x 4 + 1) every two seconds
// each (hit_s), Thargoids half again for their Thargons, until each dies, quickest kill first. A missile coming for us
// is 250 more unless the ECM will take it
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

void show_begin(void) {
    // (the same fight if the last engagement ended under 10 s ago: they went out of range and came back; the
    // budget runs on)
    if (now() - S.last > 500) S.t0 = now(), S.spent = g_show_spent = 0, S.empty = 0, S.bore = 0;
    s_last_kill = now(), s_kill_tally = tally();
    S.since = now();
    S.on = 0;
    S.msl_target = -1, S.msl_fired = 0, S.our_msl = 0;
    G.view = 0, G.next = 0;
    // the tricks this fight: by Elite's own random numbers (RAND, &02-&05), so they vary from fight to fight
    uint32_t r = (uint32_t) R(A_RAND) | (uint32_t) R(A_RAND + 1) << 8 | (uint32_t) R(A_RAND + 2) << 16 | (uint32_t) R(A_RAND + 3) << 24;
    r ^= r >> 16, r *= 0x7feb352dU, r ^= r >> 15, r *= 0x846ca68bU, r ^= r >> 16;
    S.want_msl = (r & 255) < g_sp.msl_odds * 256;
    S.want_bomb = (r >> 8 & 255) < g_sp.bomb_odds * 256;
    FB.want = (r >> 16 & 255) < g_sp.fb_odds * 256, FB.state = 0, FB.side = (r >> 24 & 1) ? 1 : -1;
    S.was_on = S.on;
}

// once a frame, from engage(): show or efficient, now
void show_frame(const ship_t *sh, int n) {
    S.last = now();
    if (!g_show || !g_curious) { S.on = 0, g_show_mode = -1; return; }
    // (frugal: the show's damage wrecks equipment and its bombs need replacing: the round-4 career sat at 650-900 Cr
    // for 80+ hours. Efficient fights cost less until the money's back)
    if (g_sp.frugal && cash() < g_sp.frugal * 10) { S.on = 0, g_show_mode = 0; return; }
    for (int i = 0; i < n; i++)          // (met: once it's been close enough to see, 2000)
        if (ship_in_space(&sh[i]) && sh[i].type > 8 && sh[i].type < 64 && dist_of(&sh[i]) < 2000) g_show_seen |= 1ull << sh[i].type;
    {   // (the time budget: an empty screen spends it empty_x times as fast; nothing 4 px or more in the view is empty)
        int seen = 0, v = space_view();
        for (int i = 0; i < n; i++)
            if (ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && sh[i].type != 1 && in_view(&sh[i], v) && size_px(&sh[i], dist_of(&sh[i])) >= 4) seen = 1;
        if (!seen && g_sp.empty_x > 1) S.empty += (int) g_sp.empty_x - 1;
        int total = (int) (g_sp.budget_base * 50), used = (int) (now() - S.t0 + S.empty);
        S.left = total - used;
        if (!S.spent && used > total) S.spent = g_show_spent = 1;
    }
    // boredom: a frame is boring when nothing is drawn 4 px or more in the view, or our target is drawn but hasn't
    // changed (its size within 1 px and its bearing within 2 deg of a second ago) and we're not firing: a long straight
    // chase, the same picture held. bore_s of it hands the fight over. (Not while the target is closing: the
    // approach is part of the fight, though it's dots at first)
    if (g_sp.bore_s && !S.spent && !witchspace()) {
        extern int combat_target(void);
        int v = space_view(), seen = 0, tg = combat_target();
        const ship_t *t = NULL;
        for (int i = 0; i < n; i++) {
            if (!ship_in_space(&sh[i]) || ship_dead(&sh[i]) || sh[i].type == 1) continue;
            if (in_view(&sh[i], v) && size_px(&sh[i], dist_of(&sh[i])) >= 4) seen = 1;
            if (sh[i].slot == tg) t = &sh[i];
        }
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
        if (S.bore > g_sp.bore_s * 50) { S.spent = g_show_spent = 1; status_("The show: nothing new on screen, finishing it"); }
    }
    if (S.spent) { S.on = 0, g_show_mode = 0; return; }
    S.margin = margin(sh, n);
    double on_m = witchspace() && g_sp.ws_on_m ? g_sp.ws_on_m : g_sp.on_m, off_m = witchspace() && g_sp.ws_off_m ? g_sp.ws_off_m : g_sp.off_m;
    int want = S.on ? S.margin >= off_m : S.margin >= on_m;
    if (g_sp.min_laser && !(laser(0) & 0x80)) want = 0;         // (min_laser 1: a beam laser or better ahead)
    if (g_sp.min_laser >= 2 && laser(0) != 0x97) want = 0;
    // (a missile coming for us without an ECM, or the energy under 128: now, whatever the dwell)
    int urgent = !want && (energy() < 128 || (!has_ecm() && S.margin < off_m));
    if (want != S.on && (now() - S.since >= (uint32_t) g_sp.dwell || urgent)) {
        S.on = want, S.since = now();
        if (want) S.was_on = 1, S.fights++;
    }
    g_show_mode = S.on;
}
int show_on(void) { return S.on; }

// ==== WITCHSPACE and CLOSING IN: the target's worth
// witchspace: the target by its group: nearness times the enemies within group_d of it, so we fly at the swarm, not
// at the best-value ship on its own out at 10,000. While a Thargoid lives, the Thargons within thg_d first (its
// Thargons go dead when it dies: they have to come first), then the Thargoid
static int thg_near(const ship_t *const *th, int nt, int *thargoid) {
    int n = 0;
    *thargoid = 0;
    for (int i = 0; i < nt; i++) {
        if (th[i]->type == 29 && !ship_dead(th[i])) *thargoid = 1;
        if (th[i]->type == 30 && !ship_dead(th[i]) && dist_of(th[i]) < g_sp.thg_d) n++;
    }
    return n;
}
static double thg_value(const ship_t *s, double v, const ship_t *const *th, int nt) {
    if (!g_sp.thg_first || !show_on() || !witchspace() || (s->type != 29 && s->type != 30)) return v;
    int tgd, n = thg_near(th, nt, &tgd);
    if (!tgd || !n) return v;
    if (s->type == 30) return dist_of(s) < g_sp.thg_d ? v * 10 : v;
    return v / 10;
}
double show_value(const ship_t *s, double v, const ship_t *const *th, int nt) {
    v = thg_value(s, v, th, nt);
    if (tally() != s_kill_tally) s_kill_tally = tally(), s_last_kill = now();      // (a kill: for stall_s)
    if (g_sp.near_pref && show_on() && !witchspace() && s->type != 1) v *= 3000.0 / (dist_of(s) + 1000);
    if (g_sp.run_value && show_on() && s->type != 1 && dist_of(s) > 2000 && range_rate(s) > 0) v *= g_sp.run_value;
    if (!g_sp.ws_group || !show_on() || !witchspace() || s->type == 1) return v;
    double d = dist_of(s);
    int near = 1;
    for (int i = 0; i < nt; i++) {
        if (th[i] == s || th[i]->type == 1) continue;
        double dx = th[i]->x - s->x, dy = th[i]->y - s->y, dz = th[i]->z - s->z;
        if (sqrt(dx * dx + dy * dy + dz * dz) < g_sp.group_d) near++;
    }
    return thg_value(s, near * 10000.0 / (d + 1000), th, nt);
}
// the rear laser: within rear_close only (a fight on the rear laser at range flew away from them, and on film was
// the rear view full of specks): turn and face it
int show_rear_ok(double dist) {
    return !(g_curious && g_sp.rear_close && (show_on() || g_sp.rear_all) && dist > g_sp.rear_close);
}

// ==== TOY / FOLLOW: toying and strafing
static int toy_now(const ship_t *t, double dist) {      // toying: holding fire to keep it big on screen
    return g_sp.toy_px && t->type != 1 && !head_on(t, dist) && t->type == TY.type && size_px(t, dist) >= g_sp.toy_px && TY.frames < g_sp.toy_s * 50;
}
// strafing (an occasional burst without the kill, or a near miss, is more interesting to watch than just following):
// bursts of 0.3-0.8 s every 1.5-4 s, while toying and while closing to the firing size
// from strafe_px: near misses (aimed beside it along its own side, fired while it's a few radii off the line) and
// grazes (on it, only while its energy is over a floor, so the kill still comes at the big close moment); none with
// the laser warm, so the kill isn't held up by the heat
static uint32_t sf_rand(void) {
    uint32_t r = (uint32_t) now() * 2654435761u ^ ((uint32_t) R(A_RAND) << 8 | R(A_RAND + 1));
    r ^= r >> 15, r *= 0x846ca68bU, r ^= r >> 16;
    return r;
}
static int sf_window(const ship_t *t, double dist) {
    if (FB.state == 1 || head_on(t, dist) || (g_sp.thg_first && t->type == 30)) return 0;
    if (toy_now(t, dist)) return 1;
    if (t->z <= 0) return 0;
    double px = size_px(t, dist), fire_px = witchspace() ? g_sp.ws_fire_px : g_sp.fire_px;
    return px >= g_sp.strafe_px && px < fire_px;
}
static void sf_aim(const ship_t *t, double *ax, double *ay, double *az) {
    if (!g_sp.strafe || !sf_window(t, dist_of(t))) { SF.until = 0; return; }
    if (now() >= SF.until && now() >= SF.next) {
        uint32_t r = sf_rand();
        SF.until = now() + 15 + r % 26, SF.miss = (r >> 8 & 255) < g_sp.strafe_miss * 256, SF.side = (r >> 16 & 1) ? 1 : -1;
        SF.next = SF.until + g_sp.strafe_gap0 + (r >> 17) % g_sp.strafe_gapr;
        SF.bursts++;
    }
    if (now() < SF.until && SF.miss) {
        double o = g_sp.strafe_off * radius_of(t);
        if (g_sp.nm_side) {      // (its motion across the line of sight, ours included: aim that way of it)
            double sp = 1.5 * t->speed, vx = t->nosev[0] * sp, vy = t->nosev[1] * sp, vz = t->nosev[2] * sp - speed();
            double pp = (double) t->x * t->x + (double) t->y * t->y + (double) t->z * t->z + 1, k = (vx * t->x + vy * t->y + vz * t->z) / pp;
            double ux = vx - k * t->x, uy = vy - k * t->y, uz = vz - k * t->z, u = sqrt(ux * ux + uy * uy + uz * uz);
            if (u > 1e-3) { *ax += o * ux / u, *ay += o * uy / u, *az += o * uz / u; return; }
        }
        o *= SF.side;
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
    int shot;
    if (SF.miss && g_sp.nm_hi > 0) {
        // (a near miss has to look near: wide misses look like nothing. On screen the beam ends at
        // the sights; the ship's centre is off/z of a radian away, its drawn radius r/z: fire only with the gap from the
        // sights to its outline between nm_lo and nm_hi of that radius)
        double gap = off / r - 1;      // (the gap over the drawn radius: both scale as 1/z)
        shot = gap >= g_sp.nm_lo && gap <= g_sp.nm_hi;
    } else shot = SF.miss ? off < g_sp.strafe_k * r : (fire && on);
    if (shot && SF.miss) { double gap = off / r - 1; int b = gap < 0 ? 0 : (int) (gap * 5) + 1; g_sf_hist[b > 15 ? 15 : b]++; }
    SF.shots += shot;
    return shot;
}
void show_strafe_count(long *bursts, long *shots) { *bursts = SF.bursts, *shots = SF.shots; }     // (lab)

// ==== CLOSING IN and FLYBY: the aim point
// intercept: a target ahead beyond close_d, aimed where we'd meet it at full speed, so we cut its corners
static void intercept_aim(const ship_t *t, double *ax, double *ay, double *az) {
    double d = dist_of(t);
    if (!g_sp.intercept || t->z <= 0 || d < g_sp.close_d || FB.state == 1 || head_on(t, d)) return;
    double sp = 1.5 * t->speed, vx = t->nosev[0] * sp, vy = t->nosev[1] * sp, vz = t->nosev[2] * sp;
    double closing = -(t->x * vx + t->y * vy + t->z * (vz - g_sp.close_speed)) / (d + 1);   // (at full speed)
    double T = fmin(60.0, d / fmax(10.0, closing));
    *ax = t->x + vx * T, *ay = t->y + vy * T, *az = t->z + vz * T;
}
// the flyby: started on the target when it's inside fb_start and coming at us; aimed fb_side beside it (along its own
// side vector: along our x it rolled with us and we chased it round in a roll for ever; scaled down near, so the nose
// doesn't swing far off it); done once it's gone by (behind us, or opening again past its nearest) or after 10 s
void show_aim(const ship_t *t, double *ax, double *ay, double *az) {
    if (show_on() && t && t->type != 1) intercept_aim(t, ax, ay, az);
    if (show_on() && t && t->type != 1) sf_aim(t, ax, ay, az);
    // (no flyby at a Thargon: it's tiny, so it never looks a pass, and as a rare type every first meeting chose one: full
    // speed in, the crawl to turn after it, again: speed pulsing in witchspace. 329 witchspace fights:
    // big ships 5.2 -> 5.6 s a fight minute, fights 123 -> 116 s)
    if (!show_on() || FB.state == 2 || !t || t->type == 1 || t->type == 30) return;
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
    double side = FB.side * g_sp.fb_side * fmin(1.0, d / 1500.0 + 0.3);
    *ax += side * t->sidev[0], *ay += side * t->sidev[1], *az += side * t->sidev[2];
}
int show_flyby(void) { return FB.state == 1 && show_on(); }
int show_fb_state(void) { return FB.state; }     // (lab)
// after a flyby, extend_s straight on (the nose where it is) so it goes astern
int show_extend(const ship_t *t) {
    return show_on() && t && t->type != 1 && g_sp.extend_s && FB.state == 2 && t->type == FB.type && now() - FB.passed < (uint32_t) (g_sp.extend_s * 50);
}
// the knife-edge: in the flyby's last knife_d (and on any close pass of the target that isn't in the sights), roll
// only, no pitch, to bring it to 3 or 9 o'clock, whichever is nearer, so it goes past on our left or right (Elite has
// no yaw: aimed beside it, the steering rolled the aim point to 12 or 6 o'clock and it went by above or below)
void show_knife(const ship_t *t, keyset_t *keys) {
    if (!show_on() || !t || t->type == 1 || !g_sp.knife_d) return;
    double d = dist_of(t);
    if (d > g_sp.knife_d || t->z < 0) return;
    if (!(show_flyby() && t->type == FB.type)) {
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
    if (IO->analogue && R(A_JSTK)) g_want_jx = fabs(err) < 3 ? 128 : 128 + (int) fmax(-125, fmin(125, err * 3)), g_want_jy = 128;
    else if (fabs(err) >= 3) ks_add(keys, err > 0 ? K_ROLL_LEFT : K_ROLL_RIGHT);
}

// ==== EVADE: the collision evasion, a priority manoeuvre when a collision is coming: any ship, Thargons and canisters too, whose closest approach is within ev_t game loops and passes inside its radius
// + ev_r; held, once started, until the predicted miss is ev_out times that. combat.c pitches away from the side it'll
// pass (towards that side, as the efficient ram dodge steers, rams went 35 -> 133 per 1000)
const ship_t *show_evade(const ship_t *s, int n, vec_t *miss) {
    static int on;
    if (!g_curious || !g_sp.ev) return NULL;
    const ship_t *worst = NULL;
    double wr = 1e30;
    for (int i = 0; i < n; i++) {
        const ship_t *sh = &s[i];
        if (!ship_in_space(sh) || ship_dead(sh) || !ship_is_ship(sh) || sh->type == 1) continue;
        double sp = 1.5 * sh->speed, vx = sh->nosev[0] * sp, vy = sh->nosev[1] * sp, vz = sh->nosev[2] * sp - speed();
        double vv = vx * vx + vy * vy + vz * vz;
        if (vv < 1e-6) continue;
        double tc = -(sh->x * vx + sh->y * vy + sh->z * vz) / vv;
        if (tc < 0 || tc > g_sp.ev_t) continue;
        vec_t q = {sh->x + vx * tc, sh->y + vy * tc, sh->z + vz * tc};
        double m = sqrt(q.x * q.x + q.y * q.y + q.z * q.z), lim = radius_of(sh) + g_sp.ev_r;
        if (m < lim * (on ? g_sp.ev_out : 1) && m / lim < wr) wr = m / lim, worst = sh, *miss = q;
    }
    if (worst && !on) g_ev_starts++;
    on = worst != NULL;
    if (on) g_ev_frames++;
    return worst;
}

// ==== CLOSING IN and TOY / FOLLOW: speed
int show_speed(const ship_t *t, double dist, int want) {
    g_show_toyspd = 0;
    if (!show_on() || t->type == 1) return want;
    if (FB.state == 1 && t->type == FB.type && dist < 2500) return g_sp.fb_speed;     // (a slow pass: it doesn't open the range)
    // the toy follow: close and ahead of us, sit where it's drawn follow_px: its speed, plus or minus the range error
    double fd = g_sp.follow_d;
    if (g_sp.follow_px) fd = fmax(250.0, fmin(900.0, 256 * radius_of(t) / g_sp.follow_px));
    if (g_sp.toy_px && !head_on(t, dist) && t->z > 0 && dist < 3 * fd && atan2(hypot((double) t->x, (double) t->y), (double) t->z) < 0.5) {
        double ts = 1.5 * t->speed * t->nosev[2];          // (its speed away from us along our nose)
        int w = (int) (ts + (dist - fd) / 25);
        g_show_toyspd = 1;
        return w < 0 ? 0 : w > 40 ? 40 : w;
    }
    if (t->z < 0 && g_sp.rear_close) return want;      // (behind us: closing fast would be flying away from it)
    if (g_sp.close_d && t->z > 0 && dist > g_sp.close_d && !head_on(t, dist)) return g_sp.close_speed;
    if (dist > g_sp.far) return g_sp.close_speed;
    return want;
}

// ==== FIRING
static int show_fire_(const ship_t *t, double dist, int fire) {
    if (!fire) return fire;
    if (!show_on()) { g_show_why[S.was_on ? 1 : 0]++; return fire; }
    if (t->type == 1) { g_show_why[2]++; return fire; }                  // (missiles: always)
    if (g_sp.thg_first && t->type == 30 && witchspace()) return size_px(t, dist) >= g_sp.thg_px || about_to_ram(t) ? fire : 0;
    if (S.our_msl && t->slot == S.msl_target) { g_show_hold = 3; return 0; }
    if (about_to_ram(t)) { g_show_why[4]++; return fire; }             // (it can't ram us dead)
    if (FB.state == 1 && t->type == FB.type) { g_show_hold = 4; return 0; }
    double fire_px = witchspace() ? g_sp.ws_fire_px : g_sp.fire_px;
    if (g_sp.stall_s && now() - s_last_kill > g_sp.stall_s * 50 && g_sp.stall_px < fire_px) fire_px = g_sp.stall_px;
    // toying: hold it big on screen for toy_s before the kill
    if (g_sp.toy_px && t->type != 1 && !head_on(t, dist)) {
        double dx = t->x - TY.x, dy = t->y - TY.y, dz = t->z - TY.z;
        if (t->type != TY.type || sqrt(dx * dx + dy * dy + dz * dz) > 2500) TY.type = t->type, TY.frames = 0;
        TY.x = t->x, TY.y = t->y, TY.z = t->z;
        if (size_px(t, dist) >= g_sp.toy_px && TY.frames < g_sp.toy_s * 50) { TY.frames++; g_show_hold = 5; return 0; }
    }
    // (it followed ships it wasn't catching: under the firing size, not coming in faster than eff_close a loop for slow_s, it's shot rather than followed)
    static struct { int slot; uint32_t since; } SL = {-1, 0};
    if (t->slot != SL.slot || range_rate(t) < -g_sp.eff_close) SL.slot = t->slot, SL.since = now();
    if (g_sp.slow_s && now() - SL.since >= (uint32_t) (g_sp.slow_s * 50)) { g_show_why[4]++; g_show_hold = 6; return fire; }
    int ok = size_px(t, dist) >= fire_px;
    g_show_why[ok ? 4 : 5]++;
    g_show_hold = 6;
    return ok;
}
int show_fire(const ship_t *t, double dist, int fire) {
    // (eff_px: the show off, curious waits for a shape (outside witchspace, unless it's hurting or about to be rammed),
    // but only while the speck's coming in faster than eff_close a loop: one getting away, or barely closing, is shot.
    // (Waiting for every speck, curious chased them into the dark and took fire: 10 game hours 137 points, not 609)
    if (g_curious && g_sp.eff_px && fire && !show_on() && !witchspace() && t->type != 1 && energy() > g_sp.eff_e && size_px(t, dist) < g_sp.eff_px &&
        range_rate(t) < -g_sp.eff_close && !about_to_ram(t)) return 0;
    if (show_on() && t->type != 1) {
        int sf = sf_fire(t, dist, fire);
        if (sf >= 0) { if (toy_now(t, dist)) TY.frames++; g_show_hold = sf ? 7 : 5; return sf; }
    }
    int r = show_fire_(t, dist, fire);
    g_show_hold = !show_on() ? 0 : !fire ? 1 : r ? 2 : g_show_hold;
    return r;
}

// ==== SHOWPIECES: a special key this frame (0: none)
int show_key(const ship_t *sh, int n, const ship_t *const *th, int nt, const ship_t *t, double off) {
    S.our_msl = 0;
    for (int i = 0; i < n; i++) if (sh[i].type == 1 && !(sh[i].ai & 0x40) && !ship_dead(&sh[i])) S.our_msl = 1;
    if (!show_on() || !t) return 0;
    // the energy bomb: a pack in close, on screen, every one of them a ship it destroys (not Thargoids, not the
    // Constrictor or Cougar, unless bomb_ws in witchspace: it's for the Thargons round them), no police about
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

// ==== FLYBY: the view to show
// a glance left or right as a ship goes past close (where it'll be a game loop and a half on: the view key takes ~6
// frames), back as soon as it's gone; after a flyby, follow it in whichever view has it. Our controls act on the ship
// whatever the view, so the steering flies on through it; but only the view's own laser fires
int show_view(int view, const ship_t *sh, int n, const ship_t *t) {
    if (!show_on() || !g_sp.glance) { G.view = 0; return view; }
    for (int i = 0; i < n; i++)         // (an incoming missile: eyes front)
        if (sh[i].type == 1 && (sh[i].ai & 0x40) && !ship_dead(&sh[i])) { G.view = 0; return view; }
    if (G.view) {
        int still = 0;
        for (int i = 0; i < n; i++)
            if (ship_is_ship(&sh[i]) && ship_in_space(&sh[i]) && !ship_dead(&sh[i]) && in_view(&sh[i], G.view) && dist_of(&sh[i]) < 2 * g_sp.glance_d) still = 1;
        if (now() < G.until && still) return G.view;
        G.view = 0;
    }
    if (g_sp.follow_s && FB.state == 2 && t && t->type == FB.type && now() - FB.passed < (uint32_t) (g_sp.follow_s * 50) && view == 0) {
        if (seen_in(t, 0)) return 0;
        for (int v = 1; v <= 3; v++) if (seen_in(t, v)) return v;
        if (space_view() && seen_in(t, space_view())) return space_view();
    }
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_is_ship(s) || !ship_in_space(s) || ship_dead(s) || !(s->newb & NEWB_HOSTILE) || s->type == 1) continue;
        double d = dist_of(s);
        if (d > g_sp.glance_d) continue;
        int side = s->x < 0 ? 2 : 3;
        if (s->z < -d / 2 || now() < G.next || view != 0 || size_px(s, d) < g_sp.glance_px) continue;
        ship_t q = ahead(s, 1.5);
        if (!in_view(s, side) && !in_view(&q, side)) continue;
        G.view = side, G.until = now() + (uint32_t) (g_sp.glance_max * 50), G.next = now() + (uint32_t) (g_sp.glance_gap * 50);
        status_("A look %s", side == 2 ? "left" : "right");
        return side;
    }
    return view;
}

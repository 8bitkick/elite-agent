#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "pilot.h"
#include "keyq.h"
#include "flight.h"

// the rules' numbers: CMA-ES in the fight lab (the tuner is in git history: tools/cma_c.py); on held-out
// test fights, +19% points a minute on pirates, +71% on Thargoids, fewer deaths on every set
combat_params_t g_cp = {
    .roll_iters = 1.726f, .pitch_iters = 3.007f, .lead = 4.5f, .ram_dist = 452, .ram_iters = 5, .turn_off = 15,
    .turn_close = 20000, .spd_turn_close = 0, .spd_turn_far = 0, .far = 4000, .spd_far = 27, .mid = 1800,
    .spd_mid = 31, .near = 1000, .spd_near = 1, .spd_close = 0, .hot = 224,
    .fire_r = 1.431f,
    .evade_on = 69, .evade_gap = 238, .evade_off_deg = 20, .evade_shield = 13,
    .t_floor = 0.25f, .t_switch = 1.409f, .w_threat = 1.412f, .hit_rate = 0.5f, .t_reach = 16000, .turn_model = 1,
    .rear_cone = 60, .bail_energy = 180, .engage_range = 16954, .pod_energy = 40, .rear_max = 0,
    .brk_energy = 230, .brk_shield = 100, .brk_until = 250, .brk_clear = 8192, .brk_n = 2,
};
combat_params_t g_cp_thg;           // witchspace: g_cp's numbers but the rear laser only for an attacker within 1500
combat_params_t *g_cpa = &g_cp;
static int s_thg_init;
// (held-out Thargoid encounters with the rear laser limited: points a minute 7.56 -> 9.94, damage taken
// -20%, deaths unchanged at 1 in 202)
// (and threats only within 8000, and turns timed the old way: the pirates' 16000 and stick-ramp turn times both
// cost witchspace points, -2% and -7%)
void combat_thg_init(void) { if (!s_thg_init) g_cp_thg = g_cp, g_cp_thg.rear_max = 1500, g_cp_thg.t_reach = 8000, g_cp_thg.turn_model = 0, s_thg_init = 1; }
static void cp_select(void) { combat_thg_init(); g_cpa = witchspace() ? &g_cp_thg : &g_cp; }

// a parameter by name, into g_cp, or with "t." into the witchspace set; 0 for an unknown name
int combat_set_param(const char *name, double v) {
    combat_params_t *P = &g_cp;
    if (!strncmp(name, "t.", 2)) { combat_thg_init(); P = &g_cp_thg; name += 2; }
#define F(f) if (!strcmp(name, #f)) { P->f = (__typeof__(P->f)) v; return 1; }
    F(roll_iters) F(pitch_iters) F(lead) F(ram_dist) F(ram_iters) F(turn_off) F(turn_close) F(spd_turn_close) F(spd_turn_far) F(far)
    F(spd_far) F(mid) F(spd_mid) F(near) F(spd_near) F(spd_close) F(hot) F(fire_r) F(evade_on) F(evade_gap) F(evade_off_deg)
    F(evade_shield) F(t_floor) F(t_switch) F(w_threat) F(hit_rate) F(t_reach) F(turn_model) F(rear_cone) F(bail_energy) F(engage_range) F(pod_energy)
    F(rear_max) F(brk_energy) F(brk_shield) F(brk_until) F(brk_clear) F(brk_stop) F(brk_n)
#undef F
    return 0;
}



int g_bail_energy;
int g_brk_log;
int g_msl_side = 2500;     // the missile-side turn, for missiles within this (0 off)
long g_sw_why[4];
extern int g_steer_log;
static double dist_of(const ship_t *s) { return sqrt((double) s->x * s->x + (double) s->y * s->y + (double) s->z * s->z); }

// kill points (the Master scores kills by type: Moxon's combat rank deep dive)
static double points_of(int type) {
    const shiptype_t *t = ship_type(type);
    return type == 1 ? 0.58 : t ? t->kill_points : 0.1;
}

// iterations to bring the nose (or the rear laser) onto a point: roll it into our pitch plane, then
// pitch onto it, at the game's top rates (roll ~0.12 rad an iteration, pitch ~0.031)
// the stick's turn rates (1/256 rad an iteration) by its offset from the centre, measured in the sim
// (stickprobe): dead zones, then steps
static double roll_rate_of(int d) {
    static const double A[][2] = {{31, 3}, {32, 6}, {40, 10}, {60, 14.6}, {80, 20}, {100, 24}, {125, 31}, {200, 31}};
    if (d <= 31) return d <= 9 ? 0 : d <= 15 ? 1 : d <= 25 ? 2 : 3;
    for (int i = 1; i < 8; i++) if (d <= A[i][0]) return A[i - 1][1] + (A[i][1] - A[i - 1][1]) * (d - A[i - 1][0]) / (A[i][0] - A[i - 1][0]);
    return 31;
}
static double pitch_rate_of(int d) {
    static const double A[][2] = {{28, 0}, {29, 1}, {41, 1}, {48, 3}, {64, 4}, {78, 6}, {91, 6}, {104, 7}, {117, 8}, {200, 8}};
    if (d <= 28) return 0;
    for (int i = 1; i < 10; i++) if (d <= A[i][0]) return A[i - 1][1] + (A[i][1] - A[i - 1][1]) * (d - A[i - 1][0]) / (A[i][0] - A[i - 1][0]);
    return 8;
}
// iterations of a key to turn through `ang` rad, from a stick `from` off the centre (0 if it's the wrong
// way: the other key snaps it there first), moving `step` an iteration
static double ramp_iters(double ang, int from, int step, double (*rate)(int)) {
    double done = 0;
    int m = from, it = 0;
    for (; done * 256 < ang * 256 && it < 120; it++) {
        m = m + step > 127 ? 127 : m + step;
        done += rate(m) / 256.0;
    }
    return it;
}
// iterations to bring the nose (or, mirrored, the rear laser) onto a point: roll it into our pitch plane,
// then pitch onto it, from where the stick is now (it moves 5 an iteration in roll, 13 in pitch: a target
// 3 deg off but level with us is ~2 s of roll from a centred stick, not the 0.2 s a full-rate roll would
// take). jx, jy: the stick as seen in this view
static double turn_iters_from(double x, double y, double z, int jx, int jy) {
    // (keys ramp the stick 5 / 13 a loop; a joystick is wherever we put it: no ramp)
    int joy = IO->analogue && R(0x2C5C) != 0;
    double d = sqrt(x * x + y * y + z * z);
    if (d < 1) return 0;
    double off = acos(fmax(-1.0, fmin(1.0, z / d)));
    double clock = atan2(x, y);
    int up = fabs(clock) <= M_PI / 2;
    double roll_err = up ? clock : clock - copysign(M_PI, clock);
    double t = 0;
    if (off > 0.03) {
        int same = (jx - 128) * -roll_err > 0;
        t += ramp_iters(fabs(roll_err), same ? abs(jx - 128) : 0, joy ? 255 : 5, roll_rate_of);
    }
    int pside = up ? -1 : 1;            // (the stick below the centre pitches towards a target above)
    int psame = (jy - 128) * pside > 0;
    t += ramp_iters(off, psame ? abs(jy - 128) : 0, joy ? 255 : 13, pitch_rate_of);
    return t;
}
static double turn_iters(double x, double y, double z) {
    if (g_cpa->turn_model) {
        int front = z >= 0 || !laser(1);
        int jx = jstx(), jy = jsty();
        if (!front) jx = 256 - jx, jy = 256 - jy;
        return turn_iters_from(x, y, z, jx, jy);
    }
    double d = sqrt(x * x + y * y + z * z);
    if (d < 1) return 0;
    double off = acos(fmax(-1.0, fmin(1.0, z / d)));
    double clock = atan2(x, y), roll = fmin(fabs(clock), M_PI - fabs(clock));
    return (off > 0.03 ? roll / 0.12 : 0) + off / 0.031;
}

// the damage a shot from the laser in view does to a ship of this type: only a military laser hurts
// the Constrictor and the Cougar, at a quarter of its power (the hit routine, from CON up)
static int laser_power(int view, int type) {
    int l = laser(view), p = l & 0x7F;
    if (type >= 31) return l == 0x97 ? p / 4 : 0;
    return p;
}

// a target's worth: kill points (plus a floor: cheap ships still shoot, plus how hard it's shooting at
// us) for the seconds it'll take: turning onto it (front or rear laser, the quicker) and burning through
// its energy at our laser's power
static double target_value(const ship_t *s) {
    if (s->type == 1) return 100;                      // a missile on its way: first
    double d = dist_of(s);
    if (d < 1) d = 1;
    double t = turn_iters(s->x, s->y, s->z);
    int view = 0;
    if (laser_power(1, s->type) > 0) {
        double tr = turn_iters(-s->x, s->y, -s->z);
        if (tr < t || !laser_power(0, s->type)) t = tr, view = 1;
    }
    int power = laser_power(view, s->type);
    if (power < 1) power = 1;
    double kill = s->energy / (power * g_cpa->hit_rate);  // iterations of fire
    const shiptype_t *ty = ship_type(s->type);
    double facing = -(s->nosev[0] * s->x + s->nosev[1] * s->y + s->nosev[2] * s->z) / d;
    double threat = (facing > 0.95 && d < g_cpa->t_reach && ty) ? ty->laser * (1 - d / g_cpa->t_reach) : 0;
    return (points_of(s->type) + g_cpa->t_floor + g_cpa->w_threat * threat) / (1 + (t + kill) / 25.0);   // (25 iterations ~ a second)
}


// attacking us or will: the hostile flag, alive, not the station; and missiles coming for us
// (first, whatever the distance). Nearest first.
int threats(const ship_t *s, int n, int rng, const ship_t **out, int max) {
    const ship_t *c[NOSH];
    double d[NOSH];
    int k = 0;
    for (int i = 0; i < n; i++) {
        const ship_t *sh = &s[i];
        if (!ship_in_space(sh) || ship_dead(sh)) continue;
        if (sh->type == 1) {
            if (sh->ai & 0x40) { c[k] = sh; d[k++] = -1; }
            continue;
        }
        // (Thargoids and Thargons are always enemies: a freshly launched Thargon isn't flagged hostile yet, and
        // ignoring it ended fights early and left us still on the chart or the countdown: three deaths)
        if (!(sh->newb & NEWB_HOSTILE) && sh->type != 29 && sh->type != 30) continue;
        if (sh->type >= 3 && sh->type <= 8) continue;   // pods, alloys, canisters, rocks: flagged hostile
                                                        // with their ship but harmless (scoop them instead)
        double dd = dist_of(sh);
        if (dd < rng) { c[k] = sh; d[k++] = dd; }
    }
    for (int i = 1; i < k; i++)         // (stable insertion sort)
        for (int j = i; j > 0 && d[j] < d[j - 1]; j--) {
            double td = d[j]; d[j] = d[j - 1]; d[j - 1] = td;
            const ship_t *tc = c[j]; c[j] = c[j - 1]; c[j - 1] = tc;
        }
    for (int i = 0; i < k && i < max; i++) out[i] = c[i];
    return k;
}

// the ship that will pass nearest within ram_iters iterations, if nearer than margin. (Five, not 15: with 15
// the dodge took over for the whole of a head-on approach, from ~1000 out, and spiralled us round an oncoming
// pirate we never lined up: all fights, deaths 197 -> 190, 15 saved, 8 newly lost)
static const ship_t *collision_risk(const ship_t *s, int n, int margin, vec_t *miss);
const ship_t *collision_ahead(const ship_t *s, int n, int margin, vec_t *miss) { return collision_risk(s, n, margin, miss); }
static const ship_t *collision_risk(const ship_t *s, int n, int margin, vec_t *miss) {
    double horizon = g_cpa->ram_iters;
    const ship_t *worst = NULL;
    double wd = 1e30;
    for (int i = 0; i < n; i++) {
        const ship_t *sh = &s[i];
        if (!ship_in_space(sh) || ship_dead(sh) || !ship_is_ship(sh)) continue;
        vec_t p = {sh->x, sh->y, sh->z};
        vec_t v = {sh->nosev[0] * 1.5 * sh->speed, sh->nosev[1] * 1.5 * sh->speed, sh->nosev[2] * 1.5 * sh->speed - speed()};
        double vv = v.x * v.x + v.y * v.y + v.z * v.z;
        double t = vv < 1e-6 ? 0 : fmax(0.0, fmin(horizon, -(p.x * v.x + p.y * v.y + p.z * v.z) / vv));
        vec_t q = {p.x + v.x * t, p.y + v.y * t, p.z + v.z * t};
        double dm = sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (dm < margin && dm < wd) { worst = sh; wd = dm; *miss = q; }
    }
    return worst;
}

// one frame of attacking sh: the keys, the angle off (degrees), and the view to fight in (0 front,
// 1 rear: with a rear laser, a target behind is fought from the rear view, where our turns look
// mirrored: the rolls swap, and so do climb and dive)
static double combat_keys(const ship_t *s, int n, const ship_t *sh, keyset_t *keys, int *view_out) {
    cp_select();
    const combat_params_t *P = g_cpa;
    double dist = dist_of(sh);
    double sp = 1.5 * sh->speed, k = P->lead;
    // per iteration, the target moves along its nose at its speed and we fly on at ours
    vec_t vel = {sh->nosev[0] * sp, sh->nosev[1] * sp, sh->nosev[2] * sp - speed()};
    vec_t aim = {sh->x + vel.x * k, sh->y + vel.y * k, sh->z + vel.z * k};
    vec_t v = {sh->x, sh->y, sh->z};
    // (the rear laser means facing away: further than rear_max, it's a chase we never close)
    int rear = laser_power(1, sh->type) > 0 && sh->z < 0 && atan2(hypot(sh->x, sh->y), -(double) sh->z) * 180 / M_PI < P->rear_cone &&
               (!P->rear_max || dist < P->rear_max);
    if (rear) {
        v = (vec_t){-v.x, v.y, -v.z};
        aim = (vec_t){-aim.x, aim.y, -aim.z};
    }
    // (the stick as seen from the rear view is mirrored too, both ways: steer() compares where it is
    // with where it should be, so it has to be in the same mirrored frame as the aim. Unmirrored, it
    // pushed the stick the wrong way and only came round when it hit the stop: the bouncing)
    int jx_ = jstx(), jy_ = jsty();
    if (rear) jx_ = 256 - jx_, jy_ = 256 - jy_;
    { extern double g_aim_tol; const shiptype_t *tt = ship_type(sh->type); g_aim_tol = atan2(tt && tt->area ? sqrt((double) tt->area) : 80, dist + 1); }
    double off = steer(aim, jx_, jy_, P->roll_iters, P->pitch_iters, keys);
    { extern double g_aim_tol; g_aim_tol = 0; }
    if (rear) {
        { extern int g_want_jx, g_want_jy; if (g_want_jx >= 0) g_want_jx = 256 - g_want_jx, g_want_jy = 256 - g_want_jy; }
        keyset_t m = {{0}, 0};
        for (int i = 0; i < keys->n; i++) {
            uint8_t c = keys->k[i];
            ks_add(&m, c == K_ROLL_LEFT ? K_ROLL_RIGHT : c == K_ROLL_RIGHT ? K_ROLL_LEFT : c == K_CLIMB ? K_DIVE : c == K_DIVE ? K_CLIMB : c);
        }
        *keys = m;
    }
    int want;
    vec_t q;
    const ship_t *rc = collision_risk(s, n, P->ram_dist, &q);
    if (rc) {
        double m = hypot(q.x, q.y);
        double mx = m > 1 ? q.x / m : 0, my = m > 1 ? q.y / m : 1;
        ks_clear(keys);
        steer((vec_t){mx * 4000, my * 4000, 1500}, jstx(), jsty(), 3, 3, keys);
        want = 30;
    } else if (off > P->turn_off && dist < P->turn_close) want = P->spd_turn_close;
    else if (off > P->turn_off) want = P->spd_turn_far;
    else if (dist > P->far) want = P->spd_far;
    else if (dist > P->mid) want = P->spd_mid;
    else if (dist > P->near) want = P->spd_near;
    else want = P->spd_close;
    // evade: shields down and another enemy's nose on us within range: jink out of its line. In
    // bursts (0.8 s, then 2.5 s of fighting: attacked from behind someone's always facing us, and
    // jinking for ever meant never shooting back), and not when our own shot's nearly lined up
    static uint32_t evade_until, evade_next;
    int may_evade = now() < evade_until || (now() >= evade_next && off > P->evade_off_deg);
    if ((fsh() < P->evade_shield || ash() < P->evade_shield) && may_evade) {
        for (int i = 0; i < n; i++) {
            const ship_t *o = &s[i];
            if (!ship_in_space(o) || !(o->newb & NEWB_HOSTILE) || ship_dead(o) || !ship_is_ship(o) || o->slot == sh->slot) continue;
            double od = dist_of(o);
            if (od == 0) od = 1;
            double facing = -(o->nosev[0] * o->x + o->nosev[1] * o->y + o->nosev[2] * o->z) / od;
            if (od < 8000 && facing > 0.97) {
                ks_clear(keys);
                ks_add(keys, K_ROLL_LEFT);
                ks_add(keys, K_CLIMB);
                want = 25;
                if (now() >= evade_until) evade_until = now() + P->evade_on, evade_next = now() + P->evade_on + P->evade_gap;
                break;
            }
        }
    }
    speed_keys(speed(), want, keys);
    const shiptype_t *ti = ship_type(sh->type);
    double r = ti && ti->area ? sqrt((double) ti->area) : 80;
    int near_ = v.z > 0 && hypot(v.x, v.y) < P->fire_r * r && v.z < 30000;
    int hot = laser_temp() > P->hot;
    int view = rear ? 1 : 0, friendly = 0;
    for (int i = 0; i < n; i++)
        if (s[i].slot >= 1 && s[i].slot != sh->slot && in_crosshairs(&s[i], view) && !(s[i].newb & NEWB_HOSTILE)) friendly = 1;
    if (near_ && !hot && !friendly && space_view() == view && laser_power(view, sh->type) > 0) ks_add(keys, K_FIRE);
    {
        extern int g_steer_log;
        if (g_steer_log)
            dbgf_("fire? %s tgt %d d %.0f v(%.0f,%.0f,%.0f) r %.0f near %d hot %d friendly %d view %d/%d E %d F %d A %d", rear ? "REAR" : "front",
                  sh->type, dist, v.x, v.y, v.z, P->fire_r * r, near_, hot, friendly, space_view(), view, energy(), fsh(), ash());
    }
    if (g_steer_log) {
        char ks[64]; int k = 0;
        for (int i = 0; i < keys->n && k < 60; i++) k += snprintf(ks + k, sizeof ks - k, "%02x ", keys->k[i]);
        dbgf_("keys out: %s(rear %d, evade until %u now %u, ram %s)", ks, rear, evade_until, now(), rc ? "dodge" : "-");
    }
    *view_out = view;
    return off;
}

static int is_docked_or_briefing(void) { return is_docked() || briefing(); }

static struct { CO; uint32_t until; int target, missile_at, view, breaking; uint8_t key; double tally0; int energy0, energy_min, kills, scn_n, ttype, tries; double tx, ty, tz; } c_en;
int combat_target(void) { return c_en.target; }     // (for the lab's aiming measures)

// status lines for the fight: each hostile announced when it appears and when it blows up. By
// counts of each type: Elite moves ships down a slot when one goes, so slots don't identify them,
// and for a frame while it does, FRIN and K% disagree. So a count has to hold for a few frames
// before it counts, up or down.
#define NTYPES 40
typedef struct { uint8_t n, up, down; } tcount_t;
static tcount_t s_alive[NTYPES], s_dying[NTYPES];
static int settled(tcount_t *c, int now_n, int hold) {   // how many more than before, once it holds
    int more = 0;
    if (now_n > c->n) {
        c->down = 0;
        if (++c->up >= hold) more = now_n - c->n, c->n = (uint8_t) now_n, c->up = 0;
    } else if (now_n < c->n) {
        c->up = 0;
        if (++c->down >= 10) c->n = (uint8_t) now_n, c->down = 0;
    } else c->up = c->down = 0;
    return more;
}
static void announce(const ship_t *sh, int n) {
    uint8_t alive[NTYPES] = {0}, dying[NTYPES] = {0};
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_in_space(s) || s->type <= 0 || s->type >= NTYPES || (s->type >= 3 && s->type <= 8)) continue;
        if (ship_dead(s)) { dying[s->type]++; continue; }      // (whatever it was: an explosion is brief)
        if (s->type == 1 ? !(s->ai & 0x40) : !(s->newb & NEWB_HOSTILE)) continue;   // missiles: homing on us
        alive[s->type]++;
    }
    for (int t = 1; t < NTYPES; t++) {
        const shiptype_t *ty = ship_type(t);
        const char *name = t == 1 ? "Missile" : ty ? ty->name : "ship";
        for (int k = settled(&s_alive[t], alive[t], 3); k > 0; k--) {
            if (t == 1) status_("Incoming missile");
            else status_("Enemy %s detected", name);
        }
        for (int k = settled(&s_dying[t], dying[t], 1); k > 0; k--) {
            status_("%s destroyed", name);
            if (t != 1) c_en.kills++;
        }
    }
}

PT_THREAD(engage(float seconds)) {
    CO_BEGIN(c_en);
    c_en.until = deadline(seconds);
    c_en.target = -1;
    c_en.ttype = 0;
    c_en.breaking = 0;
    c_en.missile_at = -1;
    c_en.tally0 = tally();
    c_en.energy0 = c_en.energy_min = energy();
    c_en.kills = 0;
    note("{\"event\": \"fight_start\", \"tally\": %.2f, \"energy\": %d}", tally(), energy());
    for (;;) {
        CO_YIELD(c_en);
        g_phase = 1;
        if (is_docked()) {
            keyset_t k = {{0}, 0};
            g_flight = k;
            hold(&k);
            CO_EXIT(c_en);
        }
        c_en.key = 0;
        {
            static ship_t sh[NOSH];
            const ship_t *th[NOSH];
            int n = ships_read(IO->ram, sh);
            announce(sh, n);
            if (energy() < c_en.energy_min) c_en.energy_min = energy();
            // (in witchspace, every Thargoid about: they keep shooting while we pick a way out)
            int nt = threats(sh, n, witchspace() ? 60000 : g_cp.engage_range, th, NOSH);
            // in witchspace there's no escape pod: below g_bail_energy, stop and let the caller jump out
            // (the countdown is 15 s of being shot at: with more of them, go sooner. A fixed 180 left us at
            // 176 among six, dead before the jump)
            if (nt && witchspace() && g_bail_energy && energy() < g_bail_energy && !hyperspace_count()) {     // (counting: fight on)
                logf_("energy %d in witchspace with %d enemies: bailing out by hyperspace", energy(), nt);
                status_("Losing to the Thargoids: hyperspace out");
                CO_EXIT(c_en);
            }
            if (!nt || expired(c_en.until)) {
                keyset_t k = {{0}, 0};
                apply_flight(&k);
                logf_("fight over: tally %.2f -> %.2f, energy %d -> %d", c_en.tally0, tally(), c_en.energy0, energy());
                status_("Fight over: %+.2f, energy %d", tally() - c_en.tally0, energy());
                note("{\"event\": \"fight_end\", \"gained\": %.2f, \"kills\": %d, \"energy\": %d, \"energy_min\": %d}",
                     tally() - c_en.tally0, c_en.kills, energy(), c_en.energy_min);
                CO_EXIT(c_en);
            }
            cp_select();
            // follow the target itself, not its slot: Elite moves every ship above a dead one down a slot, so
            // a slot number alone silently swapped targets after kills (half our "switches" before aiming)
            int prev_target = c_en.target;
            if (c_en.target >= 0 && c_en.ttype) {
                const ship_t *f = NULL;
                double fd = 2500;
                for (int i = 0; i < nt; i++) if (th[i]->type == c_en.ttype) {
                    double dx = th[i]->x - c_en.tx, dy = th[i]->y - c_en.ty, dz = th[i]->z - c_en.tz, d = sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < fd) fd = d, f = th[i];
                }
                c_en.target = f ? f->slot : -1;
            }
            {
                // the target: most kill points for the time it takes to bring the guns on it (front, or
                // the rear laser), more for one shooting at our weaker shield; a new one only if it's
                // clearly better, so we don't dither between them
                const ship_t *best = NULL, *cur = NULL;
                double bv = -1, cv = -1;
                for (int i = 0; i < nt; i++) {
                    double v = target_value(th[i]);
                    if (th[i]->slot == c_en.target) cur = th[i], cv = v;
                    if (v > bv) bv = v, best = th[i];
                }
                if (!cur || bv > g_cpa->t_switch * cv) {
                    if (cur && best != cur && g_steer_log)
                        dbgf_("switch: target t%d (value %.2f, turn %.0f, e %d, d %.0f) -> t%d (value %.2f, turn %.0f, e %d, d %.0f)",
                              cur->type, cv, turn_iters(cur->x, cur->y, cur->z), cur->energy, dist_of(cur),
                              best->type, bv, turn_iters(best->x, best->y, best->z), best->energy, dist_of(best));
                    c_en.target = best->slot;
                }
                // (why the target changed, for the lab: 0 lost it (dead, out of range or no longer hostile),
                // 1 a missile came first, 2 a better target, 3 back from a missile)
                if (prev_target >= 0 && c_en.target != prev_target) {
                    extern long g_sw_why[4];
                    g_sw_why[!cur ? (prev_target >= 0 && c_en.ttype == 1 ? 3 : 0) : best->type == 1 ? 1 : 2]++;
                }
            }
            const ship_t *t = NULL;
            for (int i = 0; i < nt; i++) if (th[i]->slot == c_en.target) t = th[i];
            if (t) c_en.ttype = t->type, c_en.tx = t->x, c_en.ty = t->y, c_en.tz = t->z; else c_en.ttype = 0;
            keyset_t keys = {{0}, 0};
            int view;
            double off;
            // break off: hurt (front shield gone, energy going, the aft shield unused), two or more of them and
            // none in close yet: tail to them, full speed, until the energy is back. Found by pairing every fight
            // where it would start (1130 of 9327; identical up to there): with them still beyond 8192 and two or
            // more, it saved 33 and lost 5; against one ship (it chases: better to fight it now) or once they're
            // in close (their guns on our tail as we turn), it lost more than it saved. Sooner (front under 100,
            // energy under 230, recharge to 250): 36 more saved, 17 lost; sooner still, no better
            {
                static uint32_t brk_logged_at;
                double nearest = 1e9;
                for (int i = 0; i < nt; i++) if (dist_of(th[i]) < nearest) nearest = dist_of(th[i]);
                if (g_cpa->brk_energy && !c_en.breaking && fsh() < g_cpa->brk_shield && energy() < g_cpa->brk_energy && ash() > 2 * fsh() + 40 &&
                    (!g_cpa->brk_clear || nearest >= g_cpa->brk_clear) && nt >= g_cpa->brk_n) {
                    extern int g_brk_log;
                    c_en.breaking = g_brk_log != 2;      // (2: only log where it would)
                    if (g_brk_log && brk_logged_at != c_en.until) {    // the situation then (the lab pairs fights on it)
                        brk_logged_at = c_en.until;
                        int n8 = 0, guns = 0; double gnear = 99999, near_ = 99999, sume = 0;
                        for (int i = 0; i < nt; i++) {
                            double od = dist_of(th[i]) + 1;
                            double fac = -(th[i]->nosev[0] * th[i]->x + th[i]->nosev[1] * th[i]->y + th[i]->nosev[2] * th[i]->z) / od;
                            if (od < 8192) n8++;
                            if (od < 8192 && fac > 0.95) { guns++; if (od < gnear) gnear = od; }
                            if (od < near_) near_ = od;
                            sume += th[i]->energy;
                        }
                        dbgf_("BRK E %d F %d A %d spd %d nt %d n8 %d guns %d gnear %.0f near %.0f sume %.0f tgt_e %d off %.0f ws %d",
                              energy(), fsh(), ash(), speed(), nt, n8, guns, gnear, near_, sume, t ? t->energy : -1,
                              t ? acos(fmax(-1.0, fmin(1.0, t->z / (dist_of(t) + 1)))) * 180 / M_PI : -1, witchspace() != 0);
                    }
                }
                if (c_en.breaking && (energy() >= g_cpa->brk_until || ash() < fsh() + 20 || (g_cpa->brk_stop && nearest < g_cpa->brk_stop))) c_en.breaking = 0;
                if (c_en.breaking) {
                    double cx = 0, cy = 0, cz = 0;
                    for (int i = 0; i < nt; i++) {
                        double od = dist_of(th[i]) + 1, w = od < 8192 ? 1 : 0.2;
                        cx += th[i]->x / od * w, cy += th[i]->y / od * w, cz += th[i]->z / od * w;
                    }
                    steer((vec_t){-cx, -cy, -cz}, jstx(), jsty(), 3, 3, &keys);
                    speed_keys(speed(), 40, &keys);
                    view = 0;
                    off = 180;
                    goto flown;
                }
            }
            // an incoming missile does 250 to the shield on the side it hits from, then to our energy: when that
            // side can't take it but the other can, and it's not about to be in our sights, turn that side to it
            // (weak-ship corpus, 1884 fights from careers: deaths 14 -> 12, none lost; all 9327 fights unchanged)
            if (g_msl_side) {
                for (int i = 0; i < n; i++) {
                    const ship_t *m = &sh[i];
                    if (m->type != 1 || !(m->ai & 0x40) || ship_dead(m) || !ship_in_space(m)) continue;
                    double md = dist_of(m) + 1, moff = acos(fmax(-1.0, fmin(1.0, m->z / md))) * 180 / M_PI;
                    int front = m->z >= 0, here = (front ? fsh() : ash()) + energy(), there = (front ? ash() : fsh()) + energy();
                    // (only if the turn's done before it arrives: half-turned we neither shoot it nor take it astern.
                    // It closes at its speed (x1.5 a game loop) and ours; we turn ~1.8 deg a loop, after the roll)
                    double closing = 1.5 * m->speed + (front ? speed() : -speed()), t_hit = closing > 1 ? md / closing : 1e9;
                    double need = front ? 100 - moff : moff - 80, t_turn = need > 0 ? need / 1.8 + 8 : 0;
                    if (md < g_msl_side && here <= 250 && there > 250 && !(front && moff < 15) && t_turn < t_hit) {
                        // nose away from it (it then comes in astern) or onto it (ahead)
                        vec_t to = front ? (vec_t){-m->x, -m->y, -m->z} : (vec_t){m->x, m->y, m->z};
                        steer(to, jstx(), jsty(), 3, 3, &keys);
                        speed_keys(speed(), front ? 40 : 10, &keys);
                        view = space_view();
                        off = 180;
                        goto flown;
                    }
                }
            }
            off = combat_keys(sh, n, t, &keys, &view);
        flown:
            // outnumbered and hurting: the energy bomb destroys every ship about but Thargoids and
            // the mission ships, each scoring its kill points; before the pod
            if (R(A_BOMB) && (energy() < 140 || (fsh() > ash() ? fsh() : ash()) < 60)) {
                int close = 0;
                for (int i = 0; i < nt; i++) {
                    int ty = th[i]->type;
                    if (ty != 1 && ty != 29 && ty != 31 && ty != 32 && dist_of(th[i]) < 6000) close++;
                }
                // not with police about (killing one makes us fugitives), unless it's that or die
                int cops = 0;
                for (int i = 0; i < n; i++)
                    if (ship_in_space(&sh[i]) && (sh[i].newb & NEWB_COP) && !ship_dead(&sh[i]) && dist_of(&sh[i]) < 12000) cops++;
                if (close >= 2 && (!cops || energy() < 60)) {
                    logf_("energy bomb: %d attackers", close);
                    {
                        static uint32_t said = 0;       // (pressed until the game clears BOMB: say it once)
                        if (!said || now() - said > 250) status_("Energy bomb");
                        said = now();
                    }
                    note("{\"event\": \"bomb\", \"tally\": %.2f}", tally());
                    c_en.key = K_TAB;
                }
            }
            // losing: the escape pod keeps the tally (only cargo is lost)
            if (!c_en.key && energy() < g_cp.pod_energy && has_escape_pod() && !witchspace()) {
                logf_("energy %d: escape pod", energy());
                status_("Escape pod launched");
                goto pod;
            }
            // ECM when a missile gets close or we're hurting; until then shoot it down (0.58 points;
            // an ECM kill scores nothing)
            if (has_ecm() && !R(A_ECMA)) {
                for (int i = 0; i < n; i++)
                    if (sh[i].type == 1 && (sh[i].ai & 0x40) && !ship_dead(&sh[i]) &&
                        (dist_of(&sh[i]) < 2500 || energy() < 160 || (fsh() < ash() ? fsh() : ash()) < 100)) {
                        ks_add(&keys, K_ECM);
                        break;
                    }
            }
            apply_flight(&keys);
            if (!c_en.key && space_view() != view) c_en.key = view == 1 ? K_F1 : K_F0;     // rear / front view
            // missiles at tough ships, when hurt, or when outnumbered: T arms, the lock comes when the
            // target crosses the sights (MSTG = its slot), then M
            if (!c_en.key) {
                int enemies = 0;
                for (int i = 0; i < nt; i++) if (th[i]->type != 1) enemies++;
                // (a missile kill scores by the target's slot number, not its type, a bug in the Master
                // version: so missiles go at cheap ships when outnumbered, and at anything to survive)
                int cheap = points_of(t->type) <= 0.4;
                if (missiles() && t->type != 1 && ((cheap && enemies >= 3) || energy() < 150) &&
                    c_en.missile_at != t->slot) {
                    int mstg = R(A_MSTG);
                    if (mstg == 0xFF && R(A_MSAR) == 0 && off < 20) c_en.key = K_TARGET;
                    else if (mstg == c_en.target) { c_en.missile_at = c_en.target; c_en.key = K_MISSILE; }
                    else if (mstg != 0xFF && mstg != c_en.target) c_en.key = K_UNARM;
                }
            }
        }
        extern int g_steer_log;
        if (c_en.key && g_steer_log) dbgf_("special key %02x (view %d)", c_en.key, space_view());
        // (view keys are read from KL, which holds the highest-numbered key down: F0 (&20) is lower
        // than every flight key, so with any held it never registered and we stayed in the rear view
        // unable to fire. Let go of everything for the press, as press_h does for H)
        if (c_en.key == K_F0 || c_en.key == K_F1) {
            hold_none();
            CO_CALL(c_en, press(c_en.key, 4, 1));
        }
        else if (c_en.key) CO_CALL(c_en, press(c_en.key, 2, 1));
        continue;
    pod:
        // held 12 frames and checked (the pod gone from the equipment, or docked): among several ships a
        // main-loop pass outlasts a short press, and twice the key was missed, we sat a minute with
        // nothing held, and reloaded the save
        { keyset_t k = {{0}, 0}; g_flight = k; hold(&k); }
        for (c_en.tries = 0; c_en.tries < 4 && has_escape_pod() && !is_docked_or_briefing(); c_en.tries++) {
            CO_CALL(c_en, press(K_ESCPOD, 12, 4));
            c_en.until = deadline(1);
            CO_WAIT(c_en, !has_escape_pod() || is_docked_or_briefing() || expired(c_en.until));
        }
        if (has_escape_pod() && !is_docked_or_briefing()) logf_("escape pod: no launch after %d presses", c_en.tries);
        CO_CALL(c_en, wait_for(is_docked_or_briefing, 60, "escape pod"));
        g_in_flight = 0;
        note("{\"event\": \"escape_pod\", \"tally\": %.2f}", tally());
        CO_EXIT(c_en);
    }
    CO_END(c_en);
}

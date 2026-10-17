#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pilot.h"
#include "keyq.h"
#include "flight.h"
#include "galaxy.h"

#define PLANET_R 24576.0

static double vlen(vec_t v) { return sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static double vdot(vec_t a, vec_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec_t vadd(vec_t a, vec_t b, double k) { return (vec_t){a.x + k * b.x, a.y + k * b.y, a.z + k * b.z}; }
static vec_t vneg(vec_t a) { return (vec_t){-a.x, -a.y, -a.z}; }
static vec_t vnorm(vec_t a) { double n = vlen(a); if (n == 0) n = 1; return (vec_t){a.x / n, a.y / n, a.z / n}; }
static vec_t pos_of(const ship_t *s) { return (vec_t){(double) s->x, (double) s->y, (double) s->z}; }
static vec_t fvec(const float *f) { return (vec_t){f[0], f[1], f[2]}; }

static int jstx_for(double alpha) {
    int a = (int) lround(alpha);
    if (a > 31) a = 31;
    if (a < -31) a = -31;
    int m = abs(a), d = m == 0 ? 0 : (m <= 3 ? m * 8 + 4 : (m * 4 + 2 > 32 ? m * 4 + 2 : 32));
    return 128 + (a > 0 ? d : -d);
}

static int jsty_for(double beta) {
    int b = (int) lround(beta);
    if (b > 8) b = 8;
    if (b < -8) b = -8;
    int m = abs(b), e = m == 0 ? 0 : (m <= 2 ? 36 : (m * 16 + 6 < 127 ? m * 16 + 6 : 127));
    return 128 + (b > 0 ? -e : e);
}

// ---- Steering: proportional on the game's own control model (Moxon: main flight loop part 2, DK4).
// Roll the target into our vertical plane (above or below, whichever's nearer), pitch onto it; the
// stick positions those rates need (jstx_for, jsty_for), and a key towards them if the stick's not
// there. roll_iters and pitch_iters: how many iterations to close the angle in (the gains)
int g_steer_log;
double g_aim_tol;            // the target's hit radius as an angle (rad), for steer; 0 outside fights
int g_preload;
double g_coast = 1.5;
double g_up_hyst = 0.35;     // radians of hysteresis in the above/below choice (joystick aiming bench: time on target 11.8 -> 12.3%)
double g_nose_hold = 0;      // (test)
double g_roll_db = 0;        // (test)        // (test: radians of hysteresis in the above/below choice)
int g_coast_min = 112;      // (only slewing, the stick near full: tracking a mover needs the easing)

double steer(vec_t v, int jx, int jy, float roll_iters, float pitch_iters, keyset_t *keys) {
    double r = hypot(v.x, v.y);
    double off = atan2(r, v.z);
    double clock = atan2(v.x, v.y);
    int up = fabs(clock) <= M_PI / 2;
    // (which of above and below to roll the target into: kept until it's well across, not flipped at
    // 90 deg: near the nose the target wanders round the clock, and with a joystick every flip swung
    // the roll stick end to end and lost the lock)
    if (g_up_hyst > 0) {
        static int last_up = -1;
        static uint32_t last_at;
        extern uint32_t now(void);
        if (last_up >= 0 && now() - last_at < 25 && up != last_up && fabs(fabs(clock) - M_PI / 2) < g_up_hyst) up = last_up;
        last_up = up, last_at = now();
    }
    double roll_err = up ? clock : clock - copysign(M_PI, clock);
    // (within g_nose_hold hit radii of the nose, no roll: the target's clock angle is noise there, and
    // pitch alone brings it in; test)
    { extern double g_aim_tol; if (g_nose_hold > 0 && g_aim_tol > 0 && off < g_nose_hold * g_aim_tol) roll_err = 0; }
    // (or roll only as far as the hit needs: the sideways miss, off * sin(roll_err), inside g_roll_db hit
    // radii; test)
    { extern double g_aim_tol;
      if (g_roll_db > 0 && g_aim_tol > 0 && off > 1e-6) {
          double allow = asin(fmin(1.0, g_roll_db * g_aim_tol / off));
          roll_err = fabs(roll_err) <= allow ? 0 : roll_err - copysign(allow, roll_err);
      } }
    double roll_w = fmin(1.0, fmin(off / 0.06, (M_PI - off) / 0.5));
    double alpha = -roll_err * 256 / roll_iters * roll_w;
    double pw = fmax(0.0, cos(roll_err));
    double beta = off * 256 / pitch_iters * pw * pw * pw * (up ? 1 : -1);
    int tx = jstx_for(alpha), ty = jsty_for(beta);
    { extern int g_want_jx, g_want_jy; g_want_jx = tx, g_want_jy = ty; }     // (the joystick goes straight there)
    // preload (test): with the pitch error too small for a rate (the stick inside the dead zone, |J| <= 28),
    // park the stick at the dead zone's edge on the error's side: still no turn, but the next correction is
    // one press (14) away, not three (and not a snap through the centre from the wrong side)
    if (g_preload && ty == 128 && off > 0.0005) ty = 128 + (beta > 0 ? -g_preload : g_preload);
    if (tx > jx + 3) ks_add(keys, K_ROLL_LEFT);
    else if (tx < jx - 3) ks_add(keys, K_ROLL_RIGHT);
    // coast then stop (aiming bench, first targets from identical starts: median frames to the kill 209 -> 192, 101
    // faster, 11 slower; all fights 133 -> 131 deaths, pts/min +1.9%. Unrestricted it held the rate while tracking
    // movers too: 145 deaths): with auto-recentre, any easing of the pitch stick is a full stop (the opposite
    // key snaps it to the centre), and building a rate back up takes three presses. So turning towards the
    // target, hold the rate we have until the angle left is what it turns through in the keys' lag, then stop
    int coast = 0;
    if (g_coast && abs(jy - 128) >= g_coast_min && (jy - 128) * (ty - 128) > 0 && abs(ty - 128) < abs(jy - 128)) {
        static const double PR[][2] = {{27, 0}, {28, 1}, {41, 1}, {48, 3}, {64, 4}, {78, 6}, {91, 6}, {104, 7}, {117, 8}, {200, 8}};
        int d = abs(jy - 128);
        double rate = 0;
        for (int i = 1; i < 10; i++) if (d <= PR[i][0]) { rate = PR[i - 1][1] + (PR[i][1] - PR[i - 1][1]) * (d - PR[i - 1][0]) / (PR[i][0] - PR[i - 1][0]); break; }
        rate /= 256;                                    // rad a game loop
        double left = off * pw;                         // the pitch angle still to go
        if (left > rate * g_coast) coast = 1;           // (g_coast: the lag, game loops)
    }
    if (coast) ;
    else if (ty > jy + 6) ks_add(keys, K_DIVE);
    else if (ty < jy - 6) ks_add(keys, K_CLIMB);
    if (g_steer_log) dbgf_("steer off %.1f roll_err %.1f alpha %.1f beta %.1f | jx %d tx %d jy %d ty %d", off * 180 / M_PI,
                           roll_err * 180 / M_PI, alpha, beta, jx - 128, tx - 128, jy - 128, ty - 128);
    return off * 180 / M_PI;
}

// ---- Acquisition planner (test, PLAN=1; joystick only). Each game loop, forward-simulate Elite's own rotation of
// the target's position (MVEIT: K2 = y - a x; z += b K2; y = K2 - b z; x += a y, a = ALPHA/256, b = BETA/256), its
// own motion along its nose and ours along z, for every combined roll+pitch rate (ALPHA, BETA) the stick can give
// held for the next loop, then the base steering law (roll it above or below, pitch flat out, stopping on it);
// take the first step of the plan that brings it into the hit cylinder soonest (and keeps it there), re-plan
// next loop. The first loop flies on the stick as it is (the lag)
int g_plan;
double g_plan_hand = 0.5;       // (deg: inside max(this, 3 hit radii) the tracking law takes over; 2: acquire 143 -> 144 frames)
static const int PL_A[] = {0, 1, -1, 2, -2, 3, -3, 8, -8, 12, -12, 16, -16, 20, -20, 24, -24, 28, -28, 31, -31};
static const int PL_B[] = {0, 1, -1, 3, -3, 4, -4, 5, -5, 6, -6, 7, -7, 8, -8};
static int pl_alpha_q(double a) {        // nearest rate the stick can give: 0..3, 8..31
    int m = (int) lround(fabs(a));
    if (m > 31) m = 31;
    if (m > 3 && m < 8) m = m < 6 ? 3 : 8;
    return a < 0 ? -m : m;
}
static int pl_beta_q(double b) {         // 0, 1, 3..8
    int m = (int) lround(fabs(b));
    if (m > 8) m = 8;
    if (m == 2) m = fabs(b) < 2 ? 1 : 3;
    return b < 0 ? -m : m;
}
typedef struct { double x, y, z, vx, vy, vz; } pl_t;
static void pl_step(pl_t *p, int al, int be, double spd) {
    double a = al / 256.0, b = be / 256.0, K2;
    K2 = p->y - a * p->x; p->z = p->z + b * K2; p->y = K2 - b * p->z; p->x = p->x + a * p->y;
    K2 = p->vy - a * p->vx; p->vz = p->vz + b * K2; p->vy = K2 - b * p->vz; p->vx = p->vx + a * p->vy;
    p->x += p->vx, p->y += p->vy, p->z += p->vz - spd;
}
static void pl_policy(const pl_t *p, int *al, int *be) {
    double r = hypot(p->x, p->y), off = atan2(r, p->z), clock = atan2(p->x, p->y);
    int up = fabs(clock) <= M_PI / 2;
    double roll_err = up ? clock : clock - copysign(M_PI, clock);
    double roll_w = fmin(1.0, fmin(off / 0.06, (M_PI - off) / 0.5));
    *al = pl_alpha_q(-roll_err * 256 / 1.726 * roll_w);
    double pw = fmax(0.0, cos(roll_err));
    *be = pl_beta_q(fmin(8.0, off * 256 * pw * pw * pw) * (up ? 1 : -1));     // (flat out, then stop on it)
}
static int pl_in(const pl_t *p, double r2) { return p->z > 0 && p->x * p->x + p->y * p->y <= r2; }
// x, y, z: the target in the view; v: its own motion per loop (view frame); r2: the hit cylinder's radius squared;
// a0, b0: the rates the stick gives now. Out: the rates to ask for. Returns the predicted loops to the sights
// one rollout: (al, be) held for `hold` loops (al = 99: the base law throughout), then the base law
static double pl_roll(pl_t p, int a0, int b0, int al0, int be0, int hold, double spd, double r2) {
    enum { H = 50 };
    pl_step(&p, a0, b0, spd);                       // (the loop already under way: the stick as it is)
    double cost = 0;
    for (int k = 1; k < H; k++) {
        if (pl_in(&p, r2)) {                         // arrived: and does it stay (the base law tracking)?
            int stay = 0;
            pl_t q = p;
            for (int m = 0; m < 4; m++) { int al, be; pl_policy(&q, &al, &be); pl_step(&q, al, be, spd); stay += pl_in(&q, r2); }
            return k + 0.5 * (4 - stay) + 1e-4 * cost;
        }
        cost += atan2(hypot(p.x, p.y), p.z) * 180 / M_PI;      // (progress between whole loops)
        int al, be;
        if (al0 != 99 && k <= hold) al = al0, be = be0; else pl_policy(&p, &al, &be);
        pl_step(&p, al, be, spd);
    }
    return H + atan2(hypot(p.x, p.y), p.z) * 180 / M_PI + 1e-4 * cost;
}
// x, y, z: the target in the view; v: its own motion per loop (view frame); r2: the hit cylinder's radius squared;
// a0, b0: the rates the stick gives now. Out: the rates to ask for. Returns the predicted loops to the sights
static double plan_rates(double x, double y, double z, double vx, double vy, double vz, double spd, double r2, int a0, int b0, int *aout, int *bout) {
    pl_t p0 = {x, y, z, vx, vy, vz};
    { pl_t q = p0; pl_step(&q, a0, b0, spd); pl_policy(&q, aout, bout); }      // the base law's command
    double best = pl_roll(p0, a0, b0, 99, 0, 0, spd, r2);
    int na = sizeof PL_A / sizeof *PL_A, nb = sizeof PL_B / sizeof *PL_B;
    for (int i = 0; i < na; i++) for (int j = 0; j < nb; j++) {
        double sc = pl_roll(p0, a0, b0, PL_A[i], PL_B[j], 1, spd, r2);
        if (sc < best) best = sc, *aout = PL_A[i], *bout = PL_B[j];
    }
    return best;
}
static int jx_for_alpha(int a) {     // a stick position giving exactly this ALPHA
    int m = abs(a), d = m == 0 ? 0 : m <= 3 ? m * 8 + 4 : m * 4 + 2;
    if (d > 127) d = 127;
    return 128 + (a >= 0 ? d : -d);
}
static int jy_for_beta(int b) {      // ... and BETA (opposite sign to the stick)
    int m = abs(b), e = m == 0 ? 0 : m <= 2 ? 36 : m * 16 + 6;
    if (e > 127) e = 127;
    return 128 + (b > 0 ? -e : e);
}

// The planner as one call (shared: fights, and anything else turning onto a point): pos, the target in the
// view (x right, y up, z ahead); vel, its own motion per game loop in the same frame; spd, our speed (game units
// a loop along z; negate it in the rear view); radius, how close to the line of fire counts as on it; jx, jy, the
// stick as it is. Sets *want_jx, *want_jy (joystick positions) and returns the predicted loops to get there
double plan_stick(vec_t pos, vec_t vel, double spd, double radius, int jx, int jy, int *want_jx, int *want_jy) {
    int a, b;
    double t = plan_rates(pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, spd, radius * radius, alpha_from_jstx(jx), beta_from_jsty(jy), &a, &b);
    *want_jx = jx_for_alpha(a), *want_jy = jy_for_beta(b);
    return t;
}

void speed_keys(int delta, int want, keyset_t *keys) {
    if (delta < want) ks_add(keys, K_SPACE);
    else if (delta > want) ks_add(keys, K_SLOW);
}

const ship_t *find_planet(const ship_t *s, int n) {
    for (int i = 0; i < n; i++) if (s[i].slot == 0 && (s[i].type == 128 || s[i].type == 130)) return &s[i];
    return NULL;
}

const ship_t *find_station(const ship_t *s, int n) {
    for (int i = 0; i < n; i++) if (s[i].slot == 1 && s[i].type == SHIP_SST) return &s[i];
    return NULL;
}

// hold them, unless the planet is close and ahead: then climb away from it, whatever loop is
// flying (the altitude is only measured every 32 iterations)
// anything flying on a course that meets ours within the margin (asteroids, traders, canisters,
// pirates): steer out of its way, whatever we're doing. (Combat did this; travel didn't, and flew
// straight into a rock for 15 s on the way to the station.) Not the station: docking flies into it
void apply_flight(keyset_t *keys) {
    if (!is_docked() && altitude() < 70) {
        static ship_t sh[NOSH];
        int n = ships_read(IO->ram, sh);
        const ship_t *pl = find_planet(sh, n);
        if (pl) {
            vec_t P = pos_of(pl);
            double d = vlen(P);
            if (d == 0) d = 1;
            if (P.z / d > -0.3) {
                // (stop first: the planet doesn't move, so at speed 0 we can't hit it; the old 15-30 while
                // turning flew us into it, steering fighting the combat aim every frame)
                ks_clear(keys);
                steer(vneg(P), jstx(), jsty(), 3, 3, keys);
                speed_keys(speed(), P.z / d > 0 ? 0 : 10, keys);
            }
        }
    }
    g_flight = *keys;
    hold(&g_flight);
}

static int jump_allowed(const ship_t *sh, int n) {
    if (witchspace() || R(A_SSPR)) return 0;
    if (R(A_FRIN + 2 + R(A_JUNK)) != 0) return 0;
    const ship_t *pl = find_planet(sh, n);
    if (!pl) return 0;
    long m = labs(pl->x);
    if (labs(pl->y) > m) m = labs(pl->y);
    if (labs(pl->z) > m) m = labs(pl->z);
    if (pl->z > 0 && m < 2 * 65536) return 0;
    return 1;
}

static struct { CO; double t0; int hp0;  const sys_t *to; } c_fin;
static uint32_t s_ignore_until;
static int s_ignore_hp;
int g_flee_n = 3;       // with the pulse laser, jump out from this many or more (0 off)
static const sys_t *flee_to(void) {
    const sys_t *c[16];
    int d[16], n = in_range(galaxy(), here_x(), here_y(), fuel(), c, d, 16);
    const sys_t *best = NULL;
    double bs = -1e9;
    for (int i = 0; i < n; i++) {
        if (jump_trap(galaxy(), c[i]->num) || d[i] > jump_max(galaxy(), nearest_system(galaxy(), here_x(), here_y())->num)) continue;
        double s = c[i]->government * 10 - d[i] / 10.0;       // the most orderly in a safe jump
        if (s > bs) bs = s, best = c[i];
    }
    return best;
}
PT_THREAD(fight_if_needed(int *fought)) {
    CO_BEGIN(c_fin);
    *fought = 0;
    c_fin.to = NULL;
    {
        static ship_t sh[NOSH];
        const ship_t *t[4];
        int n = ships_read(IO->ram, sh);
        if (!threats(sh, n, 9000, t, 1)) CO_EXIT(c_fin);
        if (g_flee_n && laser(0) == 0x0F && !witchspace() && !hyperspace_count() && nearest_system(galaxy(), here_x(), here_y()) &&
            threats(sh, n, 30000, t, 4) >= g_flee_n)
            c_fin.to = flee_to();
        // a ship we fought for a whole engagement without either side scoring (an Asp kept its distance
        // for 16 minutes and we never docked): leave it be for 3 minutes unless it hurts us
        if (!expired(s_ignore_until) && energy() + fsh() + ash() >= s_ignore_hp) CO_EXIT(c_fin);
    }
    *fought = 1;
    if (c_fin.to) { CO_CALL(c_fin, hyperspace_fighting(c_fin.to)); CO_EXIT(c_fin); }
    c_fin.t0 = tally(), c_fin.hp0 = energy() + fsh() + ash();
    CO_CALL(c_fin, engage(240));
    if (tally() == c_fin.t0 && energy() + fsh() + ash() >= c_fin.hp0) {
        s_ignore_until = deadline(180);
        s_ignore_hp = energy() + fsh() + ash() - 20;
        logf_("a fight with nothing scored either way: ignoring them for 3 minutes");
    }
    CO_END(c_fin);
}

PT_THREAD(leave_witchspace(void));      // flight.c

static int is_docked_or_briefing(void) { return is_docked() || briefing(); }

static struct { CO; uint32_t end, pod_at, last_log; int fought; } c_ts;
PT_THREAD(to_station(float seconds)) {
    CO_BEGIN(c_ts);
    c_ts.end = deadline(seconds);
    c_ts.pod_at = deadline(240);         // pinned down for 4 game minutes: dock by escape pod
    c_ts.last_log = now();
    for (;;) {
        CO_YIELD(c_ts);
        if (R(A_SSPR) || is_docked() || dying() || title() || briefing()) CO_EXIT(c_ts);
        // (only if we can buy another: flying on without one cost a ship)
        if (expired(c_ts.pod_at) && has_escape_pod() && !witchspace() && cash() >= 10000) {
            logf_("pinned down short of the station: docking by escape pod");
            note("{\"event\": \"pod_dock\", \"tally\": %.2f}", tally());
            { keyset_t k = {{0}, 0}; apply_flight(&k); }
            CO_CALL(c_ts, press(K_ESCPOD, 6, 4));
            CO_CALL(c_ts, wait_for(is_docked_or_briefing, 60, "escape pod"));
            g_in_flight = 0;
            CO_EXIT(c_ts);
        }
        if (expired(c_ts.end)) CO_FAIL(c_ts, "to station");
        CO_CALL(c_ts, fight_if_needed(&c_ts.fought));
        if (c_ts.fought) continue;
        if (witchspace()) {
            CO_CALL(c_ts, leave_witchspace());
            continue;
        }
        {
            static ship_t sh[NOSH];
            int n = ships_read(IO->ram, sh);
            const ship_t *pl = find_planet(sh, n);
            keyset_t keys = {{0}, 0};
            if (pl) {
                vec_t P = pos_of(pl);
                double off;
                int want = 40;
                if (altitude() < 60) {
                    off = steer(vneg(P), jstx(), jsty(), 3, 3, &keys);
                    want = 20;
                } else {
                    off = steer(P, jstx(), jsty(), 3, 3, &keys);
                }
                speed_keys(speed(), want, &keys);
                if (off < 10 && jump_allowed(sh, n)) ks_add(&keys, K_JUMP);
                {        // anything on a collision course but the station (an asteroid: we flew into one for 15 s)
                    extern const ship_t *collision_ahead(const ship_t *s, int n, int margin, vec_t *miss);
                    vec_t q;
                    const ship_t *obst = collision_ahead(sh, n, 452, &q);
                    if (obst && obst->type != SHIP_SST) {      // (not the station: that's where we're going)
                        double m = hypot(q.x, q.y);
                        double mx = m > 1 ? q.x / m : 0, my = m > 1 ? q.y / m : 1;
                        ks_clear(&keys);
                        steer((vec_t){mx * 4000, my * 4000, 1500}, jstx(), jsty(), 3, 3, &keys);
                        speed_keys(speed(), 30, &keys);
                    }
                }
                if (now() - c_ts.last_log > 100) {
                    c_ts.last_log = now();
                    dbgf_("to station: planet %.0f, alt %d, off %.0f, spd %d", vlen(P), altitude(), off, speed());
                }
            }
            apply_flight(&keys);
        }
    }
    CO_END(c_ts);
}

static struct {
    CO; uint32_t end, last_log; int final, fought, have_spin, spin_mc; double spin, spin_psi; int spin_jx; int32_t left;
} c_dk;
int g_dock_log;              // (DOCKLOG: the approach, frame by frame)
int g_dock_centre = 1;       // (DOCKC=0: the old aim, for comparison)
PT_THREAD(dock(float seconds)) {
    CO_BEGIN(c_dk);
    c_dk.end = deadline(seconds);
    c_dk.final = 0;
    c_dk.have_spin = 0;
    c_dk.spin_mc = -1;
    c_dk.last_log = now();
    for (;;) {
        CO_YIELD(c_dk);
        if (is_docked() || briefing()) {
            g_in_flight = 0;
            { keyset_t k = {{0}, 0}; g_flight = k; hold(&k); }
            CO_EXIT(c_dk);
        }
        if (expired(c_dk.end)) CO_FAIL(c_dk, "dock");
        if (!c_dk.final) {
            CO_CALL(c_dk, fight_if_needed(&c_dk.fought));
            if (c_dk.fought) continue;
        }
        {
            static ship_t sh[NOSH];
            int n = ships_read(IO->ram, sh);
            if (!find_station(sh, n)) {
                c_dk.final = 0;
                goto need_station;
            }
        }
        {
            static ship_t sh[NOSH];
            int n = ships_read(IO->ram, sh);
            const ship_t *st = find_station(sh, n);
            vec_t S = pos_of(st), nv = vnorm(fvec(st->nosev)), rel = vneg(S), goal;
            double along = vdot(rel, nv);
            double lat = sqrt(fmax(0.0, vdot(rel, rel) - along * along));
            double dist = vlen(rel), off;
            int jx = jstx(), jy = jsty(), want, lined;
            keyset_t keys = {{0}, 0};
            // lined up: on the slot's axis, out in front (tighter to start the final than to stay in it)
            if (c_dk.final) lined = along > 0 && lat < fmax(300, along * 0.4);
            else lined = along > 1200 && lat < fmax(120, along * 0.08);
            if (!lined) {
                if (along < 600) {
                    vec_t side = vnorm(fvec(st->roofv));
                    if (vdot(rel, side) < 0) side = vneg(side);
                    goal = vadd(vadd(S, nv, 2500), side, along < 0 ? 2500 : 0);
                } else {
                    double k = dist < 12000 ? fmin(6000, fmax(2500, along)) : 6000;
                    goal = vadd(S, nv, k);
                }
                double gd = vlen(goal);
                want = gd > 4000 ? 40 : (int) fmax(6, gd / 120);
                // turning in: near the line-up point, already in front of the slot, steer for a point on the axis
                // nearer the station, so we come onto the axis facing it (arriving at the point itself we faced
                // across the axis and turned 100 deg at speed 4 to face the station)
                if (along > 1500 && gd < 1500) {
                    goal = vadd(S, nv, fmax(1200.0, along * 0.5));
                    want = (int) fmax(8, fmin(want, 14));
                }
                c_dk.final = 0;
                off = steer(goal, jx, jy, 3, 3, &keys);
                // flying straight at it: pitch only. Near the nose the steering's roll chased the target's angle
                // round it, the stick swinging full one way and the other all the way in
                if (off < 3) { ks_del(&keys, K_ROLL_LEFT); ks_del(&keys, K_ROLL_RIGHT); }
                if (off > 60 && want > 8) want = 8;
            } else {
                // (with the joystick none of this: the stick sits where it's put, damping or not)
                if (!c_dk.final && R(A_DAMP) && !(IO->analogue && R(0x2C5C))) {
                    // damping on for the final, as the game's own docking computer flies it: the roll stick
                    // then eases back 2 a loop when let go, the only way to hold it at the station's spin
                    // (alpha 16, J 66). Off (after launch, for the fights) it can only sit at 63 or 70 or
                    // snap to the centre: the slot see-sawed 20 deg either side all the way in. (Not in a
                    // fight: fight_if_needed has run above, nothing's about.)
                    hold_none();
                    CO_CALL(c_dk, set_damping(1));
                    continue;
                }
                // aim along the slot's axis: at a point on it ahead of us that slides in to the slot's centre
                // (on the station's face, 160 out from its centre), so an offset closes onto the axis
                // rather than curving into the face. Aiming at the centre, 12 off the axis 140 out was 5 deg:
                // the roll match gave up (off > 4) and the steering's own roll turned the slot 70 deg: bumps
                // (and always at least 300 ahead of us: in close, through the slot's centre to the station's,
                // which on the axis are in line. A point 160 + half the way left was beside us 170 out: the nose
                // swung 120 deg off and pitched us into the hull)
                double ax = fmin(160 + fmax(0.0, along - 160) * 0.5, fmax(0.0, along - 300));
                vec_t aimp = vadd(S, nv, ax);
                // the nose on the slot's centre itself, where it's drawn: on the station's face, 160 out;
                // in close (under 460) through it to the station's centre, in line with it on the axis
                if (g_dock_centre) aimp = along > 460 ? vadd(S, nv, 160) : S;
                off = steer(aimp, jx, jy, 3, 3, &keys);
                // a few seconds out and centred: roll with the station so its slot holds level
                double psi = atan2(st->roofv[1], st->roofv[0]);
                int mc = mcnt();
                if (c_dk.spin_mc >= 0) {
                    int dn = (c_dk.spin_mc - mc) & 255;
                    if (dn > 0 && dn < 6) {
                        double dpsi = fmod(psi - c_dk.spin_psi + 3 * M_PI, 2 * M_PI) - M_PI;
                        double w = dpsi / dn + alpha_from_jstx(c_dk.spin_jx) / 256.0;
                        c_dk.spin = c_dk.have_spin ? 0.8 * c_dk.spin + 0.2 * w : w;
                        c_dk.have_spin = 1;
                    }
                }
                c_dk.spin_mc = mc;
                c_dk.spin_psi = psi;
                c_dk.spin_jx = jx;
                if (g_steer_log && c_dk.have_spin)
                    dbgf_("spin %.4f rad/it (alpha %.2f) psi %.3f jx %d dist %.0f mc %d sroll %d", c_dk.spin, c_dk.spin * 256, psi, jx - 128, dist, mc, st->roll);
                // (with the nose within 10 deg: a bigger turn first needs the roll too; and always in the last
                // 600, where 12 off the axis is 5 deg and the steering's own roll turned the slot 70 deg)
                if (dist < 5000 && (off < 10 || dist < 600)) {
                    // roll with it: the station spins at a fixed rate (its roll counter is always
                    // full), and in the sim its slot holds still when our roll angle per iteration
                    // (alpha) is 16 the same way, measured as dpsi*256 = -3.2 - 0.20 alpha. So hold
                    // alpha there, steady, and close the slot's remaining angle with a small, capped
                    // trim; J at the middle of its band, so alpha doesn't flicker between steps
                    double err = fmod(psi + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                    double trim = fmax(-8.0, fmin(8.0, err * 256 / 25 / 0.20));
                    double alpha = (st->roll < 0 ? -16.0 : 16.0) + trim;
                    int a8 = (int) lround(fabs(alpha));
                    int jt = a8 >= 8 ? a8 * 4 + 2 : a8 * 8 + 4;
                    if (jt > 127) jt = 127;
                    int tx = 128 + (alpha < 0 ? -jt : jt);
                    ks_del(&keys, K_ROLL_LEFT);
                    ks_del(&keys, K_ROLL_RIGHT);
                    if (g_dock_centre) {
                        // pitch on the aim's height in the view, whatever its side: rolling with the station
                        // the view turns 3.6 deg a loop, so a sideways error comes round to the vertical within
                        // 25 loops. (The steering's pitch, scaled by cos^3 of the error's angle round the nose,
                        // barely pitched for one off to the side: the slot sat 3-6 deg off the sights)
                        double ay = atan2(aimp.y, aimp.z);
                        int ty = jsty_for(ay * 256 / 3);
                        ks_del(&keys, K_DIVE);
                        ks_del(&keys, K_CLIMB);
                        if (ty > jy + 6) ks_add(&keys, K_DIVE);
                        else if (ty < jy - 6) ks_add(&keys, K_CLIMB);
                    }
                    // (easing off, same side: let go and it drifts back, 2 an iteration. The other key
                    // would snap the roll to nothing and lose the station's spin: the old judder)
                    int ease = (tx - 128) * (jx - 128) > 0 && abs(jx - tx) < 24 && abs(jx - 128) > abs(tx - 128);
                    if (tx > jx + 3) { if (!ease) ks_add(&keys, K_ROLL_LEFT); }
                    else if (tx < jx - 3) { if (!ease) ks_add(&keys, K_ROLL_RIGHT); }
                    { extern int g_want_jx; if (g_want_jx >= 0) g_want_jx = tx; }      // (a joystick: exactly the spin)
                    if (g_steer_log) dbgf_("rollmatch err %.1f deg alpha %.1f tx %d jx %d", err * 180 / M_PI, alpha, tx - 128, jx - 128);
                }
                // steady in, slowing to a bump-safe 4 (under 5: a misaligned entry is a bump, not death) for
                // the last 400; and holding back while the slot isn't level near it
                double rl = fabs(fmod(atan2(st->roofv[1], st->roofv[0]) + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2) * 180 / M_PI;
                want = dist > 2500 ? 22 : dist > 1000 ? 14 : dist > 400 ? 9 : 4;
                if (dist < 600 && rl > 20) want = 1;
                if (off > 8 && want > 4) want = 4;
                c_dk.final = 1;
            }
            speed_keys(speed(), want, &keys);
            apply_flight(&keys);
            if (g_dock_log) {     // the approach frame by frame: range, along the slot axis and off it, the angle to
                                  // the station's centre, the slot's roll off level (deg), stick, speed, shields
                double psi = atan2(st->roofv[1], st->roofv[0]);
                double err = fmod(psi + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                vec_t sc = vadd(S, nv, 160);      // the slot's centre: where it's drawn, deg right and up of the sights
                dbgf_("DK %s fr %u S %.0f %.0f %.0f mc %d slot %.2f %.2f dist %.0f along %.0f lat %.0f off %.1f roll %.1f nvz %.2f jx %d jy %d spd %d E %d F %d A %d",
                      c_dk.final ? "F" : "L", now(), S.x, S.y, S.z, mcnt(), atan2(sc.x, sc.z) * 180 / M_PI, atan2(sc.y, sc.z) * 180 / M_PI, dist, along, lat, off, err * 180 / M_PI, nv.z, jx - 128, jy - 128, speed(), energy(), fsh(), ash());
            }
            if (now() - c_dk.last_log > 50) {
                c_dk.last_log = now();
                dbgf_("%s dist %.0f along %.0f lat %.0f off %.0f spd %d", c_dk.final ? "final" : "line up", dist, along,
                      lat, off, speed());
            }
        }
        continue;
    need_station:
        c_dk.left = (int32_t)(c_dk.end - now());
        if (c_dk.left < 250) c_dk.left = 250;
        CO_CALL(c_dk, to_station(c_dk.left / 50.0f));
    }
    CO_END(c_dk);
}

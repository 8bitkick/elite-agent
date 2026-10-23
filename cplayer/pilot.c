#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pilot.h"
#include "fair.h"
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

// the stick for a pitch of u (1/256 rad a game loop, + nose up) as the game gives it (beta_from_jsty): 0 under
// |J| 32, 1 to 47, then J / 16; so 0, 1 (J 40), 3 (48), ... 8 (128), the nearest without overshooting 2 to 3
static int jsty_nose_up(double u) {
    double a = fabs(u);
    int k = a < 0.5 ? 0 : a < 2.5 ? 1 : a > 8 ? 8 : (int) lround(a);
    int mag = k == 0 ? 0 : k == 1 ? 40 : 16 * k;
    if (mag > 127) mag = 127;
    return 128 + (u > 0 ? -mag : mag);         // (JSTY under 128: BETA +, the nose up)
}
static int g_want_jy_on(void) { extern int g_want_jy; return g_want_jy >= 0; }

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
      extern int show_on(void);
      double db = g_roll_db;
      if (db > 0 && g_aim_tol > 0 && off > 1e-6) {
          double allow = asin(fmin(1.0, db * g_aim_tol / off));
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
int g_plan = 1;
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
static int jx_for_alpha(int a);
static int jy_for_beta(int b);
// (fair mode: a hand moves the stick at most s_pl_lim units a game loop: the first loop of a plan gets only that far
// from where the stick is, s_pl_jx/jy. 0: the stick goes anywhere at once)
static int s_pl_lim, s_pl_jx, s_pl_jy;
static void pl_limit(int *al, int *be) {
    if (!s_pl_lim) return;
    int jx = jx_for_alpha(*al), jy = jy_for_beta(*be), dx = jx - s_pl_jx, dy = jy - s_pl_jy;
    dx = dx > s_pl_lim ? s_pl_lim : dx < -s_pl_lim ? -s_pl_lim : dx;
    dy = dy > s_pl_lim ? s_pl_lim : dy < -s_pl_lim ? -s_pl_lim : dy;
    *al = alpha_from_jstx(s_pl_jx + dx), *be = beta_from_jsty(s_pl_jy + dy);
}
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
        if (k == 1) pl_limit(&al, &be);
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

// ---- The same planner in single precision, and bounded (g_plan_f, the default; PLANF=0 for the double one above).
// The Presto's FPU (Cortex-M33) does float only: in double every step's atan2/hypot/cos went through software, and
// the full search (316 rollouts of up to 50 loops) cost 200 ms a frame on the board (1.4 ms on a macbox core).
// Bounded: (1) a rollout stops once its loop count reaches the best plan's so far (its score can't be lower: exact);
// (2) at most g_plan_cap steps a call (pf_step's), keeping the best found by then (the candidates go small rates
// first, so the cap cuts the big ones)
int g_plan_f = 3;               // bit 0: float, bit 1: prune
int g_plan_cap = 8000;          // steps a call (0: no cap): ~11 ms on the Presto (1.4 us a step in RAM)
long g_plan_steps, g_plan_calls, g_plan_capped, g_plan_maxsteps;       // (counters: the sim's timing report)
// (PF_HOT: on the Presto, in RAM (the SDK's .time_critical sections): from flash, through the XIP cache the emulator
// shares, a step cost ~1.7 us)
#ifdef PICO_BUILD
#define PF_HOT __attribute__((noinline, section(".time_critical.plan")))
#else
#define PF_HOT
#endif
typedef struct { float x, y, z, vx, vy, vz; } pf_t;
static int s_pf_steps;
static inline void pf_step(pf_t *p, int al, int be, float spd) {   // (inlined into the PF_HOT callers)
    float a = al * (1.0f / 256), b = be * (1.0f / 256), K2;
    K2 = p->y - a * p->x; p->z = p->z + b * K2; p->y = K2 - b * p->z; p->x = p->x + a * p->y;
    K2 = p->vy - a * p->vx; p->vz = p->vz + b * K2; p->vy = K2 - b * p->vz; p->vx = p->vx + a * p->vy;
    p->x += p->vx, p->y += p->vy, p->z += p->vz - spd;
    s_pf_steps++;
}
static inline int pf_alpha_q(float a) {
    int m = (int) (fabsf(a) + 0.5f);
    if (m > 31) m = 31;
    if (m > 3 && m < 8) m = m < 6 ? 3 : 8;
    return a < 0 ? -m : m;
}
static inline int pf_beta_q(float b) {
    int m = (int) (fabsf(b) + 0.5f);
    if (m > 8) m = 8;
    if (m == 2) m = fabsf(b) < 2 ? 1 : 3;
    return b < 0 ? -m : m;
}
// (a fast atan2: a minimax polynomial for atan on [0, 1], max error ~1e-5 rad; newlib's atan2f was most of a step)
static inline float pf_atan2(float y, float x) {
    const float PI = 3.14159265f;
    float ax = fabsf(x), ay = fabsf(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0) return 0;
    float t = mn / mx, s = t * t;
    float a = t * (0.99997726f + s * (-0.33262347f + s * (0.19354346f + s * (-0.11643287f + s * (0.05265332f + s * -0.01172120f)))));
    if (ay > ax) a = PI / 2 - a;
    if (x < 0) a = PI - a;
    return y < 0 ? -a : a;
}
// the base law (pl_policy); returns the angle off the nose (radians), which the rollout's cost needs too.
// (cos(roll_err) is |y| / r: rolling to put it straight above or below)
static PF_HOT float pf_policy(const pf_t *p, int *al, int *be) {
    const float PI = 3.14159265f;
    float r = sqrtf(p->x * p->x + p->y * p->y), off = pf_atan2(r, p->z), clock = pf_atan2(p->x, p->y);
    int up = p->y >= 0;
    float roll_err = up ? clock : clock - copysignf(PI, clock);
    float roll_w = fminf(1.0f, fminf(off / 0.06f, (PI - off) / 0.5f));
    *al = pf_alpha_q(-roll_err * (256 / 1.726f) * roll_w);
    float pw = r > 0 ? fabsf(p->y) / r : 1;
    *be = pf_beta_q(fminf(8.0f, off * 256 * pw * pw * pw) * (up ? 1 : -1));
    return off;
}
static inline int pf_in(const pf_t *p, float r2) { return p->z > 0 && p->x * p->x + p->y * p->y <= r2; }
static inline float pf_off(const pf_t *p) { return pf_atan2(sqrtf(p->x * p->x + p->y * p->y), p->z) * (180 / 3.14159265f); }
// one rollout, as pl_roll; bound: the best score so far (stop once this one can't beat it: its score is at least
// the loop it arrives on)
static PF_HOT float pf_roll(pf_t p, int a0, int b0, int al0, int be0, int hold, float spd, float r2, float bound) {
    enum { H = 50 };
    pf_step(&p, a0, b0, spd);
    float cost = 0;
    for (int k = 1; k < H; k++) {
        if (k >= bound) return 1e9f;
        if (pf_in(&p, r2)) {
            int stay = 0;
            pf_t q = p;
            for (int m = 0; m < 4; m++) { int al, be; pf_policy(&q, &al, &be); pf_step(&q, al, be, spd); stay += pf_in(&q, r2); }
            return k + 0.5f * (4 - stay) + 1e-4f * cost;
        }
        int al, be;
        float off = pf_policy(&p, &al, &be);
        cost += off * (180 / 3.14159265f);
        if (al0 != 99 && k <= hold) al = al0, be = be0;
        if (k == 1) pl_limit(&al, &be);
        pf_step(&p, al, be, spd);
    }
    return H + pf_off(&p) + 1e-4f * cost;
}
static double plan_rates_f(double x, double y, double z, double vx, double vy, double vz, double spd, double r2, int a0, int b0, int *aout, int *bout) {
    pf_t p0 = {(float) x, (float) y, (float) z, (float) vx, (float) vy, (float) vz};
    float fs = (float) spd, fr2 = (float) r2;
    int prune = g_plan_f & 2;
    s_pf_steps = 0;
    { pf_t q = p0; pf_step(&q, a0, b0, fs); pf_policy(&q, aout, bout); }
    float best = pf_roll(p0, a0, b0, 99, 0, 0, fs, fr2, 1e9f);
    int na = sizeof PL_A / sizeof *PL_A, nb = sizeof PL_B / sizeof *PL_B, capped = 0;
    // (small rates first, both axes together: the order the cap cuts in)
    for (int s = 0; s < na + nb && !capped; s++) for (int i = 0; i < na; i++) {
        int j = s - i;
        if (j < 0 || j >= nb) continue;
        if (g_plan_cap && s_pf_steps >= g_plan_cap) { capped = 1; break; }
        float sc = pf_roll(p0, a0, b0, PL_A[i], PL_B[j], 1, fs, fr2, prune ? best : 1e9f);
        if (sc < best) best = sc, *aout = PL_A[i], *bout = PL_B[j];
    }
    g_plan_steps += s_pf_steps, g_plan_calls++, g_plan_capped += capped;
    if (s_pf_steps > g_plan_maxsteps) g_plan_maxsteps = s_pf_steps;
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
#include "ptime.h"
double plan_stick(vec_t pos, vec_t vel, double spd, double radius, int jx, int jy, int *want_jx, int *want_jy) {
    PT_START(t0);
    int a, b;
    { s_pl_lim = (g_fair & 8) ? (int) (g_fair_stick_rate / fmax(g_ipf, 0.2)) : 0; s_pl_jx = jx, s_pl_jy = jy; }
    double t = (g_plan_f & 1 ? plan_rates_f : plan_rates)(pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, spd, radius * radius, alpha_from_jstx(jx), beta_from_jsty(jy), &a, &b);
    *want_jx = jx_for_alpha(a), *want_jy = jy_for_beta(b);
    PT_ADD(0, t0);
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
        ship_t *const sh = g_see_buf;
        int n = see_ships(sh);
        const ship_t *pl = find_planet(sh, n);
        if (pl) {
            vec_t P = pos_of(pl);
            double d = vlen(P);
            if (d == 0) d = 1;
            if (P.z / d > -0.3) {
                { extern int g_dock_log; if (g_dock_log) dbgf_("PLANETSTOP alt %d d %.0f", altitude(), d); }
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

int g_jump_log;             // (JLOG: why no in-system jump on the way to the station)
// the dull cruise (curious; the Presto fast-forwards it in turbo): flying in to the station, 20 s and more into it,
// no ship about but junk, not yet in the station's safe zone. (The round-7 review: 30% of a viewer's time was the
// planet sliding past for 1.5-2.5 minutes after every jump)
static uint32_t s_cruise_last, s_cruise_since;
static void cruise_tick(void) {
    if (now() - s_cruise_last > 2) s_cruise_since = now();
    s_cruise_last = now();
}
int g_turbo_ui_fix = 1;     // (TURBOUI=0: the old rule, for the comparison)
int player_dull_cruise(void) {
    extern int g_curious;
    // (only in a space view, no hyperspace countdown, and a second since the last of either: the Presto's screen rolled
    // at the hyperspace selection, turbo on through the chart, Data on System and the countdown)
    static uint32_t s_ui_last;
    { extern uint32_t g_save_until; if (g_curious && !expired(g_save_until)) return 1; }     // (and a save's disk menu)
    if (view() != 0 || hyperspace_count()) s_ui_last = now();
    if (g_turbo_ui_fix && (view() != 0 || hyperspace_count() || now() - s_ui_last < 50)) return 0;
    if (!g_curious || now() - s_cruise_last > 2 || now() - s_cruise_since < 20 * 50) return 0;
    if (is_docked() || witchspace() || R(A_SSPR)) return 0;
    if (R(A_FRIN + 2 + R(A_JUNK)) != 0) return 0;      // (a ship about: no)
    return 1;
}
static int jump_allowed(const ship_t *sh, int n) {
    if (witchspace() || R(A_SSPR)) return 0;
    if (!(g_fair & FAIR_DASH) && R(A_FRIN + 2 + R(A_JUNK)) != 0) return 0;     // (fair: press J and see)
    const ship_t *pl = find_planet(sh, n);
    if (!pl) return 0;
    long m = labs(pl->x);
    if (labs(pl->y) > m) m = labs(pl->y);
    if (labs(pl->z) > m) m = labs(pl->z);
    if (pl->z > 0 && m < 2 * 65536) return 0;
    return 1;
}

// (efficient, test) on the way to the station, the in-system jump is refused while any ship but junk is about: a
// hostile that keeps its distance beyond the travel fight's reach holds us to cruising speed for minutes. When
// only such hostiles stand in the way, go and get them (they come into reach, or we give up on one that opens)
int g_chase = 0;            // (the furthest to go after one; 0 off)
static int is_junk(int t) { return (t >= 3 && t <= 10) || t == 15; }
static int fight_reach(void);
static int chase_blocker(const ship_t *sh, int n, keyset_t *keys) {
    static int slot = -1, type;
    static uint32_t since, start, gave_up_until;
    static double d0;
    if (!g_chase || witchspace() || R(A_SSPR) || !expired(gave_up_until)) return 0;
    const ship_t *pl = find_planet(sh, n), *b = NULL;
    if (!pl) return 0;
    long m = labs(pl->x);
    if (labs(pl->y) > m) m = labs(pl->y);
    if (labs(pl->z) > m) m = labs(pl->z);
    if (pl->z > 0 && m < 2 * 65536) { slot = -1; return 0; }        // (no jump here anyway)
    double bd = 1e18;
    for (int i = 0; i < n; i++) {
        const ship_t *q = &sh[i];
        if (!ship_in_space(q) || !ship_is_ship(q) || q->type == SHIP_SST || is_junk(q->type)) continue;
        if (ship_dead(q)) return 0;                                   // (exploding: it'll be gone in a moment)
        if (!(q->newb & NEWB_HOSTILE) && q->type != 29 && q->type != 30) return 0;   // (a trader: chasing won't free the jump)
        double d = vlen(pos_of(q));
        if (d < bd) bd = d, b = q;
    }
    if (!b || bd > g_chase || bd < fight_reach()) { slot = -1; return 0; }
    if (b->slot != slot || b->type != type) slot = b->slot, type = b->type, since = start = now(), d0 = bd;
    // (opening on us for 10 s, or a minute without reaching it: leave it for 3 minutes)
    if ((now() - since > 500 && bd > d0) || now() - start > 3000) { gave_up_until = deadline(180); slot = -1; return 0; }
    if (now() - since > 500) since = now(), d0 = bd;
    steer(pos_of(b), jstx(), jsty(), 3, 3, keys);
    speed_keys(speed(), 40, keys);
    return 1;
}

static struct { CO; double t0; int hp0;  const sys_t *to; uint32_t f0; } c_fin;
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
// travelling, fight hostiles within this. They spawn ~11000 out (6400 on each axis) and chase us at about our
// speed, while we cruise on with the in-system jump refused: the 9000 of the first C port left them closing for
// tens of seconds at every encounter (seeds 13, 101, 37: 0.7-0.9 h a career). Efficient mode meets them at the
// fight's own engage range (careers to Elite, with WSNOREGEN: 31.5 -> 27.4 h, all five faster, deaths 9 -> 3).
// 0: by mode (curious keeps 9000); FIGHTR=n sets it
int g_fight_reach = 0;
static int fight_reach(void) { extern int g_curious; return g_fight_reach ? g_fight_reach : g_curious ? 9000 : g_cp.engage_range; }
PT_THREAD(fight_if_needed(int *fought)) {
    CO_BEGIN(c_fin);
    *fought = 0;
    c_fin.to = NULL;
    {
        ship_t *const sh = g_see_buf;
        const ship_t *t[4];
        int n = see_ships(sh);
        if (!threats(sh, n, fight_reach(), t, 1)) CO_EXIT(c_fin);
        if (g_flee_n && laser(0) == 0x0F && !witchspace() && !hyperspace_count() && nearest_system(galaxy(), here_x(), here_y()) &&
            threats(sh, n, 30000, t, 4) >= g_flee_n)
            c_fin.to = flee_to();
        // a ship we fought for a whole engagement without either side scoring (an Asp kept its distance
        // for 16 minutes and we never docked): leave it be for 3 minutes unless it hurts us
        if (!expired(s_ignore_until) && energy() + fsh() + ash() >= s_ignore_hp) CO_EXIT(c_fin);
    }
    *fought = 1;
    if (c_fin.to) { CO_CALL(c_fin, hyperspace_fighting(c_fin.to)); CO_EXIT(c_fin); }
    c_fin.t0 = tally_seen(), c_fin.hp0 = energy() + fsh() + ash(), c_fin.f0 = now();
    CO_CALL(c_fin, engage(240));
    { extern int g_fight_frames; g_fight_frames += (int) (now() - c_fin.f0); }     // (the docking lab's clock leaves fights out)
    // (fair mode: not when the "fight" was over within 10 s: a dot we took for a pirate turned out a trader when we
    // saw it close. Ignoring everything for 3 minutes after that left real pirates alone: 232 times a career)
    if (tally_seen() == c_fin.t0 && energy() + fsh() + ash() >= c_fin.hp0 && (!g_fair || now() - c_fin.f0 > 500)) {
        s_ignore_until = deadline(180);
        s_ignore_hp = energy() + fsh() + ash() - 20;
        logf_("a fight with nothing scored either way: ignoring them for 3 minutes");
    }
    CO_END(c_fin);
}

PT_THREAD(leave_witchspace(void));      // flight.c

static int is_docked_or_briefing(void) { return is_docked() || briefing(); }

static struct { CO; uint32_t end, pod_at, last_log, f0; int fought; } c_ts;
extern int g_curious;

// curious: after a fight that left the ship hurt, sit still and recharge (energy 240, both shields 200, or
// g_recharge seconds), fighting anything that comes meanwhile. (Not called now: tried in to_station and dock,
// it sat at anarchy stations where pirates keep coming: seed 13 at 65 h, fights 2578 -> 4323, 74 dock timeouts,
// tally 3236 -> 1299. Docking refills the ship; a curious career now just doesn't set out on a new leg hurt)
int g_recharge = 90;
int show_hurt(void) { return energy() < 200 || fsh() < 128 || ash() < 128; }
static int hurt(void) { return show_hurt(); }
static int recharged(void) { return energy() >= 240 && fsh() >= 200 && ash() >= 200; }
// (the jump log's line apart, its 300 bytes off to_station's stack: the plugin's stack is the firmware's, 2 KB)
static __attribute__((noinline)) void log_no_jump(const ship_t *sh, int n) {
    char b[300]; int k = 0;
    for (int i = 0; i < n && k < 260; i++) if (ship_in_space(&sh[i]) && sh[i].type < 128)
        k += snprintf(b + k, sizeof b - k, " t%d d%.0f%s%s", sh[i].type, vlen(pos_of(&sh[i])), (sh[i].newb & NEWB_HOSTILE) ? "h" : "", ship_dead(&sh[i]) ? "x" : "");
    b[k] = 0;
    dbgf_("  no jump: sspr %d junk %d:%s", R(A_SSPR), R(A_JUNK), b);
}
static struct { CO; uint32_t until; int fought; } c_rc;
PT_THREAD(recharge(void)) {
    CO_BEGIN(c_rc);
    if (!g_curious || !g_recharge || !hurt() || witchspace()) CO_EXIT(c_rc);
    c_rc.until = deadline((float) g_recharge);
    status_("Recharging: energy %d, shields %d/%d", energy(), fsh(), ash());
    while (!expired(c_rc.until) && !recharged()) {
        CO_YIELD(c_rc);
        if (is_docked() || dying() || title() || briefing() || R(A_SSPR)) CO_EXIT(c_rc);
        CO_CALL(c_rc, fight_if_needed(&c_rc.fought));
        if (c_rc.fought) continue;
        keyset_t k = {{0}, 0};
        speed_keys(speed(), 0, &k);
        apply_flight(&k);
    }
    CO_END(c_rc);
}
#if APP_LOADED
extern const double g_dk_clear;
#else
extern double g_dk_clear;
#endif
// (a frame of the run in, out of to_station's own stack frame: its locals were 816 bytes under every fight and log
// line it calls, and the plugin ran off the bottom of the firmware's stack)
static __attribute__((noinline)) void to_station_step(void)
{
        {
            ship_t *const sh = g_see_buf;
            int n = see_ships(sh);
            const ship_t *pl = find_planet(sh, n);
            keyset_t keys = {{0}, 0};
            if (chase_blocker(sh, n, &keys)) ;
            else if (pl) {
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
                    if (g_jump_log && !jump_allowed(sh, n)) log_no_jump(sh, n);      // (why no in-system jump: what's in the bubble)
                }
            }
            // (curious: the front view when flying in. A fight left on the rear view stayed there: 1.5 minutes of an
            // empty starfield on the way to the station)
            { extern int g_curious; if (g_curious && space_view() != 0) ks_add(&keys, K_F0); }
            cruise_tick();
            { extern int g_dock_log; if (g_dock_log) { ship_t *const s2 = g_see_buf; int n2 = see_ships(s2); const ship_t *p2 = find_planet(s2, n2); dbgf_("TS fr %u mc %d jx %d jy %d spd %d pd %.0f ja %d jk %d", now(), mcnt(), jstx() - 128, jsty() - 128, speed(), p2 ? vlen(pos_of(p2)) : 0.0, jump_allowed(s2, n2), ks_has(&keys, K_JUMP)); } }
            apply_flight(&keys);
        }
}
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
        c_ts.f0 = now();
        CO_CALL(c_ts, fight_if_needed(&c_ts.fought));
        // (curious: the leg's clock stops while we fight. Its showcase fights run 3-4 times as long, and in a
        // career the leg timed out after them and reloaded the save, kills and all: 13 times in 100 h, efficient 1)
        if (c_ts.fought && g_curious) c_ts.end += now() - c_ts.f0;
        if (c_ts.fought) continue;
        if (witchspace()) {
            CO_CALL(c_ts, leave_witchspace());
            continue;
        }
        to_station_step();
    }
    CO_END(c_ts);
}

// (the docking's numbers: the sim and the lab set them from the environment; the Presto's app keeps them in the flash,
// its RAM is 72 KB)
#if APP_LOADED
#define DKP const
#else
#define DKP
#endif
static struct {
    CO; uint32_t end, last_log, f0; int final, fought, have_spin, spin_mc, commit, xhold, lroll, vf, evade, arcaim, arcon, ff0; double spin, spin_psi; int spin_jx; int32_t left;
} c_dk;
int g_dock_log;              // (DOCKLOG: the approach, frame by frame)
int g_dock_centre = 1;       // (DOCKC=0: the old aim, for comparison)
int g_dock_fast = 1;         // (DOCKFAST=0: the braking profile always)
int g_dock_trig = 1;         // (DOCKTRIG=0: the old pitch, the centre held level with the nose, for comparison)
DKP double g_dk_xoff = 1, g_dk_fade0 = 700, g_dk_fade1 = 300;   // (DKXOFF, DKFADE0/1: the hold stops under this; the height let go over this range)
DKP double g_dk_shf = 100, g_dk_sha = 0, g_dk_clr = 250;       // (DKSHF, DKSHA, DKCLR: the commit's shields, fore and aft, and its clear radius)
DKP double g_dk_cosa = 0.98480775, g_dk_cen = 4.4;                 // (DKAPPR (deg), DKCEN: the commit's approach and centre limits)
DKP double g_dk_sw = 200;        // (DKSW: inside this along the axis the aim goes from the slot's centre to the station's: off the axis,
                             // a different direction, and the centring left no time to follow it: 5-8 across at entry)
DKP double g_dk_xon = 2;         // (DKXON: the sideways offset that starts the hold)
DKP double g_dk_ylim = 0.10510423526567646;     // (DKYLIM (deg): the most the hold sets the centre off the nose, tan 6 deg: at 3, a final
                                                // begun 10 deg off at 1600 out was still 70 to the side at 700, ground along the face: 25 bumps in 6 dockings -> 3 in 3)
DKP double g_dk_g = 0.15;        // (DKG: the share of the sideways offset closed a loop)
int g_dock_dash = 1;         // (curious: the powered run in, braking late; DOCKDASH=0 off)
DKP double g_dd_k = 1.5, g_dd_m = 150;
DKP double g_dk_lined = 0.08;    // (DKLINED: the final starts this far off the axis (of the distance along it): the centring
                             // there needs the loops a fast approach doesn't leave it)
DKP double g_dk_chi = 60, g_dk_vfk = 4;     // (DKCHI, DKVFK: the vector field's approach angle far off the axis, and k x the turn radius)
DKP double g_dk_room0 = 1800;    // (DKROOM: ... and where the turn's to be done: the final then has its time to centre)
DKP double g_dk_clear = 33000, g_dk_near = 60000; // (DKCLEAR, DKNEAR: ... kept this far from the planet's centre (the stop is at ~31000, altitude 70), acted on inside near)
DKP double g_dk_rm_d = 30000; // (DKRMD: the final rolls with the station inside this)
DKP double g_dk_ev_min = 1500, g_dk_ev_spd = 8;   // (DKEVMIN, DKEVSPD: no stepping aside inside this; its speed)
DKP double g_dk_evade_x = 300; // (DKEVADE: the step aside from traffic leaving the slot, 0 off: heavy hits 17 -> 3 of 308)
DKP double g_dk_arc_r = 2500, g_dk_arc_j = 4500, g_dk_arc_end = 80, g_dk_arc_kr = 8;   // (DKARCR, DKARCJ: the approach circle's radius and where it joins the axis)
DKP double g_dk_cone5 = 4200; // (DKCONE5: the vector field's cone, 0.6 lat + this)
DKP double g_dk_blend = 3;      // (DKBLEND: the line-up hands its aim and its stick over to the final's from this x the final's threshold)
DKP double g_dk_turn_m = 1.5;    // (DKTURNM: the margin on the room to turn in)
int g_fight_frames;           // (frames spent in fights on the way in: the arrival-to-dock time leaves them out)
int g_dock_begins;           // (dock() calls so far: run_sim's DOCKSNAP saves the machine at each)
// (test DKPLAN: the turn-in onto the slot as one move. Each loop, every steady roll and pitch the stick gives (a screw:
// rolling and pulling together), rolled out on Elite's own rotation of the view (MVEIT) for the aim g and on the slot's
// angle off level e (rolling at the station's spin, ALPHA spin, holds it); the plan arriving soonest with the aim on the
// nose and, weighted by ke, the slot level; a little for moving the stick from where it is (a0, b0))
DKP double g_dk_pka = 2, g_dk_pke = 0.3, g_dk_pks = 0.05;     // (DKPKA, DKPKE, DKPKS: a loop's worth of a degree off the nose, of the slot, of stick)
static void dk_screw(vec_t g, double e, int spin, double ke, int a0, int b0, int *aout, int *bout) {
    static const signed char A[] = {0, 2, -2, 8, -8, 12, -12, 16, -16, 20, -20, 24, -24, 28, -28, 31, -31};
    static const signed char B[] = {0, 1, -1, 3, -3, 4, -4, 5, -5, 6, -6, 8, -8};
    double n = vlen(g), best = 1e30;
    *aout = a0, *bout = b0;
    for (unsigned i = 0; i < sizeof A; i++) for (unsigned j = 0; j < sizeof B; j++) {
        double a = A[i] / 256.0, b = B[j] / 256.0, x = g.x / n, y = g.y / n, z = g.z / n, ee = e, c = 1e30;
        for (int k = 0; k < 60; k++) {
            double ck = k + g_dk_pka * atan2(hypot(x, y), z) * 180 / M_PI + ke * fabs(ee) * 180 / M_PI;
            if (ck < c) c = ck;
            double K2 = y - a * x; z += b * K2; y = K2 - b * z; x += a * y;
            ee -= (A[i] - spin) / 256.0;
            if (ee < -M_PI / 2) ee += M_PI; else if (ee >= M_PI / 2) ee -= M_PI;
        }
        c += g_dk_pks * (abs(A[i] - a0) + abs(B[j] - b0));
        if (c < best) best = c, *aout = A[i], *bout = B[j];
    }
}
DKP double g_dk_plan = 1;      // (DKPLAN=0: off, for comparison)
// (a frame of the approach, out of dock()'s own stack frame, as to_station_step. 1: damping on first)
static __attribute__((noinline)) int dock_step(void)
{
        {
            ship_t *const sh = g_see_buf;
            int n = see_ships(sh);
            const ship_t *st = find_station(sh, n);
            vec_t S = pos_of(st), nv = vnorm(fvec(st->nosev)), rel = vneg(S), goal;
            double along = vdot(rel, nv);
            double lat = sqrt(fmax(0.0, vdot(rel, rel) - along * along));
            double dist = vlen(rel), off;
            int jx = jstx(), jy = jsty(), want, lined;
            keyset_t keys = {{0}, 0};
            // lined up: on the slot's axis, out in front (tighter to start the final than to stay in it)
            if (c_dk.final) lined = along > 0 && lat < fmax(300, along * 0.4);
            else lined = along > 1200 && lat < fmax(120, along * g_dk_lined);
            // (on the arc: the final takes over only once the arc has brought us onto the axis: handed over at 8% of along,
            // halfway round, the final's re-aim swung the nose 39 deg)
            if (!c_dk.final && c_dk.arcon && lat > g_dk_arc_end) lined = 0;
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
                    // (smooth: on at the final's own pace. Its speed went with the distance across to the axis, so it
                    // crawled at 8 for ~40 s sliding onto it, then the final's 22 and 40: a stop and a surge)
                    want = along > 2500 ? 22 : 14;
                }
                // (the line-up: as fast as the turn still to make allows. A loop covers ~1.1 x the speed and the tightest
                // turn is BETA 8 (8/256 rad) a loop: a turn of radius ~35 x the speed, so turning th rad takes ~35 v th.
                // The turn: onto the slot's axis, and over to the aim and back (a sideways step is two turns). The room:
                // what's left before the commit, 1800 out, with g_dk_turn_m to spare)
                // (in front of the station only: behind it the way round to the front is a fixed detour, and as a
                // half turn with little room it crawled round the station at 8 for a minute)
                // (a vector field onto the slot's axis, as fixed-wing drones line up on a runway (Nelson et al.
                // 2007): not a point on the axis to chase but a heading for where we are, down the axis, turned towards it
                // by chi = chi_inf (2/pi) atan(k e) for e off it. Far off it comes in at chi_inf; near, the heading eases
                // onto the axis, with no overshoot, arriving pointed down it. k from the turn radius at speed (~35 v), so
                // what the field asks is a turn the ship can fly. The speed: as above, for the turn still to make)
                // (inside the approach cone only, where there's room to close e at the approach angle (along > ~0.6 e at
                // 60 deg); outside it, to the axis point in front as before. With hysteresis: switching at a fixed along,
                // a ship far off to the side sat on the line and flipped between the two aims, 40 deg apart. Let go only
                // well outside it: let go 1500 out, on the axis, the old aim was 2500 out, behind us, and it turned round)
                // (adaptive: the field only when the ship arrives in the cone with the room it needs, along > 0.6 lat +
                // g_dk_cone5 (its glide onto the axis plus the final's 3000); otherwise the turn-limited approach, with no
                // detour out in front to reach the cone (that cost ~27 s an approach and timeouts)
                {
                    int was = c_dk.vf;
                    c_dk.vf = c_dk.vf ? along > 0.4 * lat : along > 0.6 * lat + g_dk_cone5;
                    if (c_dk.vf != was) dbgf_("VF %s along %.0f lat %.0f dist %.0f", c_dk.vf ? "on" : "off", along, lat, dist);
                }
                // (and one planned curve (a Dubins-style approach circle): in the plane of the axis and the ship, a
                // circle of radius g_dk_arc_r touching the axis at g_dk_arc_j out on our side. Fly to its tangent point, then
                // round it onto the axis, joining it tangentially, pointed down it: no turn in, no re-aim)
                c_dk.arcaim = 0, c_dk.arcon = 0;
                vec_t arc_c = {0, 0, 0}, arc_t = {0, 0, 1}; int on_arc = 0;     // (the circle's centre and its direction of travel where we are)
                if (along > g_dk_arc_j + 300 && lat > 60) {
                    double R = g_dk_arc_r, aj = g_dk_arc_j;
                    vec_t w = vnorm(vadd(rel, nv, -along));                  // (from the axis towards us)
                    double ca = aj, ce = R, da = along - ca, de = lat - ce, dc = hypot(da, de), ga, ge;
                    if (dc > R * 1.03) {
                        // (the tangent point where going round the circle heads towards the join: of the two, the one
                        // whose direction of travel (clockwise in (a, e): towards smaller a at the bottom) agrees with ours)
                        double b = atan2(de, da), t = acos(R / dc), best = -2;
                        ga = ca, ge = 0;
                        for (int sgn = -1; sgn <= 1; sgn += 2) {
                            double ph = b + sgn * t, pa = ca + R * cos(ph), pe = ce + R * sin(ph);
                            double ta = sin(ph), te = -cos(ph);                // (travel direction there, round towards the join)
                            double ua = pa - along, ue = pe - lat, ul = hypot(ua, ue);
                            double dot = ul > 1 ? (ua * ta + ue * te) / ul : 1;
                            if (dot > best) best = dot, ga = pa, ge = pe;
                        }
                    } else {
                        on_arc = 1, c_dk.arcon = 1, arc_c = vadd(vadd(S, nv, ca), w, ce);
                        double ph = atan2(lat - ce, along - ca);              // (on the circle: aim 0.35 rad further round)
                        arc_t = vnorm(vadd(vadd((vec_t){0, 0, 0}, nv, sin(ph)), w, -cos(ph)));
                        double ph2 = ph - 0.35;
                        if (ph2 < -M_PI / 2) ph2 = -M_PI / 2;
                        ga = ca + R * cos(ph2), ge = ce + R * sin(ph2);
                        if (ph2 <= -M_PI / 2 + 0.01) ga = ca - 2000, ge = 0;   // (at the join: down the axis)
                    }
                    goal = vadd(vadd(S, nv, ga), w, ge);
                    want = 40;
                    c_dk.arcaim = 1;
                }
                if (!c_dk.arcaim && c_dk.vf) {
                    vec_t lv = vadd(rel, nv, -along), u = vneg(nv);
                    double e = vlen(lv), chi = g_dk_chi * M_PI / 180 * (2 / M_PI) * atan(e * g_dk_vfk / (35 * 40));
                    vec_t o = {0, 0, 0}, d = e > 1 ? vadd(vadd(o, u, cos(chi)), lv, -sin(chi) / e) : u;
                    goal = vadd(o, d, 2000);                // (a point 2000 down that heading, from us)
                    double th = atan2(hypot(goal.x, goal.y), goal.z) + chi;
                    double room = dist - g_dk_room0;
                    want = th < 0.01 ? 40 : (int) fmax(8, fmin(40, room / (35 * th * g_dk_turn_m)));
                }
                if (!c_dk.vf && !c_dk.arcaim && along > 1500) {
                    double th = atan2(hypot(nv.x, nv.y), -nv.z) + 2 * atan2(hypot(goal.x, goal.y), goal.z);
                    double room = dist - g_dk_room0;
                    want = th < 0.01 ? 40 : (int) fmax(8, fmin(40, room / (35 * th * g_dk_turn_m)));
                }
                c_dk.final = 0;
                // (the planet: the field and the feed fly straight lines that can graze it, and the emergency stop low over it
                // (apply_flight: altitude under 70, distance under ~42500) spun the ship round facing it. So, as a drone avoids an
                // obstacle by its collision cone: a goal behind the planet's clearance (g_dk_clear from its centre, altitude ~84)
                // is turned to the cone's edge, in the plane of the heading, and the ship skims past it)
                {
                    const ship_t *pl = find_planet(sh, n);
                    if (pl) {
                        vec_t P = pos_of(pl);
                        double dp = vlen(P), gl = vlen(goal);
                        vec_t d = vnorm(goal);
                        double tc = fmax(0.0, fmin(gl, vdot(P, d)));          // (the segment to the goal's nearest point to the centre)
                        vec_t cp = vadd(P, d, -tc);
                        if (dp < g_dk_near && vlen(cp) < g_dk_clear) {     // (only low over it: the station orbits close, so further out every aim grazed it)
                            vec_t ph = vnorm(P);
                            double rho = asin(fmin(1.0, g_dk_clear / dp)), cg = vdot(d, ph);     // (inside the clearance: along the tangent)
                            if (cg > cos(rho)) {
                                vec_t u = vadd(d, ph, -cg);
                                if (vlen(u) < 1e-6) u = vnorm(vadd(S, ph, -vdot(S, ph)));
                                u = vnorm(u);
                                vec_t o = {0, 0, 0}, nd = vadd(vadd(o, ph, cos(rho)), u, sin(rho));
                                goal = vadd(o, nd, gl);
                            }
                        }
                    }
                }
                // (test DKBLEND: nearing the hand-over (lat under DKBLEND x the final's threshold), the aim slides from the
                // line-up's goal to the slot's centre, the final's aim, reaching it at the hand-over)
                double bw = 0;
                if (g_dk_blend > 1 && !on_arc && along > 1200) {
                    double thr = fmax(120, along * g_dk_lined), w = (g_dk_blend * thr - lat) / ((g_dk_blend - 1) * thr);
                    bw = fmax(0.0, fmin(1.0, w));
                    if (w > 0) {
                        // (to within the final's sideways hold of the slot (DKYLIM, 6 deg), on the line-up's side: the aim
                        // still closing the offset from the axis, as the final's own does)
                        vec_t sc = vnorm(vadd(S, nv, 160)), gd = vnorm(goal), o = {0, 0, 0};
                        double th = acos(fmax(-1.0, fmin(1.0, vdot(sc, gd)))), keep = fmin(th, atan(g_dk_ylim));
                        double t = th - bw * (th - keep);
                        vec_t px = vadd(gd, sc, -vdot(sc, gd));
                        if (vlen(px) > 1e-6) goal = vadd(vadd(o, sc, 2000 * cos(t)), vnorm(px), 2000 * sin(t));
                    }
                }
                off = steer(goal, jx, jy, 3, 3, &keys);
                // flying straight at it: pitch only. Near the nose the steering's roll chased the target's angle
                // round it, the stick swinging full one way and the other all the way in
                if (off < 3) { ks_del(&keys, K_ROLL_LEFT); ks_del(&keys, K_ROLL_RIGHT); }
                // (smooth: and the joystick too, which that missed: the stick swung side to side every second, the
                // station rocking. A roll only as one move: from 3 deg off until it's within 1)
                {
                    c_dk.lroll = c_dk.lroll ? off > 1 : off > 3;
                    extern int g_want_jx;
                    if (!c_dk.lroll) {
                        ks_del(&keys, K_ROLL_LEFT); ks_del(&keys, K_ROLL_RIGHT);
                        if (g_want_jx >= 0) g_want_jx = 128;
                    } else if (g_want_jx >= 0 && abs(g_want_jx - 128) > 66)
                        g_want_jx = g_want_jx > 128 ? 194 : 62;     // (a roll at ALPHA 16, the station's own spin, not the stick's end)
                }
                if (g_dk_plan && bw > 0 && goal.z > 0) {
                    extern int g_want_jx, g_want_jy;
                    if (g_want_jx >= 0) {
                        double serr = fmod(atan2(st->roofv[1], st->roofv[0]) + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                        int al, be;
                        dk_screw(goal, serr, st->roll < 0 ? -16 : 16, g_dk_pke * bw, alpha_from_jstx(jx), beta_from_jsty(jy), &al, &be);
                        g_want_jx = jx_for_alpha(al), g_want_jy = jy_for_beta(be);
                        if (g_dock_log) dbgf_("SCREW L w %.2f serr %.1f al %d be %d", bw, serr * 180 / M_PI, al, be);
                    }
                }
                // (on the circle: bank once and hold it, the circle's centre straight above the nose, and pitch round it, as a
                // plane flies a steady turn. Rolling after the aim point as it slid round the circle flicked the stick side to
                // side; this rolls only by the centre's own small drift, at most ALPHA 16)
                if (on_arc) {
                    extern int g_want_jx;
                    double err = atan2(arc_c.x, arc_c.y);
                    ks_del(&keys, K_ROLL_LEFT); ks_del(&keys, K_ROLL_RIGHT);
                    if (g_want_jx >= 0) {
                        double alpha = fabs(err) < 0.05 ? 0 : fmax(-24.0, fmin(24.0, -err * 256 / 4));
                        int a8 = (int) lround(fabs(alpha)), jt = a8 == 0 ? 0 : a8 >= 8 ? a8 * 4 + 2 : a8 * 8 + 4;
                        if (jt > 127) jt = 127;
                        g_want_jx = 128 + (alpha < 0 ? -jt : jt);
                        // (and the turn itself: the circle's rate at our speed, a pass covering ~1.1 v, plus the radial error
                        // (outside the circle: turn harder), only as much as the centre is above the nose)
                        extern int g_want_jy;
                        // (plus the heading: pitch towards the circle's own direction where we are, so we fly round it, not just at
                        // its radius: cutting inside, the radius alone eased off and it met the axis 30 deg on)
                        double R = g_dk_arc_r, dc = vlen(arc_c), herr = atan2(arc_t.y, arc_t.z);
                        double rate = 1.1 * speed() / R * 256 + (dc - R) / R * g_dk_arc_kr + herr * 256 / 6;
                        double beta = fmax(-2.0, fmin(8.0, rate)) * fmax(0.0, cos(err));
                        g_want_jy = jsty_nose_up(beta);
                        if (g_dock_log) dbgf_("ARC err %.1f dc %.0f rate %.2f beta %.2f jy %d", err * 180 / M_PI, dc, rate, beta, g_want_jy - 128);
                    }
                }
            } else {
                // (with the joystick none of this: the stick sits where it's put, damping or not)
                if (!c_dk.final && R(A_DAMP) && !(IO->analogue && R(A_JSTK))) {
                    // damping on for the final, as the game's own docking computer flies it: the roll stick
                    // then eases back 2 a loop when let go, the only way to hold it at the station's spin
                    // (alpha 16, J 66). Off (after launch, for the fights) it can only sit at 63 or 70 or
                    // snap to the centre: the slot see-sawed 20 deg either side all the way in. (Not in a
                    // fight: fight_if_needed has run above, nothing's about.)
                    hold_none();
                    return 1;
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
                if (g_dock_centre) aimp = along > g_dk_sw ? vadd(S, nv, 160) : S;
                // (traffic: a shuttle or transporter leaving the slot flies straight out along the axis, at us. Braking alone
                // didn't avoid it (it still arrives: 19 head-on hits in 308 docks, 145 off the front shield, some into energy).
                // So step aside: aim g_dk_evade_x to the side away from it, slow to 14, and once it's behind, the final re-centres)
                if (g_dk_evade_x > 0) {
                    int threat = 0; double tx = 0;
                    for (int i = 0; i < n; i++) {
                        const ship_t *q = &sh[i];
                        if (q == st || !ship_in_space(q) || ship_dead(q) || q->type >= 128) continue;
                        vec_t p = pos_of(q);
                        if (p.z > -100 && p.z < dist + 400 && hypot(p.x, p.y) < 450) threat = 1, tx = p.x;
                    }
                    if (threat) c_dk.evade = tx > 0 ? -1 : 1;
                    else c_dk.evade = 0;
                    if (c_dk.evade && dist > g_dk_ev_min) { aimp.x += c_dk.evade * g_dk_evade_x; c_dk.commit = 0; }
                }
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
                if (dist < g_dk_rm_d && (off < 10 || dist < 600)) {     // (from the final's start: the slot spun 0-90 deg beyond 5000, the commit (slot within 7) only flickered, and it crawled at 22 for 30 s)
                    // roll with it: the station spins at a fixed rate (its roll counter is always
                    // full), and in the sim its slot holds still when our roll angle per iteration
                    // (alpha) is 16 the same way, measured as dpsi*256 = -3.2 - 0.20 alpha. So hold
                    // alpha there, steady, and close the slot's remaining angle with a small, capped
                    // trim; J at the middle of its band, so alpha doesn't flicker between steps
                    double err = fmod(psi + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                    // the slot's centre onto the nose, both ways, with the slot level. Elite moves what we see each
                    // loop by our roll a and pitch b (MVEIT): K2 = y - a x; z += b K2; y = K2 - b z; x += a y. Rolling
                    // with the station (a about 16/256), the pitch sets y, and x changes only by a y: holding the
                    // centre level with the nose (y 0, as every pitch rule did) kept x as it was all the way in (16
                    // to the side at entry, the median: the face beside the slot). So hold it at y = -g x / a, where
                    // x shrinks by g a loop, and y with it: b = (K2 - y) / z, the nearest rate the stick gives
                    // (the joystick; the station's centre under 460 out, in line with the slot's)
                    if (g_dock_trig && g_want_jy_on() && aimp.z > 100) {
                        double a = alpha_from_jstx(jx) / 256.0;
                        if (fabs(a) > 0.03) {
                            double x = aimp.x, y = aimp.y, z = aimp.z, lim = z * g_dk_ylim;
                            // (in whole units, though: MVEIT works out a y as (y ALPHA) / 256 and drops the fraction, so
                            // with ALPHA 16 nothing moves under |y| 16. Asked for 5 to 9, x stayed 2-4 off and drifted.
                            // So the law's y, or 20 if that's less (x moving a unit a loop), while it's over 2 off, until
                            // it's under 1; then 0 (20 alone took 60 loops for a 60 off, more than the approach has). And
                            // the height let go over 700 to 300 out: held to the end it's the miss across the slot)
                            c_dk.xhold = c_dk.xhold ? fabs(x) >= g_dk_xoff : fabs(x) > g_dk_xon;
                            double ys = c_dk.xhold ? -copysign(fmax(fabs(g_dk_g * x / a), 20.0), x / a) : 0;
                            ys = fmax(-lim, fmin(lim, ys)) * fmax(0.0, fmin(1.0, (z - g_dk_fade1) / (g_dk_fade0 - g_dk_fade1)));
                            double b = ((y - a * x) - ys) / z;            // (rad a loop, + nose up)
                            { extern int g_want_jy; g_want_jy = jsty_nose_up(b * 256); }
                            if (g_dock_log) dbgf_("DM x %.1f y %.1f z %.0f a %.3f ys %.1f b %.2f", x, y, z, a * 256, ys, b * 256);
                        }
                    }
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
                } else if (g_dk_plan && aimp.z > 0) {
                    extern int g_want_jx, g_want_jy;
                    if (g_want_jx >= 0) {
                        double serr = fmod(psi + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                        int al, be;
                        dk_screw(aimp, serr, st->roll < 0 ? -16 : 16, g_dk_pke, alpha_from_jstx(jx), beta_from_jsty(jy), &al, &be);
                        g_want_jx = jx_for_alpha(al), g_want_jy = jy_for_beta(be);
                        if (g_dock_log) dbgf_("SCREW F serr %.1f al %d be %d", serr * 180 / M_PI, al, be);
                    }
                }
                // steady in, slowing to a bump-safe 4 (under 5: a misaligned entry is a bump, not death) for
                // the last 400; and holding back while the slot isn't level near it
                double rl = fabs(fmod(atan2(st->roofv[1], st->roofv[0]) + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2) * 180 / M_PI;
                want = dist > 2500 ? 22 : dist > 1000 ? 14 : dist > 400 ? 9 : 4;
                if (c_dk.vf && dist > 4000) want = 40;      // (the final's start, the slot still levelling: no dip to 22 with the room to brake later)
                // (curious, docking with style: hard acceleration, slowing only in time): lined up (the nose
                // within 5 deg, the slot level within 10, near the axis), the fastest speed that still brakes to 4 by 400
                // out: a speed key changes it 1 a game loop and a loop covers ~1.1 x the speed, so braking from v takes
                // ~1.1 (v^2 - 16) / 2; with g_dd_k of that and g_dd_m to spare. The last 400 at 4, as ever
                if (g_curious && g_dock_dash && dist > 400 && off < 5 && rl < 10 && lat < fmax(60.0, dist * 0.05)) {
                    double room = dist - 400 - g_dd_m;
                    int vmax = room > 0 ? (int) sqrt(2 * room / (1.1 * g_dd_k) + 16) : 4;
                    if (vmax > want) want = vmax > 40 ? 40 : vmax;
                }
                // flat out into the slot. The game docks at any speed: what it checks (main flight loop part 9) is the
                // approach within 26 deg of the slot's axis, the station's centre within 22 deg of the nose and the
                // slot within 36.6 deg of level; speed only decides what a failed docking costs (under 5 a bump, else
                // death). So with all three inside a fifth of their limits and nothing on the way in (a ship leaving
                // the slot: a collision costs shields), 40 to the end. Inside 1800 out it's committed: braking from
                // 40 to under 5 takes ~36 loops, as far as that
                if (g_dock_fast) {
                    double cosappr = -nv.z, cen = atan2(hypot(S.x, S.y), S.z) * 180 / M_PI;     // (no acos: libm's differs by machine)
                    int clear = 1;
                    for (int i = 0; i < n; i++) {
                        const ship_t *q = &sh[i];
                        if (q == st || !ship_in_space(q) || ship_dead(q) || q->type >= 128) continue;
                        vec_t p = pos_of(q);
                        if (p.z > 0 && p.z < dist + 400 && hypot(p.x, p.y) < g_dk_clr) clear = 0;
                    }
                    // (and both shields at 160 or more: the station's own shuttles and transporters, coming out of the slot
                    // as we go in, still meet us head-on, 2 dockings in 100, 145 off the shields; that never reaches energy
                    // or cargo while the shields can take it)
                    int ok = cosappr > g_dk_cosa && cen < g_dk_cen && rl < 7 && clear && fsh() >= g_dk_shf && ash() >= g_dk_sha;
                    if (g_dock_log) {
                        char tb[120]; int k = 0;
                        for (int i = 0; i < n && k < 100; i++) {
                            const ship_t *q = &sh[i];
                            if (q == st || !ship_in_space(q) || q->type >= 128) continue;
                            vec_t p = pos_of(q);
                            if (vlen(p) < 3000) k += snprintf(tb + k, sizeof tb - k, " t%d %.0f,%.0f,%.0f", q->type, p.x, p.y, p.z);
                        }
                        tb[k] = 0;
                        dbgf_("DF cosappr %.4f cen %.1f rl %.1f clear %d commit %d near%s", cosappr, cen, rl, clear, c_dk.commit, tb);
                    }
                    if (!ok && (dist > 1800 || !clear)) c_dk.commit = 0;
                    else if (ok) c_dk.commit = 1;
                    if (c_dk.commit) want = 40;
                }
                if (c_dk.evade && dist > g_dk_ev_min) { c_dk.commit = 0; if (want > g_dk_ev_spd) want = (int) g_dk_ev_spd; }     // (stepping aside: slow, no commit)
                if (dist < 600 && rl > 20) want = 1;
                if (off > 8 && want > 4 && !c_dk.commit && !(c_dk.vf && dist > 4000)) want = 4;     // (far out the turn is room enough at 40: the field's hand-over is ~26 deg)
                c_dk.final = 1;
            }
            speed_keys(speed(), want, &keys);
            // (curious: the front view when flying in. A fight left on the rear view stayed there: 1.5 minutes of an
            // empty starfield on the way to the station)
            { extern int g_curious; if (g_curious && space_view() != 0) ks_add(&keys, K_F0); }
            apply_flight(&keys);
            if (g_dock_log) {     // the approach frame by frame: range, along the slot axis and off it, the angle to
                                  // the station's centre, the slot's roll off level (deg), stick, speed, shields
                double psi = atan2(st->roofv[1], st->roofv[0]);
                double err = fmod(psi + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2;
                vec_t sc = vadd(S, nv, 160);      // the slot's centre: where it's drawn, deg right and up of the sights
                vec_t lv = vadd(rel, nv, -along);         // (and the offset across the slot and along it)
                dbgf_("DK %s fr %u S %.0f %.0f %.0f mc %d slot %.2f %.2f dist %.0f along %.0f lat %.0f off %.1f roll %.1f nvz %.2f jx %d jy %d spd %d E %d F %d A %d sd %.0f up %.0f",
                      c_dk.final ? "F" : "L", now(), S.x, S.y, S.z, mcnt(), atan2(sc.x, sc.z) * 180 / M_PI, atan2(sc.y, sc.z) * 180 / M_PI, dist, along, lat, off, err * 180 / M_PI, nv.z, jx - 128, jy - 128, speed(), energy(), fsh(), ash(),
                      vdot(lv, vnorm(fvec(st->sidev))), vdot(lv, vnorm(fvec(st->roofv))));
            }
            if (now() - c_dk.last_log > 50) {
                c_dk.last_log = now();
                dbgf_("%s dist %.0f along %.0f lat %.0f off %.0f spd %d", c_dk.final ? "final" : "line up", dist, along,
                      lat, off, speed());
            }
        }
    return 0;
}
PT_THREAD(dock(float seconds)) {
    CO_BEGIN(c_dk);
    g_dock_begins++;
    c_dk.end = deadline(seconds);
    c_dk.final = 0;
    c_dk.have_spin = 0;
    c_dk.spin_mc = -1;
    c_dk.commit = 0;
    c_dk.xhold = 0;
    c_dk.lroll = 0;
    c_dk.vf = 0;
    c_dk.evade = 0;
    c_dk.arcaim = 0;
    c_dk.arcon = 0;
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
            c_dk.f0 = now();
            CO_CALL(c_dk, fight_if_needed(&c_dk.fought));
            if (c_dk.fought && g_curious) c_dk.end += now() - c_dk.f0;      // (curious: as to_station)
            if (c_dk.fought) continue;
        }
        {
            ship_t *const sh = g_see_buf;
            int n = see_ships(sh);
            if (!find_station(sh, n)) {
                c_dk.final = 0;
                goto need_station;
            }
        }
        if (dock_step()) {      // (damping on first)
            CO_CALL(c_dk, set_damping(1));
            continue;
        }
        continue;
    need_station:
        c_dk.left = (int32_t)(c_dk.end - now());
        if (c_dk.left < 250) c_dk.left = 250;
        c_dk.ff0 = g_fight_frames;
        CO_CALL(c_dk, to_station(c_dk.left / 50.0f));
        // (curious: the fights on the way in don't count against the docking's clock either, as its own and to_station's
        // don't: a run in from a jump with eight show fights on the way timed the docking out and reloaded the last
        // save, 46-81 times in 100 game hours, 28-40 of the hours replayed)
        if (g_curious) c_dk.end += (uint32_t) (g_fight_frames - c_dk.ff0);
    }
    CO_END(c_dk);
}

// curious mode's steering numbers from the environment (run_sim and the lab): the show's roll deadband (CROLLDB), the
// stick smoothing near the aim (STKK, STKNEAR), smooth planning's weights (PSW, PSREV, PSKEEP)
void curious_env(void) {
#define E(v, name) if (getenv(name)) v = atof(getenv(name))
    { extern void cfly_env(void); cfly_env(); }     // (cfly.c's numbers: CF_*)
    if (getenv("DOCKDASH")) g_dock_dash = atoi(getenv("DOCKDASH"));
#if !APP_LOADED
    E(g_dd_k, "DDK"); E(g_dd_m, "DDM");
#endif
#undef E
}

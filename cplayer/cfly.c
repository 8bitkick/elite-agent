// Curious mode's flying in a fight (round 10; the user: "rip up the motion planning and execution for curious and
// start again ... think about the end effect we are looking for, be the director of a scene").
//
// In the front view our nose is the camera, so the stick is the camera operator. Three ways of flying, by where the
// target is in the picture:
//   WHIP   not in the picture: nothing to watch, so come round as fast as the ship turns (full roll into the pull
//          plane, full pitch): a quick swing through empty space, not a slow search
//   FRAME  in the picture, not yet near the sights: a smooth pan. Pitch eases on and off (damped on its own
//          change, a step at most a game loop, never reversed for a small error); a roll only when the ship's out
//          to the side, and then one decisive roll at a steady rate, finished before the next
//   TRACK  near the sights (within track_r hit radii): the close-up. Pitch only, damped, steady on it; a roll only
//          if it's slipping out sideways
// The stick changes once a game loop (Elite turns the ship once a loop: anything between is jitter). Efficient
// mode flies on pilot.c's steer() and its planner, as it did.
#include <math.h>
#include <stdlib.h>
#include "pilot.h"
#include "drv.h"
#include "state.h"
#include "keyq.h"

// the numbers (CF_* in the environment: cfly_env; the Presto's app: constants in the flash, its RAM is 72 KB)
#if APP_LOADED
#define CFP const
#else
#define CFP
#endif
CFP double g_cf_whip_k = 1.0;       // WHIP: loops to close the roll angle in (small: hard)
CFP double g_cf_pan_k = 2.0;        // FRAME/TRACK: loops to close the pitch angle in (the pan's speed)
CFP double g_cf_damp = 2.0;         // ... plus this many loops' worth of the angle's own change (closing: less; running: more)
CFP int g_cf_slew = 2;              // FRAME: the pitch rate's change at most this a loop (BETA 0-8)
CFP double g_cf_roll_on = 0.25;     // FRAME: a roll starts with the ship this far out sideways (of the picture's half width)
CFP double g_cf_roll_off = 0.08;    // ... and stops once it's this far in (or in the pull plane within track_r)
CFP int g_cf_roll_rate = 20;        // ... at this ALPHA (of 31): steady, decisive
CFP double g_cf_track_r = 3;        // TRACK: within this many hit radii of the aim
CFP double g_cf_hold_r = 0.3;       // ... a pitch move runs until it's within this many hit radii up or down
CFP double g_cf_start_r = 1.5;      // ... and starts only this many hit radii off
CFP int g_cf_dwell = 3;             // ... a pitch rate held this many game loops before it changes
CFP double g_cf_appr_px = 12;       // APPROACH: a target drawn under this is a speck: coarse moves
CFP int g_cf_appr_every = 8;        // ... decided every this many game loops, held between
int g_cf_plan_every = 8;        // (combat.c: off the picture, the planner re-plans every this many loops in the show)
CFP double g_cf_slip_r = 1.5;       // ... a roll only if it's this many hit radii out sideways
CFP double g_cf_up_hyst = 30;       // degrees: the pull plane (above or below) kept until the target's this far past 90

static struct { int mc, up, rolling, pitching, dwell, a, b, tx, ty, mode, appr_n; double ev; } C = {-1, 1};

static int sat(double v, int m) { int i = (int) lround(v); return i > m ? m : i < -m ? -m : i; }
static int sgn(double v) { return (v > 0) - (v < 0); }

// the stick for these rates (pilot.c's mapping: ALPHA 0-31, BETA 0-8, opposite sign to the stick's y)
static int jx_for(int a) { int m = abs(a), d = m == 0 ? 0 : m <= 3 ? m * 8 + 4 : (m * 4 + 2 > 32 ? m * 4 + 2 : 32); if (d > 127) d = 127; return 128 + (a > 0 ? d : -d); }
static int jy_for(int b) { int m = abs(b), e = m == 0 ? 0 : m <= 2 ? 36 : (m * 16 + 6 < 127 ? m * 16 + 6 : 127); return 128 + (b > 0 ? -e : e); }

int g_cf_mode;          // (for the trace: 0 whip, 1 frame, 2 track)
int g_cf_b;             // (and the pitch rate it asked for)

// v: the aim point in the view (x right, y up, z ahead; mirrored by the caller for the rear view); jx, jy the stick
// as it is (in the same frame); tol, the target's hit radius as an angle. Sets g_want_jx/jy and the keys towards
// them; returns the angle off the nose (deg), as steer() does
double cfly_steer(vec_t v, int jx, int jy, double tol, keyset_t *keys) {
    double r = hypot(v.x, v.y), off = atan2(r, v.z), clock = atan2(v.x, v.y);
    if (tol <= 0) tol = 0.01;
    if (mcnt() != C.mc) {           // (once a game loop)
        C.mc = mcnt();
        double h = g_cf_up_hyst * M_PI / 180;
        if (C.up && fabs(clock) > M_PI / 2 + h) C.up = 0;
        else if (!C.up && fabs(clock) < M_PI / 2 - h) C.up = 1;
        double er = C.up ? clock : clock - copysign(M_PI, clock);       // the roll to put it in the pull plane
        double ev = NAN;                                                // (the pitch error, for next loop's damping)
        int in_pic = v.z > 0 && fabs(v.x) < v.z / 2 && fabs(v.y) < v.z * 0.375;
        int pull = C.up ? 1 : -1, a = 0, b;
        // APPROACH: in the picture but only a speck (drawn under cf_appr_px, ~256 x the hit radius angle): nothing to
        // watch but the stars, so no fine work (the user: "moments with no big ship in focus, no pursuit... pure
        // judder"; 2/3 of fight time, 213 pitch changes a minute): coarse moves decided every cf_appr_every loops, held
        C.appr_n++;
        if (in_pic && 256 * tol < g_cf_appr_px) {
            g_cf_mode = 3;
            if (C.appr_n >= g_cf_appr_every || C.mode != 3) {
                C.appr_n = 0;
                double ey = atan2(v.y, v.z), ex = atan2(v.x, v.z), side = fabs(v.x) / (v.z / 2), d = fabs(ey) * 180 / M_PI;
                a = side > 0.2 ? (ey >= 0 ? sgn(-ex) : sgn(ex)) * 16 : 0;
                b = d > 6 ? sgn(ey) * 8 : d > 1.5 ? sgn(ey) * 4 : 0;
                C.a = a, C.b = b;
            }
            a = C.a, b = C.b;
            C.mode = 3, C.rolling = C.pitching = 0, C.ev = NAN;
            C.tx = jx_for(a), C.ty = jy_for(b), g_cf_b = b;
            goto out;
        }
        C.mode = in_pic ? 1 : 0;
        if (!in_pic) {
            // WHIP: roll it into the pull plane hard, pitch as it comes round
            g_cf_mode = 0;
            a = sat(-er * 256 / g_cf_whip_k, 31);
            b = cos(er) > 0.3 ? pull * 8 : cos(er) > 0 ? pull * 4 : 0;
            C.rolling = 0;
        } else {
            int track = off < g_cf_track_r * tol;
            g_cf_mode = track ? 2 : 1;
            // in the picture: the pitch on its height above or below the nose, the roll on how far out to the side it
            // is (each its own signed angle: no pull plane to flip between)
            double ey = atan2(v.y, v.z), ex = atan2(v.x, v.z);
            double side = fabs(v.x) / (v.z / 2);                       // out to the side, of the half width
            // the roll: one decisive roll at a time, the way that brings it in (above: towards it; below: away)
            int dir = ey >= 0 ? sgn(-ex) : sgn(ex);
            if (track) {
                if (!C.rolling && fabs(ex) > g_cf_slip_r * tol) C.rolling = dir;
                if (C.rolling && fabs(ex) < 0.5 * tol) C.rolling = 0;
            } else {
                if (!C.rolling && side > g_cf_roll_on && fabs(ex) > g_cf_slip_r * tol) C.rolling = dir;
                if (C.rolling && (side < g_cf_roll_off || fabs(ex) < tol)) C.rolling = 0;
            }
            a = C.rolling ? C.rolling * (track ? g_cf_roll_rate / 2 : g_cf_roll_rate) : 0;
            // the pitch: eased on, damped on its own change, never reversed for a small error
            double dey = C.ev == C.ev ? ey - C.ev : 0;                  // (its change since last loop, rad)
            double want = (ey + g_cf_damp * dey) * 256 / g_cf_pan_k;      // (its change added: it closes or runs, and the rate meets it)
            // (no nudging: the user, on the Presto, "up down judder": the pitch went on and off at its smallest rate, the
            // nose nudged, stopped and nudged. A move starts only clearly off (cf_start hit radii), then runs until it's
            // on (cf_hold): one deliberate move, not a stutter)
            if (!C.pitching && fabs(ey) > g_cf_start_r * tol) C.pitching = 1;
            // (and stops only on it and not drifting: a ship crossing steadily is followed at its own rate, not caught
            // up with in stops and starts)
            if (C.pitching && fabs(ey) < g_cf_hold_r * tol && fabs(dey) * 256 < 0.5) C.pitching = 0;
            if (!C.pitching) want = 0;
            int bw = sat(want, 8);
            if (bw == 0 && C.pitching) bw = sgn(ey);                    // (running: at least its smallest rate, to finish)
            if (bw && C.b && sgn(bw) != sgn(C.b) && fabs(ey) < 2 * tol) bw = 0;   // (through zero, not a flip)
            if (bw > C.b + g_cf_slew) bw = C.b + g_cf_slew;
            else if (bw < C.b - g_cf_slew) bw = C.b - g_cf_slew;
            if (fabs(ey) > 20 * M_PI / 180) bw = sgn(ey) * 8;           // (far above or below: break out, full)
            // (a change held cf_dwell loops at least, but for a break out or a stop on it)
            if (bw != C.b && C.dwell < g_cf_dwell && fabs(ey) <= 20 * M_PI / 180 && C.pitching) bw = C.b;
            if (bw != C.b) C.dwell = 0; else C.dwell++;
            b = bw;
            ev = ey;
        }
        C.ev = ev;
        C.a = a, C.b = b, g_cf_b = b;
        C.tx = jx_for(a), C.ty = jy_for(b);
    }
out:
    extern int g_want_jx, g_want_jy;
    g_want_jx = C.tx, g_want_jy = C.ty;
    // (the keyboard's way there, as steer() does it)
    if (C.tx > jx + 3) ks_add(keys, K_ROLL_LEFT);
    else if (C.tx < jx - 3) ks_add(keys, K_ROLL_RIGHT);
    if (C.ty > jy + 6) ks_add(keys, K_DIVE);
    else if (C.ty < jy - 6) ks_add(keys, K_CLIMB);
    return off * 180 / M_PI;
}

// a new target or a new fight: start fresh (the pitch's last change means nothing for another ship)
void cfly_reset(void) { C.mc = -1, C.rolling = C.pitching = C.dwell = 0, C.a = C.b = 0, C.ev = NAN; }


// the lead (curious; the user: "given curious knows the exact velocity and arc of the enemy I am surprised our
// pursuits are so shitty ... we want the effect of an experienced pilot who anticipates the enemy's moves"): its
// path flown forward along its nose at 1.5 x its speed, we along ours; aimed where it'll be when our nose gets there
// (the angle to that point at our pitch rate, BETA 8 a loop at cf_turn of that on average, worked out twice), capped
// at cf_lead_max loops. (Tune 300: a cap of 24 loops 3.9 big-ship seconds a fight minute, 8: 3.5, 4.5: 4.3, 3 and 2:
// 3.8: lead it a little and close. Its turn, from its pitch and roll counters, tried both ways round: 3.4 and 3.7)
CFP double g_cf_turn = 0.6, g_cf_lead_max = 4.5, g_cf_lead_d = 400;
vec_t cfly_predict(const ship_t *sh, double loops) {
    double x = sh->x, y = sh->y, z = sh->z, sp = 1.5 * sh->speed;
    return (vec_t){x + sh->nosev[0] * sp * loops, y + sh->nosev[1] * sp * loops, z + (sh->nosev[2] * sp - speed()) * loops};
}
double cfly_lead(const ship_t *sh, vec_t vel) {
    (void) vel;
    double k = 2;
    for (int i = 0; i < 2; i++) {
        vec_t p = cfly_predict(sh, k);
        double ang = atan2(hypot(p.x, p.y), p.z);             // (rad; behind us up to pi)
        k = 2 + ang / (g_cf_turn * 8.0 / 256);
        double cap = fmin(g_cf_lead_max, 4.5 + sqrt((double) sh->x * sh->x + (double) sh->y * sh->y + (double) sh->z * sh->z) / g_cf_lead_d);
        if (k > cap) k = cap;
    }
    return k;
}

#if !APP_LOADED
void cfly_env(void) {
#define E(v, name) if (getenv(name)) v = atof(getenv(name))
    E(g_cf_whip_k, "CF_WHIP"); E(g_cf_pan_k, "CF_PAN"); E(g_cf_damp, "CF_DAMP"); E(g_cf_slew, "CF_SLEW");
    E(g_cf_roll_on, "CF_ROLLON"); E(g_cf_roll_off, "CF_ROLLOFF"); E(g_cf_roll_rate, "CF_ROLLRATE");
    E(g_cf_start_r, "CF_START"); E(g_cf_dwell, "CF_DWELL"); E(g_cf_appr_px, "CF_APPRPX"); E(g_cf_appr_every, "CF_APPREVERY");
    if (getenv("CF_PLANEVERY")) g_cf_plan_every = atoi(getenv("CF_PLANEVERY"));
    E(g_cf_track_r, "CF_TRACK"); E(g_cf_hold_r, "CF_HOLD"); E(g_cf_slip_r, "CF_SLIP"); E(g_cf_up_hyst, "CF_UPHYST");
    E(g_cf_turn, "CF_TURN"); E(g_cf_lead_max, "CF_LEADMAX"); E(g_cf_lead_d, "CF_LEADD");
#undef E
}
#else
void cfly_env(void) {}
#endif

// The show meter (lab only): what a viewer of a fight sees, from RAM each frame, for the curious mode's
// show score (docs/fighting-showcase.md; tools/show.py turns the SHOW line into the score).
//
// On screen: Elite's projection (PROJ: screen_x = 128 + 256 x/z, screen_y = 96 - 256 y/z on the 256 x 192
// space view), in the view on screen (PLUT: rear is (-x, y, -z)), so a ship is in view when z > 0 and
// |x| < z/2, |y| < 3z/8 (plus its radius: half in view counts). Its size on screen is 256 r / z pixels,
// r = sqrt(targetable area) (a Cobra III at 1000 is ~25 px; at 9000, where fights start, ~3 px: a dot).
// A ship counts as seen from 4 px.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "state.h"
#include "ships.h"
#include "drv.h"
extern int g_show_spent;

#define SEEN_PX 4.0
#define BIG_PX 48.0
#define CLOSE_KILL_PX 20.0
#define AIR_RUN 100            // frames (2 s): a gap with nothing seen longer than this is dead air
#define NEAR_IN 800            // a close pass: in under this, out again past NEAR_OUT
#define NEAR_OUT 1500
#define REACH 20000            // enemies further than this aren't in the fight

enum { T_FRONT, T_REAR, T_MSL_LAUNCH, T_MSL_KILL, T_ECM, T_BOMB, T_SHOOTDOWN, T_PASS, T_LOOP, T_SIDE, T_REARVIEW, T_N };
static const char *TECH[T_N] = {"front", "rear", "missile", "mslkill", "ecm", "bomb", "shootdown", "pass", "loop", "side", "rearview"};

static struct {
    long frames, seen, air, run, air0;
    double scr, close;
    int kills, ckills, tech[T_N], passes;
    int thg_kills; long thg_last, tg20;
    long big, fought;            // (frames with our target drawn 24 px+; of them centred (within 4.5 deg, ~20 px) or under fire)      // (Thargons killed, the frame of the last; frames a Thargoid drawn 20 px+)
    int ptype[NOSH + 2], palive[NOSH + 2], pours[NOSH + 2];
    double px[NOSH + 2], py[NOSH + 2], pz[NOSH + 2], ppx[NOSH + 2];
    int pon[NOSH + 2];
    uint8_t near[NOSH + 2];
    uint32_t last_seen[NOSH + 2];
    int nomsl, bomb, ecmp, last_mc;
    uint32_t bomb_at, ecm_at;
    double pitch;                  // pitch summed (deg) in one direction since the last reversal
    int alive_last, start_n;
    uint32_t f0;
    long vseen[4], side_close, rear_big, showfr;
    double tmax[64];
    long air_dot, air_off, air_far;  // frames with nothing seen: one in the picture but a dot; one within 4000 out of it; all further               // each type's biggest on screen (px), alive
    int mode_sw, pmode, dmode;
    int switches, pview;
    // motion on screen, once a main loop (positions only change then): each seen enemy's screen position and
    // size against the last loop's, in the same view; our laser firing (GNTMP rising); an explosion in view
    double isx[NOSH + 2], isy[NOSH + 2], ipx[NOSH + 2];
    int itype[NOSH + 2], iview, imc, lively, gntmp;
    long lively_fr, static_fr, srun, static_air;
    long s20, s40;                 // frames with an enemy drawn 20 / 40 px or more (the user's measure: how big, how long)
    double sint;                   // the largest enemy's drawn size, summed over frames (capped at 100 px)
    double bigv;                   // big_value() of it summed over frames: how big, for how long (the user's measure, round 8)
    long spd_rev; int spd_last, spd_dir;   // our speed's changes of direction (speeding up, then slowing): pulsing
    long spd_sum, spd_fast;        // speed summed over frames, and frames at 30 or more: a high speed chase
    long stk_rev; int stk_lx, stk_ly, stk_dx, stk_dy;   // the stick's changes of direction (either axis): judder
    char kd[480];                  // each kill: distance, and how (f front, r rear, m missile, b bomb)
} M;

void showm_begin(void) {
    memset(&M, 0, sizeof M);
    M.nomsl = R(A_NOMSL), M.bomb = R(A_BOMB), M.ecmp = R(A_ECMP), M.last_mc = -1;
    M.bomb_at = M.ecm_at = 0xFFFF0000u;
    M.pview = R(A_VIEW) & 3;
    M.pmode = -2;
    M.imc = -1, M.gntmp = R(A_GNTMP);
}

static int enemy(const ship_t *s) {
    if (!ship_in_space(s) || s->type == 1 || (s->type >= 3 && s->type <= 8)) return 0;
    return (s->newb & NEWB_HOSTILE) || s->type == 29 || s->type == 30;
}
static double radius(const ship_t *s) {
    const shiptype_t *t = ship_type(s->type);
    return t && t->area ? sqrt((double) t->area) : 80;
}
// in the view on screen: its size in pixels (0 if not in view), and where (PROJ: 128 + 256 x/z, 96 - 256 y/z)
static double on_screen_at(const ship_t *s, double *sx, double *sy) {
    double x = s->x, y = s->y, z = s->z;
    if (R(A_VIEW) == 1) x = -x, z = -z;
    else if (R(A_VIEW) == 2) { double t = x; x = z, z = -t; }
    else if (R(A_VIEW) == 3) { double t = x; x = -z, z = t; }
    double r = radius(s);
    if (z <= 0 || fabs(x) > z / 2 + r || fabs(y) > z * 0.375 + r) return 0;
    if (sx) *sx = 128 + 256 * x / z, *sy = 96 - 256 * y / z;
    return 256 * r / z;
}

// how much a ship drawn this big is worth on screen, a frame (the user: "64px better than 24. too close (not possible to
// fit in screen) might be worse"): nothing for a speck under 16 px, up to 1 at 64 px (a quarter of the 256 px view),
// 1 to 160 px, down to nothing at 256 px, where it no longer fits
static double big_value(double px) {
    if (px < 16) return 0;
    if (px < 64) return (px - 16) / 48;
    if (px <= 160) return 1;
    return px < 256 ? (256 - px) / 96 : 0;
}
static double on_screen(const ship_t *s) { return on_screen_at(s, NULL, NULL); }

void showm_frame(uint32_t frame) {
    if (R(A_QQ11) != 0) return;          // (not a space view)
    static ship_t sh[NOSH];
    int n = ships_read(IO->ram, sh), alive = 0, non = 0, dot = 0, near4 = 0;
    double big = 0;
    int nomsl = R(A_NOMSL), bomb = R(A_BOMB), ecmp = R(A_ECMP);
    if (nomsl < M.nomsl) M.tech[T_MSL_LAUNCH]++;
    if (bomb == 0 && M.bomb) M.tech[T_BOMB]++, M.bomb_at = frame;
    if (ecmp && !M.ecmp) M.tech[T_ECM]++, M.ecm_at = frame;
    M.nomsl = nomsl, M.bomb = bomb, M.ecmp = ecmp;
    int cur_type[NOSH + 2] = {0}, cur_alive[NOSH + 2] = {0}, cur_ours[NOSH + 2] = {0};
    double cur_px[NOSH + 2] = {0}, tg_px = 0, my_px = 0, my_off = 180;
    extern int combat_target(void), g_fire_now;
    int my_slot = combat_target();
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        int k = s->slot;
        if (k < 0 || k >= NOSH + 2) continue;
        cur_type[k] = s->type;
        cur_alive[k] = !ship_dead(s);
        cur_ours[k] = s->type == 1 && !(s->ai & 0x40);
        double d = sqrt((double) s->x * s->x + (double) s->y * s->y + (double) s->z * s->z);
        double px = on_screen(s);
        cur_px[k] = px;
        if (s->slot == my_slot && !ship_dead(s)) {
            int v = R(A_VIEW);
            double z = v == 1 ? -s->z : s->z;
            my_px = px, my_off = acos(fmax(-1.0, fmin(1.0, z / (d + 1)))) * 180 / M_PI;
        }
        if (s->type == 29 && !ship_dead(s) && px > tg_px) tg_px = px;
        // a kill: the same ship (type, slot) alive last frame and exploding now
        if (ship_dead(s) && M.ptype[k] == s->type && M.palive[k]) {
            if (s->type == 1) {
                if (M.pours[k]) ;                                   // (our own missile ending)
                else if (frame - M.ecm_at < 100 || R(A_ECMA)) ;     // (an ECM's: counted as the ECM)
                else M.tech[T_SHOOTDOWN]++;
            } else if (enemy(s) || (s->newb & NEWB_HOSTILE)) {
                M.kills++;
                if (s->type == 30) M.thg_kills++, M.thg_last = M.frames;
                int how = T_FRONT;
                if (frame - M.bomb_at < 60) how = T_BOMB;
                else {
                    for (int j = 0; j < NOSH + 2; j++) if (M.palive[j] && M.pours[j] && M.ptype[j] == 1) {
                        double dx = M.px[j] - s->x, dy = M.py[j] - s->y, dz = M.pz[j] - s->z;
                        if (sqrt(dx * dx + dy * dy + dz * dz) < 1200) how = T_MSL_KILL;
                    }
                    if (how == T_FRONT && R(A_VIEW) == 1) how = T_REAR;
                }
                if (how != T_BOMB) M.tech[how]++;        // (a bomb counts once, as used)
                if (strlen(M.kd) < sizeof M.kd - 24) {
                    char b[24];
                    // (distance, how, its size on screen then, and the mode: S show, B the budget spent, M the margin
                    // had handed over, O no show)
                    extern int g_show_mode, g_show_spent;
                    char mode = g_show_mode == 1 ? 'S' : g_show_spent ? 'B' : g_show_mode == 0 ? 'M' : 'O';
                    snprintf(b, sizeof b, "%s%.0f%c%.0f%c", M.kd[0] ? "," : "", d, "frmmeb"[how < 4 ? how : how == T_BOMB ? 5 : 4], fmax(px, M.ppx[k]), mode);
                    strcat(M.kd, b);
                }
                if (fmax(px, M.ppx[k]) >= CLOSE_KILL_PX) M.ckills++;
            }
        }
        if (ship_dead(s) || !enemy(s) || d > REACH) continue;
        alive++;
        if (px >= SEEN_PX) non++, M.last_seen[k] = frame;
        else if (px > 0) dot++;
        if (d < 4000) near4++;
        if (px > big) big = px;
        if (px > M.tmax[s->type & 63]) M.tmax[s->type & 63] = px;
        if (d < NEAR_IN && !M.near[k] && frame - M.last_seen[k] < 100) M.near[k] = 1, M.passes++;
        if (d > NEAR_OUT) M.near[k] = 0;
    }
    // motion, once a main loop: is anything moving on screen? A seen enemy moving or changing size by 2 px or more
    // a loop (~20 px a second: across the screen in ~13 s), our laser firing, or an explosion in view
    if (R(A_MCNT) != M.imc) {
        M.imc = R(A_MCNT);
        int v = R(A_VIEW) & 3, gn = R(A_GNTMP);
        double mv = 0;
        int boom = 0;
        double nsx[NOSH + 2], nsy[NOSH + 2], npx[NOSH + 2];
        int nty[NOSH + 2];
        for (int k = 0; k < NOSH + 2; k++) nty[k] = 0;
        for (int i = 0; i < n; i++) {
            const ship_t *s = &sh[i];
            int k = s->slot;
            if (k < 0 || k >= NOSH + 2 || !ship_is_ship(s)) continue;
            double sx, sy, px = on_screen_at(s, &sx, &sy);
            if (px <= 0) continue;
            if (ship_dead(s)) { if (s->type != 1) boom = 1; continue; }
            if (!enemy(s) || px < SEEN_PX) continue;
            nsx[k] = sx, nsy[k] = sy, npx[k] = px, nty[k] = s->type;
            if (M.itype[k] == s->type && M.iview == v) {
                double m = hypot(sx - M.isx[k], sy - M.isy[k]) + fabs(px - M.ipx[k]);
                if (m > mv) mv = m;
            }
        }
        M.lively = mv >= 2 || gn > M.gntmp || boom;
        for (int k = 0; k < NOSH + 2; k++) M.itype[k] = nty[k], M.isx[k] = nsx[k], M.isy[k] = nsy[k], M.ipx[k] = npx[k];
        M.iview = v, M.gntmp = gn;
    }
    for (int k = 0; k < NOSH + 2; k++) {
        M.ptype[k] = cur_type[k], M.palive[k] = cur_alive[k], M.pours[k] = cur_ours[k], M.ppx[k] = cur_px[k];
    }
    for (int i = 0; i < n; i++) if (sh[i].slot >= 0 && sh[i].slot < NOSH + 2) M.px[sh[i].slot] = sh[i].x, M.py[sh[i].slot] = sh[i].y, M.pz[sh[i].slot] = sh[i].z;
    // pitch, summed one way: a loop is 360 deg of it (BETA &42, sign-magnitude, 1/256 rad a main loop)
    if (R(A_MCNT) != M.last_mc) {
        M.last_mc = R(A_MCNT);
        int b = R(A_BETA), beta = b & 0x80 ? -(b & 0x7F) : b;
        double deg = beta / 256.0 * 180 / M_PI;
        if (deg * M.pitch < 0) M.pitch = 0;
        M.pitch += deg;
        if (fabs(M.pitch) >= 360) M.tech[T_LOOP]++, M.pitch = 0;
    }
    if (!alive) { M.alive_last = 0; return; }       // (the fight's over, or not begun: not fight time)
    if (!M.frames) M.start_n = alive, M.f0 = frame;
    M.frames++;
    M.alive_last = alive;
    {   // the mode: frames in the show, changes between show and efficient, and the mode now (at a death: then)
        extern int g_show_mode;
        M.showfr += g_show_mode == 1;
        if (M.pmode != -2 && g_show_mode != M.pmode) M.mode_sw++;
        M.pmode = M.dmode = g_show_mode;
    }
    // the views: frames with an enemy seen in each; a pass watched from the side (one 20 px or more in the left
    // or right view), or from the rear (12 px); and how often the view changed (flicking about is no show)
    int v = R(A_VIEW) & 3;
    if (non) M.vseen[v]++;
    if (v >= 2 && big >= 20) M.side_close++;
    if (v == 1 && big >= 12) M.rear_big++;
    if (v != M.pview) M.switches++, M.pview = v;
    int cap = alive < 3 ? alive : 3;
    M.scr += (double) (non < cap ? non : cap) / cap;
    M.close += fmin(1.0, big / BIG_PX);
    M.tg20 += tg_px >= 20;
    if (my_px >= 24) M.big++, M.fought += my_off < 4.5 || g_fire_now;
    g_fire_now = 0;
    M.s20 += big >= 20, M.s40 += big >= 40, M.sint += fmin(big, 100), M.bigv += big_value(big);
    { int sp = speed(), d = sp > M.spd_last ? 1 : sp < M.spd_last ? -1 : 0;
      if (d && M.spd_dir && d != M.spd_dir) M.spd_rev++;
      if (d) M.spd_dir = d;
      M.spd_last = sp; M.spd_sum += sp; M.spd_fast += sp >= 30; }
    { int x = jstx(), y = jsty(), dx = x > M.stk_lx + 2 ? 1 : x < M.stk_lx - 2 ? -1 : 0, dy = y > M.stk_ly + 2 ? 1 : y < M.stk_ly - 2 ? -1 : 0;
      if (dx && M.stk_dx && dx != M.stk_dx) M.stk_rev++;
      if (dy && M.stk_dy && dy != M.stk_dy) M.stk_rev++;
      if (dx) M.stk_dx = dx, M.stk_lx = x;
      if (dy) M.stk_dy = dy, M.stk_ly = y; }
    // static: something seen but nothing moving, in runs over 2 s (a ship held dead centre, growing slowly)
    if (non && M.lively) M.lively_fr++;
    if (non && !M.lively) M.static_fr++, M.srun++;
    else { if (M.srun > AIR_RUN) M.static_air += M.srun; M.srun = 0; }
    if (non) { if (!M.seen && M.run > AIR_RUN) M.air0 = M.run; M.seen++; if (M.run > AIR_RUN) M.air += M.run; M.run = 0; }
    else { M.run++; if (dot) M.air_dot++; else if (near4) M.air_off++; else M.air_far++; }
}

// the SHOW line: raw sums; tools/show.py makes the score
void showm_end(const char *path, int died, int start_n, int left, int dmg, int lowest) {
    if (M.run > AIR_RUN) M.air += M.run;
    if (M.srun > AIR_RUN) M.static_air += M.srun;
    M.tech[T_PASS] = M.passes;
    if (M.side_close >= 15) M.tech[T_SIDE] = 1;
    if (M.rear_big >= 25) M.tech[T_REARVIEW] = 1;
    char t[200] = "", tm[200] = "";
    for (int i = 9; i < 64; i++) if (M.tmax[i] > 0 && strlen(tm) < sizeof tm - 12) {
        char b[16];
        snprintf(b, sizeof b, "%s%d:%.0f", tm[0] ? "," : "", i, M.tmax[i]);
        strcat(tm, b);
    }
    for (int i = 0; i < T_N; i++) if (M.tech[i]) {
        char b[24];
        snprintf(b, sizeof b, "%s%s:%d", t[0] ? "," : "", TECH[i], M.tech[i]);
        strcat(t, b);
    }
    long sfb, sfs;
    char sfh[160] = "";
    { extern long g_sf_hist[16]; int k = 0; for (int i = 0; i < 16; i++) k += snprintf(sfh + k, sizeof sfh - k, "%s%ld", i ? "," : "", g_sf_hist[i]); }
    extern long g_ev_starts, g_ev_frames;
    { extern void show_strafe_count(long *, long *); show_strafe_count(&sfb, &sfs); }
    fprintf(stderr, "SHOW %s frames %ld scr %.3f close %.3f seen %.3f air %.3f kills %d ckills %d passes %d start_n %d left %d dmg %d lowest %d died %d air0 %.3f views %ld,%ld,%ld,%ld switches %d showfr %ld modesw %d mode %d spent %d s20 %ld s40 %ld sint %.0f bigv %.0f spdrev %ld spdsum %ld spdfast %ld stkrev %ld airsplit %ld,%ld,%ld lively %ld static %ld staticair %ld strafe %ld,%ld thg %d,%ld,%ld ev %ld,%ld fought %ld,%ld sfh %s tmax %s kd %s tech %s\n",
            path, M.frames, M.frames ? M.scr / M.frames : 0, M.frames ? M.close / M.frames : 0, M.frames ? (double) M.seen / M.frames : 0,
            M.frames ? (double) M.air / M.frames : 0, M.kills, M.ckills, M.passes, start_n, left, dmg, lowest, died, M.frames ? (double) (M.seen ? M.air0 : M.air) / M.frames : 0, M.vseen[0], M.vseen[1], M.vseen[2], M.vseen[3], M.switches, M.showfr, M.mode_sw, M.dmode, g_show_spent, M.s20, M.s40, M.sint, M.bigv, M.spd_rev, M.spd_sum, M.spd_fast, M.stk_rev, M.air_dot, M.air_off, M.air_far, M.lively_fr, M.static_fr, M.static_air, sfb, sfs, M.thg_kills, M.thg_last, M.tg20, g_ev_starts, g_ev_frames, M.big, M.fought, sfh, tm[0] ? tm : "-", M.kd[0] ? M.kd : "-", t[0] ? t : "-");
}

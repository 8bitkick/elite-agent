// Fair mode (fair.h, docs/FAIR.md): what a human player could know of the game.
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "fair.h"
#include "drv.h"
#include "keys.h"

int g_fair;
int g_fair_misjump = 1;
int g_fair_stick_rate = 32;
double g_fair_noise = 0.04;
int g_fair_key_cap = 4, g_fair_key_gap = 4;
long g_fair_chk[10][4];
int g_fair_check;            // (the lab: count perception against the truth)
double g_fair_q = 1;         // the tracks' process noise (variance of a ship's own change of motion a loop, units^2)
double g_fair_lv = 1, g_fair_rv = 1, g_fair_sv = 1;     // measurement errors as modelled, x these (quantisation is correlated: a dot sits on a pixel)
int g_fair_stay = 3000;       // (combat.c) engaged, stay in the fight this much further out than it starts
double g_fair_sdv = 0;        // ... and a ship seen only on the scanner
double g_fair_dv = 1;         // a dot's estimated motion, scaled (it's poorly seen)
double g_fair_qs = 0.5;       // how much a ship's speed can change a loop (variance)
int g_fair_spd = 3;          // a drawn ship's speed: 3 filtered along its nose; 0 from the track's motion; 1, 2 (tests) the blueprint's top speed, half way
int g_fair_cyan = 1;          // an unknown cyan dot out of the safe zone is taken for a pirate even before it comes at us (a hunter goes to look)
int g_fair_look = 0;          // in the safe zone, go and look at an unknown dot (no shooting until it's seen)
int g_fair_sz = 1;            // the safe zone and police rules (0: as first written, for the record of what went wrong)
#define SZ() (g_fair_sz && R(A_SSPR))
int g_fair_oracle;           // (diagnosis only, never in play: parts of the truth, to see what each is worth)
// (tests: FAIRP=name=value,... in run_sim and the lab)
int fair_set_param(const char *name, double v) {
#define P(n, var) if (!strcmp(name, n)) { var = (__typeof__(var)) v; return 1; }
    P("q", g_fair_q) P("rate", g_fair_stick_rate)
    P("noise", g_fair_noise) P("cap", g_fair_key_cap) P("gap", g_fair_key_gap) P("mj", g_fair_misjump) P("oracle", g_fair_oracle) P("spd", g_fair_spd) P("qs", g_fair_qs) P("lv", g_fair_lv) P("rv", g_fair_rv) P("sv", g_fair_sv) P("dv", g_fair_dv) P("sdv", g_fair_sdv) P("stay", g_fair_stay) P("sz", g_fair_sz) P("cyan", g_fair_cyan) P("look", g_fair_look)
#undef P
    return 0;
}

// our own controls, per frame (we know what our hands did): the fire key
static uint8_t s_ofire[64];
void fair_note_out(int jx, int jy, int fire) { (void) jx, (void) jy; s_ofire[IO->frame & 63] = (uint8_t) fire; }

// ---- The ships as seen. Each ship is followed from frame to frame (as an eye follows it: the
// game's own positions are used only to keep the same ship the same; what the player is given is
// measured from the screen and the scanner and filtered)
#define NT 24
#define MEM_FRAMES 150          // a ship out of sight is remembered (where it should be now) this long
typedef struct {
    int used, slot, rtype;
    double tp[3];               // (continuity only: where the game had it last frame)
    int type, cls, ident;       // what it is: its type once drawn close enough (24 as 11, 26 as 12), else by colour
    double p[3], v[3], P[6][6]; // estimate: position, its own motion a game loop, their covariance
    int init, owed, pslot, moved;
    double u[3];                // on screen: its direction as drawn
    // its speed along its nose (a game loop's travel), while it's drawn: from how far it has moved since the last
    // drawing, the last drawn point carried on by our own motion alone (q, qk loops ago)
    double q[3], spd, Ps;
    int qk, has_q, spd_init;
    uint32_t last_meas;
    int wire, onscr;            // drawn as a wireframe (type and heading show) / on the screen at all, this frame
    float nose[3], roof[3], side[3];
    int cue, cue_fire, ours, face_n, prov;
    double dealt, regen;
    uint32_t dealt_at;
    int dead_seen;
} trk_t;
static trk_t s_t[NT];
static int s_last_mc = -1;
static int s_place;             // where we were (galaxy, x, y, witchspace): a new place empties the bubble
static ship_t s_out[NOSH];
static int s_nout;
static double s_tally_est, s_tally_saved = -1;
static int s_tally_hi = -1;
static uint32_t s_msl_fired_at;
static int s_msl_fired;
static int s_lock_slot = -1, s_last_mstg = 0xFF;
static int s_qf = -1, s_qa, s_qe;

enum { CL_NONE, CL_YELLOW, CL_GREEN, CL_BLUE, CL_RED, CL_CYAN, CL_MAG, CL_WHITE };
static const uint8_t SCACOL[34] = {0, CL_YELLOW, CL_GREEN, CL_BLUE, CL_BLUE, CL_BLUE, CL_RED, CL_RED, CL_RED, CL_CYAN, CL_CYAN, CL_CYAN,
    CL_MAG, CL_MAG, CL_MAG, CL_RED, CL_CYAN, CL_CYAN, CL_CYAN, CL_CYAN, CL_CYAN, CL_CYAN, CL_CYAN, CL_BLUE, CL_CYAN, CL_CYAN, CL_MAG,
    CL_CYAN, CL_CYAN, CL_WHITE, CL_CYAN, CL_CYAN, CL_NONE, CL_GREEN};     // scacol: the scanner's colours (Cougar: none)
// in the view, a far ship is a dot in its ship colour (shpcol): yellow missiles, red rocks, white Thargoids
// and Thargons, everything else cyan
static int dot_class(int t) { return t == 1 ? CL_YELLOW : (t >= 6 && t <= 8) || t == 15 ? CL_RED : t == 29 || t == 30 ? CL_WHITE : CL_CYAN; }
// beyond this z_hi a ship is drawn as a dot (blueprint byte 13, LL9 part 2)
static const uint8_t VISDIST[34] = {0, 14, 120, 8, 5, 12, 20, 50, 8, 22, 16, 50, 40, 40, 36, 50, 23, 20, 25, 20, 20, 18, 19, 19, 50,
    40, 40, 40, 40, 55, 20, 45, 34, 125};
static int same_look(int t) { return t == 24 ? 11 : t == 26 ? 12 : t; }     // (the pirate Cobra and Python are the traders' models)
static int stand_in(int cls) {           // a type for a ship known only by its colour
    switch (cls) {
    case CL_YELLOW: return 1;
    case CL_BLUE: return 5;
    case CL_RED: return 7;
    case CL_MAG: return 12;
    case CL_WHITE: return 29;
    default: return witchspace() ? 30 : 17;
    }
}
// hostile on sight: pirates' and Thargoids' models, the Fer-de-lance (spawned as a lone hostile), the Constrictor
static int hostile_type(int t) { return (t >= 17 && t <= 23) || t == 25 || (t >= 27 && t <= 31); }

static void rot(double v[3], double a, double b) {          // our roll and pitch for one game loop (MVEIT)
    double K2 = v[1] - a * v[0];
    v[2] = v[2] + b * K2;
    v[1] = K2 - b * v[2];
    v[0] = v[0] + a * v[1];
}
static void to_view(int view, const double f[3], double o[3]) {
    switch (view) {
    case 1: o[0] = -f[0], o[1] = f[1], o[2] = -f[2]; break;
    case 2: o[0] = f[2], o[1] = f[1], o[2] = -f[0]; break;
    case 3: o[0] = -f[2], o[1] = f[1], o[2] = f[0]; break;
    default: o[0] = f[0], o[1] = f[1], o[2] = f[2];
    }
}
static void from_view(int view, const double o[3], double f[3]) {
    switch (view) {
    case 1: f[0] = -o[0], f[1] = o[1], f[2] = -o[2]; break;
    case 2: f[2] = o[0], f[1] = o[1], f[0] = -o[2]; break;
    case 3: f[2] = -o[0], f[1] = o[1], f[0] = o[2]; break;
    default: f[0] = o[0], f[1] = o[1], f[2] = o[2];
    }
}
static void var_from_view(int view, const double o[3], double f[3]) {      // (variances: axes swap, signs don't matter)
    if (view == 2 || view == 3) f[0] = o[2], f[1] = o[1], f[2] = o[0];
    else f[0] = o[0], f[1] = o[1], f[2] = o[2];
}
static double len3(const double v[3]) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// the scanner's dot and stick (SCAN): x to 2 pixels of x_hi (512 units), the stick's foot to z_hi / 4 (1024),
// its height to y_hi / 2 (512), the dot kept inside the scanner's rows 194-246. Only within 63 of every hi
static int scanner(const ship_t *s, double m[3], double var[3]) {
    long ax = labs((long) s->x), ay = labs((long) s->y), az = labs((long) s->z);
    if (!(s->flags31 & 0x10) || s->type == 32 || ax >= 16384 || ay >= 16384 || az >= 16384) return 0;
    int xs = (int) (ax >> 8) * (s->x < 0 ? -1 : 1);
    int X1 = (125 + xs) & ~1;
    m[0] = (X1 - 124.5) * 256, var[0] = 512.0 * 512 / 12 + 128 * 128;
    int zq = (int) (az >> 10);
    m[2] = (s->z < 0 ? -1 : 1) * (zq * 1024 + 512), var[2] = 1024.0 * 1024 / 12;
    int Y2 = s->z < 0 ? 220 + zq : 220 - zq;
    int yq = (int) (ay >> 9), dot = Y2 + (s->y < 0 ? yq : -yq);
    if (dot < 194 || dot > 246) {       // (clipped: only that it's well above or below)
        dot = dot < 194 ? 194 : 246;
        double seen = Y2 - dot;
        m[1] = seen * 512 + (seen > 0 ? 4000 : -4000), var[1] = 6000.0 * 6000;
    } else {
        double seen = Y2 - dot;
        m[1] = seen * 512 + (seen > 0 ? 256 : seen < 0 ? -256 : 0), var[1] = 512.0 * 512 / 12;
    }
    return 1;
}

typedef struct { int ok, scr, wire, scan, cls; double m[3], var[3], u[3], rng, rsig; } meas_t;     // (u, rng, rsig: on screen, the direction and the range)
static void measure(const ship_t *s, meas_t *o) {
    memset(o, 0, sizeof *o);
    int t = s->type;
    double sm[3], sv[3];
    o->scan = scanner(s, sm, sv);
    int space = R(A_QQ11) == 0, view = R(A_VIEW) & 3;
    double f[3] = {s->x, s->y, s->z}, w[3];
    to_view(view, f, w);
    o->cls = SCACOL[t < 34 ? t : 0];
    if (space && w[2] > 0 && w[2] < 49152 && fabs(w[0]) < w[2] * 0.5 && fabs(w[1]) < w[2] * 0.375) {
        o->scr = 1;
        o->wire = ((long) w[2] >> 8) <= VISDIST[t < 34 ? t : 0] || w[2] < 4096 || (s->flags31 & 0x20);
        if (!o->scan) o->cls = dot_class(t);
        // the direction: to the pixel (x / z x 256 across, y / z x 256 down)
        double tx = floor(w[0] / w[2] * 256) / 256 + 0.5 / 256, ty = floor(w[1] / w[2] * 256) / 256 + 0.5 / 256;
        double z, sig;
        const shiptype_t *info = ship_type(t);
        double r = info && info->area ? sqrt((double) info->area) : 50;
        double spx = r * 256 / w[2];                // how big it's drawn, in pixels
        if (o->wire) {
            double q = floor(spx + 0.5);
            if (q < 1) q = 1;
            z = r * 256 / q, sig = w[2] / (2 * spx) + 10;
        } else z = 24000, sig = 9000;               // a dot: only that it's far
        if (o->scan) {                              // (and the scanner, when it says more)
            double ds = len3(sm) / sqrt(1 + tx * tx + ty * ty);
            if (sig > 400) z = ds, sig = 400;
        }
        double mv[3] = {tx * z, ty * z, z}, nrm = sqrt(1 + tx * tx + ty * ty), uv[3] = {tx / nrm, ty / nrm, 1 / nrm};
        double vv[3] = {pow(z / 512, 2) + pow(tx * sig, 2), pow(z / 512, 2) + pow(ty * sig, 2), sig * sig};
        from_view(view, mv, o->m);
        var_from_view(view, vv, o->var);
        from_view(view, uv, o->u);
        o->rng = z * nrm, o->rsig = sig * nrm;
        o->ok = 1;
    } else if (o->scan) {
        memcpy(o->m, sm, sizeof sm), memcpy(o->var, sv, sizeof sv);
        o->ok = 1;
    }
}

// a Kalman filter on (position, own motion), in our own turning frame: each game loop everything turns by our roll
// and pitch (R) and the ship moves on by its own motion: x' = R x + R v, v' = R v (and our speed off z)
static void rot_matrix(double a, double b, double M[3][3]) {
    for (int c = 0; c < 3; c++) {
        double e[3] = {c == 0, c == 1, c == 2};
        rot(e, a, b);
        for (int r = 0; r < 3; r++) M[r][c] = e[r];
    }
}
static void kf_predict(trk_t *k, const double R3[3][3], double q) {
    double F[6][6] = {{0}}, T[6][6];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) F[i][j] = F[i][j + 3] = F[i + 3][j + 3] = R3[i][j];
    for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) { double x = 0; for (int l = 0; l < 6; l++) x += F[i][l] * k->P[l][j]; T[i][j] = x; }
    for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) { double x = 0; for (int l = 0; l < 6; l++) x += T[i][l] * F[j][l]; k->P[i][j] = x; }
    for (int i = 0; i < 3; i++) k->P[i][i] += q / 4, k->P[i][i + 3] += q / 2, k->P[i + 3][i] += q / 2, k->P[i + 3][i + 3] += q;
}
static int inv3(const double A[3][3], double B[3][3]) {
    double d = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(d) < 1e-30) return 0;
    B[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) / d, B[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / d, B[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / d;
    B[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) / d, B[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / d, B[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / d;
    B[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / d, B[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / d, B[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / d;
    return 1;
}
// a measured position m with covariance Rm
static void kf_update(trk_t *k, const double m[3], const double Rm[3][3]) {
    double S[3][3], Si[3][3], K[6][3], e[3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) S[i][j] = k->P[i][j] + Rm[i][j];
    if (!inv3(S, Si)) return;
    for (int i = 0; i < 6; i++) for (int j = 0; j < 3; j++) { double x = 0; for (int l = 0; l < 3; l++) x += k->P[i][l] * Si[l][j]; K[i][j] = x; }
    for (int i = 0; i < 3; i++) e[i] = m[i] - k->p[i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) k->p[i] += K[i][j] * e[j], k->v[i] += K[i + 3][j] * e[j];
    double N[6][6];
    for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) { double x = 0; for (int l = 0; l < 3; l++) x += K[i][l] * k->P[l][j]; N[i][j] = k->P[i][j] - x; }
    for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) k->P[i][j] = 0.5 * (N[i][j] + N[j][i]);
}
// a measurement's covariance: on the screen, small across the line of sight u (the pixel) and the range's along it;
// from the scanner, its quantisation on each axis
static void cov_los(const double u[3], double sl, double sr, double C[3][3]) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) C[i][j] = sl * sl * ((i == j) - u[i] * u[j]) + sr * sr * u[i] * u[j];
}
static void cov_diag(const double v[3], double C[3][3]) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) C[i][j] = i == j ? v[i] : 0; }

// a track carried on n game loops: our own turn and speed, its own motion
static void carry(trk_t *t, int n, double a, double b, double delta) {
    double R3[3][3];
    if (n > 0) rot_matrix(a, b, R3);
    for (int i = 0; i < n; i++) {
        rot(t->p, a, b), rot(t->v, a, b);
        for (int j = 0; j < 3; j++) t->p[j] += t->v[j];
        t->p[2] -= delta;
        kf_predict(t, R3, g_fair_q);
        if (t->has_q) rot(t->q, a, b), t->q[2] -= delta, t->qk++;
        t->Ps += g_fair_qs;
    }
    t->owed -= n;
}
// drawn again: how far it moved along its nose since it was last drawn. Across the line of sight the screen
// measures to the pixel, along it only as well as the range; weighed so
static void speed_update(trk_t *t, const double m[3], double sig_r, double vmax) {
    if (!t->spd_init) t->spd = 0.6 * 1.5 * vmax, t->Ps = pow(0.5 * 1.5 * vmax, 2), t->spd_init = 1;
    if (t->has_q && t->qk > 0 && t->qk < 8) {
        double r = len3(m) + 1, u[3] = {m[0] / r, m[1] / r, m[2] / r}, d[3], n[3] = {t->nose[0], t->nose[1], t->nose[2]};
        for (int j = 0; j < 3; j++) d[j] = m[j] - t->q[j];
        double nr = n[0] * u[0] + n[1] * u[1] + n[2] * u[2], dr = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        double nl[3], dl[3], nn = 0, dn = 0;
        for (int j = 0; j < 3; j++) nl[j] = n[j] - nr * u[j], dl[j] = d[j] - dr * u[j], nn += nl[j] * nl[j], dn += dl[j] * nl[j];
        double sl2 = pow(r / 400, 2) * 2, sr2 = sig_r * sig_r * 2 + 1, k = t->qk;
        double w = nn / sl2 + nr * nr / sr2;            // (information about the speed per unit of travel)
        if (w > 1e-12) {
            double obs = (dn / sl2 + dr * nr / sr2) / (w * k), var = 1 / (w * k * k);
            double K = t->Ps / (t->Ps + var);
            t->spd += K * (obs - t->spd), t->Ps *= 1 - K;
            if (t->spd < 0) t->spd = 0;
            if (t->spd > 1.5 * vmax + 3) t->spd = 1.5 * vmax + 3;
        }
    }
    memcpy(t->q, m, sizeof t->q), t->qk = 0, t->has_q = 1;
}

static double pts_of(int type) { const shiptype_t *t = ship_type(type); return type == 1 ? 0 : t ? t->kill_points : 0; }
static int place_now(void) { return R(A_GCNT) << 17 | R(A_QQ0) << 9 | R(A_QQ1) << 1 | (R(A_MJ) != 0); }

void fair_reset(void) {
    memset(s_t, 0, sizeof s_t);
    s_last_mc = -1, s_nout = 0, s_place = -1;
    s_tally_hi = -1, s_tally_est = 0, s_tally_saved = -1;
    s_lock_slot = -1, s_last_mstg = 0xFF, s_qf = -1, s_msl_fired = 0;
    memset(s_ofire, 0, sizeof s_ofire);
}

// a game loop's worth of our own motion, from the stick and speed the game read
static void own_ab(int jx, int jy, double *a, double *b) { *a = alpha_from_jstx(jx) / 256.0, *b = beta_from_jsty(jy) / 256.0; }

static void update_tally(void) {
    // the tally: our count of the kills we've seen, each at its type's points, kept within the 256 the "RIGHT ON
    // COMMANDER!" messages (each 256 points) say it's in. (The message comes as TALLY+1 counts up: the same thing)
    int hi = R(A_TALLY + 1);
    if (s_tally_hi < 0) s_tally_est = hi * 256.0 + R(A_TALLY), s_tally_hi = hi;     // (the start: a commander's rank and record)
    if (hi != s_tally_hi) {
        if (s_tally_est < hi * 256.0) s_tally_est = hi * 256.0;
        s_tally_hi = hi;
    }
    if (s_tally_est >= (hi + 1) * 256.0) s_tally_est = (hi + 1) * 256.0 - 0.5;
    if (s_tally_est < hi * 256.0) s_tally_est = hi * 256.0;
}
double tally_seen(void) { return (g_fair & FAIR_DASH) ? s_tally_est : tally(); }
void fair_saved(void) { s_tally_saved = s_tally_est; }
void fair_reloaded(void) { if (s_tally_saved >= 0) s_tally_est = s_tally_saved; s_tally_hi = R(A_TALLY + 1); update_tally(); }
void fair_missile_fired(void) { s_msl_fired = 1, s_msl_fired_at = IO->frame; }
int fair_lock_slot(void) { return s_lock_slot; }

static int is_junk_cls(int cls) { return cls == CL_BLUE || cls == CL_RED || cls == CL_GREEN || cls == CL_NONE; }

static void perceive_update(void) {
    const uint8_t *ram = IO->ram;
    // a new place (a jump, witchspace, a death, docking): nothing we knew is there now
    if (place_now() != s_place || R(A_QQ12)) { memset(s_t, 0, sizeof s_t); s_place = place_now(); s_last_mc = -1; s_lock_slot = -1; }
    update_tally();
    int mc = R(A_MCNT), dn = s_last_mc < 0 ? 0 : (s_last_mc - mc) & 255;
    if (dn > 8) { memset(s_t, 0, sizeof s_t); dn = 0; }
    s_last_mc = mc;
    static ship_t raw[NOSH];
    int n = ships_read(ram, raw);
    double a, b;
    own_ab(R(A_JSTX), R(A_JSTY), &a, &b);
    double delta = R(A_DELTA);
    int ws = witchspace(), view = R(A_VIEW) & 3, space = R(A_QQ11) == 0;
    // continuity: the same slot and type as last frame; or, where Elite moved the ships above a dead one down a
    // slot, the same type nearest where it was. (Each ship moves once a game loop, but a loop spans frames: some
    // ships have moved and some not yet when a frame ends. So each track counts the loops it's owed, and moves
    // on when its ship does)
    int took[NT] = {0}, rawk[NOSH];
    for (int k = 0; k < NT; k++) if (s_t[k].used) s_t[k].owed += dn, s_t[k].pslot = s_t[k].slot, s_t[k].slot = -1;
    for (int i = 0; i < NOSH; i++) rawk[i] = -1;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            const ship_t *s = &raw[i];
            if (!ship_in_space(s) || rawk[i] >= 0) continue;
            double best = pass ? 2500 : 6000;
            int bk = -1;
            for (int k = 0; k < NT; k++) {
                trk_t *t = &s_t[k];
                if (!t->used || took[k] || t->rtype != s->type || (!pass && t->pslot != i)) continue;
                double d = sqrt(pow(t->tp[0] - s->x, 2) + pow(t->tp[1] - s->y, 2) + pow(t->tp[2] - s->z, 2));
                if (d < best) best = d, bk = k;
            }
            if (bk >= 0) took[bk] = 1, rawk[i] = bk;
        }
    for (int i = 0; i < n; i++) {
        const ship_t *s = &raw[i];
        if (!ship_in_space(s) || rawk[i] >= 0) continue;
        for (int k = 0; k < NT; k++) if (!s_t[k].used) {
            memset(&s_t[k], 0, sizeof s_t[k]);
            s_t[k].used = 1, s_t[k].rtype = s->type, s_t[k].cls = CL_NONE, s_t[k].pslot = -1;
            s_t[k].tp[0] = s->x, s_t[k].tp[1] = s->y, s_t[k].tp[2] = s->z;
            // (a missile appearing just after we fired one is ours)
            if (s->type == 1 && s_msl_fired && IO->frame - s_msl_fired_at < 80) s_t[k].ours = 1, s_msl_fired = 0;
            took[k] = 1, rawk[i] = k;
            break;
        }
    }
    for (int k = 0; k < NT; k++) if (s_t[k].used && !took[k]) s_t[k].used = 0;   // (gone from the bubble)
    for (int i = 0; i < n; i++) if (rawk[i] >= 0) {
        trk_t *t = &s_t[rawk[i]];
        const ship_t *s = &raw[i];
        t->slot = i;
        t->moved = t->tp[0] != s->x || t->tp[1] != s->y || t->tp[2] != s->z;
        if (g_fair_check >= 4 && t->moved && !ship_dead(s)) {     // (diagnosis: our motion model, one loop, on the truth)
            double q[3] = {t->tp[0], t->tp[1], t->tp[2]};
            rot(q, a, b);
            for (int j = 0; j < 3; j++) q[j] += s->nosev[j] * 1.5 * s->speed;
            q[2] -= delta;
            double e = sqrt(pow(q[0] - s->x, 2) + pow(q[1] - s->y, 2) + pow(q[2] - s->z, 2));
            fprintf(stderr, "MOTION err %.1f d %.0f a %.3f b %.3f delta %.0f own %.1f\n", e, len3(q), a * 256, b * 256, delta, 1.5 * s->speed);
        }
        t->tp[0] = s->x, t->tp[1] = s->y, t->tp[2] = s->z;
    }

    // our shields as drawn: a hit shows as a bar dropping, on the side it came from
    int qf = R(A_FSH) / 16, qa = R(A_ASH) / 16, qe = R(A_ENERGY) / 4, hit_f = 0, hit_a = 0;
    if (s_qf >= 0 && dn) {
        hit_f = qf < s_qf, hit_a = qa < s_qa;
        if (qe <= s_qe - 2 && !hit_f && !hit_a) hit_f = s_qf == 0, hit_a = s_qa == 0;
    }
    if (dn || s_qf < 0) s_qf = qf, s_qa = qa, s_qe = qe;

    // our laser this loop: what our hands held then (the keys land KEYQ_DELAY frames after), the laser not too hot
    int fired = space && s_ofire[(IO->frame - 2) & 63] && R(A_GNTMP) < 242 && R(A_LASER + view);
    int lpow = R(A_LASER + view) & 0x7F;
    double per_loop = (R(A_LASER + view) & 0x80) ? lpow : lpow / 5.0;     // (a pulse laser fires one loop in five)

    // the missile indicator just locked: on the ship in our sights then
    int mstg = R(A_MSTG);
    int lock_new = mstg != 0xFF && s_last_mstg == 0xFF;
    if (mstg == 0xFF) s_lock_slot = -1;
    s_last_mstg = mstg;

    for (int k = 0; k < NT; k++) {
        trk_t *t = &s_t[k];
        if (!t->used || t->slot < 0) continue;
        const ship_t *s = &raw[t->slot];
        meas_t m;
        measure(s, &m);
        if (g_fair_check == 6 && m.ok && !m.scr) fprintf(stderr, "SCN f %u k %d moved %d m %.0f %.0f %.0f true %d %d %d\n", IO->frame, k, t->moved, m.m[0], m.m[1], m.m[2], s->x, s->y, s->z);
        t->wire = m.ok && m.wire, t->onscr = m.ok && m.scr;
        if (m.ok) {
            if (!t->init) {
                double C[3][3];
                if (m.scr) {
                    double pt[3] = {m.u[0] * m.rng, m.u[1] * m.rng, m.u[2] * m.rng};
                    memcpy(t->p, pt, sizeof t->p);
                    cov_los(m.u, m.rng / 700, m.rsig, C);
                } else memcpy(t->p, m.m, sizeof t->p), cov_diag(m.var, C);
                memset(t->v, 0, sizeof t->v);
                memset(t->P, 0, sizeof t->P);
                for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) t->P[i][j] = C[i][j]; t->P[i + 3][i + 3] = 40.0 * 40; }
                t->init = 1, t->owed = 0;
            } else {
                int steps = t->moved ? (t->owed > 1 ? t->owed : 1) : t->owed >= 2 ? t->owed : 0;
                carry(t, steps, a, b, delta);
                if (steps && m.scr) {
                    // on the screen: the direction to the pixel, the range as well as the drawing (or the scanner) says
                    double pt[3] = {m.u[0] * m.rng, m.u[1] * m.rng, m.u[2] * m.rng}, C[3][3];
                    cov_los(m.u, m.rng / 700 * g_fair_lv, m.rsig * g_fair_rv, C);
                    kf_update(t, pt, C);
                } else if (steps) {
                    double C[3][3];
                    cov_diag(m.var, C);
                    for (int j = 0; j < 3; j++) C[j][j] *= g_fair_sv;
                    if (g_fair_check == 7) fprintf(stderr, "INNOV f %u steps %d a %.0f b %.0f pred %.0f %.0f %.0f m %.0f %.0f %.0f true %d %d %d v %.1f %.1f %.1f\n", IO->frame, steps, a * 256, b * 256,
                                                   t->p[0], t->p[1], t->p[2], m.m[0], m.m[1], m.m[2], s->x, s->y, s->z, t->v[0], t->v[1], t->v[2]);
                    kf_update(t, m.m, C);
                }
            }
            t->last_meas = IO->frame;
            if (m.scr) memcpy(t->u, m.u, sizeof t->u);
            if (m.wire) {
                if (!t->ident) t->ident = 1, t->type = same_look(s->type);
                for (int j = 0; j < 3; j++) t->nose[j] = s->nosev[j], t->roof[j] = s->roofv[j], t->side[j] = s->sidev[j];
                if (m.scr && (t->moved || !t->has_q)) {
                    const shiptype_t *ty = ship_type(t->type);
                    double pt[3] = {m.u[0] * m.rng, m.u[1] * m.rng, m.u[2] * m.rng};
                    speed_update(t, pt, m.rsig, ty ? ty->max_speed : 30);
                }
            } else t->has_q = 0;
            if (m.cls != CL_NONE && (t->cls == CL_NONE || m.scan)) t->cls = m.cls;
        }
        else if (t->init && t->owed > 0) carry(t, t->owed, a, b, delta);      // (out of sight: where it should be now)
        if (!t->ident) t->type = stand_in(t->cls);
        if (!t->init) continue;
        double d = len3(t->p) + 1;
        double rel[3] = {t->v[0], t->v[1], t->v[2] - delta};
        double rr = (t->p[0] * rel[0] + t->p[1] * rel[1] + t->p[2] * rel[2]) / d;     // its range's change a loop
        // behaviour: drawn firing at us (its laser lines); nose on us and closing, near, for half a second
        if (t->wire && (s->flags31 & 0x40) && !ship_dead(s)) t->cue = t->cue_fire = 1;
        if (dn && t->wire && t->type != 1) {
            double fac = -(t->nose[0] * t->p[0] + t->nose[1] * t->p[1] + t->nose[2] * t->p[2]) / d;
            if (fac > 0.95 && rr < -2 && d < 12000) t->face_n += dn; else if (t->face_n > 0) t->face_n -= dn;
            if (t->face_n >= 12 && !SZ()) t->cue = 1;     // (not by the station: traders and police come out at us)
        }
        // our hits: in our sights while our laser fired (it turns on us: ANGRY); and the missile's lock
        if ((dn && fired) || lock_new) {
            ship_t q = *s;
            q.type = t->type, q.x = (int32_t) t->p[0], q.y = (int32_t) t->p[1], q.z = (int32_t) t->p[2];
            int in = !ship_dead(s) && in_crosshairs(&q, view);
            int pw = t->type >= 31 ? R(A_LASER + view) == 0x97 : 1;
            if (dn && fired && in && pw) {
                t->dealt += per_loop * dn * (t->type >= 31 ? 0.25 : 1), t->dealt_at = IO->frame;
                if (t->type >= 9 && (!g_fair_sz || !(t->ident && (t->type == 16 || t->type == 10))) && (t->ident || !SZ())) t->cue = 1;
            }
            if (lock_new && in) s_lock_slot = t->slot;
        }
        if (t->dealt > 0 && dn) t->regen += dn / 8.0;
        // a ship we can't yet tell (a dot of a pirate's colour; magenta only coming at us): taken for a pirate until it's
        // drawn close enough to see what it is. In the safe zone (the S on the dashboard) only to go and look (prov 2: no
        // shooting until it's seen): there the ships about are traders and police from the station, and shooting one of
        // those makes us fugitives (the first fair careers did: 1,856 Vipers and 269 Transporters shot, 310 deaths)
        if (t->ident) t->prov = 0;
        else if (!ws && d < 16954 && ((t->cls == CL_CYAN && (g_fair_cyan || rr < -4)) || (t->cls == CL_MAG && rr < -4)))
            t->prov = SZ() ? (g_fair_look ? 2 : 0) : 1;
        else if (SZ() && t->prov == 1) t->prov = g_fair_look ? 2 : 0;
        if (g_fair_check == 8 && IO->frame % 10 == 0)
            fprintf(stderr, "PT f %u k %d raw t%d d %.0f true_d %.0f | cls %d ident %d type %d wire %d scr %d prov %d cue %d face %d rr %.1f | v %.1f %.1f %.1f true %.1f %.1f %.1f newb %02x\n",
                    IO->frame, k, s->type, d, len3((double[3]){s->x, s->y, s->z}), t->cls, t->ident, t->type, t->wire, t->onscr, t->prov, t->cue, t->face_n, rr,
                    t->v[0], t->v[1], t->v[2], s->nosev[0] * 1.5 * s->speed, s->nosev[1] * 1.5 * s->speed, s->nosev[2] * 1.5 * s->speed, s->newb);
        // a kill we saw (it blew up with our hits on it): its points
        if (ship_dead(s) && !t->dead_seen) {
            t->dead_seen = 1;
            if (t->dealt > 0 && IO->frame - t->dealt_at < 200) s_tally_est += pts_of(t->type);
        }
    }
    if (hit_f || hit_a) {
        int cand = -1, nc = 0, known = 0;
        for (int k = 0; k < NT; k++) {
            trk_t *t = &s_t[k];
            if (!t->used || t->slot < 0 || !t->init || t->type == 1 || is_junk_cls(t->cls)) continue;
            if (fabs(t->p[0]) > 9000 || fabs(t->p[1]) > 9000 || fabs(t->p[2]) > 9000) continue;
            if (!((t->p[2] >= 0 && hit_f) || (t->p[2] < 0 && hit_a))) continue;
            int hostile = t->cue || ws || (t->ident && hostile_type(t->type));
            if (hostile) known++; else cand = k, nc++;
        }
        if (!known && nc == 1 && !SZ()) s_t[cand].cue = 1;
    }
    // energy bomb: every ship about but Thargoids and the mission ships is gone; ours, points each
    {
        static int last_bomb = -1;
        int bomb = R(A_BOMB);
        if (last_bomb > 0 && bomb == 0 && !R(A_QQ12))
            for (int k = 0; k < NT; k++) if (s_t[k].used && s_t[k].type != 29 && s_t[k].type < 31 && !is_junk_cls(s_t[k].cls)) s_t[k].dealt += 1, s_t[k].dealt_at = IO->frame;
        last_bomb = bomb;
    }

    // what we give the player: the planet, sun and station as they are (the view, the compass, the slot), and the
    // ships as estimated (a ship whose game loop hasn't come round yet this frame carried on to where it will be)
    s_nout = 0;
    for (int i = 0; i < n && s_nout < NOSH; i++) {
        if (!ship_in_space(&raw[i])) {
            ship_t o = raw[i];
            o.vis = VIS_TYPE | VIS_HEAD | VIS_SCREEN | VIS_NOW;
            s_out[s_nout++] = o;
        }
    }
    for (int k = 0; k < NT && s_nout < NOSH; k++) {
        trk_t *t = &s_t[k];
        if (!t->used || t->slot < 0 || !t->init || IO->frame - t->last_meas > MEM_FRAMES) continue;
        const ship_t *s = &raw[t->slot];
        double p[3] = {t->p[0], t->p[1], t->p[2]}, v[3] = {t->v[0], t->v[1], t->v[2]}, nv[3] = {t->nose[0], t->nose[1], t->nose[2]};
        if (g_fair_oracle & 1) for (int j = 0; j < 3; j++) v[j] = s->nosev[j] * 1.5 * s->speed;     // (diagnosis only: the truth)
        if (g_fair_oracle & 2) p[0] = s->x, p[1] = s->y, p[2] = s->z;
        if (t->onscr && t->last_meas == IO->frame && t->owed <= 0 && !(g_fair_oracle & 2)) {
            double r = len3(t->p);
            for (int j = 0; j < 3; j++) p[j] = t->u[j] * r;
        }
        for (int j = 0; j < t->owed; j++) {
            rot(p, a, b), rot(v, a, b), rot(nv, a, b);
            for (int c = 0; c < 3; c++) p[c] += v[c];
            p[2] -= delta;
        }
        ship_t o;
        memset(&o, 0, sizeof o);
        o.slot = t->slot, o.type = t->type, o.id = (uint8_t) (k + 1);
        o.x = (int32_t) lround(p[0]), o.y = (int32_t) lround(p[1]), o.z = (int32_t) lround(p[2]);
        double sp = len3(v);
        const shiptype_t *tyv = ship_type(t->type);
        double vmax = tyv ? tyv->max_speed : 30;
        if (t->wire) {
            for (int j = 0; j < 3; j++) o.nosev[j] = (float) nv[j], o.roofv[j] = t->roof[j], o.sidev[j] = t->side[j];
            double along = v[0] * nv[0] + v[1] * nv[1] + v[2] * nv[2];
            double spd = fmax(0.0, along) / 1.5;
            if (g_fair_spd == 3 && t->spd_init) spd = t->spd / 1.5;
            if (g_fair_spd == 1 && t->type != 1) spd = vmax;
            else if (g_fair_spd == 2 && t->type != 1) spd = 0.5 * (fmin(spd, vmax) + vmax);
            o.speed = (int) lround(spd);
        } else {
            if (sp > 1) for (int j = 0; j < 3; j++) o.nosev[j] = (float) (v[j] / sp);
            // (a dot: its motion only as far as we trust it; off the screen, the scanner's steps say next to nothing)
            double spd = sp / 1.5 * (t->type == 1 ? 1 : t->onscr ? g_fair_dv : g_fair_sdv);
            if ((g_fair_spd == 1 || g_fair_spd == 2) && t->type != 1 && sp > 1) spd = g_fair_spd == 1 ? vmax : 0.5 * (fmin(spd, vmax) + vmax);
            o.speed = (int) lround(spd);
        }
        if (g_fair_check && !ship_dead(s) && s->type != 1 && (s->newb & NEWB_HOSTILE)) {
            g_fair_chk[5][0]++, g_fair_chk[5][1] += labs((long) o.speed - s->speed), g_fair_chk[5][2] += s->speed, g_fair_chk[5][3] += (long) vmax;
            // (its motion across our line of sight, which is what aiming ahead of it needs: perceived against true)
            double r = len3((double[3]){s->x, s->y, s->z}) + 1, u3[3] = {s->x / r, s->y / r, s->z / r}, vp[3], vt[3], e2 = 0, t2 = 0;
            for (int j = 0; j < 3; j++) vp[j] = o.nosev[j] * 1.5 * o.speed, vt[j] = s->nosev[j] * 1.5 * s->speed;
            double pr = vp[0] * u3[0] + vp[1] * u3[1] + vp[2] * u3[2], tr = vt[0] * u3[0] + vt[1] * u3[1] + vt[2] * u3[2];
            for (int j = 0; j < 3; j++) e2 += pow((vp[j] - pr * u3[j]) - (vt[j] - tr * u3[j]), 2), t2 += pow(vt[j] - tr * u3[j], 2);
            int row = t->wire ? 6 : t->onscr ? 8 : 7;     // (drawn, a dot on the screen, the scanner only)
            if (g_fair_check == 5) fprintf(stderr, "VEL f %u k %d wire %d scr %d owed %d moved %d d %.0f v %.1f %.1f %.1f true %.1f %.1f %.1f P %.0f %.0f\n", IO->frame, k, t->wire, t->onscr, t->owed, t->moved, r,
                                            t->v[0], t->v[1], t->v[2], vt[0], vt[1], vt[2], t->P[0][0], t->P[3][3]);
            g_fair_chk[row][0]++, g_fair_chk[row][1] += (long) (10 * sqrt(e2)), g_fair_chk[row][2] += (long) (10 * sqrt(t2));
        }
        o.flags31 = s->flags31;
        const shiptype_t *ty = ship_type(t->type);
        double e = (ty ? ty->max_energy : 100) - t->dealt + t->regen;
        if (ty && e > ty->max_energy) e = ty->max_energy;
        o.energy = (uint8_t) (e < 1 ? 1 : e > 255 ? 255 : e);
        int hostile = ws || t->cue || t->prov || (t->ident && hostile_type(t->type)) ||
                      (t->ident && t->type == 16 && R(A_FIST) >= 50);
        // (the police, and the Transporter that counts as police: only when they fire at us, or we are fugitives)
        if (g_fair_sz && t->ident && (t->type == 16 || t->type == 10)) hostile = ws || t->cue_fire || R(A_FIST) >= 50;
        if (t->type == 1) hostile = 0, o.ai = t->ours ? 0 : 0x40;
        if (is_junk_cls(t->cls) && !t->ident) hostile = 0;
        o.newb = (uint8_t) ((hostile ? NEWB_HOSTILE : 0) | (t->ident && (t->type == 16 || t->type == 10) ? NEWB_COP : 0) | (s->newb & 0x80));
        // (diagnosis only, never in play: parts of the truth, to see what each piece of knowledge is worth)
        if (g_fair_oracle & 8) o.newb = s->newb, o.ai = s->ai;
        if (g_fair_oracle & 16) o.energy = s->energy;
        if (g_fair_oracle & 32) o.type = s->type;
        if (g_fair_oracle & 64) for (int j = 0; j < 3; j++) o.nosev[j] = s->nosev[j], o.roofv[j] = s->roofv[j], o.sidev[j] = s->sidev[j];
        if (g_fair_oracle & 512) o.speed = s->speed;
        if (g_fair_oracle & 128) o.x = s->x, o.y = s->y, o.z = s->z;
        o.vis = (uint8_t) ((t->prov == 2 ? VIS_LOOK : 0) | (t->ident ? VIS_TYPE : 0) | (t->wire ? VIS_HEAD : 0) | (t->onscr ? VIS_SCREEN : 0) | (t->last_meas == IO->frame ? VIS_NOW : 0));
        o.cls = (uint8_t) t->cls;
        s_out[s_nout++] = o;
        // (the lab's check: perception against the game's own flags, for ships it has)
        if (g_fair_check) {
            int th = s->type == 1 ? (s->ai & 0x40) != 0 : (s->newb & NEWB_HOSTILE) != 0;
            int ph = s->type == 1 ? (o.ai & 0x40) != 0 : (o.newb & NEWB_HOSTILE) != 0;
            if (!ship_dead(s) && !(s->type >= 3 && s->type <= 8)) g_fair_chk[th][ph]++;
            // (against where it really is now: the game's RAM this frame, not the late copy)
            const ship_t *u = NULL;
            static ship_t now_[NOSH];
            static uint32_t now_f = 0xFFFFFFFF;
            static int now_n;
            if (now_f != IO->frame) now_n = ships_read(IO->ram, now_), now_f = IO->frame;
            if (t->slot < now_n && now_[t->slot].type == s->type) u = &now_[t->slot];
            if (th && s->type != 1 && !ship_dead(s) && u) {
                double ud = len3((double[3]){u->x, u->y, u->z}) + 1e-9, od = len3((double[3]){o.x, o.y, o.z}) + 1e-9;
                double c = (u->x * (double) o.x + u->y * (double) o.y + u->z * (double) o.z) / (ud * od);
                double ang = acos(c > 1 ? 1 : c < -1 ? -1 : c);
                const shiptype_t *ut = ship_type(u->type);
                double tol = atan2(ut && ut->area ? sqrt((double) ut->area) : 50, ud);
                g_fair_chk[2][0]++, g_fair_chk[2][1] += labs((long) o.energy - u->energy);
                g_fair_chk[2][2] += (long) sqrt(pow(o.x - u->x, 2) + pow(o.y - u->y, 2) + pow(o.z - u->z, 2));
                g_fair_chk[2][3] += (long) ud;
                g_fair_chk[3][0]++, g_fair_chk[3][1] += (long) (ang * 180 / M_PI * 1000), g_fair_chk[3][2] += ang < tol;
                if (t->onscr) g_fair_chk[4][0]++, g_fair_chk[4][1] += (long) (ang * 180 / M_PI * 1000), g_fair_chk[4][2] += ang < tol;
                if (g_fair_check > 1 && t->onscr) fprintf(stderr, "DBG f %u slot %d type %d owed %d moved %d lastm %u ang %.2f tol %.2f o %d %d %d u %d %d %d tu %.3f %.3f %.3f view %d\n", IO->frame, t->slot, s->type, t->owed, t->moved, t->last_meas, ang * 57.3, tol * 57.3, o.x, o.y, o.z, u->x, u->y, u->z, t->u[0], t->u[1], t->u[2], R(A_VIEW));
            }
        }
    }
    // (ships the game has that we don't see: counted for the check)
    if (g_fair_check) for (int i = 0; i < n; i++) {
        const ship_t *s = &raw[i];
        if (!ship_in_space(s) || ship_dead(s) || (s->type >= 3 && s->type <= 8)) continue;
        int th = s->type == 1 ? (s->ai & 0x40) != 0 : (s->newb & NEWB_HOSTILE) != 0, seen = 0;
        for (int j = 0; j < s_nout; j++) if (s_out[j].slot == s->slot && ship_in_space(&s_out[j])) seen = 1;
        if (!seen) g_fair_chk[th][2]++;
    }
}

int see_ships(ship_t out[NOSH]) {
    if (!(g_fair & FAIR_SEE)) return ships_read(IO->ram, out);
    memcpy(out, s_out, sizeof s_out[0] * (size_t) s_nout);
    return s_nout;
}

void fair_begin_frame(void) {
    if (g_fair & (FAIR_SEE | FAIR_DASH)) perceive_update();
}

void fair_end_frame(void) {
    if (g_fair) drv_fair_pump();
}

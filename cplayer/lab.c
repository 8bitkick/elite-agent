// The fight lab: the C player's fight code from many saved fight starts, in the native emulator.
// For each snapshot (a .fight recording or a raw .snap): fight on for up to four game minutes as
// the career would (sim/eval.py's method); report points, damage, deaths, pods and time to clear.
//   lab <list of snapshot paths>        one line per fight: path pts minutes cleared_s damage died pod
// Parameters: LABP="name=value,..." overrides g_cp (combat_params_t) by name; INTERCEPT, STEER.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "keyq.h"
#include "pilot.h"
#include "state.h"
#include "ships.h"
#include "keys.h"
#include "drv.h"
#include <math.h>

typedef struct beeb_t beeb_t;
beeb_t *beeb_new(const char *roms, const char *disc_path);
void beeb_analogue(beeb_t *b, int channel, int value);
const char *beeb_emu_dir(void);
void beeb_frames(beeb_t *b, int n);
uint8_t *beeb_ram(beeb_t *b);
void beeb_key(beeb_t *b, int code, int down);
size_t beeb_state_size(void);
void beeb_load(beeb_t *b, const uint8_t *buf);
void player_lab_init(player_io_t *io);
int player_lab_frame(void);
extern const char *g_fail;
extern int g_lab_pod;
typedef struct { int e, f, a, alt, spd, n, type, z, en; double d; } lastf_t;
static lastf_t s_ring[100];
static unsigned s_rn;
static long s_expo[5];
long g_trk[8][4];
static long s_ttk[4];           // frames: slewing, tracking, in the sights, no target          // gun-frames on us: all, jousting, ahead, behind; and frames
static int s_pod0;
extern uint32_t g_lab_cleared, g_lab_t0, g_pause_frames;

static beeb_t *B;
static player_io_t io;
static void p_hold(void *c, const uint8_t *k, int n) { (void) c; keyq_hold(io.frame, k, n); }
static void p_tap(void *c, uint8_t k, int d, int u) { (void) c; keyq_tap(io.frame, k, d, u); }
static uint8_t s_down[128];
// judder: a pitch (roll) key going down the opposite way to the last one, within a second of it
static int s_pdir, s_rdir, s_prev_n, s_rrev_n;
static uint32_t s_pat, s_rat;
static void key(int code, int down) {
    beeb_key(B, code, down);
    s_down[code & 127] = (uint8_t) down;
    if (!down) return;
    int p = code == K_CLIMB ? 1 : code == K_DIVE ? -1 : 0, r = code == K_ROLL_LEFT ? 1 : code == K_ROLL_RIGHT ? -1 : 0;
    if (p) { if (s_pdir && p != s_pdir && io.frame - s_pat < 50) s_prev_n++; s_pdir = p, s_pat = io.frame; }
    if (r) { if (s_rdir && r != s_rdir && io.frame - s_rat < 50) s_rrev_n++; s_rdir = r, s_rat = io.frame; }
}

// aiming: for the target the player has chosen, frames from choosing it to the game's hit test (acquire),
// then the share of frames it stays there (hold), the nose's angle off it meanwhile, and the stick at a stop
extern int combat_target(void);
static struct { int tgt, type; uint32_t since; int acquired, locked_frames, in_frames, stop_frames; double off_sum; long acq_sum; int acq_n, acq_fail;
                int was_in, episodes, gone, sw_unacq, sw_acq, lock_losses; double lx, ly, lz; } s_aim;
static void aim_frame(void) {
    static ship_t sh[NOSH];
    int n = ships_read(io.ram, sh), t = combat_target();
    const ship_t *tg = NULL;
    for (int i = 0; i < n; i++) if (sh[i].slot == t && !ship_dead(&sh[i]) && ship_in_space(&sh[i])) tg = &sh[i];
    if (!tg || R(0x87) || g_phase != 1) {
        if (s_aim.tgt >= 0) {        // the episode ends here too: the target died (usually), or the fight paused
            int alive = 0;
            for (int i = 0; i < n; i++) {
                if (sh[i].type != s_aim.type || ship_dead(&sh[i])) continue;
                double dx = sh[i].x - s_aim.lx, dy = sh[i].y - s_aim.ly, dz = sh[i].z - s_aim.lz;
                if (sqrt(dx * dx + dy * dy + dz * dz) < 2500) alive = 1;
            }
            s_aim.episodes++;
            if (!alive) s_aim.gone++; else if (s_aim.acquired) s_aim.sw_acq++; else s_aim.sw_unacq++;
            if (!s_aim.acquired) s_aim.acq_fail++;
        }
        s_aim.tgt = -1;
        return;
    }
    // the same ship as last frame? By type and position (Elite moves ships down a slot when one dies, so
    // the slot alone can't tell)
    int same = s_aim.tgt >= 0 && tg->type == s_aim.type &&
               sqrt((tg->x - s_aim.lx) * (tg->x - s_aim.lx) + (tg->y - s_aim.ly) * (tg->y - s_aim.ly) + (tg->z - s_aim.lz) * (tg->z - s_aim.lz)) < 2500;
    if (!same) {
        if (s_aim.tgt >= 0) {        // how did the last one end: still alive (we switched) or gone
            int alive = 0;
            for (int i = 0; i < n; i++) {
                if (sh[i].type != s_aim.type || ship_dead(&sh[i])) continue;
                double dx = sh[i].x - s_aim.lx, dy = sh[i].y - s_aim.ly, dz = sh[i].z - s_aim.lz;
                if (sqrt(dx * dx + dy * dy + dz * dz) < 2500) alive = 1;
            }
            s_aim.episodes++;
            if (!alive) s_aim.gone++; else if (s_aim.acquired) s_aim.sw_acq++; else s_aim.sw_unacq++;
        }
        if (s_aim.tgt >= 0 && !s_aim.acquired) s_aim.acq_fail++;
        s_aim.tgt = tg->slot, s_aim.type = tg->type, s_aim.since = io.frame, s_aim.acquired = 0;
    }
    s_aim.tgt = tg->slot, s_aim.lx = tg->x, s_aim.ly = tg->y, s_aim.lz = tg->z;
    int view = R(A_VIEW) == 1;
    int in = in_crosshairs(tg, view);
    if (!s_aim.acquired) {
        if (in) s_aim.acquired = 1, s_aim.acq_sum += io.frame - s_aim.since, s_aim.acq_n++;
        else return;
    }
    double d = sqrt((double) tg->x * tg->x + (double) tg->y * tg->y + (double) tg->z * tg->z);
    double z = view ? -tg->z : tg->z;
    s_aim.off_sum += acos(fmax(-1.0, fmin(1.0, z / (d + 1)))) * 180 / M_PI;
    s_aim.locked_frames++;
    s_aim.in_frames += in;
    if (s_aim.was_in && !in) s_aim.lock_losses++;
    s_aim.was_in = in;
    int jx = R(A_JSTX), jy = R(A_JSTY);
    s_aim.stop_frames += jx <= 4 || jx >= 252 || jy <= 4 || jy >= 252;
}

// per-fight diagnostics, from RAM each frame (the game's own hit test, HITCH, for "in the sights")
typedef struct { int present, sights, fire_on, fire_off, hot, aimed, kills, dealt, left, start_n, ph[5]; } diag_t;
static int hostile(const ship_t *s) {
    return ship_in_space(s) && s->type > 0 && s->type < 128 && s->type != 2 && !(s->type >= 3 && s->type <= 8) && (s->newb & NEWB_HOSTILE);
}
static void diag_frame(diag_t *d, int *e_prev, uint8_t *dead_prev) {
    static ship_t sh[NOSH];
    int n = ships_read(io.ram, sh), h = 0, insight = 0, aimed = 0;
    int e_now[NOSH * 64]; uint8_t dead_now[NOSH * 64];
    memset(e_now, -1, sizeof e_now); memset(dead_now, 0, sizeof dead_now);
    for (int i = 0; i < n; i++) {
        const ship_t *q = &sh[i];
        if (!hostile(q)) continue;
        int k = q->slot * 64 + (q->type & 63);
        dead_now[k] = ship_dead(q);
        if (dead_now[k] && !dead_prev[k]) d->kills++;
        if (ship_dead(q)) continue;
        h++;
        e_now[k] = q->energy;
        if (e_prev[k] >= 0 && q->energy < e_prev[k]) d->dealt += e_prev[k] - q->energy;
        if (in_crosshairs(q, space_view() == 1 ? 1 : 0)) insight = 1;
        double dd = sqrt((double) q->x * q->x + (double) q->y * q->y + (double) q->z * q->z);
        if (dd > 1 && dd < 8000 && -(q->nosev[0] * q->x + q->nosev[1] * q->y + q->nosev[2] * q->z) / dd > 0.95) aimed++;
    }
    memcpy(e_prev, e_now, sizeof e_now); memcpy(dead_prev, dead_now, sizeof dead_now);
    if (getenv("PHTRACE") && io.frame % 50 == 0) fprintf(stderr, "%6u phase %d hostiles %d E %d F %d A %d spd %d view %d qq11 %d\n", io.frame, g_phase, h, energy(), fsh(), ash(), speed(), space_view(), R(0x81));
    if (h) d->present++, d->ph[g_phase < 5 ? g_phase : 0]++;
    g_phase = 0;
    if (insight) d->sights++;
    int firing = s_down[K_FIRE] && R(0x81) == 0;
    if (firing && insight) d->fire_on++;
    if (firing && !insight) d->fire_off++;
    if (laser_temp() > 200) d->hot++;
    d->aimed += aimed;
    d->left = h;
}
static void p_analogue(void *c, int ch, uint16_t v) { (void) c; beeb_analogue(B, ch, v); }
static void p_log(void *c, int level, const char *s) { (void) c; if (getenv("LOG") && level <= atoi(getenv("LOG"))) fprintf(stderr, "%6u %s\n", io.frame, s); }

extern int combat_set_param(const char *name, double v);
static void set_param(const char *name, double v) {
    if (combat_set_param(name, v)) return;
    fprintf(stderr, "unknown parameter %s\n", name);
    exit(1);
}

int main(int argc, char **argv) {
    char roms[256], disc[256];
    snprintf(roms, sizeof roms, "%s/roms/mos3.20", beeb_emu_dir());
    snprintf(disc, sizeof disc, "%s/discs/elite-master-flicker-free-sng47.ssd", beeb_emu_dir());
    B = beeb_new(roms, disc);
    if (!B) return 1;
    if (getenv("LABP")) {
        char *s = strdup(getenv("LABP")), *tok, *save;
        for (tok = strtok_r(s, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            char *eq = strchr(tok, '=');
            if (eq) *eq = 0, set_param(tok, atof(eq + 1));
        }
    }
    { extern int g_lab_undamped; if (getenv("UNDAMPED")) g_lab_undamped = atoi(getenv("UNDAMPED")); }
    { extern int g_brk_log; if (getenv("BRKLOG")) g_brk_log = atoi(getenv("BRKLOG")); }
    { extern int g_msl_side; if (getenv("MSLSIDE")) g_msl_side = atoi(getenv("MSLSIDE")); }
    { extern int g_ws_fight; if (getenv("WSFIGHT")) g_ws_fight = atoi(getenv("WSFIGHT")); }
    { extern int g_preload; if (getenv("PRELOAD")) g_preload = atoi(getenv("PRELOAD")); }
    { extern int g_coast_min; if (getenv("COASTMIN")) g_coast_min = atoi(getenv("COASTMIN")); }
    { extern double g_coast; if (getenv("COAST")) g_coast = atof(getenv("COAST")); }
    { extern double g_roll_db; if (getenv("ROLLDB")) g_roll_db = atof(getenv("ROLLDB")); }
    { extern double g_nose_hold; if (getenv("NOSEHOLD")) g_nose_hold = atof(getenv("NOSEHOLD")); }
    { extern double g_up_hyst; if (getenv("UPHYST")) g_up_hyst = atof(getenv("UPHYST")); }
    { extern int g_joystick; if (getenv("JOYSTICK")) g_joystick = atoi(getenv("JOYSTICK")); }
    { extern int g_lab_window; if (getenv("LABWIN")) g_lab_window = atoi(getenv("LABWIN")); }
    { extern int g_steer_log; g_steer_log = getenv("STEERLOG") != NULL; }
    size_t sz = beeb_state_size();
    uint8_t *st = malloc(sz);
    char path[512];
    FILE *list = fopen(argv[1], "r");
    while (fgets(path, sizeof path, list)) {
        path[strcspn(path, "\n")] = 0;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        uint32_t hdr[4];
        if (fread(hdr, 4, 4, f) == 4 && hdr[0] == 0x54484746) { if (fread(st, 1, sz, f) != sz) { fclose(f); continue; } }
        else { fseek(f, 0, SEEK_SET); if (fread(st, 1, sz, f) != sz) { fclose(f); continue; } }
        fclose(f);
        beeb_load(B, st);
        // (the setting a career picks while docked, before the fight: the harness sets it in the snapshot,
        // since switching it here would pause the fight already on. Elite's CAPS toggle flips the byte)
        { extern int g_lab_undamped; if (g_lab_undamped) beeb_ram(B)[A_DAMP] = 0xFF; }
        { extern int g_joystick; if (g_joystick) beeb_ram(B)[0x2C5C] = 0xFF; }     // (joystick mode, as a career would have it)
        if (getenv("NODJD")) beeb_ram(B)[0x2C57] = 0xFF;     // (test: no auto-recentre: the opposite key steps back)
        for (int c = 0; c < 128; c++) beeb_key(B, c, 0);
        memset(&io, 0, sizeof io);
        io.ram = beeb_ram(B);
        io.hold = p_hold, io.tap = p_tap, io.log = p_log, io.analogue = p_analogue;
        keyq_reset();
        player_lab_init(&io);
        s_aim.tgt = -1;
        {   // the scenario, for grouping results: where, how many, what, how strong we are, where they are
            static ship_t sh[NOSH];
            int n = ships_read(io.ram, sh), nh = 0, behind = 0, maxe = 0, tsum = 0;
            double nearest = 1e9;
            char types[160] = "";
            for (int i = 0; i < n; i++) if (hostile(&sh[i])) {
                const shiptype_t *ty = ship_type(sh[i].type);
                double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
                nh++, behind += sh[i].z < 0, tsum += ty ? ty->max_energy : 0;
                if (ty && ty->max_energy > maxe) maxe = ty->max_energy;
                if (d < nearest) nearest = d;
                char t[8]; snprintf(t, sizeof t, "%d,", sh[i].type);
                if (strlen(types) + strlen(t) < sizeof types) strcat(types, t);
            }
            fprintf(stderr, "SCN %s ws %d n %d behind %d enemy_energy %d laser %d E %d F %d A %d nearest %.0f types %s\n", path, R(A_MJ) != 0, nh, behind, tsum, laser(0),
                    energy(), fsh(), ash(), nearest, types);
        }
        s_pdir = s_rdir = s_prev_n = s_rrev_n = 0;
        double t0 = tally();
        diag_t dg = {0};
        int dmg_ph[5] = {0};
        static int e_prev[NOSH * 64]; static uint8_t dead_prev[NOSH * 64];
        memset(e_prev, -1, sizeof e_prev); memset(dead_prev, 0, sizeof dead_prev); memset(s_down, 0, sizeof s_down);
        diag_frame(&dg, e_prev, dead_prev); dg.start_n = dg.left; dg.kills = 0; dg.present = 0;
        int prev = energy() + fsh() + ash(), dmg = 0, died = 0;
        g_pause_frames = 0;
        s_pod0 = has_escape_pod();
        extern int g_lab_window;
        for (int fr = 0; fr < (g_lab_window + 20) * 50; fr++) {
            if (player_lab_frame()) { died = g_fail && !strcmp(g_fail, "died"); break; }
            if (getenv("FIRSTEP")) {   // the aiming bench: the first target only, from an identical start: frames to its
                                      // end (killed, or dropped), to the first frame in the sights, in the sights
                static int ft, f0, f_in, n_in, nfr;
                if (fr == 0) ft = -1, f0 = -1, f_in = -1, n_in = 0, nfr = 0;
                int tg = combat_target();
                static double a_off, a_clock, a_d, a_fac;
                extern int g_phase;
                if (ft < 0 && tg >= 0 && g_phase == 1) {     // (engaged: before that the target number is stale)
                    ft = tg, f0 = (int) io.frame;
                    static ship_t s0[NOSH];
                    int n0 = ships_read(io.ram, s0);
                    for (int i = 0; i < n0; i++) if (s0[i].slot == ft) {
                        int rv = R(A_VIEW) == 1;
                        double x = rv ? -s0[i].x : s0[i].x, y = s0[i].y, z = rv ? -s0[i].z : s0[i].z;
                        a_d = sqrt(x * x + y * y + z * z) + 1;
                        a_off = acos(fmax(-1.0, fmin(1.0, z / a_d))) * 180 / M_PI;
                        a_clock = atan2(x, y) * 180 / M_PI;     // 0 straight up the screen, 90 right
                        a_fac = -(s0[i].nosev[0] * s0[i].x + s0[i].nosev[1] * s0[i].y + s0[i].nosev[2] * s0[i].z) / a_d;
                    }
                }
                if (ft >= 0) {
                    static ship_t sh[NOSH];
                    int n = ships_read(io.ram, sh), alive = 0;
                    for (int i = 0; i < n; i++) if (sh[i].slot == ft && ship_in_space(&sh[i])) {
                        alive = !ship_dead(&sh[i]);
                        if (alive && in_crosshairs(&sh[i], R(A_VIEW) == 1)) { n_in++; if (f_in < 0) f_in = (int) io.frame - f0; }
                    }
                    nfr++;
                    if (getenv("SLEWLOG")) {     // every frame of the first target: where it is (deg: off the nose, clock 0 = up
                                                  // the screen, 90 right), the stick, our speed, its range and whether it faces us
                        for (int i = 0; i < n; i++) if (sh[i].slot == ft) {
                            int rv = R(A_VIEW) == 1;
                            double x = rv ? -sh[i].x : sh[i].x, y = sh[i].y, z = rv ? -sh[i].z : sh[i].z, d = sqrt(x * x + y * y + z * z) + 1;
                            fprintf(stderr, "SL %4d off %6.2f clock %5.0f | jx %4d jy %4d | spd %2d d %5.0f in %d\n", nfr, acos(fmax(-1.0, fmin(1.0, z / d))) * 180 / M_PI,
                                    atan2(x, y) * 180 / M_PI, (int) R(0xC1) - 128, (int) R(0xC2) - 128, R(A_DELTA), d, in_crosshairs(&sh[i], rv));
                        }
                    }
                    if (!alive || tg != ft || nfr > 1500) {
                        fprintf(stderr, "FE %s %s frames %d first_in %d in %d off %.0f clock %.0f d %.0f fac %.2f\n", path, !alive ? "killed" : tg != ft ? "dropped" : "timeout", nfr, f_in, n_in, a_off, a_clock, a_d, a_fac);
                        break;
                    }
                }
            }
            io.taps_done += (uint32_t) keyq_step(io.frame, key);
            beeb_frames(B, 1);
            io.frame++;
            if (getenv("TRACE") && fr > atoi(getenv("TRACE")))
                fprintf(stderr, "%d E %d F %d A %d qq12 %d qq11 %d mcnt %d delta %d lasct %d damp %d\n", fr, energy(), fsh(), ash(), R(0x87), R(0x81), R(0x84), R(0x75), R(0x0E78), R(A_DAMP));
            { extern int g_pausing; if (io.frame < g_lab_t0 || g_pausing) continue; }      // (not fighting: setting up, or paused)
            aim_frame();
            if (getenv("TRACEJSON") && io.frame % 2 == 0) {     // the fight frame by frame, for the battle viewer
                static FILE *tj;
                if (!tj) tj = fopen(getenv("TRACEJSON"), "w");
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh), tg = combat_target();
                fprintf(tj, "{\"f\":%u,\"E\":%d,\"F\":%d,\"A\":%d,\"spd\":%d,\"view\":%d,\"fire\":%d,\"jx\":%d,\"jy\":%d,\"tgt\":%d,\"ships\":[",
                        io.frame, energy(), fsh(), ash(), R(A_DELTA), R(A_VIEW), ks_has(&g_flight, K_FIRE), jstx() - 128, jsty() - 128, tg);
                for (int i = 0, first = 1; i < n; i++) {
                    const ship_t *q = &sh[i];
                    fprintf(tj, "%s{\"slot\":%d,\"type\":%d,\"x\":%d,\"y\":%d,\"z\":%d,\"n\":[%.3f,%.3f,%.3f],\"r\":[%.3f,%.3f,%.3f],\"s\":[%.3f,%.3f,%.3f],\"e\":%d,\"dead\":%d,\"hostile\":%d,\"firing\":%d}",
                            first ? "" : ",", q->slot, q->type, q->x, q->y, q->z, q->nosev[0], q->nosev[1], q->nosev[2], q->roofv[0], q->roofv[1], q->roofv[2],
                            q->sidev[0], q->sidev[1], q->sidev[2], q->energy, ship_dead(q) ? 1 : 0, (q->newb & NEWB_HOSTILE) ? 1 : 0, (q->flags31 & 0x40) ? 1 : 0);
                    first = 0;
                }
                fprintf(tj, "]}\n");
                fflush(tj);
            }
            if (getenv("EPI")) {     // a line per target taken: the situation then, and frames to the sights
                static int cur = -2, cur_type, got;
                static uint32_t since;
                static double s_off, s_side, s_d, s_rate, s_fac;
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh), tg = combat_target();
                const ship_t *t = NULL;
                for (int i = 0; i < n; i++) if (sh[i].slot == tg && ship_in_space(&sh[i]) && !ship_dead(&sh[i])) t = &sh[i];
                // (the same ship by type and place: Elite renumbers the slots above a ship that dies)
                static double lx, ly, lz;
                int same = t && t->type == cur_type && sqrt((t->x - lx) * (t->x - lx) + (t->y - ly) * (t->y - ly) + (t->z - lz) * (t->z - lz)) < 2500;
                if (t) lx = t->x, ly = t->y, lz = t->z;
                if (t && !same) {
                    if (cur >= -1 && !got) fprintf(stderr, "EPI end %d frames, never in the sights | off %.0f side %.0f d %.0f rate %.2f fac %.2f\n", (int) (io.frame - since), s_off, s_side, s_d, s_rate, s_fac);
                    cur = tg, cur_type = t->type, got = 0, since = io.frame;
                    int rear = R(A_VIEW) == 1;
                    double x = rear ? -t->x : t->x, z = rear ? -t->z : t->z, y = t->y, d = sqrt(x * x + y * y + z * z) + 1;
                    s_off = acos(fmax(-1.0, fmin(1.0, z / d))) * 180 / M_PI;
                    double clock = atan2(x, y) * 180 / M_PI;
                    s_side = fmin(fabs(clock), 180 - fabs(clock));        // roll needed to bring it above or below
                    s_d = d;
                    double vx = t->nosev[0] * 1.5 * t->speed, vy = t->nosev[1] * 1.5 * t->speed, vz = t->nosev[2] * 1.5 * t->speed - R(A_DELTA);
                    double rv = (x * vx + y * vy + z * vz) / d;
                    s_rate = sqrt(fmax(0.0, vx * vx + vy * vy + vz * vz - rv * rv)) / d * 180 / M_PI;     // deg a loop across our view
                    s_fac = -(t->nosev[0] * t->x + t->nosev[1] * t->y + t->nosev[2] * t->z) / d;
                }
                if (t && !got && in_crosshairs(t, R(A_VIEW) == 1)) {
                    got = 1;
                    fprintf(stderr, "EPI hit %d frames | off %.0f side %.0f d %.0f rate %.2f fac %.2f type %d\n", (int) (io.frame - since), s_off, s_side, s_d, s_rate, s_fac, t->type);
                }
            }
            {   // where the fight's time goes: our target slewing (> 10 deg off), tracking (within 10, not in the
                // sights), in the sights; or no target
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh), tg = combat_target(), k = 3;
                for (int i = 0; i < n; i++) if (sh[i].slot == tg && ship_in_space(&sh[i]) && !ship_dead(&sh[i])) {
                    int rear = R(A_VIEW) == 1;
                    double x = rear ? -sh[i].x : sh[i].x, z = rear ? -sh[i].z : sh[i].z, y = sh[i].y;
                    double off = atan2(sqrt(x * x + y * y), z) * 180 / M_PI;
                    k = in_crosshairs(&sh[i], rear) ? 2 : off < 10 ? 1 : 0;
                    if (getenv("TRKLOG") && k >= 1) {  // per frame: the target's angles in our view (deg, x right y up), range, stick
                        double d = sqrt(x * x + y * y + z * z) + 1;
                        fprintf(stderr, "TL %6u ax %6.2f ay %6.2f d %5.0f jx %4d jy %4d in %d\n", io.frame, atan2(x, z) * 180 / M_PI, atan2(y, z) * 180 / M_PI,
                                d, (int) R(0xC1) - 128, (int) R(0xC2) - 128, k == 2);
                    }
                    if (getenv("TRK") && k >= 1) {     // aiming: by distance, the error over the sights' tolerance there
                        extern long g_trk[8][4];       // [distance bin]: frames tracking, frames in, sum of off/tol, frames off/tol < 3
                        double d = sqrt(x * x + y * y + z * z) + 1;
                        const shiptype_t *ty = ship_type(sh[i].type);
                        double tol = atan2(ty && ty->area ? sqrt((double) ty->area) : 80, d) * 180 / M_PI;
                        int b = d < 1000 ? 0 : d < 2000 ? 1 : d < 4000 ? 2 : d < 8000 ? 3 : 4;
                        g_trk[b][k == 2]++;
                        if (k == 1) { g_trk[b][2] += (long) (off / tol * 10); g_trk[b][3] += off / tol < 3; }
                    }
                }
                s_ttk[k]++;
            }
            {   // exposure (TACTICS: a hostile's laser hits us with its nose within ~13.5 deg, dot >= 35/36, and
                // every axis under 8192): frames by guns on us, and how: a joust (our nose on it too), ahead, behind
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh);
                for (int i = 0; i < n; i++) {
                    const ship_t *q = &sh[i];
                    if (!ship_in_space(q) || ship_dead(q) || q->type == 1 || !(q->newb & NEWB_HOSTILE) || (q->type >= 3 && q->type <= 8)) continue;
                    if (labs(q->x) >= 8192 || labs(q->y) >= 8192 || labs(q->z) >= 8192) continue;
                    double d = sqrt((double) q->x * q->x + (double) q->y * q->y + (double) q->z * q->z) + 1;
                    double fac = -(q->nosev[0] * q->x + q->nosev[1] * q->y + q->nosev[2] * q->z) / d;
                    if (fac < 35.0 / 36) continue;
                    s_expo[0]++;
                    double ours = acos(fmax(-1.0, fmin(1.0, q->z / d))) * 180 / M_PI;
                    if (ours < 15) s_expo[1]++;
                    else if (q->z > 0) s_expo[2]++;
                    else s_expo[3]++;
                }
                s_expo[4]++;
            }
            if (getenv("FTRACE") && io.frame % atoi(getenv("FTRACE")) == 0) {   // frame trace: us, our target, every enemy
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh), t = combat_target();
                char buf[1400]; int k = 0;
                k += snprintf(buf + k, sizeof buf - k, "%6u L%02x/%02x T%3d E%3d F%3d A%3d spd%2d v%d ph%d tgt%2d |", io.frame, laser(0), laser(1), laser_temp(), energy(), fsh(), ash(), R(A_DELTA), R(A_VIEW), g_phase, t);
                for (int i = 0; i < n && k < (int) sizeof buf - 80; i++) {
                    const ship_t *q = &sh[i];
                    if (!ship_in_space(q) || q->type < 3 || (q->type <= 8 && !ship_dead(q))) continue;
                    double d = sqrt((double) q->x * q->x + (double) q->y * q->y + (double) q->z * q->z) + 1;
                    double fac = -(q->nosev[0] * q->x + q->nosev[1] * q->y + q->nosev[2] * q->z) / d;   // its nose on us: 1
                    double off = acos(fmax(-1.0, fmin(1.0, (R(A_VIEW) == 1 ? -q->z : q->z) / d))) * 180 / M_PI;
                    k += snprintf(buf + k, sizeof buf - k, " %s%d:t%d d%.0f off%.0f fac%.2f e%d%s%s", q->slot == t ? "*" : "", q->slot, q->type, d, off, fac, q->energy,
                                  in_crosshairs(q, R(A_VIEW) == 1) ? " IN" : "", ship_dead(q) ? " X" : "");
                }
                fprintf(stderr, "FT %s\n", buf);
            }
            if (getenv("COLLOG")) {      // a sudden loss: the nearest ship then (type, distance, largest axis)
                static int hp_prev = -1;
                int hp_ = energy() + fsh() + ash();
                if (hp_prev >= 0 && hp_prev - hp_ >= 30) {
                    static ship_t sh[NOSH];
                    int n = ships_read(io.ram, sh);
                    const ship_t *b = NULL; double bd = 1e18;
                    for (int i = 0; i < n; i++) if (ship_in_space(&sh[i])) {
                        double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
                        if (d < bd) bd = d, b = &sh[i];
                    }
                    if (b) printf("COL drop %d type %d dist %.0f box %d\n", hp_prev - hp_, b->type, bd,
                                  (int) fmax(fabs((double) b->x), fmax(fabs((double) b->y), fabs((double) b->z))));
                }
                hp_prev = hp_;
            }
            if (getenv("ROTLOG")) {      // per iteration: the stick, and the planet's direction (our rotation)
                static int lm = -1;
                int m = R(A_MCNT);
                if (m != lm) {
                    static ship_t sh[NOSH];
                    int n = ships_read(io.ram, sh);
                    for (int i = 0; i < n; i++) if (sh[i].slot == 0 && sh[i].type >= 128)
                        printf("ROT %u %d %d %d %d %d %d\n", io.frame, m, R(A_JSTX), R(A_JSTY), sh[i].x, sh[i].y, sh[i].z);
                    lm = m;
                }
            }
            {   // a ring of the last 2 s: on a death, the frame before DEATH cleared space (RES2) is the last alive
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh), k = 0;
                lastf_t *L = &s_ring[s_rn++ % 100];
                *L = (lastf_t){energy(), fsh(), ash(), R(A_ALTIT), R(A_DELTA), 0, 0, 0, 0, 1e9};
                for (int i = 0; i < n; i++) if (ship_in_space(&sh[i])) {
                    k++;
                    if (sh[i].type >= 128) continue;
                    double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
                    if (d < L->d) L->d = d, L->type = sh[i].type, L->z = sh[i].z, L->en = sh[i].energy;
                }
                L->n = k;
            }
            int ph_now = g_phase;
            if (!R(0x87)) diag_frame(&dg, e_prev, dead_prev);
            int now_ = energy() + fsh() + ash();
            if (now_ < prev) { dmg += prev - now_; static int byph[5]; (void) byph; dmg_ph[ph_now < 5 ? ph_now : 0] += prev - now_; }
            prev = now_;
        }
        if (died && getenv("DCAUSE")) {
            // back from the end to the empty frame DEATH's reset leaves, then the frame before it; and the
            // biggest one-frame loss in the second before (a collision costs 128 + its energy / 2 at once)
            int m = s_rn < 100 ? (int) s_rn : 100, j = m - 1;
            while (j >= 0 && s_ring[(s_rn - m + j) % 100].n != 0) j--;
            while (j >= 0 && s_ring[(s_rn - m + j) % 100].n == 0) j--;
            if (j >= 0) {
                const lastf_t *L = &s_ring[(s_rn - m + j) % 100];
                int big = 0, bi = j;
                for (int i = j; i > 0 && i > j - 50; i--) {
                    const lastf_t *p = &s_ring[(s_rn - m + i - 1) % 100], *q = &s_ring[(s_rn - m + i) % 100];
                    int loss = (p->e + p->f + p->a) - (q->e + q->f + q->a);
                    if (loss > big) big = loss, bi = i - 1;
                }
                const lastf_t *P = &s_ring[(s_rn - m + bi) % 100];
                const char *why = P->type && P->d < 400 && big >= 100 ? "collision" : L->alt < 4 ? "planet" : "shot";
                // (and the energy a second before, and whether a pod was fitted: R(A_ESCP) is read at the death)
                const lastf_t *L50 = &s_ring[(s_rn - m + (j >= 50 ? j - 50 : 0)) % 100];
                fprintf(stderr, "DCAUSE %s %s: last E %d F %d A %d spd %d | biggest loss %d with t%d at d%.0f z%d (e%d) | 1s before E %d F %d A %d | pod %d\n", path, why,
                        L->e, L->f, L->a, L->spd, big, P->type, P->d, P->z, P->en, L50->e, L50->f, L50->a, s_pod0);
            } else fprintf(stderr, "DCAUSE %s unknown: no reset frame\n", path);
        }
        if (getenv("TRK")) {
            static const char *bn[] = {"<1000", "1-2k", "2-4k", "4-8k", ">8k"};
            for (int b = 0; b < 5; b++) if (g_trk[b][0] + g_trk[b][1])
                fprintf(stderr, "TRK %-6s tracking %6ld in %6ld (%4.1f%%) | off/tolerance when out: mean %.1f, within 3x %.0f%%\n", bn[b], g_trk[b][0], g_trk[b][1],
                        100.0 * g_trk[b][1] / (g_trk[b][0] + g_trk[b][1]), g_trk[b][0] ? g_trk[b][2] / 10.0 / g_trk[b][0] : 0, g_trk[b][0] ? 100.0 * g_trk[b][3] / g_trk[b][0] : 0);
            memset(g_trk, 0, sizeof g_trk);
        }
        if (getenv("SWWHY")) { extern long g_sw_why[4]; fprintf(stderr, "SWWHY lost %ld missile %ld better %ld back %ld\n", g_sw_why[0], g_sw_why[1], g_sw_why[2], g_sw_why[3]); }
        fprintf(stderr, "EXPO %ld %ld %ld %ld %ld\n", s_expo[0], s_expo[1], s_expo[2], s_expo[3], s_expo[4]);
        fprintf(stderr, "TTK %ld %ld %ld %ld\n", s_ttk[0], s_ttk[1], s_ttk[2], s_ttk[3]);
        memset(s_ttk, 0, sizeof s_ttk);
        memset(s_expo, 0, sizeof s_expo);
        { extern const char *g_lab_why; if (getenv("WHY") && g_lab_why) fprintf(stderr, "%s: %s\n", path, g_lab_why); g_lab_why = 0; }
        printf("%s %.3f %.2f %.1f %d %d %d  %d %d %d %d %d %d %d %d %d %d  %d %d\n", path, died ? 0.0 : tally() - t0, (io.frame - (g_lab_t0 == 0xFFFFFFFF ? 0 : g_lab_t0) - g_pause_frames) / 3000.0, g_lab_cleared / 50.0, dmg,
               died, g_lab_pod, dg.kills, dg.start_n, dg.left, dg.present, dg.sights, dg.fire_on, dg.fire_off, dg.hot, dg.aimed, dg.dealt,
               s_prev_n, s_rrev_n);
        if (getenv("PHASES")) fprintf(stderr, "PHASES %d %d %d %d %d DMG %d %d %d %d %d\n", dg.ph[0], dg.ph[1], dg.ph[2], dg.ph[3], dg.ph[4],
                                      dmg_ph[0], dmg_ph[1], dmg_ph[2], dmg_ph[3], dmg_ph[4]);
        fflush(stdout);
    }
    fprintf(stderr, "TGT episodes %d gone %d switched_unacquired %d switched_acquired %d lock_losses %d\n",
            s_aim.episodes, s_aim.gone, s_aim.sw_unacq, s_aim.sw_acq, s_aim.lock_losses);
        fprintf(stderr, "AIM acquire %.2f s avg over %d (never %d) | hold %.1f%% of %d locked frames | off %.2f deg avg | stick at a stop %.1f%%\n",
            s_aim.acq_n ? s_aim.acq_sum / 50.0 / s_aim.acq_n : 0, s_aim.acq_n, s_aim.acq_fail,
            s_aim.locked_frames ? 100.0 * s_aim.in_frames / s_aim.locked_frames : 0, s_aim.locked_frames,
            s_aim.locked_frames ? s_aim.off_sum / s_aim.locked_frames : 0, s_aim.locked_frames ? 100.0 * s_aim.stop_frames / s_aim.locked_frames : 0);
    return 0;
}

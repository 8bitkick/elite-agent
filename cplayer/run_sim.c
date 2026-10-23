// The C player on the native emulator (sim/libbeeb.c), in lockstep like sim/simlink.py: keys land
// `delay` frames after the player asks for them, as over the Presto's own key path.
//   run_sim [minutes of game time] [disc] [journal.jsonl] [final machine state]
// It stops early at the rank of Elite (a kill tally of 6400).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "player.h"
#include "ships.h"
#include <math.h>
#include "pilot.h"
#include "keyq.h"
#include "galaxy.h"
#include "elite_ram.h"
#include "audit.h"

typedef struct beeb_t beeb_t;
beeb_t *beeb_new(const char *roms, const char *disc_path);
int beeb_model(void);
int beeb_disc_busy(beeb_t *b);           // the machine (machine_hook.h's model: 1 Model B, 3 Master 128)
void beeb_analogue(beeb_t *b, int channel, int value);
const char *beeb_emu_dir(void);
void beeb_frames(beeb_t *b, int n);
int beeb_reboot(beeb_t *b);
uint8_t *beeb_ram(beeb_t *b);
void beeb_key(beeb_t *b, int code, int down);
size_t beeb_state_size(void);
void beeb_save(beeb_t *b, uint8_t *buf);
void beeb_load(beeb_t *b, const uint8_t *buf);
int beeb_shot(beeb_t *b, uint8_t *out, int frames);
int beeb_shot_height(void);
int beeb_disc_dump(beeb_t *b, const char *path);

static beeb_t *B;
static player_io_t io;
static FILE *journal;

// INLOG=1: every input (held keys, taps, the joystick) and a hash of RAM each frame: where two builds part
static void p_hold(void *ctx, const uint8_t *c, int n) {
    (void) ctx;
    { static int on = -1; if (on < 0) on = getenv("INLOG") != NULL; if (on) { printf("IN %u h", io.frame); for (int i = 0; i < n; i++) printf(" %d", c[i]); printf("\n"); } }
    keyq_hold(io.frame, c, n);
}
static void p_tap(void *ctx, uint8_t c, int down, int up) {
    (void) ctx;
    { static int on = -1; if (on < 0) on = getenv("INLOG") != NULL; if (on) printf("IN %u t %d %d %d\n", io.frame, c, down, up); }
    keyq_tap(io.frame, c, down, up);
}
// ---- fight recordings (FIGHTS=dir): the machine at the start of each fight and every key change
// after it, so the fight replays exactly in the sim (sim/replay_fight.py); an index of them all
static const char *s_fight_dir;
static FILE *s_fight_index;
static uint8_t *s_snap;
static struct { uint32_t frame; uint8_t code, down; } s_ev[200000];
static int s_nev, s_recording, s_nfight;
static uint32_t s_rec_start;
static char s_enemies[600], s_killed[600];

// ---- film (FILM=dir): every input to the machine (keys and joystick) by frame in dir/inputs.bin, and the
// machine every SNAPEVERY frames (default 15000: 5 game minutes) in dir/NNNNNNNNN.snap: any moment of the
// career replays exactly from the snapshot before it (sim/film.c), without the player
static FILE *s_film;
static const char *s_film_dir;
static void film_input(uint8_t type, uint8_t a, uint16_t v) {
    if (!s_film) return;
    uint32_t fr = io.frame;
    fwrite(&fr, 4, 1, s_film); fwrite(&type, 1, 1, s_film); fwrite(&a, 1, 1, s_film); fwrite(&v, 2, 1, s_film);
}

static void key(int code, int down) {
    beeb_key(B, code, down);
    film_input(0, (uint8_t) code, (uint16_t) down);
    if (s_recording && s_nev < (int) (sizeof s_ev / sizeof s_ev[0]))
        s_ev[s_nev].frame = io.frame - s_rec_start, s_ev[s_nev].code = (uint8_t) code, s_ev[s_nev].down = (uint8_t) down, s_nev++;
}

static void fight_begin(void) {
    if (!s_snap) s_snap = malloc(beeb_state_size());
    beeb_save(B, s_snap);
    s_rec_start = io.frame;
    s_nev = 0;
    s_enemies[0] = s_killed[0] = 0;
    s_recording = 1;
}

static void fight_end(const char *how, const char *json) {
    if (!s_recording) return;
    s_recording = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/%05d.fight", s_fight_dir, s_nfight);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[4] = {0x54484746, (uint32_t) beeb_state_size(), (uint32_t) s_nev, io.frame - s_rec_start};   // "FGHT"
    fwrite(hdr, 4, 4, f);
    fwrite(s_snap, 1, beeb_state_size(), f);
    for (int i = 0; i < s_nev; i++) { fwrite(&s_ev[i].frame, 4, 1, f); fwrite(&s_ev[i].code, 1, 1, f); fwrite(&s_ev[i].down, 1, 1, f); }
    fclose(f);
    const uint8_t *r = io.ram;
    fprintf(s_fight_index, "{\"id\": %d, \"start\": %u, \"frames\": %u, \"end\": \"%s\", \"g\": %d, \"x\": %d, \"y\": %d, "
            "\"witchspace\": %d, \"enemies\": [%s], \"killed\": [%s], \"result\": %s, \"tally_end\": %d, \"energy_end\": %d}\n",
            s_nfight, s_rec_start, io.frame - s_rec_start, how, r[A_GCNT] + 1, r[A_QQ0], r[A_QQ1], r[A_MJ] != 0, s_enemies, s_killed,
            json ? json : "null", r[A_TALLY] | r[A_TALLY + 1] << 8, r[A_ENERGY]);
    fflush(s_fight_index);
    s_nfight++;
}

// ---- session recordings (SESSIONS=dir): the machine at each launch, kept with how the session
// ended (docked, stuck and why, died): every failure can be replayed (START=file)
static const char *s_sess_dir;
static FILE *s_sess_index;
static uint8_t *s_sess_snap;
static int s_sess_on, s_nsess;
static uint32_t s_sess_start;
static void sess_begin(void) {
    if (!s_sess_snap) s_sess_snap = malloc(beeb_state_size());
    beeb_save(B, s_sess_snap);
    s_sess_start = io.frame, s_sess_on = 1;
}
static void sess_end(const char *how, const char *json) {
    if (!s_sess_on) return;
    s_sess_on = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/%05d.fight", s_sess_dir, s_nsess);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[4] = {0x54484746, (uint32_t) beeb_state_size(), 0, io.frame - s_sess_start};
    fwrite(hdr, 4, 4, f);
    fwrite(s_sess_snap, 1, beeb_state_size(), f);
    fclose(f);
    const uint8_t *r = io.ram;
    fprintf(s_sess_index, "{\"id\": %d, \"start\": %u, \"frames\": %u, \"end\": \"%s\", \"witchspace\": %d, \"tally\": %d, \"note\": %s}\n",
            s_nsess, s_sess_start, io.frame - s_sess_start, how, r[A_MJ] != 0, r[A_TALLY] | r[A_TALLY + 1] << 8, json ? json : "null");
    fflush(s_sess_index);
    s_nsess++;
}

static void add_name(char *list, size_t cap, const char *name) {
    size_t n = strlen(list);
    snprintf(list + n, cap - n, "%s\"%s\"", n ? ", " : "", name);
}
static int32_t s_adc[4] = {-1, -1, -1, -1};     // the joystick values we last set (-1: never); the core keeps them outside the machine
static void p_analogue(void *c, int ch, uint16_t v) {
    (void) c;
    { static int on = -1; if (on < 0) on = getenv("INLOG") != NULL; if (on && s_adc[ch & 3] != v) printf("IN %u a%d %u\n", io.frame, ch, v); }
    beeb_analogue(B, ch, v);
    if (s_adc[ch & 3] != v) film_input(1, (uint8_t) ch, v);
    s_adc[ch & 3] = v;
}
static void film_snap(void) {
    char path[600];
    snprintf(path, sizeof path, "%s/%09u.snap", s_film_dir, io.frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static uint8_t *st;
    if (!st) st = malloc(beeb_state_size());
    beeb_save(B, st);
    uint32_t hdr[2] = {0x4D4C4946, (uint32_t) beeb_state_size()};    // "FILM"
    fwrite(hdr, 4, 2, f);
    fwrite(s_adc, 4, 4, f);
    fwrite(st, 1, beeb_state_size(), f);
    fclose(f);
    fflush(s_film);
}
static void p_log(void *ctx, int level, const char *s) {
    if (s_sess_dir && level == PLOG_STATUS && !strncmp(s, "Launched from", 13)) { sess_end("restarted", NULL); sess_begin(); }
    (void) ctx;
    static const char *const TAG[] = {"STATUS ", "", "  "};

    if (level == PLOG_STATUS && s_recording) {
        char name[64];
        if (sscanf(s, "Enemy %63[^\n]", name) == 1 && strstr(name, " detected")) { *strstr(name, " detected") = 0; add_name(s_enemies, sizeof s_enemies, name); }
        else if (strstr(s, " destroyed") && strncmp(s, "Missile", 7)) {
            snprintf(name, sizeof name, "%.*s", (int) (strstr(s, " destroyed") - s), s);
            add_name(s_killed, sizeof s_killed, name);
        }
    }
    else printf("%7.1f %s%s\n", io.frame / 50.0 / 60.0, TAG[level], s);
    fflush(stdout);
}
// the black box: the last 300 frames (6 s) of us and the three nearest ships, printed when we die, with a
// guess at why (the frame before the death sequence: QQ11 13 or the debris loop's LASCT 64)
typedef struct { unsigned fr; uint8_t e, f, a, spd, alt, tmp, q11, q12, mc, las; struct { uint8_t type; int d; int z; } s[3]; } bb_t;
#define BBN 300
static bb_t s_bb[BBN];
static unsigned s_bbn;
static void bb_record(void) {
    static ship_t sh[NOSH];
    bb_t *b = &s_bb[s_bbn++ % BBN];
    const uint8_t *r = io.ram;
    *b = (bb_t){io.frame, r[A_ENERGY], r[A_FSH], r[A_ASH], r[A_DELTA], r[A_ALTIT], r[A_CABTMP], r[A_QQ11], r[A_QQ12], r[A_MCNT], r[A_LASCT], {{0}}};
    int n = ships_read(r, sh), k = 0;
    for (int i = 0; i < n; i++) {
        if (!ship_in_space(&sh[i])) continue;
        int d = (int) sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
        int j = k < 3 ? k++ : 3;
        if (j == 3) { if (d >= b->s[2].d) continue; j = 2; }
        for (; j > 0 && d < b->s[j - 1].d; j--) b->s[j] = b->s[j - 1];
        b->s[j].type = sh[i].type, b->s[j].d = d, b->s[j].z = sh[i].z;
    }
}
static void bb_dump(void) {
    unsigned n = s_bbn < BBN ? s_bbn : BBN, i0 = s_bbn - n;
    int last = -1;          // the last frame before the death sequence
    for (unsigned i = i0; i < s_bbn; i++) {
        const bb_t *b = &s_bb[i % BBN];
        // (also a sudden refill: the game resets the energy as the death starts, a frame before the screen shows it)
        if (b->q11 == 13 || b->q12 || (b->mc == 255 && b->las > 0) || (last >= 0 && b->e > s_bb[last].e + 100)) break;
        last = (int) (i % BBN);
    }
    if (last >= 0) {
        const bb_t *b = &s_bb[last];
        const char *why = b->s[0].type && b->s[0].d < 400 ? "collision" : b->alt < 4 ? "planet" : b->tmp > 240 ? "heat" : b->e < 40 ? "shot down" : "unknown";
        printf("DEATHCAUSE %s: frame %u E %d F %d A %d spd %d alt %d temp %d nearest t%d d%d z%d\n", why, b->fr, b->e, b->f, b->a, b->spd,
               b->alt, b->tmp, b->s[0].type, b->s[0].d, b->s[0].z);
    } else printf("DEATHCAUSE none: no death sequence in the last %u frames\n", n);
    for (unsigned i = i0; i < s_bbn; i += 5) {
        const bb_t *b = &s_bb[i % BBN];
        printf("BB %u E %d F %d A %d spd %d alt %d q11 %d mc %d las %d |", b->fr, b->e, b->f, b->a, b->spd, b->alt, b->q11, b->mc, b->las);
        for (int j = 0; j < 3 && b->s[j].type; j++) printf(" t%d d%d z%d", b->s[j].type, b->s[j].d, b->s[j].z);
        printf("\n");
    }
}
static int s_stop;      // STOPTALLY=n: stop at the first save with the tally at n or more (a commander to hand on: DISCDUMP)
static void p_note(void *ctx, const char *s) {
    (void) ctx;
    if (getenv("STOPTALLY") && strstr(s, "\"event\": \"save\"")) {
        const char *t = strstr(s, "\"tally\": ");
        if (t && atof(t + 9) >= atof(getenv("STOPTALLY"))) s_stop = 1;
    }
    if (strstr(s, "\"fight_start\"")) {     // the state we go into each fight in (a hurt start is the career's doing)
        static ship_t sh[NOSH];
        int n = ships_read(io.ram, sh), k = 0;
        double near_ = 1e9;
        for (int i = 0; i < n; i++) if (ship_in_space(&sh[i]) && (sh[i].newb & NEWB_HOSTILE) && !ship_dead(&sh[i])) {
            k++;
            double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
            if (d < near_) near_ = d;
        }
        printf("%7.1f FSTART E %d F %d A %d hostiles %d nearest %.0f ws %d sspr %d\n", io.frame / 3000.0, io.ram[A_ENERGY], io.ram[A_FSH], io.ram[A_ASH], k,
               near_, io.ram[A_MJ] != 0, io.ram[A_SSPR]);
    }
    if (strstr(s, "\"died\"")) bb_dump();
    if (getenv("STOPFAIL") && (strstr(s, "\"died\"") || strstr(s, "\"stuck\""))) {      // the first death or restart ends the run
        if (journal) { fprintf(journal, "{\"frame\": %u, \"e\": %s}\n", io.frame, s); fflush(journal); }
        printf("STOPFAIL at frame %u: %s\n", io.frame, s);
        fflush(stdout);
        exit(3);
    }
    if (journal) { fprintf(journal, "{\"frame\": %u, \"e\": %s}\n", io.frame, s); fflush(journal); }
    if (s_sess_dir) {
        if (strstr(s, "\"died\"")) sess_end("died", s);
        else if (strstr(s, "\"stuck\"")) sess_end("stuck", s);
        else if (strstr(s, "\"docked\"")) sess_end("docked", s);
    }
    if (getenv("STOPDOCK")) {         // a session replay: the first dock, death or stuck ends it
        static int ended;
        if (!ended && (strstr(s, "\"docked\"") || strstr(s, "\"died\"") || strstr(s, "\"stuck\""))) {
            ended = 1;
            printf("SESSION %s\n", strstr(s, "\"docked\"") ? "docked" : strstr(s, "\"died\"") ? "died" : "stuck");
            fflush(stdout);
            exit(0);
        }
    }
    if (s_fight_dir) {
        if (strstr(s, "\"fight_start\"")) { fight_end("restarted", NULL); fight_begin(); }
        else if (strstr(s, "\"fight_end\"")) fight_end("over", s);
        else if (strstr(s, "\"escape_pod\"")) fight_end("escape pod", s);
        else if (strstr(s, "\"died\"")) fight_end("died", s);
        else if (strstr(s, "\"stuck\"")) fight_end("stuck", s);
        else if (strstr(s, "\"docked\"")) fight_end("docked", s);
    }
}

// SHOTS=from-to:step (frames): screens as PPM files in /tmp/claude-1000/shots, for debugging
static uint32_t s_shot_from, s_shot_to, s_shot_step;
static void shoot(void) {
    static uint8_t *px;
    int h = beeb_shot_height();
    if (!px) px = malloc(2048 * (size_t) h);
    int w = beeb_shot(B, px, 1);
    static const uint8_t PAL[8][3] = {{0,0,0},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
    char path[256];
    snprintf(path, sizeof path, "/tmp/claude-1000/shots/%07u.ppm", io.frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(PAL[px[i] & 7], 1, 3, f);
    fclose(f);
}

// DOCKSNAP=dir: the machine (and the stick values the core keeps outside it) each time dock() begins: the
// docking lab's starts (lab LABDOCK=1)
static void dock_snap(void) {
    static const char *dir; static int last = -1, on = -1;
    extern int g_dock_begins;
    if (on < 0) on = (dir = getenv("DOCKSNAP")) != NULL;
    if (!on || g_dock_begins == last) return;
    last = g_dock_begins;
    char path[600];
    snprintf(path, sizeof path, "%s/s%s_%09u.dock", dir, getenv("SKEW") ? getenv("SKEW") : "0", io.frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static uint8_t *st;
    if (!st) st = malloc(beeb_state_size());
    beeb_save(B, st);
    uint32_t hdr[4] = {0x4B434F44, (uint32_t) s_adc[0], (uint32_t) s_adc[1], (uint32_t) s_adc[2]};     // "DOCK"
    fwrite(hdr, 4, 4, f);
    fwrite(st, 1, beeb_state_size(), f);
    fclose(f);
}
static void step(void) {
    dock_snap();
    { static int on = -1; if (on < 0) on = getenv("INLOG") != NULL;
      if (on) { uint32_t h = 2166136261u; for (int i = 0; i < 0x8000; i++) h = (h ^ io.ram[i]) * 16777619u; printf("RAM %u %08x\n", io.frame, h); } }
    io.taps_done += (uint32_t) keyq_step(io.frame, key);
    if (s_shot_step && io.frame >= s_shot_from && io.frame <= s_shot_to && (io.frame - s_shot_from) % s_shot_step == 0) shoot();
    else beeb_frames(B, 1);
    io.frame++;
}

// PTIME=1 (with PLAYER_TIMING built in): the player's cost a frame, by what it was doing: not fighting; fighting in
// normal space, efficient or the show; in witchspace, efficient or the show. Average, p99, max, and the sections
// (cplayer/ptime.h) inside it, printed at the end. A host's nanoseconds: the Presto (RP2350, 150 MHz) runs the same code
// some 20-40 times slower
#include "ptime.h"
#ifdef PLAYER_TIMING
long long g_pt[8];
static long long s_pt0;
static struct { long n; double sum; long long max; long hist[200]; double sec[8]; long long secmax[8]; } s_ptc[5];
static const char *PTN[5] = {"not fighting", "fight, efficient", "fight, show", "witchspace, efficient", "witchspace, show"};
static void ptime_frame_start(void) { memset(g_pt, 0, sizeof g_pt); s_pt0 = ptime_ns(); }
static void ptime_frame_end(void) {
    long long dt = ptime_ns() - s_pt0;
    extern int g_phase, g_show_mode;
    int c = g_phase != 1 ? 0 : (witchspace() ? 3 : 1) + (g_show_mode == 1);
    s_ptc[c].n++, s_ptc[c].sum += dt;
    if (dt > s_ptc[c].max) s_ptc[c].max = dt;
    int b = (int) (dt / 2000); if (b > 199) b = 199;          // (2 us bins up to 400 us)
    s_ptc[c].hist[b]++;
    static int slow = -1;
    if (slow < 0) { const char *e = getenv("PTIME"); slow = e && atoi(e) >= 2; }
    if (slow && dt > 300000) fprintf(stderr, "PTSLOW %.1f us context %d frame %ld\n", dt / 1000.0, c, s_ptc[c].n);   // (PTIME=2)
    for (int i = 0; i < 6; i++) { s_ptc[c].sec[i] += g_pt[i]; if (g_pt[i] > s_ptc[c].secmax[i]) s_ptc[c].secmax[i] = g_pt[i]; }
}
static void ptime_report(void) {
    static const char *SN[6] = {"plan_stick", "target", "combat_keys", "show_frame", "show_view", "jump_max table"};
    for (int c = 0; c < 5; c++) {
        if (!s_ptc[c].n) continue;
        long acc = 0; int p99 = 0;
        for (int b = 0; b < 200; b++) { acc += s_ptc[c].hist[b]; if (acc >= s_ptc[c].n * 0.99) { p99 = b; break; } }
        fprintf(stderr, "PTIME %-22s frames %8ld  avg %6.1f us  p99 %s%4d us  max %7.1f us |", PTN[c], s_ptc[c].n, s_ptc[c].sum / s_ptc[c].n / 1000,
                p99 == 199 ? ">" : " ", p99 * 2, s_ptc[c].max / 1000.0);
        for (int i = 0; i < 6; i++) fprintf(stderr, " %s avg %.1f max %.1f |", SN[i], s_ptc[c].sec[i] / s_ptc[c].n / 1000, s_ptc[c].secmax[i] / 1000.0);
        fprintf(stderr, "\n");
    }
    extern long g_plan_steps, g_plan_calls, g_plan_capped, g_plan_maxsteps;
    if (g_plan_calls) fprintf(stderr, "PTIME planner (float): %ld calls, %.0f steps a call, max %ld, capped %ld\n", g_plan_calls, (double) g_plan_steps / g_plan_calls, g_plan_maxsteps, g_plan_capped);
}
#else
static void ptime_frame_start(void) {}
static void ptime_frame_end(void) {}
static void ptime_report(void) {}
#endif

static int s_collog, s_turbolog;
// TIMESPLIT=1: where the game time goes, frame by frame (printed at the end, not in the journal): the disc
// drive spinning (loads and saves, and the second or so it runs on after), docked, fighting, witchspace, a hunting session's
// flying, other flying; and fights started a game hour of normal-space flying
static const char *const SPLIT[] = {"disc", "docked", "fight", "witchspace", "hunt flying", "other flying"};
static long s_split[6], s_ns_fights, s_ns_fly, s_ns_loops;
static int s_last_mcnt = -1;
static int s_split_on, s_last_phase;
static long s_turbo_fr, s_turbo_ui;
int main(int argc, char **argv) {
    double minutes = argc > 1 ? atof(argv[1]) : 60;
    const char *home = getenv("HOME");
    char roms[256], disc[256];
    // (the machine's ROMs and its Elite: the Master's (libbeeb.c) or the Model B's (libbeebjit.c, run_sim_b))
    snprintf(roms, sizeof roms, "%s/roms/%s", beeb_emu_dir(), beeb_model() == 1 ? "" : "mos3.20");
    snprintf(disc, sizeof disc, "%s/discs/%s", beeb_emu_dir(), beeb_model() == 1 ? "elite-disc-flicker-free-sth.ssd" : "elite-master-flicker-free-sng47.ssd");
    if (getenv("DISC")) snprintf(disc, sizeof disc, "%s", getenv("DISC"));      // (DISC=file.ssd: another copy of the disc, a career saved on it)
    B = beeb_new(roms, argc > 2 && *argv[2] ? argv[2] : disc);
    if (!B) { fprintf(stderr, "beeb_new failed\n"); return 1; }
    journal = fopen(argc > 3 ? argv[3] : "/dev/null", "w");
    { extern int g_launch_clear; if (getenv("LCLEAR")) g_launch_clear = atoi(getenv("LCLEAR")); }
    { extern int g_undamped; if (getenv("UNDAMPED")) g_undamped = atoi(getenv("UNDAMPED")); }
    { extern int g_brk_log; if (getenv("BRKLOG")) g_brk_log = atoi(getenv("BRKLOG")); }
    { extern double g_weak_risk; if (getenv("WEAKRISK")) g_weak_risk = atof(getenv("WEAKRISK")); }
    if (getenv("SHOPORDER")) {      // the first items of the shopping list, by number (ECM 0, pod 1, bay 2, beam 3, ...)
        extern int g_shop_order[];
        int order[16], n = 0, used[16] = {0};
        for (char *s = getenv("SHOPORDER"); *s && n < 10; ) { order[n] = atoi(s); used[order[n]] = 1; n++; while (*s && *s != ',') s++; if (*s) s++; }
        for (int i = 0; i < 10; i++) if (!used[i]) order[n++] = i;
        for (int i = 0; i < 10; i++) g_shop_order[i] = order[i];
    }
    { extern int g_msl_side; if (getenv("MSLSIDE")) g_msl_side = atoi(getenv("MSLSIDE")); }
    { extern int g_flee_n; if (getenv("FLEEN")) g_flee_n = atoi(getenv("FLEEN")); }
    { extern int g_ws_fight; if (getenv("WSFIGHT")) g_ws_fight = atoi(getenv("WSFIGHT")); }
    { extern int g_ws_shield; if (getenv("WSSHIELD")) g_ws_shield = atoi(getenv("WSSHIELD")); }
    { extern int g_dock_log; g_dock_log = getenv("DOCKLOG") != NULL; }
    { extern int g_rr_log; g_rr_log = getenv("RRLOG") != NULL; }
    s_collog = getenv("COLLOG") != NULL;
    s_turbolog = getenv("TURBOLOG") != NULL;
    s_split_on = getenv("TIMESPLIT") != NULL;
    { extern int g_turbo_ui_fix; if (getenv("TURBOUI")) g_turbo_ui_fix = atoi(getenv("TURBOUI")); }
    { extern void curious_env(void); curious_env(); }      // (curious steering numbers: CROLLDB, CRC*, PSW...)
    { extern int g_fight_reach; if (getenv("FIGHTR")) g_fight_reach = atoi(getenv("FIGHTR")); }
    { extern int g_ws_noregen; if (getenv("WSNOREGEN")) g_ws_noregen = atoi(getenv("WSNOREGEN")); }
    { extern double g_clu_deg, g_clu_w; if (getenv("CLUDEG")) g_clu_deg = atof(getenv("CLUDEG")); if (getenv("CLUW")) g_clu_w = atof(getenv("CLUW")); }
    { extern int g_clu_ws; if (getenv("CLUWS")) g_clu_ws = atoi(getenv("CLUWS")); }
    { extern int g_chase; if (getenv("CHASE")) g_chase = atoi(getenv("CHASE")); }
    { extern int g_brk_rear; if (getenv("BRKREAR")) g_brk_rear = atoi(getenv("BRKREAR")); }
    { extern double g_explore_ratio; if (getenv("EXPLORE")) g_explore_ratio = atof(getenv("EXPLORE")); }
    { extern int g_chain_quick; if (getenv("CHQ")) g_chain_quick = atoi(getenv("CHQ")); }
    { extern int g_brk_mode; if (getenv("BRKOFF")) g_brk_mode = atoi(getenv("BRKOFF")); }
    { extern int g_opp_fire; if (getenv("OPPFIRE")) g_opp_fire = atoi(getenv("OPPFIRE")); }
    { extern int g_jump_log; g_jump_log = getenv("JLOG") != NULL; }
    { extern int g_joystick; if (getenv("JOYSTICK")) g_joystick = atoi(getenv("JOYSTICK")); }
    { extern int g_dock_centre; if (getenv("DOCKC")) g_dock_centre = atoi(getenv("DOCKC")); }
    { extern int g_dock_smooth; if (getenv("DOCKSMOOTH")) g_dock_smooth = atoi(getenv("DOCKSMOOTH")); }
    { extern double g_dk_turn_m; if (getenv("DKTURNM")) g_dk_turn_m = atof(getenv("DKTURNM")); }
    { extern double g_dk_room0; if (getenv("DKROOM")) g_dk_room0 = atof(getenv("DKROOM")); }
    { extern double g_dk_lined; if (getenv("DKLINED")) g_dk_lined = atof(getenv("DKLINED")); }
    { extern double g_dk_lvl_d; if (getenv("DKLVL")) g_dk_lvl_d = atof(getenv("DKLVL")); }
    { extern double g_dk_chi, g_dk_vfk; if (getenv("DKCHI")) g_dk_chi = atof(getenv("DKCHI")); if (getenv("DKVFK")) g_dk_vfk = atof(getenv("DKVFK")); }
    { extern double g_dk_cone5; if (getenv("DKCONE5")) g_dk_cone5 = atof(getenv("DKCONE5")); }
    { extern double g_ts_aim; if (getenv("TSAIM")) g_ts_aim = atof(getenv("TSAIM")); }
    { extern int g_ts_view; if (getenv("TSVIEW")) g_ts_view = atoi(getenv("TSVIEW")); }
    { extern int g_jump_plan; extern double g_jump_to; if (getenv("JPLAN")) g_jump_plan = atoi(getenv("JPLAN")); if (getenv("JTO")) g_jump_to = atof(getenv("JTO")); }
    { extern double g_dk_arc_r, g_dk_arc_j; if (getenv("DKARCR")) g_dk_arc_r = atof(getenv("DKARCR")); if (getenv("DKARCJ")) g_dk_arc_j = atof(getenv("DKARCJ")); }
    { extern double g_dk_arc_kr; if (getenv("DKARCKR")) g_dk_arc_kr = atof(getenv("DKARCKR")); }
    { extern double g_dk_l1; if (getenv("DKL1")) g_dk_l1 = atof(getenv("DKL1")); }
    { extern double g_dk_vfmin; if (getenv("DKVFMIN")) g_dk_vfmin = atof(getenv("DKVFMIN")); }
    { extern double g_dk_evade_x; if (getenv("DKEVADE")) g_dk_evade_x = atof(getenv("DKEVADE")); }
    { extern double g_dk_ev_min, g_dk_ev_spd; if (getenv("DKEVMIN")) g_dk_ev_min = atof(getenv("DKEVMIN")); if (getenv("DKEVSPD")) g_dk_ev_spd = atof(getenv("DKEVSPD")); }
    { extern double g_dk_vfin; if (getenv("DKVFIN")) g_dk_vfin = atof(getenv("DKVFIN")); }
    { extern double g_dk_rm_d; if (getenv("DKRMD")) g_dk_rm_d = atof(getenv("DKRMD")); }
    { extern int g_dk_avoid; extern double g_dk_clear; if (getenv("DKAVOID")) g_dk_avoid = atoi(getenv("DKAVOID")); if (getenv("DKCLEAR")) g_dk_clear = atof(getenv("DKCLEAR")); }
    { extern double g_dk_near; if (getenv("DKNEAR")) g_dk_near = atof(getenv("DKNEAR")); }
    { extern double g_dk_ylim; if (getenv("DKYLIM")) g_dk_ylim = tan(atof(getenv("DKYLIM")) * M_PI / 180); }
    { extern void plandock_env(void); plandock_env(); }
    { extern int g_dock_trig; if (getenv("DOCKTRIG")) g_dock_trig = atoi(getenv("DOCKTRIG")); }
    { extern int g_dock_fast; if (getenv("DOCKFAST")) g_dock_fast = atoi(getenv("DOCKFAST")); }
    { extern double g_dk_g; if (getenv("DKG")) g_dk_g = atof(getenv("DKG")); }
    { extern double g_dk_xon; if (getenv("DKXON")) g_dk_xon = atof(getenv("DKXON")); }
    { extern int g_curious; if (getenv("CURIOUS")) g_curious = atoi(getenv("CURIOUS")); }
    {   // fair mode (fair.h, docs/FAIR.md): FAIR=1 the information limits; FAIRHANDS=1 the hand limits too; FAIRMASK=bits (1 see, 2 dashboard, 8 stick, 16 keys)
        extern int g_fair, g_fair_misjump, g_fair_stick_rate, g_fair_key_cap, g_fair_key_gap; extern double g_fair_noise;
        if (getenv("FAIR") && atoi(getenv("FAIR"))) g_fair = 3;           // (the information limits)
        if (getenv("FAIRHANDS") && atoi(getenv("FAIRHANDS"))) g_fair |= 24;     // (and, apart, the hand limits)
        if (getenv("FAIRMASK")) g_fair = atoi(getenv("FAIRMASK"));
        if (getenv("FAIRMJ")) g_fair_misjump = atoi(getenv("FAIRMJ"));
        if (getenv("FAIRSTICK")) g_fair_stick_rate = atoi(getenv("FAIRSTICK"));
        if (getenv("FAIRNOISE")) g_fair_noise = atof(getenv("FAIRNOISE"));
        if (getenv("FAIRCAP")) g_fair_key_cap = atoi(getenv("FAIRCAP"));
        if (getenv("FAIRGAP")) g_fair_key_gap = atoi(getenv("FAIRGAP"));
        if (getenv("FAIRP")) {      // fair-mode tests: name=value,... (fair_set_param)
            extern int fair_set_param(const char *name, double v);
            char *fp = strdup(getenv("FAIRP")), *tok, *sv;
            for (tok = strtok_r(fp, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
                char *eq = strchr(tok, '=');
                if (eq) { *eq = 0; if (!fair_set_param(tok, atof(eq + 1))) fprintf(stderr, "FAIRP: unknown %s\n", tok); }
            }
        }
    }
    if (getenv("CP")) {     // fight parameter overrides, name=value,... (as the lab's LABP)
        char *cp = strdup(getenv("CP")), *tok, *save;
        for (tok = strtok_r(cp, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            double v = atof(eq + 1);
            extern int combat_set_param(const char *name, double v);
            if (!combat_set_param(tok, v)) fprintf(stderr, "CP: unknown parameter %s\n", tok);
        }
    }
    { extern int g_steer_log; g_steer_log = getenv("STEERLOG") != NULL; }
    { extern int g_chain_energy, g_chain_shield; if (getenv("CHE")) g_chain_energy = atoi(getenv("CHE")); if (getenv("CHS")) g_chain_shield = atoi(getenv("CHS")); }
    { extern int g_chain; if (getenv("CHAIN")) g_chain = atoi(getenv("CHAIN")); }
    if (getenv("SHOTS")) sscanf(getenv("SHOTS"), "%u-%u:%u", &s_shot_from, &s_shot_to, &s_shot_step);
    if ((s_sess_dir = getenv("SESSIONS"))) {
        char p[512];
        snprintf(p, sizeof p, "%s/index.jsonl", s_sess_dir);
        s_sess_index = fopen(p, "w");
        if (!s_sess_index) { fprintf(stderr, "can't write %s\n", p); return 1; }
    }
    if ((s_fight_dir = getenv("FIGHTS"))) {
        char p[512];
        snprintf(p, sizeof p, "%s/index.jsonl", s_fight_dir);
        s_fight_index = fopen(p, "w");
        if (!s_fight_index) { fprintf(stderr, "can't write %s\n", p); return 1; }
    }
    beeb_frames(B, 700);                 // (the boot to the title screen)
    for (int i = 0; i < 40 && !getenv("START") && !elite_detect(beeb_model(), beeb_ram(B)); i++) beeb_frames(B, 50);    // (Solid Elite loads for ~22 s)
    if (getenv("START")) {               // start from a recorded fight's machine (a .fight file)
        FILE *f = fopen(getenv("START"), "rb");
        uint32_t hdr[4];
        if (!f || fread(hdr, 4, 4, f) != 4 || hdr[0] != 0x54484746) { fprintf(stderr, "bad START\n"); return 1; }
        uint8_t *st = malloc(hdr[1]);
        if (fread(st, 1, hdr[1], f) != hdr[1]) return 1;
        fclose(f);
        beeb_load(B, st);
        free(st);
        for (int c = 0; c < 128; c++) beeb_key(B, c, 0);     // (the snapshot's held keys: ours start with none)
    }
    // SKEW=n: n frames more before the player starts (the game's random numbers then differ: a different
    // career from the same start)
    if (getenv("SKEW")) beeb_frames(B, atoi(getenv("SKEW")));
    { extern int g_plan; if (getenv("PLAN")) g_plan = atoi(getenv("PLAN")); }
    { extern int g_plan_f, g_plan_cap; if (getenv("PLANF")) g_plan_f = atoi(getenv("PLANF")); if (getenv("PLANCAP")) g_plan_cap = atoi(getenv("PLANCAP")); }
    { extern int g_show; if (getenv("SHOW")) g_show = atoi(getenv("SHOW")); }
    if (getenv("SHOWP")) {      // show.c's numbers, name=value,...
        extern int show_set_param(const char *name, double v);
        char *s = strdup(getenv("SHOWP")), *tok, *save;
        for (tok = strtok_r(s, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            char *eq = strchr(tok, '=');
            if (eq && (*eq = 0, !show_set_param(tok, atof(eq + 1)))) { fprintf(stderr, "unknown show parameter %s\n", tok); exit(1); }
        }
    }
    { extern int g_no_missions; if (getenv("NOMISSION")) g_no_missions = atoi(getenv("NOMISSION")); }
    { extern int g_no_pod; if (getenv("NOPOD")) g_no_pod = atoi(getenv("NOPOD")); }
    if (getenv("NAME")) { extern char g_name[8]; snprintf(g_name, sizeof g_name, "%s", getenv("NAME")); }
    if (getenv("LOAD")) { extern char g_load_name[8]; strncpy(g_load_name, getenv("LOAD"), 7); }
    { extern int g_save; if (getenv("SAVE")) g_save = atoi(getenv("SAVE")); }
    io.ram = beeb_ram(B);
    io.hold = p_hold; io.tap = p_tap; io.log = p_log; io.note = p_note; io.analogue = p_analogue;
    io.machine = beeb_model();
    player_init(&io);
    if (!elite_detect(io.machine, io.ram)) { fprintf(stderr, "no Elite this player knows on the disc\n"); return 1; }
    printf("playing %s\n", g_ev.name);
    uint32_t end = (uint32_t) (minutes * 60 * 50);
    clock_t t0 = clock();
    int last_qq26 = -1, check_at = -1, checks = 0, bad = 0;
    uint32_t snap_every = getenv("SNAPEVERY") ? (uint32_t) atoi(getenv("SNAPEVERY")) : 15000;
    if ((s_film_dir = getenv("FILM"))) {
        char p[600];
        snprintf(p, sizeof p, "%s/inputs.bin", s_film_dir);
        if (!(s_film = fopen(p, "wb"))) { fprintf(stderr, "can't write %s\n", p); return 1; }
    }
    int last_tp = -1;
    // RESETAT=frame: the machine reset there and the disc booted, the player started afresh, as the
    // Presto does when a disc is booted or ejected (its frame count from 0 again)
    uint32_t reset_at = getenv("RESETAT") ? (uint32_t) atoi(getenv("RESETAT")) : 0;
    while (io.frame < end && !s_stop) {
        if (reset_at && io.frame == reset_at) {
            reset_at = 0;
            if (beeb_reboot(B)) { fprintf(stderr, "RESETAT: no reset on this machine\n"); return 1; }
            beeb_frames(B, 2500);           // (the Presto's ELITE_AUTOSTART: the player starts 50 s after the reset)
            end -= io.frame;
            io.frame = 0;
            player_init(&io);
            printf("RESET: the disc booted, the player started again 2500 frames on (QQ11 %d QQ12 %d distaway %d)\n",
                   io.ram[A_QQ11], io.ram[A_QQ12], io.ram[A_distaway]);
        }
        if (s_film) {
            if (io.frame % snap_every == 0) film_snap();
            // (the moments for a film: each change of the mission byte, in the journal)
            if (io.ram[A_TP] != last_tp) {
                if (last_tp >= 0 && journal) { fprintf(journal, "{\"frame\": %u, \"e\": {\"event\": \"tp\", \"from\": %d, \"to\": %d}}\n", io.frame, last_tp, io.ram[A_TP]); fflush(journal); }
                last_tp = io.ram[A_TP];
            }
        }
        if (getenv("AVLCHECK")) {
            if (io.ram[A_QQ26] != last_qq26) { last_qq26 = io.ram[A_QQ26]; check_at = (int) io.frame + 5; }
            if ((int) io.frame == check_at) {
                checks++;
                for (int i = 0; i < 17; i++)
                    if (item_avail(i, io.ram[A_QQ28], io.ram[A_QQ26]) != io.ram[A_AVL + i]) {
                        bad++;
                        fprintf(stderr, "avl %d: eco %d qq26 %d: %d, not %d\n", i, io.ram[A_QQ28], io.ram[A_QQ26], io.ram[A_AVL + i],
                                item_avail(i, io.ram[A_QQ28], io.ram[A_QQ26]));
                    }
            }
        }
        {   // (SCRAMBLE: the player plays on a copy of RAM with the hidden bytes scrambled; the audit counts its reads)
            static int lev = -1;
            if (lev < 0) lev = getenv("SCRAMBLE") ? atoi(getenv("SCRAMBLE")) : 0;
            const uint8_t *real = io.ram;
            io.ram = scramble(real, lev);
            g_audit_on = 1;
            ptime_frame_start();
            player_frame();
            ptime_frame_end();
            if (s_turbolog) {      // (TURBOLOG: cruise turbo's frames, and those on a screen that isn't a space view or counting)
                extern int player_dull_cruise(void);
                if (player_dull_cruise()) { s_turbo_fr++; if (io.ram[A_QQ11] != 0 || io.ram[A_QQ22 + 1]) s_turbo_ui++; }
            }
            if (s_collog) {      // (COLLOG: a sudden loss of 100+ with a ship within 500: a ram, as the lab counts them)
                static int hp_prev = -1;
                int hp_ = io.ram[A_ENERGY] + io.ram[A_FSH] + io.ram[A_ASH];
                if (hp_prev >= 0 && hp_prev - hp_ >= 100) {
                    static ship_t sh[NOSH];
                    int n = ships_read(io.ram, sh);
                    double bd = 1e18; int bt = 0;
                    for (int i = 0; i < n; i++) if (ship_in_space(&sh[i])) {
                        double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
                        if (d < bd) bd = d, bt = sh[i].type;
                    }
                    if (bd < 500) fprintf(stderr, "COLRAM frame %u drop %d type %d dist %.0f\n", io.frame, hp_prev - hp_, bt, bd);
                }
                hp_prev = hp_;
            }
            g_audit_on = 0;
            io.ram = real;
        }
        step();
        bb_record();
        if (s_split_on) {
            extern int g_phase;
            int player_session_kind(void);
            int k;
            if (beeb_disc_busy(B)) k = 0;
            else if (io.ram[A_QQ12]) k = 1;
            else if (g_phase == 1) k = 2;
            else if (io.ram[A_MJ]) k = 3;
            else k = player_session_kind() == 0 ? 4 : 5;
            s_split[k]++;
            if (k >= 2 && !io.ram[A_MJ]) {
                s_ns_fly++;
                int dm = s_last_mcnt < 0 ? 0 : (s_last_mcnt - io.ram[A_MCNT]) & 255;
                if (dm < 16) s_ns_loops += dm;      // (the game's main loop: a ship can spawn every 256)
                if (g_phase == 1 && s_last_phase != 1) s_ns_fights++;
            }
            s_last_phase = g_phase;
            s_last_mcnt = io.ram[A_MCNT];
        }
        if (getenv("RAMAT")) {       // RAMAT=frame,frame,...: the 32 KB of main RAM at those frames, to ram-<frame>.bin
            static char *spec;
            if (!spec) spec = getenv("RAMAT");
            for (char *p = spec; *p; ) {
                if ((uint32_t) atol(p) == io.frame) {
                    char nm[64];
                    snprintf(nm, sizeof nm, "ram-%u.bin", io.frame);
                    FILE *f = fopen(nm, "wb");
                    if (f) { fwrite(io.ram, 1, 0x8000, f); fclose(f); }
                }
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
        }
        if (getenv("WATCH")) {       // every 10 frames between two frames: the ships about
            static unsigned w0, w1; static int init;
            if (!init) { sscanf(getenv("WATCH"), "%u-%u", &w0, &w1); init = 1; }
            if (io.frame >= w0 && io.frame <= w1 && io.frame % (getenv("WATCH1") ? 1 : 10) == 0) {
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh);
                printf("%7u W E %d F %d A %d spd %d view %d qq11 %d:", io.frame, io.ram[A_ENERGY], io.ram[A_FSH], io.ram[A_ASH], io.ram[A_DELTA], io.ram[A_VIEW], io.ram[A_QQ11]);
                printf(" q12 %d mcnt %d lasct %d away %d", io.ram[A_QQ12], io.ram[A_MCNT], io.ram[A_LASCT], A_distaway ? io.ram[A_distaway] : -1);
                for (int i = 0; i < n; i++)
                    printf(" [%d t%d d%.0f nb%02x f%02x]", sh[i].slot, sh[i].type, sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z), sh[i].newb, sh[i].flags31);
                printf("\n");
            }
        }
        if (getenv("FISTLOG")) {    // our legal status worsening: when, and the ships about then
            static int last = -1;
            int f = io.ram[A_FIST];
            if (last >= 0 && f != last) {
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh);
                printf("FIST %d -> %d at frame %u view %d las %d |", last, f, io.frame, io.ram[A_VIEW], io.ram[A_LASCT]);
                for (int i = 0; i < n; i++) if (ship_in_space(&sh[i])) printf(" s%d t%d d%.0f newb %02x%s", sh[i].slot, sh[i].type,
                    sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z), sh[i].newb, ship_dead(&sh[i]) ? " X" : "");
                printf("\n");
            }
            last = f;
        }
        if (getenv("BUMPS")) {      // a sudden loss of shields + energy: a hit or a collision
            static int prev = -1;
            int now_ = io.ram[A_ENERGY] + io.ram[A_FSH] + io.ram[A_ASH];
            if (prev >= 0 && prev - now_ >= 25 && now_ > 0)
            {
                const uint8_t *st = io.ram + 0x400 + 37;      // slot 1: the station
                printf("%7.1f BUMP -%d (E %d F %d A %d speed %d safe %d docked %d) station nosev_z_hi %d (need >=214) roofv_x_hi %d (need >=80) z %d\n",
                       io.frame / 3000.0, prev - now_, io.ram[A_ENERGY], io.ram[A_FSH], io.ram[A_ASH], io.ram[A_DELTA], io.ram[A_SSPR] != 0,
                       io.ram[A_QQ12] != 0, st[14], st[16] & 0x7F, st[6] | st[7] << 8);
            }
            prev = now_;
        }
        {   // (the kill that made Elite: its frame, for SHOTS)
            static int was = -1;
            if (was >= 0 && was < 25 && io.ram[A_TALLY + 1] >= 25) printf("%7.1f TALLY ELITE at frame %u\n", io.frame / 3000.0, io.frame);
            was = io.ram[A_TALLY + 1];
        }
        if (io.frame % 3000 == 0 && io.ram[A_TALLY + 1] >= 25 && io.ram[A_QQ12]) {    // Elite, and docked
            printf("%7.1f ELITE: tally %d after %.1f game hours\n", io.frame / 50.0 / 60.0,
                   io.ram[A_TALLY] | io.ram[A_TALLY + 1] << 8, io.frame / 50.0 / 3600.0);
            if (getenv("ENDSTATUS")) {      // (and the Status page, ENDSTATUS seconds of it, filmed)
                uint32_t n = (uint32_t) (atof(getenv("ENDSTATUS")) * 50);
                // (every key let go first: the player may be holding one, CTRL for a mis-jump, and CTRL f8 isn't Status)
                for (int c = 0; c < 0x80; c++) beeb_key(B, c, 0);
                beeb_frames(B, 10);
                // (and RETURN: a prompt (the chart's Planet Name?) takes the f8 as typing)
                beeb_key(B, K_RETURN, 1); beeb_frames(B, 6); beeb_key(B, K_RETURN, 0); beeb_frames(B, 50);
                beeb_key(B, K_F8, 1); beeb_frames(B, 10); beeb_key(B, K_F8, 0); beeb_frames(B, 25);
                for (uint32_t i = 0; i < n; i++) { if (i % 2 == 0) shoot(); else beeb_frames(B, 1); io.frame++; }
            }
            break;
        }
    }
    if (s_film) { film_snap(); fclose(s_film); }
    if (argc > 4) {
        size_t n = beeb_state_size();
        uint8_t *buf = malloc(n);
        beeb_save(B, buf);
        FILE *f = fopen(argv[4], "wb");
        if (f) { fwrite(buf, 1, n, f); fclose(f); }
        free(buf);
    }
    if (getenv("DISCDUMP")) {      // the disc as the game left it (its saves), read back out of its track data
        int bad = beeb_disc_dump(B, getenv("DISCDUMP"));
        fprintf(stderr, "disc written to %s (%d sectors unreadable)\n", getenv("DISCDUMP"), bad);
    }
    if (getenv("AVLCHECK")) fprintf(stderr, "markets checked %d, items wrong %d\n", checks, bad);
    audit_dump();
    if (getenv("PTIME")) ptime_report();
    double s = (double) (clock() - t0) / CLOCKS_PER_SEC;
    if (s_split_on) {
        long all = 0;
        for (int i = 0; i < 6; i++) all += s_split[i];
        for (int i = 0; i < 6; i++) fprintf(stderr, "SPLIT %-13s %7.2f h %5.1f%%\n", SPLIT[i], s_split[i] / 180000.0, 100.0 * s_split[i] / (all ? all : 1));
        fprintf(stderr, "SPLIT fights started in normal space: %ld in %.2f h of flying there (%.1f an hour)\n", s_ns_fights, s_ns_fly / 180000.0, s_ns_fights / (s_ns_fly / 180000.0 + 1e-9));
        fprintf(stderr, "SPLIT the main loop there: %.2f iterations a frame\n", s_ns_loops / (s_ns_fly + 1e-9));
    }
    if (s_turbolog) fprintf(stderr, "TURBO cruise frames %ld, on a screen that isn't a space view or in a countdown %ld\n", s_turbo_fr, s_turbo_ui);
    fprintf(stderr, "%u frames in %.1f s (%.0fx real time)\n", io.frame, s, io.frame / 50.0 / s);
    return 0;
}

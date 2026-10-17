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

typedef struct beeb_t beeb_t;
beeb_t *beeb_new(const char *roms, const char *disc_path);
void beeb_analogue(beeb_t *b, int channel, int value);
const char *beeb_emu_dir(void);
void beeb_frames(beeb_t *b, int n);
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

static void p_hold(void *ctx, const uint8_t *c, int n) { (void) ctx; keyq_hold(io.frame, c, n); }
static void p_tap(void *ctx, uint8_t c, int down, int up) { (void) ctx; keyq_tap(io.frame, c, down, up); }
// ---- fight recordings (FIGHTS=dir): the machine at the start of each fight and every key change
// after it, so the fight replays exactly in the sim (sim/replay_fight.py); an index of them all
static const char *s_fight_dir;
static FILE *s_fight_index;
static uint8_t *s_snap;
static struct { uint32_t frame; uint8_t code, down; } s_ev[200000];
static int s_nev, s_recording, s_nfight;
static uint32_t s_rec_start;
static char s_enemies[600], s_killed[600];

static void key(int code, int down) {
    beeb_key(B, code, down);
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
static void p_analogue(void *c, int ch, uint16_t v) { (void) c; beeb_analogue(B, ch, v); }
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
static void p_note(void *ctx, const char *s) {
    (void) ctx;
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

static void step(void) {
    io.taps_done += (uint32_t) keyq_step(io.frame, key);
    if (s_shot_step && io.frame >= s_shot_from && io.frame <= s_shot_to && (io.frame - s_shot_from) % s_shot_step == 0) shoot();
    else beeb_frames(B, 1);
    io.frame++;
}

int main(int argc, char **argv) {
    double minutes = argc > 1 ? atof(argv[1]) : 60;
    const char *home = getenv("HOME");
    char roms[256], disc[256];
    snprintf(roms, sizeof roms, "%s/roms/mos3.20", beeb_emu_dir());
    snprintf(disc, sizeof disc, "%s/discs/elite-master-flicker-free-sng47.ssd", beeb_emu_dir());
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
    { extern int g_joystick; if (getenv("JOYSTICK")) g_joystick = atoi(getenv("JOYSTICK")); }
    { extern int g_dock_centre; if (getenv("DOCKC")) g_dock_centre = atoi(getenv("DOCKC")); }
    { extern int g_curious; if (getenv("CURIOUS")) g_curious = atoi(getenv("CURIOUS")); }
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
    { extern int g_no_missions; if (getenv("NOMISSION")) g_no_missions = atoi(getenv("NOMISSION")); }
    { extern int g_no_pod; if (getenv("NOPOD")) g_no_pod = atoi(getenv("NOPOD")); }
    if (getenv("LOAD")) { extern char g_load_name[8]; strncpy(g_load_name, getenv("LOAD"), 7); }
    { extern int g_save; if (getenv("SAVE")) g_save = atoi(getenv("SAVE")); }
    io.ram = beeb_ram(B);
    io.hold = p_hold; io.tap = p_tap; io.log = p_log; io.note = p_note; io.analogue = p_analogue;
    player_init(&io);
    uint32_t end = (uint32_t) (minutes * 60 * 50);
    clock_t t0 = clock();
    int last_qq26 = -1, check_at = -1, checks = 0, bad = 0;
    while (io.frame < end) {
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
        player_frame();
        step();
        bb_record();
        if (getenv("WATCH")) {       // every 10 frames between two frames: the ships about
            static unsigned w0, w1; static int init;
            if (!init) { sscanf(getenv("WATCH"), "%u-%u", &w0, &w1); init = 1; }
            if (io.frame >= w0 && io.frame <= w1 && io.frame % (getenv("WATCH1") ? 1 : 10) == 0) {
                static ship_t sh[NOSH];
                int n = ships_read(io.ram, sh);
                printf("%7u W E %d F %d A %d spd %d view %d qq11 %d:", io.frame, io.ram[A_ENERGY], io.ram[A_FSH], io.ram[A_ASH], io.ram[A_DELTA], io.ram[A_VIEW], io.ram[0x81]);
                printf(" q12 %d mcnt %d lasct %d away %d", io.ram[A_QQ12], io.ram[A_MCNT], io.ram[A_LASCT], io.ram[A_distaway]);
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
                printf("FIST %d -> %d at frame %u view %d las %d |", last, f, io.frame, io.ram[A_VIEW], io.ram[0x0E78]);
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
                       io.ram[0x87] != 0, st[14], st[16] & 0x7F, st[6] | st[7] << 8);
            }
            prev = now_;
        }
        if (io.frame % 3000 == 0 && io.ram[A_TALLY + 1] >= 25 && io.ram[0x87]) {    // Elite, and docked
            printf("%7.1f ELITE: tally %d after %.1f game hours\n", io.frame / 50.0 / 60.0,
                   io.ram[A_TALLY] | io.ram[A_TALLY + 1] << 8, io.frame / 50.0 / 3600.0);
            break;
        }
    }
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
    double s = (double) (clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "%u frames in %.1f s (%.0fx real time)\n", io.frame, s, io.frame / 50.0 / s);
    return 0;
}

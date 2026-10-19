// film: replay a stretch of a recorded career exactly (run_sim FILM=dir: snapshots + every input) and write
// its frames as raw RGB24 on stdout, for ffmpeg. No player: the machine gets the inputs it got in the career.
//   film ROMDIR DISC FILMDIR FROM TO [STEP] > frames.rgb      (frames FROM..TO of the career, every STEP)
//   ... | ffmpeg -f rawvideo -pix_fmt rgb24 -s WxH -r 50 -i - out.mp4      (W x H printed on stderr)
// (the disc is the original: a replay that loads a commander the career saved since won't match)
// gcc -O2 -fwrapv -o sim/film sim/film.c sim/libbeeb.c cplayer/ships.c -Irp2350-bbc-micro/core -lm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <math.h>
#include "../cplayer/ships.h"

// SCAN=1: no pictures, a line a frame instead: frame, view, ships drawn in the view (not the station), and a
// "show" score: the sum of their apparent sizes (blueprint radius / distance), to find the stretches where
// the enemies are close and on screen
// SCAN=2: the show meter's view (cplayer/showmeter.c): a line a frame: frame, view, the largest enemy drawn in the
// view (px across its hit radius, 256 r / z, in the picture z > 0, |x| < z/2 + r, |y| < 3z/8 + r), enemies alive,
// and each kill this frame (an enemy alive last frame, exploding now) as its size on screen then (0: not in view)
static int is_enemy(const ship_t *s) {
    if (!ship_in_space(s) || s->type == 1 || (s->type >= 3 && s->type <= 8)) return 0;
    return (s->newb & NEWB_HOSTILE) || s->type == 29 || s->type == 30;
}
static double drawn_px(const ship_t *s, int view) {
    double x, y = s->y, z;
    switch (view) {
    case 1: x = -s->x; z = -s->z; break;
    case 2: x = s->z; z = -s->x; break;
    case 3: x = -s->z; z = s->x; break;
    default: x = s->x; z = s->z; break;
    }
    const shiptype_t *t = ship_type(s->type);
    double r = t && t->area ? sqrt(t->area) : 80;
    if (z <= 0 || fabs(x) > z / 2 + r || fabs(y) > z * 0.375 + r) return 0;
    return 256 * r / z;
}
static void scan2(uint32_t fr, const uint8_t *ram)
{
    static ship_t sh[NOSH];
    static int ptype[NOSH + 2], palive[NOSH + 2];
    static double ppx[NOSH + 2];
    int n = ships_read(ram, sh), view = ram[0x0E77], alive = 0;
    if (ram[0x81] != 0) { printf("%u -1 0 0\n", fr); return; }      // (not a space view)
    double big = 0;
    char kills[128] = "";
    int ct[NOSH + 2] = {0}, ca[NOSH + 2] = {0};
    double cp[NOSH + 2] = {0};
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        int k = s->slot;
        if (k < 0 || k >= NOSH + 2) continue;
        double px = drawn_px(s, view);
        ct[k] = s->type, ca[k] = !ship_dead(s), cp[k] = px;
        if (ship_dead(s) && ptype[k] == s->type && palive[k] && s->type != 1 && ((s->newb & NEWB_HOSTILE) || s->type >= 29) && strlen(kills) < 100) {
            char b[16];
            snprintf(b, sizeof b, " K%.0f", fmax(px, ppx[k]));
            strcat(kills, b);
        }
        if (ship_dead(s) || !is_enemy(s)) continue;
        alive++;
        if (px > big) big = px;
    }
    for (int k = 0; k < NOSH + 2; k++) ptype[k] = ct[k], palive[k] = ca[k], ppx[k] = cp[k];
    printf("%u %d %.1f %d%s\n", fr, view, big, alive, kills);
}
static void scan(uint32_t fr, const uint8_t *ram)
{
    static ship_t sh[NOSH];
    int n = ships_read(ram, sh), k = 0, view = ram[0x0E77];
    double score = 0;
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_in_space(s) || s->type == 1) continue;       // (not missiles)
        double x, y, z;
        switch (view) {
        case 1: x = -s->x; y = s->y; z = -s->z; break;
        case 2: x = s->z; y = s->y; z = -s->x; break;
        case 3: x = -s->z; y = s->y; z = s->x; break;
        default: x = s->x; y = s->y; z = s->z; break;
        }
        if (z <= 0 || fabs(x) > z || fabs(y) > z) continue;
        const shiptype_t *t = ship_type(s->type);
        double r = t ? sqrt(t->area) : 60;
        k++;
        score += r * 256 / (z + r);
    }
    printf("%u %d %d %.1f\n", fr, view, k, score);
}
typedef struct beeb_t beeb_t;
beeb_t *beeb_new(const char *roms, const char *disc_path);
void beeb_frames(beeb_t *b, int n);
uint8_t *beeb_ram(beeb_t *b);
void beeb_key(beeb_t *b, int code, int down);
void beeb_analogue(beeb_t *b, int channel, int value);
int beeb_shot(beeb_t *b, uint8_t *out, int frames);
int beeb_shot_height(void);
size_t beeb_state_size(void);
void beeb_load(beeb_t *b, const uint8_t *buf);

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "film ROMDIR DISC FILMDIR FROM TO [STEP]\n"); return 1; }
    const char *dir = argv[3];
    uint32_t from = (uint32_t) atol(argv[4]), to = (uint32_t) atol(argv[5]), step = argc > 6 ? (uint32_t) atol(argv[6]) : 1;
    // the last snapshot at or before FROM
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return 1; }
    struct dirent *e;
    long best = -1;
    while ((e = readdir(d))) {
        unsigned f;
        if (sscanf(e->d_name, "%u.snap", &f) == 1 && f <= from && (long) f > best) best = f;
    }
    closedir(d);
    if (best < 0) { fprintf(stderr, "no snapshot before frame %u\n", from); return 1; }
    beeb_t *B = beeb_new(argv[1], argv[2]);
    if (!B) { fprintf(stderr, "beeb_new failed\n"); return 1; }
    char path[1024];
    snprintf(path, sizeof path, "%s/%09ld.snap", dir, best);
    FILE *f = fopen(path, "rb");
    uint32_t hdr[2];
    int32_t adc[4];
    if (!f || fread(hdr, 4, 2, f) != 2 || hdr[0] != 0x4D4C4946 || hdr[1] != beeb_state_size() || fread(adc, 4, 4, f) != 4) {
        fprintf(stderr, "bad snapshot %s\n", path);
        return 1;
    }
    uint8_t *st = malloc(hdr[1]);
    if (fread(st, 1, hdr[1], f) != hdr[1]) return 1;
    fclose(f);
    beeb_load(B, st);
    for (int i = 0; i < 4; i++) if (adc[i] >= 0) beeb_analogue(B, i, adc[i]);
    snprintf(path, sizeof path, "%s/inputs.bin", dir);
    FILE *in = fopen(path, "rb");
    if (!in) { perror(path); return 1; }
    struct { uint32_t frame; uint8_t type, a; uint16_t v; } r;
    int have = 0;
    #define NEXT() (have = fread(&r.frame, 4, 1, in) == 1 && fread(&r.type, 1, 1, in) == 1 && fread(&r.a, 1, 1, in) == 1 && fread(&r.v, 2, 1, in) == 1)
    NEXT();
    while (have && r.frame < (uint32_t) best) NEXT();
    int h = beeb_shot_height();
    uint8_t *px = malloc(2048 * (size_t) h), *rgb = NULL;
    static const uint8_t PAL[8][3] = {{0,0,0},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
    fprintf(stderr, "from snapshot %ld\n", best);
    for (uint32_t fr = (uint32_t) best; fr <= to; fr++) {
        while (have && r.frame == fr) {
            if (r.type == 0) beeb_key(B, r.a, r.v);
            else beeb_analogue(B, r.a, r.v);
            NEXT();
        }
        if (getenv("SCAN")) {
            beeb_frames(B, 1);
            if (fr >= from) { if (atoi(getenv("SCAN")) == 2) scan2(fr + 1, beeb_ram(B)); else scan(fr + 1, beeb_ram(B)); }
        } else if (fr >= from && (fr - from) % step == 0) {
            int w = beeb_shot(B, px, 1);
            if (!rgb) { rgb = malloc((size_t) w * h * 3); fprintf(stderr, "%dx%d\n", w, h); }
            for (int i = 0; i < w * h; i++) memcpy(rgb + i * 3, PAL[px[i] & 7], 3);
            fwrite(rgb, 3, (size_t) w * h, stdout);
        } else beeb_frames(B, 1);
    }
    if (getenv("CHECK")) {     // CHECK=dir/(TO+1).snap: is the replay's RAM the career's?
        FILE *g = fopen(getenv("CHECK"), "rb");
        beeb_t *C = beeb_new(argv[1], argv[2]);
        if (!g || fseek(g, 8 + 16, SEEK_SET) || fread(st, 1, hdr[1], g) != hdr[1]) { fprintf(stderr, "bad CHECK\n"); return 1; }
        fclose(g);
        beeb_load(C, st);
        int diff = 0;
        for (int i = 0; i < 0x8000; i++) diff += beeb_ram(B)[i] != beeb_ram(C)[i];
        fprintf(stderr, "CHECK: %d bytes of main RAM differ\n", diff);
    }
    return 0;
}

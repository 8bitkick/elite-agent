// The page's machine and player (core_wasm.c, player_wasm.c) built natively, with the Tube: a career in Curious mode
// (or MODE=0/1), its log on stdout (LOG=2 for more), PPM screenshots every SHOT frames; KEYS=1 shows the keys and
// the stick, PEEK=n a few of the game's bytes every n frames, NOTUBE=1 runs the disc on the Master alone. Build: see README.md.
//   native_run <roms dir> <tube rom> <disc> <frames>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
int core_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
void core_tube(const uint8_t *rom);
int core_start(int skew, int mode);
void core_frame(int draw);
void core_key(int code, int down);
void core_analogue(int channel, int value);
uint32_t *core_rgba(void);
int core_width(void), core_height(void), core_tube_hooks(void);
uint32_t wasm_frame_no(void);
double wasm_tally(void);
static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = calloc(1, *n + 256); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b;
}
void js_key(int code, int down) { if (getenv("KEYS")) printf("%7u key %02x %d\n", wasm_frame_no(), code, down); core_key(code, down); }
void js_analogue(int ch, int v) { if (getenv("KEYS")) printf("%7u adc %d %d\n", wasm_frame_no(), ch, v); core_analogue(ch, v); }
void js_log(int level, const char *line, int len) { if (level <= (getenv("LOG") ? atoi(getenv("LOG")) : 1)) printf("%7u %.*s\n", wasm_frame_no(), len, line); }
void js_note(const char *json, int len) { printf("%7u NOTE %.*s\n", wasm_frame_no(), len, json); }
int main(int argc, char **argv) {
    static const char *names[] = {"mos", "dfs", "viewsht", "edit", "basic4", "adfs", "view", "terminal"};
    const uint8_t *roms[8]; size_t n;
    for (int i = 0; i < 8; i++) { char p[512]; snprintf(p, sizeof p, "%s/%s.rom", argv[1], names[i]); roms[i] = slurp(p, &n); }
    const uint8_t *trom = slurp(argv[2], &n);
    uint8_t *disc = slurp(argv[3], &n);
    core_boot(roms, disc, (uint32_t) n);
    if (!getenv("NOTUBE")) core_tube(trom);
    int mode = getenv("MODE") ? atoi(getenv("MODE")) : 2;
    int ok = core_start(13, mode);
    printf("core_start: %d, hooks %08x\n", ok, core_tube_hooks());
    { extern uint8_t *core_tube_mem(void); uint8_t *pm = core_tube_mem(); printf("F_FRAME:"); for (int i = 0; i < 16; i++) printf(" %02x", pm[0x7FCE + i]); printf("  @80d8:"); for (int i = 0; i < 8; i++) printf(" %02x", pm[0x80d8 + i]); printf("\n"); }
    if (!ok) return 1;
    int frames = atoi(argv[4]), shot = getenv("SHOT") ? atoi(getenv("SHOT")) : 0;
    for (int f = 1; f <= frames; f++) {
        int draw = shot && (f % shot == 0 || f % shot == shot - 1);
        core_frame(draw);
        if (getenv("PEEK") && f % atoi(getenv("PEEK")) == 0) { extern uint8_t *wasm_ram(void); uint8_t *r = wasm_ram(); printf("%7d QQ11 %02x QQ12 %02x MCNT %02x JSTX %02x JSTY %02x JSTK %02x ALP1 %02x BET1 %02x\n", f, r[0x81], r[0x87], r[0x84], r[0xC1], r[0xC2], r[0x281C], r[0x31], r[0x2B]); }
        if (shot && f % shot == 0) {
            char p[64]; snprintf(p, sizeof p, "run%07d.ppm", f);
            FILE *o = fopen(p, "wb"); int W = core_width(), H = core_height(); fprintf(o, "P6 %d %d 255\n", W, H);
            uint32_t *px = core_rgba();
            for (int i = 0; i < W * H; i++) { uint8_t c[3] = {px[i] & 255, (px[i] >> 8) & 255, (px[i] >> 16) & 255}; fwrite(c, 1, 3, o); }
            fclose(o);
        }
    }
    { extern int elite_detect(int, const uint8_t *); printf("tally %.2f after %d frames; player frame %u, version %d\n", wasm_tally(), frames, wasm_frame_no(), elite_detect(0, 0)); }
    return 0;
}

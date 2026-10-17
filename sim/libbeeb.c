// The BBC Master (Kieran Connell's core, from the rp2350-bbc-micro submodule) as a library for the
// development build: the lab and run_sim step it a frame at a time, read its RAM, press keys and
// save/restore the whole machine. No video.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#define RENDER_INDEXED 1
#include "machine.c"
#include "teletext.c"
#include "render.c"

typedef struct {
    struct machine m;
    struct disc disc;
} beeb_t;

static const uint8_t *load(const char *dir, const char *name, size_t want)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = calloc(1, want);
    size_t n = fread(buf, 1, want, f);
    fclose(f);
    (void)n;
    return buf;
}

// where the emulator is (its roms/ and discs/, fetched by setup.sh): $ELITE_EMU, or the submodule
// beside the directory this program is in (cplayer/../rp2350-bbc-micro)
const char *beeb_emu_dir(void)
{
    static char dir[1024];
    if (getenv("ELITE_EMU")) return getenv("ELITE_EMU");
    ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 32);
    if (n <= 0) return "rp2350-bbc-micro";
    dir[n] = 0;
    char *s = strrchr(dir, '/');
    if (s) *s = 0;
    strcat(dir, "/../rp2350-bbc-micro");
    return dir;
}

beeb_t *beeb_new(const char *roms, const char *disc_path)
{
    beeb_t *b = calloc(1, sizeof *b);
    struct machine *m = &b->m;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) m->swram[i] = calloc(0x4000, 1);
    m->mos = load(roms, "mos.rom", 16384);
    static const char *names[] = {"dfs.rom", "viewsht.rom", "edit.rom", "basic4.rom", "adfs.rom", "view.rom", "terminal.rom"};
    for (int i = 0; i < 7; i++) m->rom[0x9 + i] = load(roms, names[i], 16384);
    if (!m->mos || !m->rom[9]) return NULL;
    m->vs = NULL;
    machine_reset(m);
    FILE *f = fopen(disc_path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *image = calloc(1, (size_t)size + 256);
    if (fread(image, 1, (size_t)size, f) != (size_t)size) return NULL;
    fclose(f);
    uint32_t *pulses = calloc((size_t)DISC_TRACKS * DISC_BYTES_PER_TRACK, sizeof(uint32_t));
    disc_init(&b->disc, pulses, 1, 1);
    disc_load_ssd(&b->disc, image, (uint32_t)((size + 255) & ~255));
    free(image);
    machine_set_disc(m, 0, &b->disc);
    machine_autoboot(m);
    return b;
}

void beeb_frames(beeb_t *b, int n)
{
    struct machine *m = &b->m;
    for (int i = 0; i < n; i++) machine_run(m, m->cpu.cycles + 40000);
}

uint8_t *beeb_ram(beeb_t *b) { return b->m.ram; }
uint64_t beeb_cycles(beeb_t *b) { return b->m.cpu.cycles; }

void beeb_key(beeb_t *b, int code, int down)
{
    machine_queue_key(&b->m, b->m.synced + 2000, (code >> 4) & 7, code & 15, down);
}

// A snapshot: the saving machine's address (to work out where its internal pointers pointed),
// the struct, then the sideways RAM. Portable between instances and processes: after the copy
// every pointer is put back to this instance's own (callbacks, discs, ROMs, page tables).
size_t beeb_state_size(void) { return 8 + sizeof(struct machine) + MACHINE_SWRAM_SLOTS * 0x4000; }

void beeb_save(beeb_t *b, uint8_t *buf)
{
    uint64_t addr = (uint64_t)(uintptr_t)&b->m;
    memcpy(buf, &addr, 8);
    memcpy(buf + 8, &b->m, sizeof(struct machine));
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) memcpy(buf + 8 + sizeof(struct machine) + i * 0x4000, b->m.swram[i], 0x4000);
}

void beeb_load(beeb_t *b, const uint8_t *buf)
{
    struct machine *m = &b->m;
    uint64_t old_addr;
    memcpy(&old_addr, buf, 8);
    const struct machine *old = (const struct machine *)(buf + 8);
    // what belongs to this instance
    uint8_t *sw[MACHINE_SWRAM_SLOTS];
    const uint8_t *rom[16], *mos = m->mos;
    memcpy(sw, m->swram, sizeof sw);
    memcpy(rom, m->rom, sizeof rom);
    struct via sys = m->sysvia, usr = m->usrvia;
    struct fdc fdc = m->fdc;
    // where the old one's pointers pointed (relative to it)
    int rset = old->cpu.rd_tab == old->cpu.rd_tabs[1];
    int wset = old->cpu.wr_tab == old->cpu.wr_tabs[1];
    int cur = -1;
    if (old->fdc.cur) {
        uintptr_t d1 = (uintptr_t)old_addr + offsetof(struct machine, fdc) + offsetof(struct fdc, drive) + sizeof(struct fdc_drive);
        cur = (uintptr_t)old->fdc.cur == d1 ? 1 : 0;
    }
    memcpy(m, old, sizeof(struct machine));
    memcpy(m->swram, sw, sizeof sw);
    memcpy(m->rom, rom, sizeof rom);
    m->mos = mos;
    m->vs = NULL;
    m->cpu.m = m;
    m->cpu.rd_tabs[0] = m->rd_tab[0];
    m->cpu.rd_tabs[1] = m->rd_tab[1];
    m->cpu.wr_tabs[0] = m->wr_tab[0];
    m->cpu.wr_tabs[1] = m->wr_tab[1];
    m->cpu.rd_tab = m->cpu.rd_tabs[rset];
    m->cpu.wr_tab = m->cpu.wr_tabs[wset];
    m->sysvia.port_cb = sys.port_cb;
    m->sysvia.ctx = sys.ctx;
    m->usrvia.port_cb = usr.port_cb;
    m->usrvia.ctx = usr.ctx;
    // the 1770: this instance's callbacks and discs, the old one's state
    for (int i = 0; i < 2; i++) m->fdc.drive[i].disc = fdc.drive[i].disc;
    m->fdc.cur = cur < 0 ? NULL : &m->fdc.drive[cur];
    m->fdc.set_nmi = fdc.set_nmi;
    m->fdc.ctx = fdc.ctx;
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) memcpy(sw[i], buf + 8 + sizeof(struct machine) + i * 0x4000, 0x4000);
    remap_all(m);
}

// A screenshot: run two frames with the video stream on, through the core's renderer, into
// out (RENDER_WIDTH x RENDER_HEIGHT bytes of colour numbers 0-15). Returns the width.
static struct vstream s_stream;
static struct render s_render;
static render_px *s_canvas;

static void consume(struct render *r, struct vstream *vs)
{
    while (vs_available(vs) > 0) {
        uint16_t w = vs->ring[vs->tail & (VS_RING - 1)];
        if (!(w & VS_EVENT)) {
            int n = 0;
            while (n < vs_available(vs) && !(vs->ring[(vs->tail + n) & (VS_RING - 1)] & VS_EVENT)) n++;
            render_consume_n(r, vs, n);
            continue;
        }
        vs->tail++;
        render_word(r, w);
    }
}

int beeb_shot(beeb_t *b, uint8_t *out, int frames)
{
    struct machine *m = &b->m;
    if (!s_canvas) {
        s_canvas = calloc(RENDER_WIDTH * RENDER_HEIGHT, sizeof(render_px));
        render_init(&s_render, s_canvas);
    }
    memset(&s_stream, 0, sizeof s_stream);
    m->vs = &s_stream;
    for (int i = 0; i < frames; i++) {
        uint64_t until = m->cpu.cycles + 40000;
        while (m->cpu.cycles < until) {
            machine_run(m, m->cpu.cycles + 2000 < until ? m->cpu.cycles + 2000 : until);
            consume(&s_render, &s_stream);
        }
    }
    m->vs = NULL;
    for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) out[i] = (uint8_t)s_canvas[i];
    return RENDER_WIDTH;
}

int beeb_shot_height(void) { return RENDER_HEIGHT; }

// the disc in drive 0 as an .ssd image, read back out of its track data (what the game saved to it)
int beeb_disc_dump(beeb_t *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t s[256];
    int bad = 0;
    for (int t = 0; t < 80; t++)
        for (int side = 0; side < b->disc.sides; side++)
            for (int sc = 0; sc < 10; sc++) {
                if (!disc_read_sector(&b->disc, side, t, sc, s)) { memset(s, 0, 256); bad++; }
                fwrite(s, 1, 256, f);
            }
    fclose(f);
    return bad;
}

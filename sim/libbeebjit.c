// The BBC Model B (beebjit, the rp2350-bbc-micro port's core and embedded platform layer: the Presto's
// Model B) as a library for the development build, with the same calls as sim/libbeeb.c (the Master):
// run_sim_b steps it a frame (40,000 cycles) at a time, reads its RAM, presses keys and moves the joystick.
//
// beebjit runs a machine to completion; The machine runs
// on its own stack (ucontext) and a timer every 40,000 cycles swaps back to the caller: one thread, in lockstep.
// Emulated time runs flat out (its sleeps only move a fake clock). No snapshots (beeb_save/beeb_load) and no screenshots yet.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ucontext.h>

#include "beebjit_embedded.h"
#include "beebjit_host.h"
#include "bbc.h"
#include "keyboard.h"
#include "adc.h"
#include "cpu_driver.h"
#include "timing.h"
#include "disc_drive.h"

typedef struct beeb_t {
    struct bbc_struct *bbc;
    ucontext_t emu, caller;
    uint8_t *stack;
    int finished;
    uint64_t frames;
} beeb_t;

static beeb_t *s_b;
static uint64_t s_fake_us;

// ---- beebjit_host.h: emulated time only (flat out), no terminal
uint64_t beebjit_host_time_us(void) { return s_fake_us; }
void beebjit_host_sleep_us(uint64_t us) { s_fake_us += us; }
int beebjit_host_term_read(uint8_t *p) { (void) p; return 0; }
void beebjit_host_term_write(uint8_t b) { (void) b; }
static uint8_t s_big[8u << 20] __attribute__((aligned(16)));
size_t beebjit_host_big_region(uint8_t **p) { *p = s_big; return sizeof s_big; }
void *beebjit_host_fixed_region(void *a, size_t n) { (void) a; (void) n; return NULL; }

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

// a frame: 40,000 cycles of the 2 MHz clock (as libbeeb.c's), by a timer (fast mode sends no vsyncs)
static uint32_t s_timer;
static void on_frame(void *p)
{
    (void) p;
    struct timing_struct *t = bbc_get_timing(s_b->bbc);
    (void) timing_stop_timer(t, s_timer);
    (void) timing_start_timer_with_value(t, s_timer, 40000);
    s_b->frames++;
    swapcontext(&s_b->emu, &s_b->caller);
    keyboard_read_queue(bbc_get_keyboard(s_b->bbc));     // (the keys the caller pressed, now)
}

static void emu_main(void)
{
    beebjit_embedded_run(s_b->bbc);
    s_b->finished = 1;
    fprintf(stderr, "beebjit: the machine stopped\n");
    swapcontext(&s_b->emu, &s_b->caller);
    for (;;) swapcontext(&s_b->emu, &s_b->caller);
}

static uint8_t s_matrix_keys[0x80];
static const char *s_copy;
static void remove_copy(void) { if (s_copy) remove(s_copy); }     // BBC matrix code (row << 4 | column) -> beebjit key

// roms: the directory with os/os12.rom and general/{dfs09,basic2}.rom (the emulator's roms/)
beeb_t *beeb_new(const char *roms, const char *disc_path)
{
    if (s_b) return NULL;       // (one machine a process: beebjit's embedded layer is single-instance)
    static char os[600], dfs[600], basic[600];
    snprintf(os, sizeof os, "%s/os/os12.rom", roms);
    snprintf(dfs, sizeof dfs, "%s/general/dfs09.rom", roms);
    snprintf(basic, sizeof basic, "%s/general/basic2.rom", roms);
    beeb_t *b = calloc(1, sizeof *b);
    s_b = b;
    struct beebjit_embedded_config c;
    memset(&c, 0, sizeof c);
    c.p_os_rom = os;
    c.p_rom_names[15] = basic;
    c.p_rom_names[14] = dfs;
    c.p_opt_flags = "video:border-chars=0,video:no-deinterlace-bitmap";
    c.p_log_flags = "";
    c.fast_flag = 0;          // (fast mode stops the vsyncs: docked Elite hangs; emulated time runs flat out anyway)
    c.autoboot_flag = 1;
    // the disc: a copy (the game saves commanders to it, and the copy takes the writes: the image given
    // isn't changed), in $TMPDIR
    static char copy[600];
    {
        const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
        snprintf(copy, sizeof copy, "%s/run_sim_b-%d.ssd", tmp, (int) getpid());
        FILE *in = fopen(disc_path, "rb"), *out = in ? fopen(copy, "wb") : NULL;
        if (!in || !out) { fprintf(stderr, "can't copy the disc %s to %s\n", disc_path, copy); return NULL; }
        static uint8_t buf[1 << 18];
        size_t n = fread(buf, 1, sizeof buf, in);
        fwrite(buf, 1, n, out);
        fclose(in), fclose(out);
        atexit(remove_copy);
    }
    c.p_disc_names[0] = copy;
    s_copy = copy;
    b->bbc = beebjit_embedded_create(&c);
    if (!b->bbc) return NULL;
    bbc_replace_disc(b->bbc, copy, 0, 1);      // (writeable: the 8271's writes go to the copy)
    for (uint32_t key = 1; key < k_keyboard_key_SPECIAL_release_all; key++) {
        int32_t row, col;
        keyboard_bbc_key_to_rowcol((uint8_t) key, &row, &col);
        if (row >= 0 && row < 8 && col >= 0 && col < 16 && !s_matrix_keys[row << 4 | col]) s_matrix_keys[row << 4 | col] = (uint8_t) key;
    }
    s_timer = timing_register_timer(bbc_get_timing(b->bbc), "libbeebjit_frame", on_frame, NULL);
    (void) timing_start_timer_with_value(bbc_get_timing(b->bbc), s_timer, 40000);
    b->stack = malloc(1 << 20);
    getcontext(&b->emu);
    b->emu.uc_stack.ss_sp = b->stack;
    b->emu.uc_stack.ss_size = 1 << 20;
    b->emu.uc_link = NULL;
    makecontext(&b->emu, emu_main, 0);
    return b;
}

void beeb_frames(beeb_t *b, int n)
{
    for (int i = 0; i < n && !b->finished; i++) swapcontext(&b->caller, &b->emu);
}

uint8_t *beeb_ram(beeb_t *b) { return bbc_get_mem_read(b->bbc); }
uint64_t beeb_cycles(beeb_t *b) { return b->frames * 40000; }

void beeb_key(beeb_t *b, int code, int down)
{
    uint8_t k = s_matrix_keys[code & 0x7F];
    if (!k) return;
    struct keyboard_struct *kb = bbc_get_keyboard(b->bbc);
    if (down) keyboard_system_key_pressed(kb, k);
    else keyboard_system_key_released(kb, k);
}

void beeb_analogue(beeb_t *b, int channel, int value) { adc_set_channel_value(bbc_get_adc(b->bbc), (uint32_t) (channel & 3), (uint16_t) value); }

// not yet on the Model B: snapshots, screenshots, the disc read back
size_t beeb_state_size(void) { return 16; }
void beeb_save(beeb_t *b, uint8_t *buf) { (void) b; memset(buf, 0, 16); }
void beeb_load(beeb_t *b, const uint8_t *buf) { (void) b; (void) buf; fprintf(stderr, "beeb_load: no snapshots on the Model B build\n"); }
int beeb_shot(beeb_t *b, uint8_t *out, int frames) { beeb_frames(b, frames); memset(out, 0, 640 * 512); return 640; }
int beeb_shot_height(void) { return 512; }
int beeb_disc_dump(beeb_t *b, const char *path) { (void) b; (void) path; return -1; }

// the machine (machine_hook.h's model): a Model B
int beeb_reboot(beeb_t *b) { (void) b; return -1; }      // (not on the Model B yet: RESETAT)

int beeb_model(void) { return 1; }
// the disc drive spinning (run_sim's time split: the game waiting on the disc, or just after)
int beeb_disc_busy(beeb_t *b) { return disc_drive_is_spinning(bbc_get_drive_0(b->bbc)); }

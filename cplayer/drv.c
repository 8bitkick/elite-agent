#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "drv.h"

player_io_t *IO;
const char *g_fail;
uint32_t g_gen = 1;
int g_in_flight;
keyset_t g_flight;
static keyset_t s_held = {{0}, 0xff};       // (n = 0xff: nothing sent yet)
static uint32_t s_taps_sent;

int ks_has(const keyset_t *s, uint8_t k) {
    for (int i = 0; i < s->n; i++) if (s->k[i] == k) return 1;
    return 0;
}
void ks_add(keyset_t *s, uint8_t k) { if (!ks_has(s, k) && s->n < sizeof s->k) s->k[s->n++] = k; }
void ks_del(keyset_t *s, uint8_t k) {
    for (int i = 0; i < s->n; i++) if (s->k[i] == k) { s->k[i] = s->k[--s->n]; return; }
}

static int ks_same(const keyset_t *a, const keyset_t *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (!ks_has(b, a->k[i])) return 0;
    return 1;
}

void drv_reset_keys(void) { memset(&s_held, 0, sizeof s_held); ks_clear(&g_flight); s_taps_sent = IO ? IO->taps_done : 0; }   // (a new machine: nothing held, no taps owed)    // (a new machine: nothing held)
int g_phase;
int g_hold_ctrl;     // CTRL held too, whatever else is (a forced mis-jump: flight.c)
void hold(const keyset_t *s) {
    keyset_t k = *s;
    if (g_hold_ctrl) ks_add(&k, K_CTRL);
    if (ks_same(&k, &s_held)) return;
    s_held = k;
    IO->hold(IO->ctx, k.k, k.n);
}

void hold_none(void) {
    keyset_t e = {{0}, 0};
    ks_clear(&g_flight);
    hold(&e);
}

static void vlog(int level, const char *fmt, va_list ap) {
    if (!IO->log) return;
    char buf[200];
    vsnprintf(buf, sizeof buf, fmt, ap);
    IO->log(IO->ctx, level, buf);
}
void logf_(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(PLOG_INFO, fmt, ap); va_end(ap); }
void dbgf_(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(PLOG_DEBUG, fmt, ap); va_end(ap); }
void status_(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(PLOG_STATUS, fmt, ap); va_end(ap); }

// rank as the game names it, from the kill tally; legal status from FIST
const char *rank_name(void) {
    int hi = R(A_TALLY + 1), lo = R(A_TALLY);
    if (hi >= 25) return "Elite";
    if (hi >= 10) return "Deadly";
    if (hi >= 2) return "Dangerous";
    if (hi || lo >= 128) return "Competent";
    if (lo >= 64) return "Above Average";
    if (lo >= 32) return "Average";
    if (lo >= 16) return "Poor";
    if (lo >= 8) return "Mostly Harmless";
    return "Harmless";
}
const char *legal_name(void) { return legal() >= 50 ? "Fugitive" : legal() ? "Offender" : "Clean"; }

void note(const char *fmt, ...) {
    if (!IO->note) return;
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    IO->note(IO->ctx, buf);
}

int key_for_char(char c) {
    static const uint8_t letters[26] = {K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M, K_N, K_O,
                                        K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z};
    static const uint8_t digits[10] = {K_0, K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9};
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == ' ') return K_SPACE;
    return -1;
}

static int taps_done_by(uint32_t target) { return (int32_t)(IO->taps_done - target) >= 0; }

static struct { CO; uint32_t target; } c_press;
PT_THREAD(press(uint8_t key, int down, int up)) {
    CO_BEGIN(c_press);
    IO->tap(IO->ctx, key, down, up);
    c_press.target = ++s_taps_sent;
    CO_WAIT(c_press, taps_done_by(c_press.target));
    CO_END(c_press);
}

static struct { CO; uint32_t target; } c_type;
PT_THREAD(type_text(const char *text)) {
    CO_BEGIN(c_type);
    for (const char *p = text; *p; p++) {
        int k = key_for_char(*p);
        if (k >= 0) {
            IO->tap(IO->ctx, (uint8_t) k, 3, 4);
            s_taps_sent++;
        }
    }
    c_type.target = s_taps_sent;
    CO_WAIT(c_type, taps_done_by(c_type.target));
    CO_END(c_type);
}

static struct { CO; uint32_t until; } c_idle;
PT_THREAD(idle(int frames)) {
    CO_BEGIN(c_idle);
    c_idle.until = now() + (uint32_t) frames;
    CO_WAIT(c_idle, expired(c_idle.until));
    CO_END(c_idle);
}

static struct { CO; uint32_t until; } c_wait;
PT_THREAD(wait_for(cond_fn cond, float seconds, const char *what)) {
    CO_BEGIN(c_wait);
    c_wait.until = deadline(seconds);
    CO_WAIT(c_wait, cond() || expired(c_wait.until));
    if (!cond()) CO_FAIL(c_wait, what);
    CO_END(c_wait);
}

static struct { CO; uint32_t until, step, target; } c_hu;
PT_THREAD(hold_until(uint8_t key, cond_fn cond, float seconds, const char *what)) {
    CO_BEGIN(c_hu);
    c_hu.until = deadline(seconds);
    for (;;) {
        IO->tap(IO->ctx, key, 8, 2);
        c_hu.target = ++s_taps_sent;
        CO_WAIT(c_hu, taps_done_by(c_hu.target));
        c_hu.step = now() + 25;
        CO_WAIT(c_hu, cond() || expired(c_hu.step));
        if (cond()) break;
        if (expired(c_hu.until)) {
            logf_("%s: no (key %02x, screen %d, witchspace %d, flight keys held %d, mcnt %d, held ctrl %d)", what, key, view(), witchspace(), g_flight.n, mcnt(), g_hold_ctrl);
            CO_FAIL(c_hu, what);
        }
    }
    CO_END(c_hu);
}

static struct { CO; uint32_t until; int n, lx, ly; } c_settle;
PT_THREAD(settle(cond_fn cond, int frames, float seconds, const char *what)) {
    CO_BEGIN(c_settle);
    c_settle.until = deadline(seconds);
    c_settle.n = 0;
    c_settle.lx = c_settle.ly = -1;
    while (c_settle.n < frames) {
        CO_YIELD(c_settle);
        if (cond() && cursor_x() == c_settle.lx && cursor_y() == c_settle.ly) c_settle.n++;
        else c_settle.n = 0;
        c_settle.lx = cursor_x();
        c_settle.ly = cursor_y();
        if (expired(c_settle.until)) CO_FAIL(c_settle, what);
    }
    CO_END(c_settle);
}

int g_frozen;
uint32_t g_pause_frames;
double g_ipf = 0.5;            // main-loop iterations a frame, smoothed (MCNT counts down once an iteration)
void drv_frame_start(void) {
    {
        static int last = -1;
        int m = mcnt(), dm = (last - m) & 255;
        if (last >= 0) g_ipf = 0.98 * g_ipf + 0.02 * (dm < 8 ? dm : 0);
        last = m;
        // paused (the main loop isn't running, so its counter stands still): frames counted from the eighth
        // (a busy loop can take 6), with those 8 added then, as the time the game stood still
        // (counted as paused only while we're toggling: a busy loop among many ships can stand still 8 frames too)
        extern int g_pausing;
        g_frozen = dm == 0 ? g_frozen + 1 : 0;
        if (g_pausing && g_frozen == 8) g_pause_frames += 8;
        else if (g_pausing && g_frozen > 8) g_pause_frames++;
    }
    // (dying() alone isn't a death: MCNT passes 0xFF every 256 iterations, and with the laser just fired
    // that matches. In the death sequence (DEATH's debris loop) it holds; so: held for half a second. The
    // title screen counts only after that: docking shows the hangar with QQ12 set and QQ11 0, which read
    // as the title, and we "died" and reloaded the save with the ship safely docked)
    static int dying_frames, saw_death;
    dying_frames = dying() ? dying_frames + 1 : 0;
    if (!g_in_flight) saw_death = 0;
    if (dying_frames >= 25) saw_death = 1;
    if (g_in_flight && (dying_frames >= 25 || (title() && saw_death))) {
        g_in_flight = 0;
        g_fail = "died";
    }
}

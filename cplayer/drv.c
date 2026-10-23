#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "drv.h"
#include "keys.h"
#include "state.h"
#include "fair.h"

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
// The joystick (JSTK set, the platform with an analogue port): roll and pitch go to the ADC instead
// of the keys. steer() says where it wants the stick (g_want_jx/jy, this frame); keys from anything
// else move a virtual stick as the keyboard would. Calibrated in the simulator: JSTX = 3 + ch0 * 250 /
// 65536, JSTY = 1 + ch1 * 253 / 65536
int g_want_jx = -1, g_want_jy = -1;
static int joystick_on(void) { return IO && IO->analogue && R(A_JSTK) != 0; }
// the stick out to the port: JSTX/JSTY units (as the game will read them) to ADC values
static void analogue_out(int jx, int jy) {
    // (the game's own options flip what the port reads: the calibration's with JSTGY &2C5A set, as
    // RESET leaves it, and JSTE &2C5B clear)
    if (!ev_master()) {
        // (the disc version: JSTX/JSTY are the ADC's high byte (OSBYTE 128), EOR JSTE, and JSTY EOR JSTGY)
        jx = jx < 1 ? 1 : jx > 254 ? 254 : jx, jy = jy < 1 ? 1 : jy > 254 ? 254 : jy;
        if (R(A_JSTE)) jx ^= 0xFF, jy ^= 0xFF;
        if (R(A_JSTGY)) jy ^= 0xFF;
        IO->analogue(IO->ctx, 0, (uint16_t) (jx << 8 | 0x80));
        IO->analogue(IO->ctx, 1, (uint16_t) (jy << 8 | 0x80));
        return;
    }
    if (R(A_JSTE)) jx = 256 - jx, jy = 256 - jy;
    if (!R(A_JSTGY)) jy = 256 - jy;
    jx = jx < 3 ? 3 : jx > 253 ? 253 : jx;
    jy = jy < 1 ? 1 : jy > 254 ? 254 : jy;
    long vx = (long) (jx - 3) * 65536 / 250, vy = (long) (jy - 1) * 65536 / 253;      // (65536 at the top: clamp,
    IO->analogue(IO->ctx, 0, (uint16_t) (vx > 0xFFFF ? 0xFFFF : vx));                // not wrap to 0: full right
    IO->analogue(IO->ctx, 1, (uint16_t) (vy > 0xFFFF ? 0xFFFF : vy));                // read as full left)
}
// fair mode: where we've put the stick (we know our own hand), and where the hand is going
int g_own_jx = -1, g_own_jy = -1;
static int s_want_sx = -1, s_want_sy = -1;
static double s_act_sx = 128, s_act_sy = 128, s_nx, s_ny;
static void stick_out(keyset_t *k) {
    int jx = g_want_jx, jy = g_want_jy, rx = R(A_JSTX), ry = R(A_JSTY);
    if (g_fair && g_own_jx >= 0) rx = g_own_jx, ry = g_own_jy;
    // (off the space view, a chart or a docked screen, the stick reads as the cursor: centred unless
    // something's steering it there; left leaning, the chart's cursor drifted after a system was
    // chosen and we jumped somewhere else)
    if (R(A_QQ11) != 0) rx = ry = 128;
    if (jx < 0) jx = rx + (ks_has(k, K_ROLL_LEFT) ? 7 : 0) - (ks_has(k, K_ROLL_RIGHT) ? 7 : 0);
    if (jy < 0) jy = ry + (ks_has(k, K_DIVE) ? 14 : 0) - (ks_has(k, K_CLIMB) ? 14 : 0);
    if (g_fair & FAIR_STICK) s_want_sx = jx, s_want_sy = jy;        // (the hand moves it: drv_fair_pump)
    else {
        analogue_out(jx, jy);
        g_own_jx = jx < 3 ? 3 : jx > 253 ? 253 : jx, g_own_jy = jy < 1 ? 1 : jy > 254 ? 254 : jy;
    }
    ks_del(k, K_ROLL_LEFT), ks_del(k, K_ROLL_RIGHT), ks_del(k, K_DIVE), ks_del(k, K_CLIMB);
    g_want_jx = g_want_jy = -1;
}
static keyset_t s_want_keys;
static int s_have_want;
void hold(const keyset_t *s) {
    keyset_t k = *s;
    if (joystick_on()) stick_out(&k);
    else g_own_jx = g_own_jy = -1;
    if (g_hold_ctrl) ks_add(&k, K_CTRL);
    if (g_fair & FAIR_KEYS) { s_want_keys = k, s_have_want = 1; return; }    // (the fingers: drv_fair_pump)
    if (ks_same(&k, &s_held)) return;
    s_held = k;
    IO->hold(IO->ctx, k.k, k.n);
}

// fair mode, once a frame: the hand moves the stick towards where it's wanted, at most g_fair_stick_rate a
// frame, missing by a little (motor noise, more for a bigger deflection: Harris & Wolpert's signal-dependent
// noise); the fingers hold at most g_fair_key_cap keys (the most needed first), and no key changes twice
// within g_fair_key_gap frames
static uint32_t s_key_at[128], s_rng = 12345;
static double gauss(void) {         // (deterministic: the same run twice is the same run)
    double u = 0;
    for (int i = 0; i < 4; i++) { s_rng = s_rng * 1103515245u + 12345u; u += (s_rng >> 8 & 0xFFFF) / 65536.0; }
    return (u - 2) * 1.732;
}
static int key_rank(uint8_t c) {
    static const uint8_t P[] = {K_CTRL, K_FIRE, K_ECM, K_JUMP, K_SPACE, K_SLOW, K_ROLL_LEFT, K_ROLL_RIGHT, K_DIVE, K_CLIMB};
    for (int i = 0; i < (int) sizeof P; i++) if (P[i] == c) return i;
    return 20;
}
void drv_fair_pump(void) {
    if (!g_fair) return;
    if ((g_fair & FAIR_STICK) && joystick_on() && s_want_sx >= 0) {
        double dx = s_want_sx - 128, dy = s_want_sy - 128, a = 0.9;      // (the noise wanders over ~10 frames)
        s_nx = a * s_nx + sqrt(1 - a * a) * (1 + g_fair_noise * fabs(dx)) * gauss();
        s_ny = a * s_ny + sqrt(1 - a * a) * (1 + g_fair_noise * fabs(dy)) * gauss();
        double tx = s_want_sx + s_nx, ty = s_want_sy + s_ny, r = g_fair_stick_rate;
        s_act_sx += fmax(-r, fmin(r, tx - s_act_sx));
        s_act_sy += fmax(-r, fmin(r, ty - s_act_sy));
        int jx = (int) lround(s_act_sx), jy = (int) lround(s_act_sy);
        analogue_out(jx, jy);
        g_own_jx = jx < 3 ? 3 : jx > 253 ? 253 : jx, g_own_jy = jy < 1 ? 1 : jy > 254 ? 254 : jy;
    }
    if ((g_fair & FAIR_KEYS) && s_have_want) {
        keyset_t w = s_want_keys, k = {{0}, 0};
        for (int i = 1; i < w.n; i++)           // (most needed first)
            for (int j = i; j > 0 && key_rank(w.k[j]) < key_rank(w.k[j - 1]); j--) { uint8_t t = w.k[j]; w.k[j] = w.k[j - 1]; w.k[j - 1] = t; }
        int cap = g_fair_key_cap - (IO->taps_done != s_taps_sent ? 1 : 0);     // (a finger on a tapped key)
        if (w.n > cap) w.n = (uint8_t) (cap < 0 ? 0 : cap);
        int sent = s_held.n != 0xff;
        for (int i = 0; i < w.n; i++)           // wanted down: down unless it went up too recently
            if ((sent && ks_has(&s_held, w.k[i])) || IO->frame - s_key_at[w.k[i] & 127] >= (uint32_t) g_fair_key_gap) ks_add(&k, w.k[i]);
        if (sent) for (int i = 0; i < s_held.n; i++)       // held, not wanted: up unless it went down too recently
            if (!ks_has(&w, s_held.k[i]) && IO->frame - s_key_at[s_held.k[i] & 127] < (uint32_t) g_fair_key_gap && k.n < g_fair_key_cap) ks_add(&k, s_held.k[i]);
        if (!sent || !ks_same(&k, &s_held)) {
            for (int c = 0; c < 128; c++) {
                int was = sent && ks_has(&s_held, (uint8_t) c), is = ks_has(&k, (uint8_t) c);
                if (was != is) s_key_at[c] = IO->frame;
            }
            s_held = k;
            IO->hold(IO->ctx, k.k, k.n);
        }
    }
    fair_note_out(joystick_on() ? g_own_jx : -1, joystick_on() ? g_own_jy : -1, s_held.n != 0xff && ks_has(&s_held, K_FIRE));
}
// (fair mode: a finger taps a key for at least 4 frames down and 3 up: ~7 keys a second, a fast typist)
static void tap(uint8_t key, int down, int up) {
    if (g_fair & FAIR_KEYS) { if (down < 4) down = 4; if (up < 3) up = 3; }
    IO->tap(IO->ctx, key, down, up);
}

void hold_none(void) {
    keyset_t e = {{0}, 0};
    ks_clear(&g_flight);
    hold(&e);
}

static void vlog(int level, const char *fmt, va_list ap) {
    if (!IO->log) return;
    static char buf[200];      // (static: off the stack, under the deepest calls)
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
    static char buf[300];
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
    tap(key, down, up);
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
            tap((uint8_t) k, 3, 4);
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
        tap(key, 8, 2);
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

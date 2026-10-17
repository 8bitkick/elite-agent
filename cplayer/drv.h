// The driver: frames, held keys, presses, waits . Every helper is a
// coroutine (pt.h) with its state in a static struct; CO_* wrap the pt.h macros with the failure
// rule: a helper that can't do its job sets g_fail and returns, and every CO_CALL then returns up
// to the career loop, which recovers (back to the last save). A death does the same, from
// player_frame. g_gen counts these: a coroutine started in an older generation starts afresh.
#pragma once
#include <stdint.h>
#include "pt.h"
#include "state.h"
#include "keys.h"

extern const char *g_fail;
extern uint32_t g_gen;
extern int g_in_flight;             // a title screen or the death animation now means we died

#define CO pt_t pt; uint32_t gen
#define CO_BEGIN(c) if ((c).gen != g_gen) { (c).pt.lc = 0; (c).gen = g_gen; } PT_BEGIN(&(c).pt)
#define CO_END(c) PT_END(&(c).pt)
#define CO_WAIT(c, cond) PT_WAIT_UNTIL(&(c).pt, cond)
#define CO_YIELD(c) PT_YIELD(&(c).pt)
#define CO_EXIT(c) PT_EXIT(&(c).pt)
#define CO_CALL(c, call) do { PT_CALL(&(c).pt, call); if (g_fail) PT_EXIT(&(c).pt); } while (0)
#define CO_FAIL(c, why) do { g_fail = (why); PT_EXIT(&(c).pt); } while (0)

static inline uint32_t now(void) { return IO->frame; }
static inline uint32_t deadline(float seconds) { return IO->frame + (uint32_t)(seconds * 50); }
static inline int expired(uint32_t d) { return (int32_t)(IO->frame - d) >= 0; }

// the flight keys held (and kept held through presses)
typedef struct { uint8_t k[12]; uint8_t n; } keyset_t;
extern int g_want_jx, g_want_jy;     // (drv.c: where steer() wants the joystick this frame; cleared with the keys)
static inline void ks_clear(keyset_t *s) { s->n = 0; g_want_jx = g_want_jy = -1; }
int ks_has(const keyset_t *s, uint8_t k);
void ks_add(keyset_t *s, uint8_t k);
void ks_del(keyset_t *s, uint8_t k);
extern keyset_t g_flight;
void hold(const keyset_t *s);       // exactly these held (sent only when they change)
void hold_none(void);
void drv_reset_keys(void);
extern int g_hold_ctrl;
// what the player is doing this frame (lab diagnostics): 0 other, 1 fighting, 2 on the chart,
// 3 hyperspace countdown, 4 fleeing in a countdown
extern int g_phase;

void logf_(const char *fmt, ...);   // INFO
void dbgf_(const char *fmt, ...);   // DEBUG
void status_(const char *fmt, ...); // STATUS: one short line per event
const char *rank_name(void);
const char *legal_name(void);
void note(const char *fmt, ...);    // a journal event (JSON object text)

typedef int (*cond_fn)(void);
PT_THREAD(press(uint8_t key, int down, int up));
PT_THREAD(type_text(const char *text));
PT_THREAD(idle(int frames));
PT_THREAD(wait_for(cond_fn cond, float seconds, const char *what));
// a screen key, read once per main-loop pass: tap it until cond holds
PT_THREAD(hold_until(uint8_t key, cond_fn cond, float seconds, const char *what));
// cond held and the text cursor still for `frames` frames running: the game is at a prompt
PT_THREAD(settle(cond_fn cond, int frames, float seconds, const char *what));
// player_frame's death check, and the generation bump on failure
void drv_frame_start(void);
extern double g_ipf;        // main-loop iterations a frame (measured)
extern int g_frozen;         // frames the game's main loop has stood still (paused, say)
extern uint32_t g_pause_frames;     // frames the game stood paused, all told

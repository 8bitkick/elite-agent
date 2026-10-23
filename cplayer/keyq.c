#include <string.h>
#include "keyq.h"

// (a push lands KEYQ_DELAY frames on, always: q holds only the last few frames' pushes, a hold when the keys change and
// the taps of a name typed at once (type_text: 12 letters at most). The taps wait in taps[] one at a time, and
// type_text waits for its own. 64 each, the indices kept within it: 256 were 6.9 KB of the plugin's 72)
#define QN 64
#define QM (QN - 1)
static struct { uint32_t due; uint8_t kind, n, down, up, c[16]; } q[QN];
static struct { uint8_t code, down, up; } taps[QN];
static uint8_t qh, qt, th, tt, hold_now[16], nhold, is_down[16];
static int phase = -1, left;

void keyq_reset(void) { qh = qt = th = tt = nhold = 0; phase = -1; memset(is_down, 0, sizeof is_down); }

static void push(uint32_t frame, int kind, const uint8_t *c, int n, int down, int up) {
    uint32_t due = frame + KEYQ_DELAY;
    if (qh != qt && (int32_t) (q[(qt - 1) & QM].due - due) > 0) due = q[(qt - 1) & QM].due;
    if (((qt + 1) & QM) == qh) return;       // (full: can't happen at the player's rates)
    q[qt].due = due, q[qt].kind = (uint8_t) kind, q[qt].n = (uint8_t) (n > 16 ? 16 : n);
    q[qt].down = (uint8_t) down, q[qt].up = (uint8_t) up;
    memcpy(q[qt].c, c, q[qt].n);
    qt = (qt + 1) & QM;
}
void keyq_hold(uint32_t frame, const uint8_t *codes, int n) { push(frame, 0, codes, n, 0, 0); }
void keyq_tap(uint32_t frame, uint8_t code, int down, int up) { push(frame, 1, &code, 1, down, up); }

int keyq_step(uint32_t frame, void (*key)(int code, int down)) {
    int done = 0;
    while (qh != qt && (int32_t) (q[qh].due - frame) <= 0) {
        if (q[qh].kind == 0) nhold = q[qh].n, memcpy(hold_now, q[qh].c, nhold);
        else if (((tt + 1) & QM) != th) taps[tt].code = q[qh].c[0], taps[tt].down = q[qh].down, taps[tt].up = q[qh].up, tt = (tt + 1) & QM;
        qh = (qh + 1) & QM;
    }
    uint8_t want[16] = {0};
    for (int i = 0; i < nhold; i++) want[(hold_now[i] & 127) >> 3] |= (uint8_t) (1u << (hold_now[i] & 7));
    if (th != tt) {
        if (phase < 0) phase = 0, left = taps[th].down;
        if (phase == 0) want[(taps[th].code & 127) >> 3] |= (uint8_t) (1u << (taps[th].code & 7));
        if (--left <= 0) {
            if (phase == 0) phase = 1, left = taps[th].up < 1 ? 1 : taps[th].up;
            else th = (th + 1) & QM, phase = -1, done++;
        }
    }
    for (int c = 0; c < 128; c++) {
        int w = (want[c >> 3] >> (c & 7)) & 1;
        if (w != ((is_down[c >> 3] >> (c & 7)) & 1)) {
            key(c, w);
            is_down[c >> 3] ^= (uint8_t) (1u << (c & 7));
        }
    }
    return done;
}

void keyq_release(void (*key)(int code, int down)) {
    for (int c = 0; c < 128; c++)
        if ((is_down[c >> 3] >> (c & 7)) & 1) key(c, 0);
    memset(is_down, 0, sizeof is_down);
    keyq_reset();
}

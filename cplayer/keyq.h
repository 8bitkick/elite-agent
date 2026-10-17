// The player's keys on their way to the machine, the same on every platform: holds and taps land
// KEYQ_DELAY frames after they're asked for (as over the WiFi link the rules were tuned on), in
// order; taps one at a time. keyq_step() before each emulated frame says which keys go up or down.
#pragma once
#include <stdint.h>

#define KEYQ_DELAY 2

void keyq_reset(void);
void keyq_hold(uint32_t frame, const uint8_t *codes, int n);
void keyq_tap(uint32_t frame, uint8_t code, int down, int up);
// at `frame`: calls key(code, down) for each change; returns how many taps finished
int keyq_step(uint32_t frame, void (*key)(int code, int down));
void keyq_release(void (*key)(int code, int down));     // everything up

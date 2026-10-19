// Player cost timing (PLAYER_TIMING, the sim only: run_sim's build defines it; the Presto build doesn't, and
// then these are no-ops). g_pt[i]: nanoseconds spent in section i this frame; run_sim reads and clears them.
#pragma once
#ifdef PLAYER_TIMING
#include <time.h>
static inline long long ptime_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
extern long long g_pt[8];
#define PT_START(v) long long v = ptime_ns()
#define PT_ADD(i, v) (g_pt[i] += ptime_ns() - (v))
#else
#define PT_START(v) (void) 0
#define PT_ADD(i, v) (void) 0
#endif
// sections: 0 plan_stick, 1 target choice, 2 combat_keys, 3 show_frame, 4 show_view, 5 jump_max's table (galaxy.c)

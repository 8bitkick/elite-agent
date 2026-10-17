// Launch, hyperspace, the chart cursor, witchspace, galactic jumps.
#pragma once
#include "drv.h"
#include "galaxy.h"

PT_THREAD(launch(void));
extern int g_misjump;       // the next hyperspace() forces a mis-jump into witchspace (CTRL held at the jump)
PT_THREAD(allow_misjumps(void));
extern uint32_t g_pause_frames;
PT_THREAD(set_damping(int on));
PT_THREAD(hyperspace_fighting(const sys_t *dest));   // jump out, fighting through the countdown            // keyboard damping on or off (in the pause: no game time)
PT_THREAD(hyperspace(const sys_t *dest));     // H (re-selects dest by cursor if no countdown); waits for arrival
PT_THREAD(select_by_cursor(const sys_t *dest));
PT_THREAD(leave_witchspace(void));
PT_THREAD(galactic_jump(void));                // from docked: launch, CTRL+H, dock where we land

// Named views onto Elite's RAM.
#pragma once
#include <stdint.h>
#include "player.h"
#include "elite_ram.h"

extern player_io_t *IO;
extern int g_fair;                  // (fair.h: FAIR_DASH 2 the dashboard as drawn; any bit: our own stick from our hand)
extern int g_own_jx, g_own_jy;
#ifdef READ_AUDIT          // (the fair-mode leak audit: every read the player makes, by address and call site)
void read_audit(uint16_t a, const char *file, int line);
static inline uint8_t R_(uint16_t a, const char *f, int l) { read_audit(a, f, l); return IO->ram[a]; }
#define R(a) R_((uint16_t) (a), __FILE__, __LINE__)
#define R16(a) ((uint16_t) (R(a) | R((a) + 1) << 8))
#else
static inline uint8_t R(uint16_t a) { return IO->ram[a]; }
static inline uint16_t R16(uint16_t a) { return (uint16_t)(IO->ram[a] | IO->ram[a + 1] << 8); }
#endif

static inline int view(void) { return R(A_QQ11); }              // 0 space; screens otherwise
static inline int space_view(void) { return R(A_VIEW); }
static inline int is_docked(void) { return R(A_QQ12) && R(A_QQ11); }
static inline int galaxy(void) { return R(A_GCNT); }
static inline int here_x(void) { return R(A_QQ0); }
static inline int here_y(void) { return R(A_QQ1); }
static inline int target_x(void) { return R(A_QQ9); }
static inline int target_y(void) { return R(A_QQ10); }
static inline int target_dist(void) { return R16(A_QQ8); }
static inline int witchspace(void) { return R(A_MJ) != 0; }
static inline int32_t cash(void) {           // tenths of a credit
    return (int32_t)((uint32_t)R(A_CASH) << 24 | (uint32_t)R(A_CASH + 1) << 16 | (uint32_t)R(A_CASH + 2) << 8 | R(A_CASH + 3));
}
static inline int fuel(void) { return R(A_QQ14); }
static inline int cargo(int i) { return R(A_QQ20 + i); }
static inline int cargo_bay(void) { return R(A_CRGO); }
static inline int laser(int v) { return R(A_LASER + v); }
static inline int missiles(void) { return R(A_NOMSL); }
static inline double tally(void) { return R16(A_TALLY) + R(A_TALLYL) / 256.0; }
static inline int legal(void) { return R(A_FIST); }
static inline int mission(void) { return R(A_TP); }
// (fair mode: as the bars show them: the energy banks to 4, the shields to 16)
static inline int energy(void) { return (g_fair & 2) ? R(A_ENERGY) / 4 * 4 + 2 : R(A_ENERGY); }
static inline int fsh(void) { return (g_fair & 2) ? R(A_FSH) / 16 * 16 + 8 : R(A_FSH); }
static inline int ash(void) { return (g_fair & 2) ? R(A_ASH) / 16 * 16 + 8 : R(A_ASH); }
static inline int speed(void) { return R(A_DELTA); }
static inline int altitude(void) { return (g_fair & 2) ? R(A_ALTIT) / 16 * 16 + 8 : R(A_ALTIT); }
static inline int laser_temp(void) { return (g_fair & 2) ? R(A_GNTMP) / 16 * 16 + 8 : R(A_GNTMP); }
static inline int hyperspace_count(void) { return R(A_QQ22 + 1); }
static inline int jstx(void) { return g_fair && g_own_jx >= 0 ? g_own_jx : R(A_JSTX); }
static inline int jsty(void) { return g_fair && g_own_jy >= 0 ? g_own_jy : R(A_JSTY); }
static inline int mcnt(void) { return R(A_MCNT); }
static inline int has_ecm(void) { return R(A_ECM) != 0; }
static inline int has_scoops(void) { return R(A_BST) != 0; }
static inline int has_energy_unit(void) { return R(A_ENGY) != 0; }
static inline int has_dock_computer(void) { return R(A_DKCMP) != 0; }
static inline int has_hyperdrive(void) { return R(A_GHYP) != 0; }
static inline int has_escape_pod(void) { return R(A_ESCP) != 0; }
static inline int title(void) {               // 1 or 2 on the title screens, else 0
    if (R(A_QQ12) && R(A_QQ11) == 0) {
        int d = R(A_distaway);
        return d == 200 ? 1 : d == 100 ? 2 : 0;
    }
    return 0;
}
static inline int dying(void) {         // DEATH's debris loop: MCNT held at 255 while LASCT counts down from 64
    // (not DELTA 0: still "flying" we pressed for speed through it, and missed the death until the title)
    return !R(A_QQ12) && R(A_QQ11) == 0 && R(A_MCNT) == 0xff && R(A_LASCT) > 0;
}
static inline int briefing(void) {            // a mission briefing's pages, before QQ12 says docked
    if (title() || R(A_QQ12) || witchspace()) return 0;
    int t = R(A_FRIN);
    // (DOENTRY refills energy and both shields before BRIEF: DEATH's screen is QQ11 13 with space emptied
    // too, and read as a briefing it ended the docking "safely" and hid the death)
    if (R(A_ENERGY) != 255 || R(A_FSH) != 255 || R(A_ASH) != 255) return 0;
    return (R(A_QQ11) == 13 || R(A_QQ11) == 1) && t != 128 && t != 130;
}
static inline int cursor_x(void) { return R(0x10); }    // XC, YC: the text cursor
static inline int cursor_y(void) { return R(0x12); }

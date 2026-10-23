// Named views onto Elite's RAM.
#pragma once
#include <stdint.h>
#include "player.h"
#include "elite_ram.h"

extern player_io_t *IO;
extern int g_zp_held;               // Solid Elite caught drawing: the zero page it borrows as it last was (elite_zp_frame)
extern uint8_t g_zp_keep[50];       // (&06-&27, then &D2-&E1)
extern int g_view_latch;            // Solid Elite's briefing view (13, 1) while it zeroes QQ11 to draw its ship, or -1
static inline uint8_t ram_(uint16_t a) {
    if (g_view_latch >= 0 && a == A_QQ11) return (uint8_t) g_view_latch;
    if (g_zp_held && a < 0x100) {
        if (a >= 0x06 && a <= 0x27) return g_zp_keep[a - 0x06];
        if (a >= 0xD2 && a <= 0xE1) return g_zp_keep[34 + a - 0xD2];
    }
    return IO->ram[a];
}
#if NO_FAIR
#define g_fair 0                    // (built without fair mode: always off, and what it would do the compiler leaves out)
#else
extern int g_fair;                  // (fair.h: FAIR_DASH 2 the dashboard as drawn; any bit: our own stick from our hand)
#endif
extern int g_own_jx, g_own_jy;
#ifdef READ_AUDIT          // (the fair-mode leak audit: every read the player makes, by address and call site)
void read_audit(uint16_t a, const char *file, int line);
static inline uint8_t R_(uint16_t a, const char *f, int l) { read_audit(a, f, l); return ram_(a); }
#define R(a) R_((uint16_t) (a), __FILE__, __LINE__)
#define R16(a) ((uint16_t) (R(a) | R((a) + 1) << 8))
#else
static inline uint8_t R(uint16_t a) { return ram_(a); }
static inline uint16_t R16(uint16_t a) { return (uint16_t)(R(a) | R(a + 1) << 8); }
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
// (the Master scores a kill by its type, the fraction in TALLYL; the disc version 1 a kill)
static inline double tally(void) { return R16(A_TALLY) + (A_TALLYL ? R(A_TALLYL) / 256.0 : 0); }
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
        if (g_ev.title_type[0]) {         // (the disc version: TITLE's ship, a Cobra Mk III then a Krait)
            int t = R(A_TYPE);
            return t == g_ev.title_type[0] ? 1 : t == g_ev.title_type[1] ? 2 : 0;
        }
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
static inline int cursor_x(void) { return R(A_XC); }    // XC, YC: the text cursor
static inline int cursor_y(void) { return R(A_YC); }

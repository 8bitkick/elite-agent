// Which Elite the player is on, and where that Elite keeps things (docs/DUAL.md).
//
// Three versions, one build: the BBC Master's (Mark Moxon's flicker-free SNG47), Eben Upton's Solid Elite on
// the Master (SNG47's game logic, moved about), and the BBC Micro disc version on the Model B (Moxon's
// flicker-free disc, the Stairway to Hell image). Every address the
// player reads is a field of g_ev, which elite_detect() fills from the version's table once it has
// found that version's code in RAM; the A_ names (elite_ram.h) read through it.
#pragma once
#include <stdint.h>

enum { EV_NONE = 0, EV_MASTER = 1, EV_BBC_DISC = 2, EV_SOLID = 3 };
// the machine, as the platform says (machine_hook.h's enum machine_model; 0: not said)
enum { EV_MODEL_UNKNOWN = 0, EV_MODEL_BBC_B = 1, EV_MODEL_BBC_B_PLUS = 2, EV_MODEL_MASTER_128 = 3 };

typedef struct {
    uint8_t id;                 // EV_*
    const char *name;
    // the game's workspace, A_ and Moxon's names (elite_ram.h; 0: not in this version)
    uint16_t A_ALTIT, A_ASH, A_AVL, A_BOMB, A_BST, A_CABTMP, A_CASH, A_COMC, A_CRGO, A_DAMP, A_DELTA,
             A_DKCMP, A_ECM, A_ECMA, A_ENERGY, A_ENGY, A_ESCP, A_FIST, A_FRIN, A_FSH, A_GCNT, A_GHYP,
             A_GNTMP, A_JSTX, A_JSTY, A_JUNK, A_K_PC, A_LASCT, A_LASER, A_LASX, A_MANY, A_MCNT, A_MJ, A_MSAR,
             A_MSTG, A_NA_PC, A_NAME, A_NOMSL, A_PATG, A_QQ0, A_QQ1, A_QQ10, A_QQ11, A_QQ12, A_QQ14, A_QQ20,
             A_QQ22, A_QQ24, A_QQ25, A_QQ26, A_QQ28, A_QQ29, A_QQ8, A_QQ9, A_SSPR, A_SVC, A_TALLY, A_TALLYL,
             A_TP, A_VIEW, A_de, A_distaway, A_tek, A_JSTK, A_JSTE, A_JSTGY, A_DJD, A_ECMP, A_BETA, A_XC,
             A_YC, A_RAND, A_TYPE;
    uint8_t ni;                 // NI%: bytes a ship block in K%
    uint8_t save_cmp;           // the commander bytes from TP that NA% (the last save) has to match
    uint8_t title_type[2];      // TITLE's ship (TYPE) on the two title screens (0: distaway tells them)
    uint8_t joy_fire_key;       // 1: in joystick mode the A key still fires (the Master); 0: only the
                                // joystick's fire button does (the disc version), which the player can't press
    uint8_t kills_by_type;      // 1: a kill scores by the ship's type (KWL%/KWH%, TALLYL); 0: 1 a kill
} elite_ver_t;

extern elite_ver_t g_ev;        // the version being played (id EV_NONE until detected)

// Looks for a known Elite in RAM (the 32 KB of main RAM) on the machine `model` (EV_MODEL_*); fills
// g_ev and returns its id, or EV_NONE (not loaded yet, half loaded, or an Elite this player doesn't
// know: it isn't played). Once found, it stays found (elite_forget: the machine was reset).
int elite_detect(int model, const uint8_t *ram);
void elite_forget(void);
// Once a frame, before the player reads: zero page as the game has it (state.h's R()). Solid Elite's renderer
// borrows &06-&27 and &D2-&E1 (ENERGY, the shields, QQ8-QQ10, the text cursor) while it draws, its
// bank paged; caught mid-draw, those bytes are kept from the last frame that wasn't.
void elite_zp_frame(const uint8_t *ram);
// (the Master's table, as the build had it fixed: run_sim and lab before a machine is up)
void elite_assume_master(void);
// (the Master's game: SNG47 or Solid Elite, which plays the same game at other addresses)
static inline int ev_master(void) { return g_ev.id == EV_MASTER || g_ev.id == EV_SOLID; }
static inline int ev_disc(void) { return g_ev.id == EV_BBC_DISC; }
// a wait that a disc load can be part of (the disc version loads its flight code at a launch, its docked
// code at a dock or a death, a ship file at a hyperspace arrival: ~5 s each from the 8271)
static inline float ev_disc_wait(float seconds) { return ev_disc() ? seconds + 12 : seconds; }

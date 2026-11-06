// The C player as WebAssembly, for the web page (web/index.html) on jsbeeb. The page copies the machine's 32 KB of main
// RAM into ram[] before each frame, calls wasm_frame(), and is told through two imports which keys go up or down and
// where the joystick goes; the player's log lines and journal events come back through two more.
#include <stdint.h>
#include <string.h>
#include "player.h"
#include "keyq.h"
#include "state.h"

#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
IMPORT("key") void js_key(int code, int down);
IMPORT("analogue") void js_analogue(int channel, int value);
IMPORT("log") void js_log(int level, const char *line, int len);
IMPORT("note") void js_note(const char *json, int len);

static uint8_t ram[0x8000];
static player_io_t io;

static void p_hold(void *c, const uint8_t *k, int n) { (void) c; keyq_hold(io.frame, k, n); }
static void p_tap(void *c, uint8_t k, int d, int u) { (void) c; keyq_tap(io.frame, k, d, u); }
static void p_log(void *c, int level, const char *line) { (void) c; js_log(level, line, (int) strlen(line)); }
static void p_note(void *c, const char *json) { (void) c; js_note(json, (int) strlen(json)); }
static void p_analogue(void *c, int ch, uint16_t v) { (void) c; js_analogue(ch, v); }
static void key(int code, int down) { js_key(code, down); }

__attribute__((export_name("wasm_ram"))) uint8_t *wasm_ram(void) { return ram; }

// mode: 0 Optimal, 1 Fair (the two AI pilot models fly), 2 Curious; machine: 3 the Master 128
__attribute__((export_name("wasm_init"))) int wasm_init(int mode, int machine) {
    extern int g_fair, g_curious;
    g_fair = mode == 1 ? 3 : 0;
    g_curious = mode == 2;
    { extern int g_pilots; g_pilots = 1; }       // (every mode flies its own AI pilot models)
    memset(&io, 0, sizeof io);
    io.ram = ram; io.hold = p_hold; io.tap = p_tap; io.log = p_log; io.note = p_note; io.analogue = p_analogue;
    io.machine = machine;
    keyq_reset();
    player_init(&io);
    extern int elite_detect(int model, const uint8_t *ram);
    return elite_detect(io.machine, io.ram);
}

// one emulated frame: the player decides on this frame's RAM, then the keys due before the next frame go to the machine
__attribute__((export_name("wasm_frame"))) void wasm_frame(void) {
    player_frame();
    io.taps_done += (uint32_t) keyq_step(io.frame, key);
    io.frame++;
}

// the commander's name (what the player saves as), up to 7 letters
__attribute__((export_name("wasm_name"))) char *wasm_name(void) { extern char g_name[8]; return g_name; }

// the action worth watching at real speed: 1 in a fight, 2 in the docking's final
// (the final first: the phase stays at "fight" from the first fight until the next launch, so a docking after a
// fight read as a fight, and ACTION never slowed for its final: on the Solid disc, with a fight before the first
// docking, never at all)
__attribute__((export_name("wasm_action"))) int wasm_action(void) {
    extern int g_phase; extern int pilot_dock_final(void);
    return pilot_dock_final() ? 2 : g_phase == 1 ? 1 : 0;
}

// the networks' senses at their last decisions, and when (the player's frame): the page runs the networks again in
// JavaScript from them, to light the panel's units
extern float g_wsp_last_f[], g_dock_last_f[]; extern uint32_t g_wsp_last_frame, g_dock_last_frame;
__attribute__((export_name("wasm_fight_senses"))) float *wasm_fight_senses(void) { return g_wsp_last_f; }
__attribute__((export_name("wasm_fight_when"))) uint32_t wasm_fight_when(void) { return g_wsp_last_frame; }
// which dogfight network made the last decision: 0 fair mode's, 1 the camera (Curious, the show on), 2 the interceptor
extern int g_wsp_last_net;
__attribute__((export_name("wasm_fight_net"))) int wasm_fight_net(void) { return g_wsp_last_net; }
__attribute__((export_name("wasm_dock_senses"))) float *wasm_dock_senses(void) { return g_dock_last_f; }
__attribute__((export_name("wasm_dock_when"))) uint32_t wasm_dock_when(void) { return g_dock_last_frame; }

// for the page's panel: the kill tally and the cash (tenths of a credit), as the game holds them
__attribute__((export_name("wasm_tally"))) double wasm_tally(void) { return tally(); }
__attribute__((export_name("wasm_cash"))) int32_t wasm_cash(void) { return cash(); }

__attribute__((export_name("wasm_frame_no"))) uint32_t wasm_frame_no(void) { return io.frame; }

// a commander to load from the disc when next docked (the page's chapters: a save put on the disc as E.<name>)
__attribute__((export_name("wasm_load"))) char *wasm_load(void) { extern char g_load_name[8]; return g_load_name; }

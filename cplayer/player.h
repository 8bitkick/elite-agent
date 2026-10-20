// The Elite player: plays Acornsoft Elite (the BBC Master's or the Model B's disc version: elite_ver.h) by looking
// at the machine's RAM and pressing keys. Nothing else: the platform gives it read-only RAM and a
// keyboard, and calls player_frame() once per emulated frame, between frames.
//
// Platforms: cplayer/run_sim.c and lab.c (the native emulator, for development) and the Presto
// firmware (presto/elite_presto.c).
#pragma once
#include <stdint.h>

// log levels: STATUS a short line per event (docked, bought, enemy detected...); INFO what it's
// doing and why; DEBUG the approach and docking lines, several a second
enum { PLOG_STATUS, PLOG_INFO, PLOG_DEBUG };

typedef struct {
    const uint8_t *ram;     // the 32 KB of main RAM (&0000-&7FFF), as of the end of the last frame
    uint32_t frame;         // the frame number (the platform increments it)
    // exactly these keys (BBC matrix codes, row << 4 | column) held down from the next frame on
    void (*hold)(void *ctx, const uint8_t *codes, int n);
    // press code for `down` frames then let go for `up`, after any taps already queued; the
    // platform times them, and counts them done in taps_done
    void (*tap)(void *ctx, uint8_t code, int down, int up);
    uint32_t taps_done;     // taps finished (the platform increments it)
    // a line for the log at a level (PLOG_*), and an event for the journal (JSON object text);
    // both optional
    void (*log)(void *ctx, int level, const char *line);
    void (*note)(void *ctx, const char *json);
    // the joystick: the analogue port's channel 0-3 at 0..0xFFFF (0x8000 centred), held there from
    // its next conversion (optional: NULL where the platform has none)
    void (*analogue)(void *ctx, int channel, uint16_t value);
    void *ctx;
    // the machine (machine_hook.h's model: 1 Model B, 3 Master 128; 0 not said): with what's in its RAM,
    // which Elite this is (elite_ver.h)
    int machine;
} player_io_t;

void player_init(player_io_t *io);
void player_frame(void);
int player_dull_cruise(void);   // (curious) a cruise with nothing to see: the platform may fast-forward it

// The whole machine as WebAssembly: Kieran Connell's Master core (the Presto's and run_sim's, sim/libbeeb.c) with the player
// beside it (web/player_wasm.c). One binary for the browser and for Node, and it steps exactly as run_sim's step() does:
// the player decides on this frame's RAM, the keys due go to the machine, the machine runs 40,000 cycles.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sn76489.h"

static struct sn76489 s_sn;
#define MACHINE_SOUND_WRITE(m, byte) sn_write(&s_sn, (byte))
#include "../sim/libbeeb.c"
#include "sn76489.c"

#define EXPORT(name) __attribute__((export_name(name)))

// from player_wasm.c
extern uint8_t *wasm_ram(void);
extern int wasm_init(int mode, int machine);
extern void wasm_frame(void);
extern int elite_detect(int model, const uint8_t *ram);

static beeb_t *B;
static uint8_t *s_px;
static uint32_t *s_rgba;
static int16_t s_audio[640];

EXPORT("core_alloc") void *core_alloc(uint32_t n) { return calloc(1, n); }

// roms: 8 pointers of 16 KB each, the MOS then ROM slots 9 to 15, in the page's own memory (kept)
EXPORT("core_boot") int core_boot(const uint8_t **roms, const uint8_t *image, uint32_t size) {
    B = calloc(1, sizeof *B);
    struct machine *m = &B->m;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) m->swram[i] = calloc(0x4000, 1);
    m->mos = roms[0];
    for (int i = 0; i < 7; i++) m->rom[0x9 + i] = roms[1 + i];
    m->vs = NULL;
    machine_reset(m);
    uint32_t *pulses = calloc((size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, sizeof(uint32_t));
    disc_init(&B->disc, pulses, 1, 1);
    disc_load_ssd(&B->disc, image, (size + 255) & ~255u);
    machine_set_disc(m, 0, &B->disc);
    machine_autoboot(m);
    sn_reset(&s_sn);
    return 1;
}

// the boot as run_sim's: to the title screen, a game that loads for longer waited for, skew frames more; then the player.
// Returns 0 if it isn't an Elite the player knows
EXPORT("core_start") int core_start(int skew, int mode) {
    beeb_frames(B, 700);
    for (int i = 0; i < 40 && !elite_detect(beeb_model(), beeb_ram(B)); i++) beeb_frames(B, 50);
    beeb_frames(B, skew);
    memcpy(wasm_ram(), beeb_ram(B), 0x8000);
    return wasm_init(mode, beeb_model());
}

// one frame. draw: this frame through the renderer, into the indexed picture (core_rgba() gives it in colour)
EXPORT("core_frame") void core_frame(int draw) {
    memcpy(wasm_ram(), beeb_ram(B), 0x8000);
    wasm_frame();
    if (draw) {
        if (!s_px) s_px = malloc((size_t) RENDER_WIDTH * RENDER_HEIGHT);
        beeb_shot(B, s_px, 1);
    } else beeb_frames(B, 1);
}

EXPORT("core_key") void core_key(int code, int down) { beeb_key(B, code, down); }
EXPORT("core_analogue") void core_analogue(int channel, int value) { beeb_analogue(B, channel, value); }
EXPORT("core_width") int core_width(void) { return RENDER_WIDTH; }
EXPORT("core_height") int core_height(void) { return RENDER_HEIGHT; }

EXPORT("core_rgba") uint32_t *core_rgba(void) {
    static const uint32_t PAL[8] = {0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff, 0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff};
    if (!s_rgba) s_rgba = malloc((size_t) RENDER_WIDTH * RENDER_HEIGHT * 4);
    if (s_px) for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) s_rgba[i] = PAL[s_px[i] & 7];
    return s_rgba;
}

// a frame of the sound chip, as it stands now, at 31,250 Hz: 625 samples
EXPORT("core_audio") int16_t *core_audio(void) {
    for (int i = 0; i < 625; i++) s_audio[i] = (int16_t) (sn_render8(&s_sn) / 8);
    return s_audio;
}

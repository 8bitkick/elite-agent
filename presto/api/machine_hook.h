// The interface for a program run alongside the machine (a player: it reads the BBC's RAM and
// presses keys, nothing else). The platform fills in a machine_hook_io and calls
// machine_frame_hook once per emulated frame, between frames; passes any command it doesn't know
// to machine_command_hook; and shows machine_program_json on its web page. Weak references in
// the platform (src/common/app.c), nothing unless a build supplies them: -DAPP_SOURCES=... (and
// _INCLUDES, _DEFINES; first called MACHINE_EXTRA_SOURCES ...). The loader makes a program with
// these an app (app_api.h), the way programs are now built: on their own, or compiled in.
//
// Either machine (-DMACHINE=beeb|master) runs a program: io->model says which it is, and a program
// built for one machine checks it before trusting anything else (the Model B has no shadow RAM,
// ANDY or HAZEL: those are NULL, size 0). MACHINE_HOOK_VERSION goes up whenever this interface
// changes; a program checks io->version against the version it was built for, too.
// machine_boot_image (a loading screen) is the Master's only: the Model B boots too quickly to
// show one.
#ifndef MACHINE_HOOK_H
#define MACHINE_HOOK_H
#include <stdbool.h>
#include <stdint.h>

#define MACHINE_HOOK_VERSION 4

// which machine this is (a program built for one checks it's on it)
enum machine_model {
    MACHINE_MODEL_BBC_B = 1,
    MACHINE_MODEL_BBC_B_PLUS = 2,
    MACHINE_MODEL_MASTER_128 = 3,
};

struct machine_hook_io {
    uint32_t version;               // MACHINE_HOOK_VERSION the platform was built with
    uint32_t model;                 // enum machine_model
    uint32_t mos;                   // the MOS version, e.g. 320 for MOS 3.20
    uint32_t frame;                 // emulated frames since boot
    uint32_t resets;                // times the machine has been reset since boot (BREAK, a disc
                                    // booted, a save state restored): its RAM starts over
    // The machine's RAM, where it is in the platform's memory. On the Master the 6502 sees these
    // through its paging (ACCCON, ROMSEL), which a program reading RAM has to follow itself.
    uint8_t *ram;    uint32_t ram_size;      // main RAM, BBC &0000-&7FFF (32 KB)
    uint8_t *shadow; uint32_t shadow_size;   // LYNNE, the shadow screen RAM at &3000-&7FFF (20 KB)
    uint8_t *andy;   uint32_t andy_size;     // ANDY, &8000-&8FFF (4 KB)
    uint8_t *hazel;  uint32_t hazel_size;    // HAZEL, &C000-&DFFF (8 KB)
    // A key of the BBC's keyboard matrix down or up, from the next frame on
    void (*key)(int row, int col, bool down);
    // An analogue input: the joystick port's channel 0-3 at 0..0xFFFF (0x8000 centred), from its
    // next conversion: a joystick held where the program wants it
    void (*analogue)(int channel, uint16_t value);
};

void machine_frame_hook(const struct machine_hook_io *io);
// a command for the program (over USB "!<cmd>", or the web's POST /api/command): true if it took
// it, with any answer in reply (a line of text, may be left empty)
bool machine_command_hook(const char *cmd, char *reply, uint32_t reply_size);
// the program's panel on the web page (GET /api/program), as JSON, or NULL for none:
//   {"title": "...", "fields": [["name", "value"], ...], "progress": {"label": "...", "value": n, "max": n},
//    "log": ["line", ...], "actions": [{"label": "...", "cmd": "...", "on": true|false}, ...]}
// every member optional. Called between frames; the string has to last until the next call.
const char *machine_program_json(void);
// the program's own loading screen, or NULL for none: a picture, 480 x 256 pixels, two a byte (the
// left one in the low nibble), each bit 0 blue, 1 green, 2 red (bit 3 ignored), shown while the
// ROMs and the disc go into memory, with the platform's line saying how far they've got under it
struct machine_boot_image { uint16_t w, h; const uint8_t *px; };
const struct machine_boot_image *machine_boot_image(void);

#endif

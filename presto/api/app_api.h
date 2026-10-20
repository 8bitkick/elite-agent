// The app interface (docs/dual-boot.md): an RP2350 program run alongside the machine, built on its
// own and flashed into the app slot (partition 2), or compiled into the firmware. Either way it
// starts with a struct app_header, and the firmware's loader (app.c) checks it, sets up its RAM
// and calls its entry points; the app does everything else through the struct app_api the
// loader hands its init. A built-in app is one whose header is compiled in (-DAPP_SOURCES=...):
// the same header, the same calls, nothing loaded.
//
// The rules for an app:
// - it touches no hardware and calls nothing of the Pico SDK: everything goes through app_api
//   (and the machine_hook_io in it), so the firmware can change underneath it;
// - it brings its own libm and string functions (statically linked, -ffreestanding);
// - it is linked at APP_SLOT_XIP and runs from there (execute in place, through the XIP cache);
//   its .data and .bss are linked at APP_RAM_BASE, the fixed app RAM area: the loader copies
//   .data in and zeroes .bss before anything is called;
// - its entry points are all called on core 0 between frames, on the firmware's stack
//   (APP_STACK_SIZE of it at least), never at the same time;
// - the versions are checked both ways: the loader refuses an app built for another
//   APP_API_VERSION, another machine or a disc it hasn't got (and says why: /api/app, the panel);
//   the app refuses an app_api older than it needs (init returns false).
//
// APP_API_VERSION goes up whenever any of this changes in a way an app built before can't
// follow. struct app_api only ever grows at its end (its size says how much there is).
#ifndef APP_API_H
#define APP_API_H

#include <stdbool.h>
#include <stdint.h>

#include "machine_hook.h"

#define APP_API_VERSION 1
#define APP_MAGIC 0x50504142u           // "BAPP"

// The app slot: partition 2 of src/launcher/partitions.json, 1 MB at 14 MB. The machine running
// from its own slot sees the flash through the boot ROM's address translation; the loader maps
// the top 4 MB window (0x10c00000) back onto the flash itself (QMI ATRANS3), so the app runs
// at its real address, cached.
#define APP_SLOT_OFFSET 0x00e00000u
#define APP_SLOT_SIZE   0x00100000u
#define APP_SLOT_XIP    (0x10000000u + APP_SLOT_OFFSET)

// The app RAM area: the top of the SRAM's main 512 KB, kept out of the firmware's heap. A machine
// that has none (the Model B: its JIT wants the SRAM) says so in app_api.ram_size, 0, and refuses
// an app that needs RAM.
#define APP_RAM_SIZE (72 * 1024)
#define APP_RAM_BASE (0x20080000u - APP_RAM_SIZE)
// the stack an app's entry points can count on (core 0's, in SCRATCH_Y)
#define APP_STACK_SIZE 3072

// What the firmware gives the app: handed to its init, to keep.
struct app_api {
    uint32_t version;                   // APP_API_VERSION the firmware was built with
    uint32_t size;                      // sizeof (struct app_api) there: members past it are missing
    const struct machine_hook_io *io;   // the machine (machine_hook.h): its model, RAM, keys
    void (*log)(const char *line);      // a line of text (no newline) to USB and the panel's log
    uint32_t (*time_us)(void);          // microseconds since boot, wrapping
    bool (*turbo_get)(void);            // the machine running as fast as it can
    void (*turbo_set)(bool on);
    void *ram; uint32_t ram_size;       // the app RAM area (APP_RAM_BASE, APP_RAM_SIZE; 0: none)
};

// What the app gives the firmware: at the start of the slot (or compiled in).
struct app_header {
    uint32_t magic;                     // APP_MAGIC
    uint16_t api_version;               // APP_API_VERSION it was built against
    uint16_t header_size;               // sizeof (struct app_header) it was built with
    uint32_t machine;                   // enum machine_model it runs on (0: any)
    char name[24];                      // "elite-agent"
    char version[24];                   // its own version, shown as it is
    // the image in the slot: this header, the code and constants, .data's first values, the disc
    uint32_t image_size;                // its bytes from here on
    uint32_t crc;                       // their CRC-32, taken with this as 0 (0: none, built in)
    // its RAM, at APP_RAM_BASE: .data (copied from data_load), then .bss (zeroed)
    uint32_t data_load, data_size, bss_size;
    uint32_t stack_size;                // the stack it needs, at most APP_STACK_SIZE
    // the disc it needs in drive 0 ("" none): by name (shown) and by hash (FNV-1a over the image,
    // as drive.c takes it). Carried in the slot (disc_size bytes at disc_load), it is the
    // machine's built-in disc while the app runs (and its saved games the save disc's); not
    // carried, the machine's own built-in disc has to be it.
    char disc[32];
    uint32_t disc_hash, disc_load, disc_size;
    // The entry points, any of them NULL. boot_image is called first, at power on, before init,
    // so it may use only constant data; init at the first frame (false: it won't run, having
    // logged why); then frame once per emulated frame, command for "!<cmd>" over USB and
    // POST /api/command (true: taken, any answer in reply), program_json for the web panel
    // (machine_hook.h has the formats).
    const struct machine_boot_image *(*boot_image)(void);
    bool (*init)(const struct app_api *api);
    void (*frame)(const struct machine_hook_io *io);
    bool (*command)(const char *cmd, char *reply, uint32_t reply_size);
    const char *(*program_json)(void);
};

// (a header's layout is the API's: the same for the firmware and an app built apart, 148 bytes)
_Static_assert(sizeof (void *) != 4 || sizeof (struct app_header) == 148, "struct app_header's layout changed");

// An app's header: APP_HEADER(.name = "...", .machine = ..., .init = ..., ...). Built as a loaded
// app (APP_LOADED, by its own build) it goes first in the image, with its RAM from the linker
// script (the symbols below) and the image's size, CRC and disc from the build's packer; compiled
// into the firmware it is just a constant the loader finds (app_header).
#if APP_LOADED
extern char __app_data_load[], __app_data_size[], __app_bss_size[];
#define APP_HEADER_RAM .data_load = (uint32_t) __app_data_load, .data_size = (uint32_t) __app_data_size, \
                       .bss_size = (uint32_t) __app_bss_size,
#define APP_HEADER_AT __attribute__((section(".app_header"), used))
#else
#define APP_HEADER_RAM
#define APP_HEADER_AT __attribute__((used))
#endif
#define APP_HEADER(...) \
    const struct app_header app_header APP_HEADER_AT = { \
        .magic = APP_MAGIC, .api_version = APP_API_VERSION, .header_size = sizeof (struct app_header), \
        APP_HEADER_RAM __VA_ARGS__ }

#endif

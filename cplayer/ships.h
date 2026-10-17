// Ships in the local bubble (K% and FRIN) and the aiming maths.
#pragma once
#include <stdint.h>

#define NOSH 12
#define SHIP_SST 2                  // the space station (slot 1)
#define NEWB_TRADER 0x01
#define NEWB_BOUNTY_HUNTER 0x02
#define NEWB_HOSTILE 0x04
#define NEWB_PIRATE 0x08
#define NEWB_COP 0x40

typedef struct {
    int slot, type;
    int32_t x, y, z;                // front-view frame: x right, y up, z forward
    float nosev[3], roofv[3], sidev[3];   // 1.0 = &6000
    int speed, accel, roll, pitch;
    uint8_t flags31, ai, energy, newb;
} ship_t;

typedef struct {
    const char *name;
    int max_energy, max_speed, laser, missiles, bounty, area;
    float kill_points;
    uint8_t newb;
} shiptype_t;

int ships_read(const uint8_t *ram, ship_t out[NOSH]);    // the occupied slots, in order; returns count
const shiptype_t *ship_type(int type);                    // NULL for planets/sun
static inline int ship_dead(const ship_t *s) { return (s->flags31 & 0xA0) != 0; }
static inline int ship_is_ship(const ship_t *s) { return s->type > 0 && s->type < 128; }
// a ship flying about, any slot: not the planet or sun (types 128+) nor the station. (Not "slot >= 2": in
// witchspace there's no planet or sun, and the first two Thargoids take slots 0 and 1)
static inline int ship_in_space(const ship_t *s) { return s->type > 0 && s->type < 128 && s->type != 2; }
int in_crosshairs(const ship_t *s, int view);            // HITCH
int alpha_from_jstx(int jstx);
int beta_from_jsty(int jsty);

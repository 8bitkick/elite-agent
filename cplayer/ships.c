// Ships in the local bubble (K% and FRIN) and the aiming maths.
// Plain C99, no allocation, no stdio.
#include "ships.h"

#define NI 37               // bytes per K% ship block
#define K_BASE 0x0400       // K%: slot i at K_BASE + i * NI
#define FRIN 0x0E41         // slot -> type; 0 = empty (the first 0 ends the list)
#define UNIT 24576.0        // 0x6000: orientation vectors' 1.0

// id: name, max energy, max speed, laser power, missiles, bounty (Cr/10), targetable area,
//     KWH% + KWL%/256, E% (from elite-data.asm)
#define T(name, en, sp, la, mi, bo, ar, kwh, kwl, nb) { name, en, sp, la, mi, bo, ar, (float)(kwh) + (float)(kwl) / 256.0f, nb }
static const shiptype_t SHIP_TYPES[34] = {
    { 0, 0, 0, 0, 0, 0, 0, 0.0f, 0 },      // unused (type 0 = empty slot)
    T("Missile", 2, 44, 0, 0, 0, 40 * 40, 0, 149, 0x00),
    T("Space station", 240, 0, 0, 6, 0, 160 * 160, 0, 0, 0x00),
    T("Escape pod", 17, 8, 0, 0, 0, 16 * 16, 0, 16, 0x01),
    T("Alloy plate", 16, 16, 0, 0, 0, 10 * 10, 0, 10, 0x00),
    T("Cargo canister", 17, 15, 0, 0, 0, 20 * 20, 0, 10, 0x00),
    T("Boulder", 20, 30, 0, 0, 1, 30 * 30, 0, 6, 0x00),
    T("Asteroid", 60, 30, 0, 0, 5, 80 * 80, 0, 8, 0x00),
    T("Splinter", 20, 10, 0, 0, 0, 16 * 16, 0, 10, 0x00),
    T("Shuttle", 32, 8, 0, 0, 0, 50 * 50, 0, 16, 0x21),
    T("Transporter", 32, 10, 0, 0, 0, 50 * 50, 0, 17, 0x61),
    T("Cobra Mk III", 150, 28, 2, 3, 0, 95 * 95, 0, 234, 0xA0),
    T("Python", 250, 20, 3, 3, 0, 80 * 80, 0, 170, 0xA0),
    T("Boa", 250, 24, 3, 4, 0, 70 * 70, 0, 213, 0xA0),
    T("Anaconda", 252, 14, 7, 7, 0, 100 * 100, 1, 0, 0xA1),
    T("Rock hermit", 180, 30, 0, 2, 0, 80 * 80, 0, 85, 0xA1),
    T("Viper", 140, 32, 2, 1, 0, 75 * 75, 0, 26, 0xC2),
    T("Sidewinder", 70, 37, 2, 0, 50, 65 * 65, 0, 85, 0x0C),
    T("Mamba", 90, 30, 2, 2, 150, 70 * 70, 0, 128, 0x8C),
    T("Krait", 80, 30, 2, 0, 100, 60 * 60, 0, 85, 0x8C),
    T("Adder", 85, 24, 2, 0, 40, 50 * 50, 0, 90, 0x8C),
    T("Gecko", 70, 30, 2, 0, 55, 99 * 99, 0, 85, 0x0C),
    T("Cobra Mk I", 90, 26, 2, 2, 75, 99 * 99, 0, 170, 0x8C),
    T("Worm", 30, 23, 1, 0, 0, 99 * 99, 0, 50, 0x05),
    T("Cobra Mk III (pirate)", 150, 28, 2, 2, 175, 95 * 95, 1, 42, 0x8C),
    T("Asp Mk II", 150, 40, 5, 1, 200, 60 * 60, 1, 21, 0x8C),
    T("Python (pirate)", 250, 20, 3, 3, 200, 80 * 80, 1, 42, 0x8C),
    T("Fer-de-lance", 160, 30, 2, 2, 0, 40 * 40, 1, 64, 0x82),
    T("Moray", 100, 25, 2, 0, 50, 30 * 30, 0, 192, 0x0C),       // never spawned
    T("Thargoid", 240, 39, 2, 6, 500, 99 * 99, 2, 170, 0x0C),
    T("Thargon", 20, 30, 2, 0, 50, 40 * 40, 0, 33, 0x04),
    T("Constrictor", 252, 36, 6, 4, 0, 65 * 65, 5, 85, 0x04),
    T("Cougar", 252, 40, 6, 4, 0, 70 * 70, 5, 85, 0x20),
    T("Dodo station", 240, 0, 0, 0, 0, 180 * 180, 0, 0, 0x00),  // only via XX21 swap (type 2)
};

const shiptype_t *ship_type(int type)
{
    return (type >= 1 && type <= 33) ? &SHIP_TYPES[type] : 0;
}

// 24-bit sign-magnitude coordinate: 23-bit magnitude, sign in bit 7 of the top byte
static int32_t sm24(uint8_t lo, uint8_t hi, uint8_t sign)
{
    int32_t v = (int32_t)(sign & 0x7F) << 16 | (int32_t)hi << 8 | lo;
    return (sign & 0x80) ? -v : v;
}

// 16-bit sign-magnitude orientation component as a fraction (1.0 = 0x6000)
static float sm16(uint8_t lo, uint8_t hi)
{
    int v = (hi & 0x7F) << 8 | lo;
    return (float)((double)((hi & 0x80) ? -v : v) / UNIT);
}

static int sm8(uint8_t b) { return (b & 0x80) ? -(b & 0x7F) : (b & 0x7F); }
static int s8(uint8_t b) { return (b & 0x80) ? (int)b - 256 : b; }

static void parse_block(const uint8_t *b, int slot, int type, ship_t *s)
{
    int i;
    s->slot = slot;
    s->type = type;
    s->x = sm24(b[0], b[1], b[2]);
    s->y = sm24(b[3], b[4], b[5]);
    s->z = sm24(b[6], b[7], b[8]);
    for (i = 0; i < 3; i++) {
        s->nosev[i] = sm16(b[9 + 2 * i], b[10 + 2 * i]);
        s->roofv[i] = sm16(b[15 + 2 * i], b[16 + 2 * i]);
        s->sidev[i] = sm16(b[21 + 2 * i], b[22 + 2 * i]);
    }
    s->speed = b[27];
    s->accel = s8(b[28]);
    s->roll = sm8(b[29]);
    s->pitch = sm8(b[30]);
    s->flags31 = b[31];
    s->ai = b[32];
    s->energy = b[35];
    s->newb = b[36];
}

int ships_read(const uint8_t *ram, ship_t out[NOSH])
{
    int i;
    for (i = 0; i < NOSH; i++) {
        uint8_t t = ram[FRIN + i];
        if (t == 0)
            break;
        parse_block(ram + K_BASE + i * NI, i, t, &out[i]);
    }
    return i;
}

// HITCH: a cylinder of the blueprint's targetable area along the view axis, 0 <= z < 65536
int in_crosshairs(const ship_t *s, int view)
{
    const shiptype_t *info = ship_type(s->type);
    int32_t x, y, z, ax, ay;
    if (!ship_is_ship(s) || (s->flags31 & 0x20) || !info)
        return 0;
    switch (view) {         // to_view (PLUT)
    case 1: x = -s->x; y = s->y; z = -s->z; break;
    case 2: x = s->z; y = s->y; z = -s->x; break;
    case 3: x = -s->z; y = s->y; z = s->x; break;
    default: x = s->x; y = s->y; z = s->z; break;
    }
    ax = x < 0 ? -x : x;
    ay = y < 0 ? -y : y;
    if (z < 0 || z >= 0x10000 || ax > 255 || ay > 255)
        return 0;
    return ax * ax + ay * ay <= info->area;
}

// ALPHA (signed, 1/256 rad per iteration) from the roll rate JSTX (main loop part 2)
int alpha_from_jstx(int jstx)
{
    int d = jstx - 128;
    int m = (d < 0 ? -d : d) >> 2;
    if (m < 8)
        m >>= 1;
    return d >= 0 ? m : -m;
}

// BETA (signed) from the pitch rate JSTY, with the 6502's unknown carry taken as 0
int beta_from_jsty(int jsty)
{
    int d = jsty - 128;
    int a = (d >= 0 ? d : -d - 1) + 4;
    int m = a >> 4;
    if (m < 3)
        m >>= 1;
    return d >= 0 ? -m : m;
}

// The weight-space pilot (wsp/): the dogfight law as a hand-written, then evolved, MLP, for combat_keys()
#pragma once
#include <stdint.h>

#define WSP_NF 24       // features (wsp/elite_wsp/features.py has the same list)

typedef struct {
    double x, y, z;         // the target in ship space, as the view sees it (mirrored for the rear view)
    double vx, vy, vz;      // its velocity, game units a loop, the same frame
    double own;             // our speed along +z of that frame (negative in the rear view)
    double r;               // its hit radius, sqrt(blueprint area)
    int laser_temp, energy, witchspace;
    int jstx, jsty;         // where the stick is (mirrored for the rear view)
    // how sure the senses are (fair mode's tracker; efficient mode: seen, fresh, exact)
    int heading_seen;       // the heading read from a wireframe, not guessed from motion
    double age;             // frames since the target was last measured
    double range_sd;        // the range's uncertainty as a fraction of the range
    double vel_sd;          // the motion estimate's uncertainty, game units a loop
    int laser;              // our front laser's byte (its power, the damage a hit, in bits 0-6)
} wsp_in_t;

typedef struct { uint8_t jstx, jsty, fire, speed; float alpha, beta; } wsp_out_t;

void wsp_features(const wsp_in_t *in, float f[WSP_NF]);
wsp_out_t wsp_pilot(const wsp_in_t *in);
// the same with exploration and action repeat (WSPN) and a residual (WSPR): *decided when a new action was drawn
wsp_out_t wsp_pilot_at(const wsp_in_t *in, uint32_t frame, int *decided, float f[WSP_NF]);
void wsp_episode_reset(void);
float wsp_speed_sample(void);     // the speed last drawn, before rounding

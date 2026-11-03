// The weight-space pilot (elite-wsp): the dogfight law as a hand-written MLP, for combat_keys()
#pragma once
#include <stdint.h>
typedef struct { uint8_t jstx, jsty, fire, speed; } wsp_out_t;
wsp_out_t wsp_pilot(int32_t tx, int32_t ty, int32_t tz, const float nosev[3], int tspeed,
                    int own_speed, int energy, int fsh, int laser_temp, float hit_radius, int seen);

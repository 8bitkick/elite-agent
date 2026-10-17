// Flying (steering, travel to the station, docking) and fighting.
#pragma once
#include "drv.h"
#include "ships.h"

typedef struct { double x, y, z; } vec_t;
double steer(vec_t v, int jx, int jy, float roll_iters, float pitch_iters, keyset_t *keys);   // returns degrees off
double plan_stick(vec_t pos, vec_t vel, double spd, double radius, int jx, int jy, int *want_jx, int *want_jy);   // the acquisition planner
extern int g_plan;
extern double g_plan_hand;
void speed_keys(int delta, int want, keyset_t *keys);
void apply_flight(keyset_t *keys);          // hold them (through the planet guard)
const ship_t *find_planet(const ship_t *s, int n);
const ship_t *find_station(const ship_t *s, int n);

PT_THREAD(fight_if_needed(int *fought));    // engages if there's a threat within 9000
PT_THREAD(to_station(float seconds));
PT_THREAD(dock(float seconds));

// combat
typedef struct {
    // steering gains, and the lead: aim where the target will be this many iterations on
    float roll_iters, pitch_iters, lead;
    // dodge anything passing within ram_dist in the next ram_iters iterations; speeds: turning (off more than turn_off degrees, nearer or
    // further than turn_close), then by distance: beyond far, mid, near, and closer
    int ram_dist, ram_iters, turn_off, turn_close, spd_turn_close, spd_turn_far, far, spd_far, mid, spd_mid, near, spd_near,
        spd_close, hot;
    float fire_r;           // fire with the target within fire_r x its radius of the sights
    // evade bursts (frames on, then frames before the next), not when our shot's within evade_off_deg,
    // when a shield's under evade_shield
    int evade_on, evade_gap, evade_off_deg, evade_shield;
    // the target: (points + t_floor + w_threat x its threat) / (seconds to turn and kill at hit_rate);
    // switch only for t_switch better
    float t_floor, t_switch, w_threat, hit_rate;
    int t_reach;            // an enemy with its nose on us counts as a threat within this range, more the nearer
                            // (16000: four pirates closing from 9000 counted for nothing with 8000, and we spent
                            // five seconds on a Python too far off to hit us while they drained the front shield)
    int turn_model;         // 1: target choice times turns from the stick as it is (ramps, dead zones); 0: full rate
    int rear_cone;          // a target this close to dead astern is fought with the rear laser
    int bail_energy;        // witchspace: jump out below this
    int engage_range;       // fight hostiles this close (witchspace: all)
    int pod_energy;         // launch the escape pod below this energy (it keeps the tally: not a death)
    int rear_max;           // the rear laser only takes targets closer than this (0: any distance)
    // break off (0 off): front shield under brk_shield and energy under brk_energy with them ahead: turn the
    // full aft shield to them and open the range, until energy is back over brk_until
    int brk_energy, brk_shield, brk_until;
    int brk_clear, brk_stop, brk_n;     // (brk_n: at least this many of them)    // only with none of them nearer than brk_clear; turn back when one comes nearer than brk_stop (0: no limit)
} combat_params_t;
extern combat_params_t g_cp;
extern combat_params_t g_cp_thg;    // in witchspace (combat_thg_init makes it)
void combat_thg_init(void);
extern combat_params_t *g_cpa;      // the set in use this frame
int threats(const ship_t *s, int n, int rng, const ship_t **out, int max);
PT_THREAD(engage(float seconds));
extern int g_bail_energy;       // in witchspace, engage() gives up below this energy (0: never)

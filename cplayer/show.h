// Showcase fighting, curious mode (show.c): the hooks combat.c calls. Each returns efficient mode's own decision
// unchanged unless the show is on (curious mode, g_show, and the superiority margin says we're clearly on top).
#pragma once
#include "ships.h"
#include "drv.h"

// The show's numbers, by what they steer (show.c says why each value; docs/fighting-showcase.md has the history)
typedef struct {
    // MODE: show or efficient, moment by moment
    float on_m, off_m;          // the superiority margin: show above on_m, efficient below off_m
    float ws_on_m, ws_off_m;    // ... in witchspace
    int dwell;                  // frames either holds before it may change (no flapping)
    float hit_s, hold_s;        // the margin's model: their hits a second each; the seconds our held fire adds
    int reserve;                // energy kept back: what's over it counts
    int min_laser;              // the kit floor: 1 a beam laser or better ahead, 2 a military laser
    int frugal;                 // no show under this many credits; no bombs under three times it
    int budget_base;            // the show's time a fight (seconds), then efficient finishes it
    float empty_x;              // an empty screen spends the budget this many times as fast
    float bore_s;               // a stretch this long with nothing new on screen hands the fight over

    // CLOSING IN: speed and aim until it's near
    int close_speed, far;       // full speed (close_speed) while the target is further than far...
    float close_d;              // ... or ahead of us and further than close_d
    int intercept;              // ahead beyond close_d: aim at where we'd meet it (lead pursuit)
    int near_pref;              // normal space: a target's worth by its nearness too
    float run_value;            // one over 2000 out and opening the range is worth this much
    int rear_all;               // the rear laser only within rear_close, the show off too
    float rear_close;           // the rear laser only for a target this close behind (else turn and face it)

    // FIRING: hold for a kill that's seen
    float fire_px, ws_fire_px;  // hold fire until it's this big on screen (px across its hit radius); in witchspace
    float stall_s, stall_px;    // no kill for stall_s: fire from stall_px (a joust never reaches fire_px)
    float eff_px; int eff_e;    // the show off: no shot at one drawn under eff_px while our energy's over eff_e
    float eff_close;            // ... and it's closing faster than this a game loop (else it's shot)
    float slow_s;               // the show on: under the firing size and not closing that fast for this long, it's shot
    int col_fire;               // shoot a target about to pass within col_r in col_t game loops, whatever its size
    float col_t; int col_r;

    // TOY / FOLLOW: big time on the target before the kill
    float toy_px, toy_s;        // hold it toy_s seconds at toy_px or more
    float follow_px; int follow_d;   // sit where it's drawn follow_px (250-900 out); follow_d if follow_px is 0
    float toy_safe;             // no toying or follow speed inside this of a target coming nose-first
    int strafe;                 // bursts while toying and while closing to the firing size: near misses and grazes
    float strafe_miss;          // the share of bursts that are near misses (aimed strafe_off radii beside it)
    float strafe_off, strafe_k; // ... fired while it's within strafe_k radii of the line
    float nm_lo, nm_hi;         // a near miss fires only with the sights this far outside its drawn outline (x its drawn radius; nm_hi 0: off)
    int nm_side;                // aimed off on the side it's moving towards on screen (0: along its own side, either way)
    int strafe_temp, strafe_e;  // no burst with the laser this hot; a graze only while its energy is over strafe_e
    float strafe_px;            // closing: bursts from this size on
    int strafe_gap0, strafe_gapr;   // frames between bursts: gap0 + up to gapr more (near-continuous: fire on a big ship)

    // FLYBY: one target a fight met without a shot, passing beside us, glanced at
    float fb_odds;              // the share of fights with one
    int fb_side, fb_start, fb_speed;   // aimed fb_side beside it from fb_start out; our speed in its last 2500
    int novel_look;             // and at a rare type, or one met for the first time
    int knife_d;                // inside this, roll it to 3 or 9 o'clock (the knife-edge)
    int pass_knife, pass_cpa;   // ... on any close pass of the target (within pass_cpa)
    float follow_s, extend_s;   // after it: follow it in whichever view has it; fly straight on first
    int glance, glance_d;       // a look left or right at an enemy going past within glance_d...
    float glance_max, glance_gap, glance_px;   // ... for up to glance_max s, glance_gap s apart, at glance_px or more

    // SHOWPIECES: the missile, the energy bomb
    float msl_odds; int msl_near, msl_far;     // the share of fights with a missile, at one between msl_near and msl_far
    float bomb_odds; int bomb_n, bomb_dist;    // the share with the bomb, bomb_n of them within bomb_dist
    int bomb_ws;                // the bomb in witchspace, Thargoids about (their Thargons die)

    // WITCHSPACE: the swarm
    int ws_group, group_d;      // target the nearest group (enemies within group_d of it)
    int thg_first;              // the Thargons first while a Thargoid lives...
    float thg_d, thg_px;        // ... those within thg_d, shot once drawn thg_px

    // EVADE: the collision evasion (the show on or off)
    int ev; float ev_t, ev_r, ev_out;   // a ship passing within its radius + ev_r inside ev_t game loops; held to ev_out x
} show_params_t;
extern show_params_t g_sp;
extern int g_show;
int show_set_param(const char *name, double v);

// mode
void show_begin(void);                      // a fight starts
void show_frame(const ship_t *sh, int n);   // once a frame in a fight: show or efficient, now
int show_on(void);
extern int g_show_mode, g_show_spent;       // 1 show, 0 efficient, -1 neither (not curious, not fighting)
// the decisions, in combat.c's order
double show_value(const ship_t *s, double v, const ship_t *const *th, int nt);   // a target's worth
void show_aim(const ship_t *t, double *ax, double *ay, double *az);   // the aim point (intercept, strafe, flyby)
int show_extend(const ship_t *t);           // straight on after a flyby (1: the aim is overridden)
int show_flyby(void);                       // a flyby on (the planner stands down: it would aim at the ship itself)
void show_knife(const ship_t *t, keyset_t *keys);   // the knife-edge roll on a close pass
const ship_t *show_evade(const ship_t *s, int n, vec_t *miss);   // a collision coming (any ship), or NULL
int show_speed(const ship_t *t, double dist, int want);
int show_fire(const ship_t *t, double dist, int fire);
int show_rear_ok(double dist);              // the rear laser allowed at this range
int show_key(const ship_t *sh, int n, const ship_t *const *th, int nt, const ship_t *t, double off);   // a special key, or 0
int show_view(int view, const ship_t *sh, int n, const ship_t *t);     // the view to show
int show_novel(int type);                   // a rare type, or (in a career) one not met before
// the lab's and the sim's counters
extern int g_show_lab;
extern long g_ev_frames, g_ev_starts;

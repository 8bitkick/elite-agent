// Showcase fighting, curious mode (show.c): hooks for combat.c; each returns efficient mode's decision
// unchanged unless the show is on (g_curious, g_show, the readiness gate at the fight's start, still healthy)
#pragma once
#include "ships.h"
#include "drv.h"

typedef struct {
    float fire_px;          // hold fire until the target is this big on screen (pixels across its hit radius)
    int hold_max;           // (unused: the budget below took its place)
    int budget_base, budget_per;   // the show's time a fight: base + per enemy (the most at once) seconds, then efficient
    int safe_shield, safe_energy;   // below either: efficient mode for the rest of the fight
    int close_speed, far;   // close in at this speed while the target is further than far
    int ws;                 // the show in witchspace too (0: efficient there)
    float ws_fire_px;       // fire_px in witchspace
    float msl_odds;         // the share of fights with a missile showpiece: at a target on screen between msl_near and msl_far
    int msl_near, msl_far;
    float bomb_odds;        // the share of fights where the energy bomb ends it, with bomb_n of them within bomb_dist
    int bomb_n, bomb_dist;
    int glance, glance_d;   // look left or right (glance 1) at an enemy going past within glance_d, at least 20 px
    float glance_max, glance_gap;   // for up to glance_max seconds, no sooner than glance_gap after the last
    float glance_px;        // and only at one this big (px)
    int gate;               // 1: the superiority margin decides, moment by moment; 0: the old fixed gate (kit and health at the start)
    float on_m, off_m;      // show above on_m, efficient below off_m
    int dwell;              // frames either holds before it may change
    float hit_s;            // their hits a second each, for the margin
    float hold_s;           // and the seconds the show's holding fire adds, all of them shooting
    int reserve;            // energy kept back: what's over it counts
    float fb_odds;          // the share of fights with a flyby: one target met without a shot, passing fb_side beside us
    int fb_side, fb_start;  // (from fb_start out)
    int fb_speed;           // our speed in the flyby's last 2500
    float weave_deg, weave_s;   // hold-fire weave: this far off it, circling every weave_s (0 off)
    float follow_s, extend_s;   // after a flyby: follow it in whichever view has it; fly straight on first
    float empty_x;          // an empty screen spends the time budget this many times as fast
    int bomb_ws;            // the bomb in witchspace, Thargoids about (Thargons die)
    int glance_pred;
    float ws_on_m, ws_off_m;
    float weave_stop;       // the weave stops at this share of the firing size
    int budget_nmax;
    int cam;
    int tail_rear;
    int ws_group, group_d;      // witchspace: target the nearest group (enemies within group_d of it)
    float finish_s, finish_px;  // the budget's last finish_s: fire from finish_px
    float weave_fac;            // no weave on a target whose nose is further off us than this (cos; -2: always weave)
    int pass_knife, pass_cpa;
    int fire_recede;            // fire at a target opening on us whatever its size
    float weave_t;
    float toy_px, toy_s;        // hold it toy_s seconds at toy_px or more before the kill (0: off)
    int follow_d;
    float bore_s;
    int frugal;                 // no show under this many credits (0: off); no bombs under three times it
    int pursue;
    int col_fire;               // shoot a target about to pass within col_r in col_t game loops, whatever its size
    float col_t; int col_r;
    float ram_iters;
    float toy_safe;             // no toying or follow speed inside this of a target coming nose-first
    int strafe;                 // toying: short laser bursts now and then (0.3-0.8 s, every 1.5-4 s), near misses and grazes
    float strafe_miss;          // the share of bursts that are near misses (aimed strafe_off radii beside it)
    float strafe_off, strafe_k; // a near miss: aimed this many radii beside; fired while it's within strafe_k radii of the line
    int strafe_temp, strafe_e;
    float strafe_px;            // strafe 2: also while closing to the firing size, from this size on  // no burst with the laser this hot; a grazing hit only while its energy is over strafe_e
    int noplan;                 // curious mode without the acquisition planner (the stopgap for its cost on the Presto; off)            // the ram dodge looks this far ahead while the show is on (0: efficient's 5)                 // on boredom, chase it down first (1), then hand over               // hand over after this long with nothing new on screen (0: off; the time budget instead)               // and sit this far behind it meanwhile              // stop the weave this many game loops before the firing range (0: by size only)   // the knife-edge on any close pass of the target (within pass_cpa)          // after the budget: no rear laser beyond this (0 off)                // the camera: 1 in witchspace, 2 everywhere (0 off)        // the budget counts at most this many enemies (0: all)    // the margin's thresholds in witchspace (0: on_m, off_m)        // glances on where it will be, and an empty one ends at once
    int min_laser;          // the kit floor: 0 none, 1 a beam laser or better ahead, 2 a military laser
    int novel_look;         // a flyby at a rare type, or one met for the first time, whatever the draw
    int knife_d;            // inside this, roll it to 3 or 9 o'clock (0: no knife-edge)
    int finish_e;           // (0 off) wound it at range down to this energy, then hold the last shots until it is close
} show_params_t;
extern show_params_t g_sp;
extern int g_show;
int show_set_param(const char *name, double v);
int show_ready(void);       // the show is on now
void show_begin(void);      // a fight starts
void show_frame(const ship_t *sh, int n);   // once a frame in a fight: show or efficient, now (the margin)
extern int g_show_mode, g_show_spent;     // 1 show, 0 efficient, -1 neither (not curious, not fighting)
int show_on(void);
int show_fire(const ship_t *t, double dist, int fire);
int show_speed(const ship_t *t, double dist, int want);
int show_view(int view, const ship_t *sh, int n, const ship_t *t);     // the view to show (a glance at a pass)
int show_novel(int type);   // a rare type, or (in a career) one not met before
extern int g_show_lab;
int show_flyby(void);
int show_rear_ok(double dist);
double show_ram_iters(double iters);
int show_noplan(void);      // curious: no acquisition planner     // the ram dodge's look-ahead
double show_value(const ship_t *s, double v, const ship_t *const *th, int nt);   // the target's worth (witchspace groups)     // the rear laser allowed at this range (the tail rule)
int show_weave(const ship_t *t, int rear, double *ax, double *ay, double *az);   // the hold-fire weave, the post-flyby extend (1: on)       // a flyby on (the planner stands down: it would aim at the ship itself)
void show_knife(const ship_t *t, keyset_t *keys);     // the flyby's knife-edge roll (left / right pass)
void show_aim(const ship_t *t, double *ax, double *ay, double *az);   // the flyby's aim point
int show_key(const ship_t *sh, int n, const ship_t *const *th, int nt, const ship_t *t, double off);   // a special key, or 0

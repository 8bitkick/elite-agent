// Fair mode: the player limited to what a human could see and do (docs/FAIR.md). A switch, apart from
// efficient and curious: g_fair = 0 is the player as it always was, to the byte.
//
// Information: the ships as the screen and the scanner show them (perceive(): direction exact in the
// view, distance from the drawing's size or the scanner, type only once drawn close enough, heading
// only while drawn, motion estimated; hostility from behaviour, energy from our own hits); the
// dashboard bars as drawn; the kill tally from the kills we see. Inputs: everything seen 200 ms late,
// the stick moved at a hand's speed with a hand's precision, at most four keys down, each key at
// most ~6 times a second.
#pragma once
#include <stdint.h>
#include "ships.h"

enum {
    FAIR_SEE = 1,       // the ships as perceived (perceive()), not K%
    FAIR_DASH = 2,      // the dashboard as drawn; the tally, missile lock and mass-lock as a player knows them
    // (4 was a 200 ms perception delay: dropped, docs/FAIR.md)
    FAIR_STICK = 8,     // the stick at a hand's speed and precision
    FAIR_KEYS = 16,     // at most g_fair_key_cap keys down, each changing at most every g_fair_key_gap frames
    FAIR_ALL = 27
};
extern int g_fair;              // FAIR_* bits (0: off)
extern int g_fair_misjump;      // the forced mis-jump (author credits + CTRL) allowed in fair mode
extern int g_fair_stick_rate;   // stick units a frame (32: lock to lock in 8 frames)
extern double g_fair_noise;     // motor noise: sd = 1 + this x the deflection (0.04)
extern int g_fair_key_cap, g_fair_key_gap;
extern int g_fair_stay;

void fair_reset(void);          // a new machine (player_init, the lab)
void fair_begin_frame(void);    // player_frame: the perception update
void fair_end_frame(void);      // ... and the hands
int see_ships(ship_t out[NOSH]);    // the ships the decisions use: ships_read(), or in fair mode perceive()
double tally_seen(void);        // the kill tally as the player knows it (exact outside fair mode)
void fair_saved(void);          // (the tally kept with a save, and back with a reload)
void fair_reloaded(void);
void fair_missile_fired(void);  // we pressed M: the missile that appears next is ours
int fair_lock_slot(void);       // the ship in our sights when the missile indicator locked (-1: none seen)
extern int g_own_jx, g_own_jy;  // (drv.c) where we've put the stick (JSTX units; -1 not yet)
void fair_note_out(int jx, int jy, int fire);  // (drv.c) what we sent this frame
extern long g_fair_chk[10][4];   // (lab, FAIRCHK) perception against the truth
extern int g_fair_check;

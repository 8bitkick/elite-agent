// The docking network (wsp_dock.c): hyperspace exit to docked as weights (wsp/elite_wsp/dock_author.py). pilot.c's
// to_station_step and dock_step call it once a frame in place of their own laws (WSPAPP=1).
#ifndef WSP_DOCK_H
#define WSP_DOCK_H

#define WSPD_N 47

typedef struct {
    double P[3];            // the planet's position (ship space)
    int altitude;
    int station;            // the station in sight (dock()): S, nv, rv, spin valid
    double S[3], nv[3], rv[3];
    int spin;
    int own, fsh, ash, clear;
    double col; double miss[3]; // something on a collision course (pilot's collision_ahead, not the station), its miss vector;
                            // in the final, how close a ship before the slot will pass, 0..1 (pilot's app_pass)
    int up, final, commit;      // the bits from the last loop
} wsp_dock_in_t;

typedef struct {
    double goal[3];         // the slot's centre (the planet without the station), for the C's guards
    double alpha, beta;     // the rates wanted (ALPHA / BETA units, + nose up)
    int speed, jump;
    int up, final, commit;  // for the next loop
} wsp_dock_out_t;

void wsp_dock_features(const wsp_dock_in_t *in, int jsty, float f[WSPD_N]);
wsp_dock_out_t wsp_dock_at(const wsp_dock_in_t *in, int jsty, float f[WSPD_N]);

#endif

// The galaxy and market model (as the game computes them):
// systems from the galaxy seeds, distances as the game computes them, prices and availability.
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t num, x, y, economy, government, tech;   // tech: internal 0-14 (shown +1)
    uint16_t population, productivity, radius;
    char name[12];
} sys_t;

extern double g_risk_mult;
const sys_t *galaxy_systems(int galaxy);             // all 256, in system order (computed once, cached)
int distance_qq8(int x0, int y0, int x1, int y1);    // light years x 10, as QQ8
const sys_t *nearest_system(int galaxy, int qq9, int qq10);     // TT111's choice
// systems with 0 < distance <= max_qq8, nearest first; returns how many (up to max)
extern uint8_t g_unreachable[8][32];
extern int g_unreachable_n;
int jump_max(int galaxy, int num);
int jump_trap(int galaxy, int num);
extern uint8_t g_loop_reach[256];
extern int g_loop_reach_on;
void safe_reach(int galaxy, int from);
extern int g_safe_jumps;
int in_range(int galaxy, int x, int y, int max_qq8, const sys_t **out, int *dist, int max);
const sys_t *find_system(int galaxy, const char *name);          // case-insensitive; NULL if none
int item_price_tenths(int item, int economy, int qq26);         // as the market shows it
double exp_price_tenths(int item, int economy);                  // mean over QQ26 0-255
int equipment_available(int tek);
int item_avail(int item, int economy, int qq26);                  // a market's stock (GVL)                                // items offered at this tech level
extern const char *const ITEM_NAMES[17];

// the best cargo for a destination, and the best destination from here
typedef struct {
    const sys_t *dest;
    int dist;                   // tenths of a LY
    uint8_t buys[17];
    double profit;              // expected, tenths of a credit
    int cost;
    double score;
} plan_t;
extern const double GOV_RISK[8];
void best_cargo(int here_economy, int qq26, const uint8_t avl[17], int dest_economy, int cash, int room_t,
                int illegal_ok, uint8_t buys[17], int *spend, double *profit);
int plan_trip(int galaxy, int x, int y, int here_economy, int qq26, const uint8_t avl[17], int cash, int fuel,
              int crgo, const uint8_t cargo[17], plan_t *out);   // 0 if nowhere in range
// the same, with `bonus` (tenths) for a system not yet in the `seen` bitmap: curiosity
int plan_trip_seen(int galaxy, int x, int y, int here_economy, int qq26, const uint8_t avl[17], int cash, int fuel,
                   int crgo, const uint8_t cargo[17], const uint8_t seen[32], double bonus, plan_t *out);
// the best trade loop in the galaxy: two systems within one tank, expected profit per jump each
// way (nominal stock, mean prices, less fuel and risk) with `cash` and `room_t` tonnes of hold
double best_loop(int galaxy, int cash, int room_t, const sys_t **a, const sys_t **b);
// the same over systems i0..i1-1 only, carrying on from `best` (a step at a time, between frames)
double best_loop_part(int galaxy, int cash, int room_t, int i0, int i1, double best, const sys_t **a, const sys_t **b);
double loop_rate(const sys_t *a, const sys_t *b, int cash, int room_t);
// routes: hops of up to 7 LY; risk-weighted; returns the number of systems in
// path (start first), 0 if unreachable
int route(int galaxy, int x, int y, int to_x, int to_y, float risk_weight, const sys_t **path, int max);

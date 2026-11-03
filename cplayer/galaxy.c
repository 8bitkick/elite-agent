#include "ptime.h"
#include <string.h>
// The galaxy (systems from the seeds, as the game makes them), markets, trade planning and routes.
// (Ported from the first, Python, agent and checked bit-exact against it: same integer quirks and tie-breaking.)
// Note: build with -std=c99 (or -ffp-contract=off) so the compiler doesn't fuse a*b+c into FMA.
#include "galaxy.h"
#include <stddef.h>

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

static const uint8_t GALAXY0_SEEDS[6] = {0x4A, 0x5A, 0x48, 0x02, 0x53, 0xB7};

static const char QQ16[32][3] = {
    "AL", "LE", "XE", "GE", "ZA", "CE", "BI", "SO",
    "US", "ES", "AR", "MA", "IN", "DI", "RE", "A?",
    "ER", "AT", "EN", "BE", "RA", "LA", "VE", "TI",
    "ED", "OR", "QU", "AN", "TE", "IS", "RI", "ON",
};

const char *const ITEM_NAMES[17] = {
    "Food", "Textiles", "Radioactives", "Slaves", "Liquor/Wines", "Luxuries", "Narcotics",
    "Computers", "Machinery", "Alloys", "Firearms", "Furs", "Minerals", "Gold", "Platinum",
    "Gem-Stones", "Alien Items",
};

typedef struct { uint8_t base_price; int8_t factor; uint8_t unit, base_quantity, mask; } item_t;
// QQ23; unit: 0 t, 1 kg, 2 g
static const item_t QQ23[17] = {
    {19, -2, 0, 6, 0x01},   {20, -1, 0, 10, 0x03},  {65, -3, 0, 2, 0x07},   {40, -5, 0, 226, 0x1F},
    {83, -5, 0, 251, 0x0F}, {196, 8, 0, 54, 0x03},  {235, 29, 0, 8, 0x78},  {154, 14, 0, 56, 0x03},
    {117, 6, 0, 40, 0x07},  {78, 1, 0, 17, 0x1F},   {124, 13, 0, 29, 0x07}, {176, -9, 0, 220, 0x3F},
    {32, -1, 0, 53, 0x03},  {97, -1, 1, 66, 0x07},  {171, -2, 1, 55, 0x1F}, {45, -1, 2, 250, 0x0F},
    {53, 15, 0, 192, 0x07},
};

const double GOV_RISK[8] = {1.0, 0.7, 0.5, 0.4, 0.25, 0.15, 0.1, 0.05};
double g_risk_mult = 1;        // how much the danger weighs: more while our ship is weak (set by the career)

static int item_byte1(int item)
{
    const item_t *it = &QQ23[item];
    int f = it->factor;
    return (f < 0 ? 0x80 : 0) | (it->unit << 5) | (f < 0 ? -f : f);
}

// ---------------------------------------------------------------------------
// Seeds and system data
// ---------------------------------------------------------------------------

static void twist(uint8_t s[6])
{
    int t = s[0] + s[2];
    int x = t & 0xFF, c = t >> 8;
    t = s[1] + s[3] + c;
    int y = t & 0xFF;
    s[0] = s[2]; s[1] = s[3];
    s[2] = s[4]; s[3] = s[5];
    t = x + s[2];
    s[4] = t & 0xFF;
    c = t >> 8;
    s[5] = (y + s[3] + c) & 0xFF;
}

static void system_name(const uint8_t seeds[6], char *out)
{
    uint8_t s[6];
    for (int i = 0; i < 6; i++) s[i] = seeds[i];
    int pairs = (s[0] & 0x40) ? 4 : 3, k = 0;
    for (int i = 0; i < pairs; i++) {
        int n = s[5] & 0x1F;
        if (n) {
            const char *p = QQ16[n];
            out[k++] = p[0];
            if (p[1] != '?') out[k++] = p[1];
        }
        twist(s);
    }
    out[k] = 0;
    // str.capitalize(): first upper, rest lower (all letters here are A-Z)
    for (int i = 1; i < k; i++)
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] += 'a' - 'A';
}

static void system_data(const uint8_t s[6], int num, sys_t *sy)
{
    int qq3 = s[1] & 7;
    int qq4 = (s[2] >> 3) & 7;
    if ((qq4 >> 1) == 0) qq3 |= 2;
    int flipped = qq3 ^ 7;
    int qq5 = flipped + (s[3] & 3);
    qq5 = (qq5 + (qq4 >> 1) + (qq4 & 1)) & 0xFF;
    int qq6 = (qq5 * 4 + qq3 + qq4 + 1) & 0xFF;
    int p = ((flipped + 3) * (qq4 + 4)) & 0xFF;     // MULTU: high byte always 0, keep low
    int prod = ((p * qq6) << 3) & 0xFFFF;
    sy->num = (uint8_t)num;
    sy->x = s[3];
    sy->y = s[1];
    sy->economy = (uint8_t)qq3;
    sy->government = (uint8_t)qq4;
    sy->tech = (uint8_t)qq5;
    sy->population = (uint16_t)qq6;
    sy->productivity = (uint16_t)prod;
    sy->radius = (uint16_t)(((s[5] & 0x0F) + 11) * 256 + s[3]);
    system_name(s, sy->name);
}

static sys_t cache[256];
static int cache_galaxy = -1;

const sys_t *galaxy_systems(int galaxy)
{
    galaxy &= 7;
    if (cache_galaxy != galaxy) {
        uint8_t s[6];
        for (int i = 0; i < 6; i++) {
            int b = GALAXY0_SEEDS[i];
            for (int g = 0; g < galaxy; g++) b = ((b << 1) | (b >> 7)) & 0xFF;
            s[i] = (uint8_t)b;
        }
        for (int i = 0; i < 256; i++) {
            system_data(s, i, &cache[i]);
            for (int k = 0; k < 4; k++) twist(s);
        }
        cache_galaxy = galaxy;
    }
    return cache;
}

// ---------------------------------------------------------------------------
// Distances and selection
// ---------------------------------------------------------------------------

static int ll5(int r, int q)
{
    int y = r & 0xFF, s = q & 0xFF, x = 0, res = 0;
    for (int i = 0; i < 8; i++) {
        int c;
        if (x < res) c = 0;
        else if (x == res && y < 64) c = 0;
        else {
            int t = y - 64;
            y = t & 0xFF;
            int borrow = t < 0;
            t = x - res - borrow;
            x = t & 0xFF;
            c = t < 0 ? 0 : 1;
        }
        res = ((res << 1) | c) & 0xFF;
        for (int j = 0; j < 2; j++) {
            int cs = s >> 7;
            s = (s << 1) & 0xFF;
            int cy = y >> 7;
            y = ((y << 1) | cs) & 0xFF;
            x = ((x << 1) | cy) & 0xFF;
        }
    }
    return res;
}

int distance_qq8(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int k = dx * dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    dy >>= 1;
    int py = dy * dy;
    int lo = (py & 0xFF) + (k & 0xFF);
    int q = lo & 0xFF;
    int hi = (py >> 8) + (k >> 8) + (lo >> 8);
    if (hi > 0xFF) hi = 0xFF;
    return (ll5(hi, q) << 2) & 0xFFFF;
}

static int iabs(int v) { return v < 0 ? -v : v; }

const sys_t *nearest_system(int galaxy, int qq9, int qq10)
{
    const sys_t *sys = galaxy_systems(galaxy), *best = NULL;
    int t = 127;
    for (int i = 0; i < 256; i++) {
        int d = (iabs(sys[i].x - qq9) >> 1) + (iabs(sys[i].y - qq10) >> 1);
        if (d < t) { t = d; best = &sys[i]; }
    }
    return best;
}

// a system the chart cursor can select: the nearest to its own spot (none nearer by (|dx|/2 + |dy|/2)). Cached
// a galaxy at a time: jump_max's table asked it 65,536 times over, a 256-system search each, and on the Presto
// that first visit took a frame to 136 ms (the curious career's first mis-jump)
static uint8_t s_own[8][32], s_own_ok[8];     // (a bit a system)
static int own_spot(int galaxy, int i)
{
    galaxy &= 7;
    if (!s_own_ok[galaxy]) {
        const sys_t *sys = galaxy_systems(galaxy);
        for (int j = 0; j < 256; j++) if (nearest_system(galaxy, sys[j].x, sys[j].y) == &sys[j]) s_own[galaxy][j >> 3] |= (uint8_t) (1u << (j & 7));
        s_own_ok[galaxy] = 1;
    }
    return (s_own[galaxy][(i & 255) >> 3] >> (i & 7)) & 1;
}

// systems the game wouldn't let us jump to (H refused twice: a near neighbour takes the selection)
uint8_t g_unreachable[8][32];
int g_unreachable_n;            // (systems left out so far: the farm model is made again when it grows)

// the longest jump from a system that a random mis-jump (3 in 256 jumps; it drops us at (x, y XOR 31)
// with the fuel spent) can't strand: from there, some system must be in reach of what's left. Stranded
// in witchspace there's no pod and no way out; without saves that's the career (0.5 LY to spare)
int g_safe_jumps = 1;
// the distance (distance_qq8) to the nearest selectable system from (x, y), not counting one on the spot; 999: none.
// distance_qq8 is the square root of min(dx^2 + (dy/2)^2, 0xFFFF), never decreasing as that grows, so the nearest
// is the one with the least such sum, and one square root does (jump_max's table took 131,072 of them, 3.6 ms on a
// macbox core, ~100 ms on the Presto)
static int nearest_reach(int galaxy, int x, int y)
{
    const sys_t *sys = galaxy_systems(galaxy);
    int best = -1, bs = 1 << 30;
    for (int j = 0; j < 256; j++) {
        int dx = sys[j].x - x, dy = sys[j].y - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        dy >>= 1;
        int s = dx * dx + dy * dy;
        if (s > 0 && s < bs && own_spot(galaxy, j)) bs = s, best = j;
    }
    return best < 0 ? 999 : distance_qq8(x, y, sys[best].x, sys[best].y);
}
static uint8_t s_jmax[8][256], s_jmax_ok[8], s_trap[8][32];      // (the longest is 65; a trap a bit a system)
int jump_max(int galaxy, int num)
{
    if (!g_safe_jumps) return 70;
    galaxy &= 7;
    if (!s_jmax_ok[galaxy]) {
        PT_START(tj0);
        const sys_t *sys = galaxy_systems(galaxy);
        for (int i = 0; i < 256; i++) {
            int reach = nearest_reach(galaxy, sys[i].x, sys[i].y ^ 31);
            int m = 70 - reach - 5;
            s_jmax[galaxy][i] = (uint8_t) (m < 0 ? 0 : m);
        }
        // traps: no neighbour within that range, so no safe way on: never planned into
        for (int i = 0; i < 256; i++) if (!(nearest_reach(galaxy, sys[i].x, sys[i].y) <= s_jmax[galaxy][i])) s_trap[galaxy][i >> 3] |= (uint8_t) (1u << (i & 7));
        s_jmax_ok[galaxy] = 1;
        PT_ADD(5, tj0);
    }
    return s_jmax[galaxy][num & 255];
}
int jump_trap(int galaxy, int num)
{
    if (!g_safe_jumps) return 0;
    jump_max(galaxy, num);
    return (s_trap[galaxy & 7][(num & 255) >> 3] >> (num & 7)) & 1;
}

int in_range(int galaxy, int x, int y, int max_qq8, const sys_t **out, int *dist, int max)
{
    const sys_t *sys = galaxy_systems(galaxy);
    int n = 0;
    for (int i = 0; i < 256; i++) {
        if ((g_unreachable[galaxy & 7][i >> 3] >> (i & 7)) & 1) continue;
        int d = distance_qq8(x, y, sys[i].x, sys[i].y);
        if (!(d > 0 && d <= max_qq8)) continue;
        // (the chart cursor snaps to the nearest system by (|dx|/2 + |dy|/2): one that loses its own
        // spot to a neighbour can't be selected, and the jump is refused)
        if (!own_spot(galaxy, i)) continue;
        // insert after every entry with dist <= d (systems arrive in num order: stable)
        int j = n;
        while (j > 0 && dist[j - 1] > d) j--;
        if (j >= max) continue;
        int last = n < max ? n : max - 1;
        for (int k = last; k > j; k--) { out[k] = out[k - 1]; dist[k] = dist[k - 1]; }
        out[j] = &sys[i]; dist[j] = d;
        if (n < max) n++;
    }
    return n;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c; }

const sys_t *find_system(int galaxy, const char *name)
{
    const sys_t *sys = galaxy_systems(galaxy);
    for (int i = 0; i < 256; i++) {
        const char *a = sys[i].name, *b = name;
        while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return &sys[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Market
// ---------------------------------------------------------------------------

static int item_price_qq24(int item, int economy, int qq26)
{
    const item_t *it = &QQ23[item];
    int b1 = item_byte1(item);
    int a = (it->base_price + (qq26 & it->mask)) & 0xFF;
    int v = (economy * (b1 & 31)) & 0xFF;
    a = (b1 & 0x80) ? ((a - v) & 0xFF) : ((a + v) & 0xFF);
    return a;
}

int item_price_tenths(int item, int economy, int qq26)
{
    return item_price_qq24(item, economy, qq26) * 4;
}

static int32_t exp_sum[17][8];
static uint8_t exp_done[17][8];

double exp_price_tenths(int item, int economy)
{
    if (!exp_done[item][economy]) {
        int32_t t = 0;
        for (int q = 0; q < 256; q++) t += item_price_tenths(item, economy, q);
        exp_sum[item][economy] = t;
        exp_done[item][economy] = 1;
    }
    return (double)exp_sum[item][economy] / 256;
}

int equipment_available(int tek)
{
    int a = tek + 3;
    return a >= 12 ? 14 : a;
}

// ---------------------------------------------------------------------------
// trade planning
// ---------------------------------------------------------------------------

static int floordiv(int a, int b)   // Python //, b > 0
{
    int q = a / b;
    if ((a % b) != 0 && a < 0) q--;
    return q;
}

void best_cargo(int here_economy, int qq26, const uint8_t avl[17], int dest_economy, int cash, int room_t,
                int illegal_ok, uint8_t buys[17], int *spend, double *profit)
{
    struct { double ratio, gain; int buy, item; } c[17], t;
    int nc = 0;
    for (int item = 0; item < 17; item++) {
        buys[item] = 0;
        if (avl[item] <= 0) continue;
        if (!illegal_ok && (item == 3 || item == 6 || item == 10)) continue;
        int buy = item_price_tenths(item, here_economy, qq26);
        double gain = exp_price_tenths(item, dest_economy) - buy;
        if (gain > 1 && buy > 0) {
            c[nc].ratio = gain / buy; c[nc].gain = gain; c[nc].buy = buy; c[nc].item = item;
            nc++;
        }
    }
    // sort descending by (ratio, gain, buy, item)
    for (int i = 1; i < nc; i++) {
        t = c[i];
        int j = i;
        while (j > 0) {
            int gt;   // t > c[j-1] ?
            if (t.ratio != c[j - 1].ratio) gt = t.ratio > c[j - 1].ratio;
            else if (t.gain != c[j - 1].gain) gt = t.gain > c[j - 1].gain;
            else if (t.buy != c[j - 1].buy) gt = t.buy > c[j - 1].buy;
            else gt = t.item > c[j - 1].item;
            if (!gt) break;
            c[j] = c[j - 1];
            j--;
        }
        c[j] = t;
    }
    int sp = 0;
    double pr = 0.0;
    int budget = cash;   // reserve = 0
    for (int i = 0; i < nc; i++) {
        int item = c[i].item, buy = c[i].buy;
        int n = floordiv(budget - sp, buy);
        if (avl[item] < n) n = avl[item];
        if (item <= 12) { if (room_t < n) n = room_t; }
        else if (n > 199) n = 199;
        if (n <= 0) continue;
        buys[item] = (uint8_t)n;
        sp += n * buy;
        pr += n * c[i].gain;
        if (item <= 12) room_t -= n;
    }
    *spend = sp;
    *profit = pr;
}

// (systems in a jump's range: at most 19 within 7 LY of any point of any galaxy, counted over all 8 x 65,536; room
// for 64, as agent.c's own lists have)
#define RNG_N 64
static const sys_t *rng_sys[RNG_N];
static int rng_dist[RNG_N];

int plan_trip(int galaxy, int x, int y, int here_economy, int qq26, const uint8_t avl[17], int cash, int fuel,
              int crgo, const uint8_t cargo[17], plan_t *out)
{
    return plan_trip_seen(galaxy, x, y, here_economy, qq26, avl, cash, fuel, crgo, cargo, NULL, 0, out);
}

int plan_trip_seen(int galaxy, int x, int y, int here_economy, int qq26, const uint8_t avl[17], int cash, int fuel,
                   int crgo, const uint8_t cargo[17], const uint8_t seen[32], double bonus, plan_t *out)
{
    (void)fuel;
    const double risk_weight = 200.0;
    int held = 0;
    for (int i = 0; i < 13; i++) held += cargo[i];
    int room_t = crgo - 1 - held - 1;
    if (room_t < 0) room_t = 0;
    const sys_t *here = nearest_system(galaxy, x, y);
    // (stuck in a trap anyway, after a pod or a way out of witchspace: one full-range jump, the risk taken)
    int n = in_range(galaxy, x, y, here && !jump_trap(galaxy, here->num) ? jump_max(galaxy, here->num) : 70, rng_sys, rng_dist, RNG_N);
    {
        int k = 0;
        for (int i = 0; i < n; i++) if (!jump_trap(galaxy, rng_sys[i]->num)) rng_sys[k] = rng_sys[i], rng_dist[k++] = rng_dist[i];
        n = k;
    }
    int found = 0;
    for (int i = 0; i < n; i++) {
        const sys_t *sy = rng_sys[i];
        int dist = rng_dist[i];
        int fuel_cost = 2 * dist;
        uint8_t buys[17];
        int spend;
        double profit;
        best_cargo(here_economy, qq26, avl, sy->economy, cash - fuel_cost, room_t, 0, buys, &spend, &profit);
        double score = profit - fuel_cost - risk_weight * g_risk_mult * GOV_RISK[sy->government] * 10 + 0.0;
        if (seen && !(seen[sy->num >> 3] & (1u << (sy->num & 7)))) score += bonus;
        if (!found || score > out->score) {
            found = 1;
            out->dest = sy;
            out->dist = dist;
            for (int k = 0; k < 17; k++) out->buys[k] = buys[k];
            out->profit = profit;
            out->cost = spend;
            out->score = score;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// route: Dijkstra over hops, popping in (cost, num) order
// ---------------------------------------------------------------------------

static double r_best[256];
static int16_t r_prev[256];
static uint8_t r_seen[256], r_done[256];

int route(int galaxy, int x, int y, int to_x, int to_y, float risk_weight, const sys_t **path, int max)
{
    const sys_t *sys = galaxy_systems(galaxy);
    const sys_t *start = nearest_system(galaxy, x, y), *goal = nearest_system(galaxy, to_x, to_y);
    if (!start || !goal) return 0;
    double rw = risk_weight;
    for (int i = 0; i < 256; i++) { r_seen[i] = 0; r_done[i] = 0; }
    r_best[start->num] = 0.0; r_prev[start->num] = -1; r_seen[start->num] = 1;
    for (;;) {
        int n = -1;
        for (int i = 0; i < 256; i++)
            if (r_seen[i] && !r_done[i] && (n < 0 || r_best[i] < r_best[n])) n = i;
        if (n < 0 || n == goal->num) break;
        r_done[n] = 1;
        double c = r_best[n];
        const sys_t *a = &sys[n];
        for (int b = 0; b < 256; b++) {
            int d = distance_qq8(a->x, a->y, sys[b].x, sys[b].y);
            if (!(d > 0 && d <= 70)) continue;
            // (one the chart cursor can't select, losing its spot to a neighbour, as in in_range: a hop
            // through Razari, a unit from Alace in galaxy 3, landed at Alace each time, for 30 game hours)
            if (!own_spot(galaxy, b)) continue;
            if (b != goal->num && jump_trap(galaxy, b)) continue;
            // a hop a random mis-jump could strand us on (see jump_max): only where there's no other way,
            // as 25 hops' worth (Lave has no safe way out to the Leoned - Ceesxe loop; without the move
            // the ship stayed weak for hours and died to pirate packs)
            int unsafe = !jump_trap(galaxy, n) && d > jump_max(galaxy, n);
            double nc = c + 1 + (unsafe ? 25 : 0) + (b == goal->num ? 0 : rw * g_risk_mult * GOV_RISK[sys[b].government]);
            if (!r_seen[b] || nc < r_best[b]) {
                r_best[b] = nc; r_prev[b] = (int16_t)n; r_seen[b] = 1;
            }
        }
    }
    if (!r_seen[goal->num]) return 0;
    int len = 0;
    for (int k = goal->num; k >= 0; k = r_prev[k]) len++;
    if (len > max) return 0;
    int i = len;
    for (int k = goal->num; k >= 0; k = r_prev[k]) path[--i] = &sys[k];
    return len;
}

// a market's stock, as GVL sets it on arrival (alien items: none)
int item_avail(int item, int economy, int qq26)
{
    if (item >= 16) return 0;
    const item_t *it = &QQ23[item];
    int f = it->factor < 0 ? -it->factor : it->factor;
    int a = (it->base_quantity + (qq26 & it->mask)) & 0xFF;
    a = it->factor < 0 ? a + economy * f : a - economy * f;
    a &= 0xFF;
    if (a & 0x80) a = 0;
    return a & 63;
}

// ---------------------------------------------------------------------------
// Trade loops across the whole galaxy
// ---------------------------------------------------------------------------

static double one_way(const sys_t *a, const sys_t *b, int cash, int room_t)
{
    uint8_t buys[17];
    int spend;
    double best = 0;
    // mean over a few market swings (QQ26): prices and stock, as GVL will set them on arrival
    for (int q = 0; q < 256; q += 64) {
        double profit;
        uint8_t avl[17];
        for (int i = 0; i < 17; i++) avl[i] = (uint8_t) item_avail(i, a->economy, q + 32);
        best_cargo(a->economy, q + 32, avl, b->economy, cash, room_t, 0, buys, &spend, &profit);
        best += profit / 4;
    }
    return best;
}

double loop_rate(const sys_t *a, const sys_t *b, int cash, int room_t)
{
    int d = distance_qq8(a->x, a->y, b->x, b->y);
    double risk = 200.0 * 10 * g_risk_mult * (GOV_RISK[a->government] + GOV_RISK[b->government]) / 2;
    return (one_way(a, b, cash, room_t) + one_way(b, a, cash, room_t)) / 2 - 2 * d - risk;
}

double best_loop(int galaxy, int cash, int room_t, const sys_t **a, const sys_t **b)
{
    double best = -1e30;
    *a = *b = NULL;
    return best_loop_part(galaxy, cash, room_t, 0, 256, best, a, b);
}

// safe hops from one system to every other (255: not by safe jumps), for the loop search: the best loop
// in the galaxy is no use if getting there means hops a mis-jump could strand us on
uint8_t g_loop_reach[256];
int g_loop_reach_on;
void safe_reach(int galaxy, int from)
{
    const sys_t *sys = galaxy_systems(galaxy);
    int q[256], h = 0, t = 0;
    memset(g_loop_reach, 255, sizeof g_loop_reach);
    g_loop_reach[from & 255] = 0;
    q[t++] = from & 255;
    while (h < t) {
        int a = q[h++];
        for (int b = 0; b < 256; b++) {
            if (g_loop_reach[b] != 255) continue;
            int d = distance_qq8(sys[a].x, sys[a].y, sys[b].x, sys[b].y);
            if (d > 0 && d <= jump_max(galaxy, a) && !jump_trap(galaxy, b) && own_spot(galaxy, b)) {
                g_loop_reach[b] = (uint8_t) (g_loop_reach[a] + 1 < 254 ? g_loop_reach[a] + 1 : 254);
                q[t++] = b;
            }
        }
    }
}

double best_loop_part(int galaxy, int cash, int room_t, int i0, int i1, double best, const sys_t **a, const sys_t **b)
{
    const sys_t *sys = galaxy_systems(galaxy);
    for (int i = i0; i < i1; i++) {
        if (g_loop_reach_on && g_loop_reach[i] > 12) continue;
        int n = in_range(galaxy, sys[i].x, sys[i].y, jump_max(galaxy, i), rng_sys, rng_dist, RNG_N);
        for (int k = 0; k < n; k++) {
            if (rng_sys[k]->num <= i) continue;
            if (rng_dist[k] > jump_max(galaxy, rng_sys[k]->num)) continue;      // (the way back too)
            if (jump_trap(galaxy, i) || jump_trap(galaxy, rng_sys[k]->num)) continue;
            double r = loop_rate(&sys[i], rng_sys[k], cash, room_t);
            if (r > best) best = r, *a = &sys[i], *b = rng_sys[k];
        }
    }
    return best;
}

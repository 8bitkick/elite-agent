// The career: trade, fit out, hunt bounties in anarchies or farm Thargoids in witchspace, fly both
// missions; efficient goes straight for Elite, curious explores; both save at every dock.
// After a death or a step that went wrong, the game is picked up again (the last save, if any).
// player_frame() runs it one frame at a time.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "player.h"
#include "drv.h"
#include "docked.h"
#include "flight.h"
#include "pilot.h"
#include "galaxy.h"
#include "fair.h"

#define NAME "CLAUDE"
static const int ORARRA[2] = {144, 33}, CEERDI[2] = {215, 84}, BIRERA[2] = {63, 72};

// ---- the shopping list, in order: (item, price in tenths, cash to keep back after buying)
// (E.C.M. and the escape pod first: without saves the starting ship's deaths were missiles it had no
// E.C.M. for, and a pod turns a lost fight into a new ship with the tally kept)
enum { S_ECM, S_POD, S_CARGO_BAY, S_BEAM, S_ENERGY, S_SCOOPS, S_MILITARY, S_REAR, S_BOMB, S_HYPER, S_N };
static const struct { int item, price, keep, view; const char *name; } SHOP[S_N] = {
    {EQ_ECM, 6000, 1000, 0, "ECM"}, {EQ_ESCAPE_POD, 10000, 1500, 0, "escape pod"},
    {EQ_CARGO_BAY, 4000, 1500, 0, "cargo bay"}, {EQ_BEAM, 10000, 4000, 0, "beam laser"},
    {EQ_ENERGY_UNIT, 15000, 6000, 0, "energy unit"},
    {EQ_SCOOPS, 5250, 3000, 0, "fuel scoops"},
    {EQ_MILITARY, 60000, 3000, 0, "military laser"}, {EQ_BEAM, 10000, 5000, 1, "rear beam laser"},
    {EQ_BOMB, 9000, 5000, 0, "energy bomb"}, {EQ_HYPERDRIVE, 50000, 3000, 0, "galactic hyperdrive"},
};

// ---- curiosity: the systems we've docked at (per galaxy), and hunts at each anarchy (this galaxy)
static uint8_t g_seen[8][32];
static uint8_t g_hunts[256];
static int g_hunts_galaxy = -1;
static const sys_t *here_sys(void) { return nearest_system(galaxy(), here_x(), here_y()); }
static int is_seen(int g, int n) { return (g_seen[g][n >> 3] >> (n & 7)) & 1; }
static int seen_count(int g) { int c = 0; for (int i = 0; i < 256; i++) c += is_seen(g, i); return c; }
static void mark_seen(void) {
    const sys_t *h = here_sys();
    if (!h) return;
    if (!is_seen(galaxy(), h->num))
        note("{\"event\": \"new_system\", \"name\": \"%s\", \"galaxy\": %d, \"seen\": %d}", h->name, galaxy() + 1, seen_count(galaxy()) + 1);
    g_seen[galaxy()][h->num >> 3] |= (uint8_t) (1u << (h->num & 7));
    if (g_hunts_galaxy != galaxy()) { memset(g_hunts, 0, sizeof g_hunts); g_hunts_galaxy = galaxy(); }
}
// the two ways to play: efficient goes straight for Elite (the missions, then whatever scores fastest);
// curious sees the galaxies on the way (new systems pay extra in the trade plan, anarchies are hunted once
// each, witchspace is one session in three, and after the missions every galaxy in turn)
#ifndef PLAYER_CURIOUS
#define PLAYER_CURIOUS 0
#endif
int g_curious = PLAYER_CURIOUS;
#define NOVELTY (g_curious ? 3000 : 0)    // tenths: expected profit credited to a system never docked at
#define EXPLORE_SEEN (g_curious ? 16 : 24)     // after both missions: on to the next galaxy once we've docked at this many here

static int m1(void) { return mission() & 3; }
static int m2(void) { return (mission() >> 2) & 3; }
// (test) efficient without the missions: rank is the kill tally alone; the missions' galactic hyperdrive
// meant ~4 game hours of trading in the first 15, with no farming meanwhile
int g_no_missions;
static int m2_due(void) { return !g_no_missions && m1() == 2 && m2() == 0 && tally_seen() >= 1280; }
static int explore_galaxy(void) { return g_curious && m1() == 2 && m2() == 3 && galaxy() < 7 && seen_count(galaxy()) >= EXPLORE_SEEN; }
static int wants_hyperdrive(void) {
    return !has_hyperdrive() && ((!g_no_missions && m1() == 1 && galaxy() == 0) || (m2_due() && galaxy() == 1) || explore_galaxy());
}
static int ready_for_m1(void) { return !g_no_missions && m1() == 1 && laser(0) == 0x97 && (galaxy() == 1 || has_hyperdrive()); }
static const int *m2_target(void) {
    if (galaxy() != 2 || g_no_missions) return NULL;
    return m2() == 1 ? CEERDI : m2() == 2 ? BIRERA : NULL;
}
// the escape pod: never a gate, and bought (or replaced) only when rich. With a save at every dock a lost
// fight costs the trip since, ~1-5 minutes; the pod cost 1000 Cr a use (often trading to afford it),
// shopping trips, and up to 4 game hours waiting for one before farming. Rich, it costs nothing
#define POD_RICH 200000         // tenths: 20000 Cr
// (test) no escape pod: with a save at every dock a lost fight costs the trip since; the pod costs
// 1000 Cr a use, shopping trips, and held witchspace farming back (there is no pod in witchspace)
int g_no_pod;
static int farm_ready(void) {
    return (laser(0) == 0x8F || laser(0) == 0x97) && has_ecm() && has_energy_unit();
}

static int have(int s) {
    switch (s) {
        case S_CARGO_BAY: return cargo_bay() > 22;
        case S_ECM: return has_ecm();
        case S_BEAM: return laser(0) == 0x8F || laser(0) == 0x97;
        case S_POD: return has_escape_pod();
        case S_ENERGY: return has_energy_unit();
        case S_SCOOPS: return has_scoops();
        case S_MILITARY: return laser(0) == 0x97;
        case S_REAR: return laser(1) != 0;
        case S_BOMB: return R(A_BOMB) != 0;
        case S_HYPER: return has_hyperdrive();
    }
    return 1;
}
static int on_list(int s) { return (s != S_HYPER || (wants_hyperdrive() && laser(0) == 0x97)) && !(s == S_POD && (g_no_pod || cash() < POD_RICH)); }

// the first thing on the list we haven't got: S_N if none, and whether we can afford it
// (beam before the pod and the bay: in 18 careers each, dead by 4 h 15 -> 11; beam before ECM: missiles
// killed the early ship)
int g_shop_order[S_N] = {S_ECM, S_BEAM, S_POD, S_CARGO_BAY, S_ENERGY, S_SCOOPS, S_MILITARY, S_REAR, S_BOMB, S_HYPER};
static int next_purchase(int *affordable) {
    for (int i = 0; i < S_N; i++) {
        int s = g_shop_order[i];
        if (!on_list(s) || have(s)) continue;
        *affordable = cash() >= SHOP[s].price + SHOP[s].keep;
        return s;
    }
    return S_N;
}

// breadth-first hops (<= 7 LY) from here to the nearest system matching `ok`; the path in path[]
static int bfs(int (*ok)(const sys_t *), const sys_t **path, int max) {
    static int16_t prev[256];
    static uint8_t q[256];
    const sys_t *sys = galaxy_systems(galaxy());
    const sys_t *start = nearest_system(galaxy(), here_x(), here_y());
    if (!start) return 0;
    for (int i = 0; i < 256; i++) prev[i] = -2;
    int head = 0, tail = 0, found = -1;
    q[tail++] = start->num;
    prev[start->num] = -1;
    while (head < tail) {
        int a = q[head++];
        if (a != start->num && ok(&sys[a])) { found = a; break; }
        const sys_t *nb[64];
        int d[64], n = in_range(galaxy(), sys[a].x, sys[a].y, 70, nb, d, 64);
        for (int i = 0; i < n; i++)
            if (prev[nb[i]->num] == -2) { prev[nb[i]->num] = (int16_t) a; q[tail++] = nb[i]->num; }
    }
    if (found < 0) return 0;
    int n = 0;
    for (int a = found; a >= 0; a = prev[a]) n++;
    if (n > max) return 0;
    int i = n;
    for (int a = found; a >= 0; a = prev[a]) path[--i] = &sys[a];
    return n;
}
static int g_need_tek;
static int has_tek(const sys_t *s) { return s->tech >= g_need_tek; }
static int is_anarchy(const sys_t *s) { return s->government == 0; }
static int g_hunts_here;
static const sys_t *g_ground;      // the anarchy we're on our way to hunt at
static int is_fresher_anarchy(const sys_t *s) { return s->government == 0 && g_hunts[s->num] < g_hunts_here; }
static int is_ground(const sys_t *s) { return s == g_ground; }
int show_hurt(void);        // (pilot.c: energy under 200 or a shield under 128)
// (curious: trips since the last hunting or witchspace session. The solid career, after galaxy 8 was charted,
// flew 85 h between two systems: the way to a fresher anarchy went by bfs from the anarchy and by route() from
// the hop, route() failed there, the fallback was the nearest anarchy (the one just left), and back again. No
// session was ever scored after that, so the one-in-three rule never chose witchspace again either)
static int g_no_session;
// (curious: the systems the last four trips started from. A, B, A, B is a ping-pong (the round-3 career shuttled
// between two systems for ~100 h, 600 'hunt chosen' trips per 40 h and no hunt): then hunt at the nearest anarchy,
// hunted or not, reached by breadth-first search, a hop nearer each time)
static int g_last_sys[4] = {-1, -1, -1, -1}, g_force_hunt;

// ---- shopping, in list order: save up for the next thing before anything after it
static struct { CO; int i, s; int32_t cash0; } c_shop;
static int g_had_pod, g_had_bomb;      // once owned, never fly without them again
PT_THREAD(shop(void)) {
    CO_BEGIN(c_shop);
    g_had_pod |= has_escape_pod() && !g_no_pod;
    g_had_bomb |= R(A_BOMB) != 0;
    // a used escape pod or energy bomb first, before saving for anything else
    // (replacements keep money back like the rest of the list: a pod bought with our last 1000 Cr left
    // 4.5 Cr, no fuel, and every jump refused)
    if (g_had_pod && !has_escape_pod() && EQ_ESCAPE_POD < equipment_available(R(A_tek)) && cash() >= POD_RICH) {
        c_shop.cash0 = cash();
        CO_CALL(c_shop, equip(EQ_ESCAPE_POD, 0));
        if (g_bought) status_("Fitted escape pod for %.1f Cr (a replacement)", (c_shop.cash0 - cash()) / 10.0);
    }
    if (g_had_bomb && !R(A_BOMB) && EQ_BOMB < equipment_available(R(A_tek)) && cash() >= 9000 + 3000 + 2000) {
        c_shop.cash0 = cash();
        CO_CALL(c_shop, equip(EQ_BOMB, 0));
        if (g_bought) status_("Fitted energy bomb for %.1f Cr (a replacement)", (c_shop.cash0 - cash()) / 10.0);
    }
    for (c_shop.i = 0; c_shop.i < S_N; c_shop.i++) {
        int s = c_shop.s = g_shop_order[c_shop.i];
        if (!on_list(s) || have(s) || SHOP[s].item >= equipment_available(R(A_tek))) continue;
        if (cash() < SHOP[s].price + SHOP[s].keep) break;
        c_shop.cash0 = cash();
        CO_CALL(c_shop, equip(SHOP[c_shop.s].item, SHOP[c_shop.s].view));
        if (g_bought) status_("Fitted %s for %.1f Cr", SHOP[c_shop.s].name, (c_shop.cash0 - cash()) / 10.0);
        if (g_bought) note("{\"event\": \"equip\", \"item\": \"%s\", \"cash\": %.1f}", SHOP[c_shop.s].name, cash() / 10.0);
    }
    while (missiles() < 3 && cash() > 20000 && equipment_available(R(A_tek)) > 1) {
        CO_CALL(c_shop, equip(EQ_MISSILE, 0));
        if (!g_bought) break;
        status_("Bought a missile (%d aboard)", missiles());
    }
    CO_END(c_shop);
}

static int name_is_ours(void) { return !memcmp(IO->ram + A_NAME, NAME, strlen(NAME)); }

// saving: both modes save at every dock. A save takes ~200 frames (4 s) docked; a trip is 10-15 minutes,
// so it costs under 0.5% of the time, and a death costs only the trip since (back to the save). Fewer
// saves would only trade time to Elite for the count (run_sim SAVE=0 forces none, for the fight tests)
int g_save = 1;
int saving(void) { return g_save > 0; }
static struct { CO; } c_save;
// (never a broke commander over a solvent save: an escape pod left one with 25 Cr, fuel ate the rest,
// and every dock saved it, down to 5 Cr. A reload then put it back where it couldn't buy fuel: 115 times)
static long s_saved_cash = -1;
PT_THREAD(save_ours(void)) {
    CO_BEGIN(c_save);
    if (saving() && cash() < 2000 && s_saved_cash >= 0 && cash() < s_saved_cash) CO_EXIT(c_save);
    if (saving()) {
        CO_CALL(c_save, save_commander(name_is_ours() ? NULL : NAME));
        s_saved_cash = cash();
    }
    CO_END(c_save);
}

static int is_front_view(void) { return view() == 0; }

// ---- one hop, trading on the way; through a dangerous system without docking if one tank does both
static struct { CO; const sys_t *dest, *then; uint8_t buys[17]; } c_fly;
PT_THREAD(fly_to(const sys_t *dest, const sys_t *then)) {
    CO_BEGIN(c_fly);
    c_fly.dest = dest;
    c_fly.then = then;
    CO_CALL(c_fly, sell_all());
    CO_CALL(c_fly, refuel());
    CO_CALL(c_fly, shop());
    {
        uint8_t avl[17], held = 0;
        int spend;
        double profit;
        for (int i = 0; i < 17; i++) avl[i] = R(A_AVL + i);
        for (int i = 0; i < 13; i++) held += (uint8_t) cargo(i);
        int room = cargo_bay() - 2 - held;
        best_cargo(R(A_QQ28), R(A_QQ26), avl, c_fly.dest->economy, cash() - 200, room < 0 ? 0 : room, 0, c_fly.buys, &spend, &profit);
    }
    CO_CALL(c_fly, buy(c_fly.buys));
    CO_CALL(c_fly, save_ours());
    note("{\"event\": \"depart\", \"to\": \"%s\", \"cash\": %.1f, \"tally\": %.2f}", c_fly.dest->name, cash() / 10.0, tally());
    CO_CALL(c_fly, select_system(c_fly.dest->name));
    CO_CALL(c_fly, show_system_data(is_seen(galaxy(), c_fly.dest->num) ? 6 : 10));
    CO_CALL(c_fly, launch());
    CO_CALL(c_fly, hyperspace(c_fly.dest));
    if (witchspace()) {
        CO_CALL(c_fly, leave_witchspace());
    } else if (c_fly.then && c_fly.dest->government <= 1 && !(g_curious && show_hurt()) &&      // (curious: not on hurt)
               distance_qq8(c_fly.dest->x, c_fly.dest->y, c_fly.then->x, c_fly.then->y) <= fuel() - (70 - jump_max(galaxy(), c_fly.dest->num))) {
        // (and enough left over that a mis-jump on the second leg can't strand us: see jump_max)
        logf_("through %s without stopping, on to %s", c_fly.dest->name, c_fly.then->name);
        CO_CALL(c_fly, select_by_cursor(c_fly.then));
        CO_CALL(c_fly, hold_until(K_F0, is_front_view, 3, "front view"));
        CO_CALL(c_fly, hyperspace(c_fly.then));
    }
    CO_CALL(c_fly, dock(900));
    CO_END(c_fly);
}

// ---- a bounty hunt: launch, out of the safe zone (away from the planet: the slot faces it), wait
// for pirates and fight them; back to the station when worn down or after `minutes`
static struct { CO; uint32_t until, scoop_until; double tally0; int fights, fought, scoop_slot, held, scooped; uint8_t hold[17]; } c_hunt;

static double dist_of(const ship_t *s) { return sqrt((double) s->x * s->x + (double) s->y * s->y + (double) s->z * s->z); }
static int tonnes_held(void) { int t = 0; for (int i = 0; i < 13; i++) t += cargo(i); return t; }
// the nearest canister, alloy plate or escape pod worth scooping (scoops fitted, room in the hold)
static const ship_t *scoopable(const ship_t *sh, int n) {
    if (!has_scoops() || tonnes_held() >= cargo_bay() - 1) return NULL;
    const ship_t *best = NULL;
    for (int i = 0; i < n; i++) {
        const ship_t *s = &sh[i];
        if (!ship_in_space(s) || ship_dead(s) || (s->newb & 0x80) || (s->type != 3 && s->type != 4 && s->type != 5)) continue;
        if (s->slot == c_hunt.scoop_slot && expired(c_hunt.scoop_until)) continue;    // given up on this one
        double d = dist_of(s);
        if (d < 12000 && (!best || d < dist_of(best))) best = s;
    }
    return best;
}
PT_THREAD(hunt(float minutes)) {
    CO_BEGIN(c_hunt);
    c_hunt.tally0 = tally();
    c_hunt.fights = c_hunt.scooped = 0;
    c_hunt.scoop_slot = -1;
    c_hunt.held = tonnes_held();
    for (int i = 0; i < 17; i++) c_hunt.hold[i] = (uint8_t) cargo(i);
    {
        const sys_t *h = here_sys();
        status_("Hunting bounties at %s", h ? h->name : "?");
    }
    CO_CALL(c_hunt, launch());
    c_hunt.until = deadline(minutes * 60);
    while (R(A_SSPR)) {
        CO_YIELD(c_hunt);
        CO_CALL(c_hunt, fight_if_needed(&c_hunt.fought));
        if (c_hunt.fought) continue;
        static ship_t sh[NOSH];
        int n = see_ships(sh);
        const ship_t *pl = find_planet(sh, n);
        keyset_t keys = {{0}, 0};
        steer(pl ? (vec_t){-pl->x, -pl->y, -pl->z} : (vec_t){0, 0, 1}, jstx(), jsty(), 3, 3, &keys);
        speed_keys(speed(), 40, &keys);
        apply_flight(&keys);
    }
    logf_("out of the safe zone; waiting for pirates");
    for (;;) {
        CO_YIELD(c_hunt);
        {
            static ship_t sh[NOSH];
            const ship_t *t[1];
            int n = see_ships(sh);
            if (threats(sh, n, g_cp.engage_range, t, 1)) goto fight;     // (engage's own range: further off, it'd stop at once)
            if (tonnes_held() > c_hunt.held) {
                c_hunt.scooped += tonnes_held() - c_hunt.held;
                note("{\"event\": \"scooped\", \"held\": %d}", tonnes_held());
                for (int i = 0; i < 17; i++)
                    if (cargo(i) > c_hunt.hold[i]) status_("Scooped %s %dt", ITEM_NAMES[i], cargo(i) - c_hunt.hold[i]);
            }
            c_hunt.held = tonnes_held();
            for (int i = 0; i < 17; i++) c_hunt.hold[i] = (uint8_t) cargo(i);
            // (curious: home sooner, energy under 200 or a shield under 100: its show fights leave the front shield low,
            // and the next wave met it there)
            if (energy() < 150 || fsh() + ash() < 250 || expired(c_hunt.until)) break;
            if (g_curious && (energy() < 200 || fsh() < 100 || ash() < 100)) break;
            keyset_t keys = {{0}, 0};
            const ship_t *cn = scoopable(sh, n);
            if (cn) {
                // scooping: it has to pass within 64 on each axis, below our centre (or it's a collision),
                // so aim a little above it and come in slowly
                if (cn->slot != c_hunt.scoop_slot) c_hunt.scoop_slot = cn->slot, c_hunt.scoop_until = deadline(45);
                double d = dist_of(cn);
                // flying straight leaves its x and y as they are: line up early, then (close, where a
                // roll would swing it round the nose) gentle pitch only
                if (d > 1500 || cn->z < 200 || fabs((double) cn->x) > 40) steer((vec_t){cn->x, cn->y + 30, cn->z}, jstx(), jsty(), 3, 3, &keys);
                else steer((vec_t){0, cn->y + 30, cn->z}, jstx(), jsty(), 3, 6, &keys);
                speed_keys(speed(), d > 3000 ? 24 : d > 800 ? 12 : 6, &keys);
                apply_flight(&keys);
                continue;
            }
            const ship_t *pl = find_planet(sh, n);
            if (pl) steer((vec_t){pl->x, pl->y, pl->z}, jstx(), jsty(), 3, 3, &keys);   // nose to the planet: a short way back
            speed_keys(speed(), 4, &keys);
            apply_flight(&keys);
        }
        continue;
    fight:
        c_hunt.fights++;
        CO_CALL(c_hunt, engage(240));
    }
    logf_("hunt over: %d fights, %d t scooped, tally %.2f -> %.2f", c_hunt.fights, c_hunt.scooped, c_hunt.tally0, tally());
    status_("Hunt over: %d fights, %+.2f", c_hunt.fights, tally() - c_hunt.tally0);
    CO_CALL(c_hunt, dock(900));
    note("{\"event\": \"farmed\", \"tally\": %.2f, \"gained\": %.2f, \"fights\": %d, \"scooped\": %d, \"cash\": %.1f}", tally(),
         tally() - c_hunt.tally0, c_hunt.fights, c_hunt.scooped, cash() / 10.0);
    CO_END(c_hunt);
}

// ---- farming Thargoids: a forced mis-jump (2.66 points a Thargoid, three every time), fight them,
// then out to the nearest system. The hop leaves fuel for the way out: a mis-jump costs the hop's
// fuel and drops us at (x, y XOR 31) (MJP in the source)
static struct { CO; const sys_t *dest; double tally0; uint32_t t0; } c_ws;
static const sys_t *witch_hop(void) {
    const sys_t *out[1], *near[2];
    int d[1], nd[2];
    // (0.2 LY to spare on both: the game's distance can round against ours, and a jump it refuses is a stuck)
    if (!in_range(galaxy(), here_x(), here_y() ^ 31, 68, out, d, 1)) return NULL;
    int budget = 68 - d[0];                  // (after a refuel)
    int n = in_range(galaxy(), here_x(), here_y(), budget, near, nd, 2);
    return n ? near[0] : NULL;
}
PT_THREAD(witch(void)) {
    CO_BEGIN(c_ws);
    c_ws.dest = witch_hop();
    if (!c_ws.dest) CO_FAIL(c_ws, "no hop leaves fuel for witchspace");
    c_ws.tally0 = tally();
    c_ws.t0 = now();
    CO_CALL(c_ws, sell_all());
    CO_CALL(c_ws, refuel());
    CO_CALL(c_ws, shop());
    CO_CALL(c_ws, save_ours());
    CO_CALL(c_ws, allow_misjumps());         // (pause, X, unpause: it works docked too)
    status_("Into witchspace for Thargoids (a forced mis-jump towards %s)", c_ws.dest->name);
    CO_CALL(c_ws, select_system(c_ws.dest->name));
    CO_CALL(c_ws, show_system_data(is_seen(galaxy(), c_ws.dest->num) ? 6 : 10));
    CO_CALL(c_ws, launch());
    CO_CALL(c_ws, hold_until(K_F0, is_front_view, 3, "front view"));
    g_misjump = 1;
    CO_CALL(c_ws, hyperspace(c_ws.dest));
    if (witchspace()) CO_CALL(c_ws, leave_witchspace());
    CO_CALL(c_ws, dock(900));
    CO_END(c_ws);
}

// what each way of scoring has made, in points per game minute (dock to dock): the career picks the
// better, after trying each a few times
static struct { double pts, mins; int n; } g_rate[2];      // 0 bounty hunting, 1 witchspace
static double rate_of(int k) { return g_rate[k].mins > 0 ? g_rate[k].pts / g_rate[k].mins : 0; }
static void rate_add(int k, double pts, uint32_t frames) {
    g_rate[k].pts += pts, g_rate[k].mins += frames / 3000.0, g_rate[k].n++;
    status_("%s: %+.2f in %.1f min (hunting %.2f/min over %d, witchspace %.2f/min over %d)", k ? "Witchspace" : "Hunt", pts,
            frames / 3000.0, rate_of(0), g_rate[0].n, rate_of(1), g_rate[1].n);
    note("{\"event\": \"session\", \"kind\": \"%s\", \"points\": %.2f, \"minutes\": %.2f}", k ? "witchspace" : "hunt", pts,
         frames / 3000.0);
}
// Thargoids need a military laser (240 energy each, three at once; StarDot and the Elite wiki agree)
// (and money for fuel: Thargoids pay points, not cash, and every session buys fuel; broke, a jump can't
// start and nothing else can be done. Below WITCH_CASH, trade or hunt bounties first)
#define WITCH_CASH 1000      // tenths: 100 Cr
static int witch_ready(void) { return farm_ready() && laser(0) == 0x97 && laser(1) != 0 && cash() >= WITCH_CASH && witch_hop() != NULL; }
// (why not witchspace now: the first condition that fails, for the trip log)
static int choose_witch(void);
static const char *witch_why(void) {
    if (!(laser(0) == 0x8F || laser(0) == 0x97)) return "no witch: laser";
    if (!has_ecm()) return "no witch: ecm";
    if (!has_energy_unit()) return "no witch: energy unit";
    if (laser(0) != 0x97) return "no witch: military";
    if (laser(1) == 0) return "no witch: rear laser";
    if (cash() < WITCH_CASH) return "no witch: cash";
    if (witch_hop() == NULL) return "no witch: hop";
    return choose_witch() ? "witch" : "hunt chosen";
}
double g_explore_ratio = 0;     // (test, EXPLORE: 0 always explore)
static int choose_witch(void) {
    if (!witch_ready()) return 0;
    if (g_fair && !g_fair_misjump) return 0;       // (fair, without the Easter egg: no forced mis-jumps)
    if (g_curious) return g_rate[1].n * 2 <= g_rate[0].n;          // one session in three
    if (g_rate[1].n < 3) return 1;
    if (g_rate[0].n < 3) return 0;
    // one session in ten to the other: rates change as the ship and the galaxy do. (Efficient: not while one is
    // g_explore_ratio times the other: witchspace runs ~6 pts/min to hunting's ~1.3 all career, and the
    // re-measuring hunts were ~20 sessions, ~2.8 game hours)
    int best = rate_of(1) > rate_of(0);
    double hi = fmax(rate_of(0), rate_of(1)), lo = fmin(rate_of(0), rate_of(1));
    if ((g_rate[0].n + g_rate[1].n) % 10 == 9 && !(g_explore_ratio > 0 && hi > g_explore_ratio * lo)) return !best;
    return best;
}
static struct { double t; uint32_t f; int kind, pending; } g_sess;      // kind: -1 none

// ---- one step of the career from docked
static struct { CO; const sys_t *path[24]; int n; plan_t plan; int i; double far; const sys_t *a, *b; } c_trip;
double g_weak_risk = 4;
PT_THREAD(trip(void)) {
    CO_BEGIN(c_trip);
    {
        // a shopping trip: the next thing on the list is affordable but not sold here
        int aff = 0, s = next_purchase(&aff);
        if (s < S_N && aff && SHOP[s].item >= equipment_available(R(A_tek))) {
            for (g_need_tek = 0; g_need_tek < 15 && equipment_available(g_need_tek) <= SHOP[s].item; g_need_tek++) {}
            c_trip.n = bfs(has_tek, c_trip.path, 24);
            if (c_trip.n > 1) {
                logf_("shopping trip for the %s: %s", SHOP[s].name, c_trip.path[1]->name);
                note("{\"event\": \"trip\", \"why\": \"shop %s\"}", SHOP[s].name);
                CO_CALL(c_trip, fly_to(c_trip.path[1], NULL));
                CO_EXIT(c_trip);
            }
        }
    }
    if (ready_for_m1()) {                         // mission 1: the Constrictor at Orarra, galaxy 2
        if (galaxy() == 0) {
            CO_CALL(c_trip, sell_all());
            CO_CALL(c_trip, refuel());
            CO_CALL(c_trip, galactic_jump());
            CO_EXIT(c_trip);
        }
        if (here_x() == ORARRA[0] && here_y() == ORARRA[1]) {
            CO_CALL(c_trip, sell_all());
            CO_CALL(c_trip, refuel());
            CO_CALL(c_trip, shop());
            CO_CALL(c_trip, save_ours());
            CO_CALL(c_trip, hunt(8));
            CO_EXIT(c_trip);
        }
        c_trip.n = route(galaxy(), here_x(), here_y(), ORARRA[0], ORARRA[1], 3.0f, c_trip.path, 24);
        if (c_trip.n > 1) {
            CO_CALL(c_trip, fly_to(c_trip.path[1], c_trip.n > 2 ? c_trip.path[2] : NULL));
            CO_EXIT(c_trip);
        }
    }
    if (m2_due() && galaxy() == 1 && has_hyperdrive()) {   // mission 2: galaxy 3 at tally 1280
        CO_CALL(c_trip, sell_all());
        CO_CALL(c_trip, refuel());
        CO_CALL(c_trip, galactic_jump());
        CO_EXIT(c_trip);
    }
    {
        const int *tg = m2_target();
        if (tg && (here_x() != tg[0] || here_y() != tg[1])) {
            c_trip.n = route(galaxy(), here_x(), here_y(), tg[0], tg[1], 3.0f, c_trip.path, 24);
            if (c_trip.n > 1) {
                CO_CALL(c_trip, fly_to(c_trip.path[1], c_trip.n > 2 ? c_trip.path[2] : NULL));
                CO_EXIT(c_trip);
            }
        }
    }
    if (explore_galaxy() && has_hyperdrive()) {   // both missions done: a new galaxy to chart
        note("{\"event\": \"explore\", \"galaxy\": %d, \"seen\": %d}", galaxy() + 2, seen_count(galaxy()));
        CO_CALL(c_trip, sell_all());
        CO_CALL(c_trip, refuel());
        CO_CALL(c_trip, galactic_jump());
        CO_EXIT(c_trip);
    }
    {
        // saving for something big (the military laser, a hyperdrive): bounties pay too little,
        // so trade until it's affordable
        int aff = 0, sv = next_purchase(&aff);
        if (sv < S_N && !aff && SHOP[sv].price >= 50000) {
            static int said = -1;
            if (said != sv) status_("Saving up for the %s: trading", SHOP[sv].name), said = sv;
            note("{\"event\": \"trip\", \"why\": \"saving %s\"}", SHOP[sv].name);
            goto trading;
        }
    }
    note("{\"event\": \"trip\", \"why\": \"%s\"}", witch_why());
    if (g_curious) g_no_session++;
    // (the backstop: a dozen trips with no session at all, witchspace if it's ready)
    if (choose_witch() || (g_curious && g_no_session >= 12 && witch_ready())) {
        g_no_session = 0;
        g_sess.t = tally_seen(), g_sess.f = now(), g_sess.kind = 1;
        CO_CALL(c_trip, witch());
        g_sess.pending = 1;          // (scored once docked again: a reload in between takes its points back)
        CO_EXIT(c_trip);
    }
    if (g_curious) {
        const sys_t *h0 = here_sys();
        int me = h0 ? h0->num : -1;
        if (me != g_last_sys[0]) {
            for (int i = 3; i > 0; i--) g_last_sys[i] = g_last_sys[i - 1];
            g_last_sys[0] = me;
        }
        if (me >= 0 && g_last_sys[2] == me && g_last_sys[1] == g_last_sys[3] && g_last_sys[1] != me && !g_force_hunt) {
            g_force_hunt = 1, g_ground = NULL;
            logf_("ping-pong between two systems: hunting at the nearest anarchy");
            note("{\"event\": \"pingpong\"}");
        }
    }
    if (farm_ready()) {                           // bounty hunting, moving round the anarchies
        const sys_t *h = here_sys();
        if (h && h->government == 0) {
            // three sessions here, then on to the nearest anarchy we've hunted less
            g_hunts_here = g_hunts[h->num];
            if (g_force_hunt) g_force_hunt = 0, g_hunts_here = 0;      // (curious, after a ping-pong: here, now)
            if (g_hunts_here >= (g_curious ? 1 : 3) && (c_trip.n = bfs(is_fresher_anarchy, c_trip.path, 12)) > 1) {
                g_ground = c_trip.path[c_trip.n - 1];
                logf_("hunted %s %d times: on to %s", h->name, g_hunts_here, g_ground->name);
                CO_CALL(c_trip, fly_to(c_trip.path[1], NULL));
                CO_EXIT(c_trip);
            }
            if (g_hunts[h->num] < 255) g_hunts[h->num]++;
            g_ground = NULL;
            CO_CALL(c_trip, sell_all());
            CO_CALL(c_trip, refuel());
            CO_CALL(c_trip, shop());
            CO_CALL(c_trip, save_ours());
            g_sess.t = tally_seen(), g_sess.f = now(), g_sess.kind = 0;
            g_no_session = 0;
            CO_CALL(c_trip, hunt(5));
            g_sess.pending = 1;
            CO_EXIT(c_trip);
        }
        if (g_ground && !(g_curious && g_force_hunt)) c_trip.n = route(galaxy(), here_x(), here_y(), g_ground->x, g_ground->y, 3.0f, c_trip.path, 24);
        else c_trip.n = 0;
        // (curious: no route, then the same breadth-first way that chose it, a hop nearer each time)
        if (g_curious && g_ground && c_trip.n < 2) c_trip.n = bfs(is_ground, c_trip.path, 24);
        if (!g_ground || c_trip.n < 2) c_trip.n = bfs(is_anarchy, c_trip.path, 24);
        if (c_trip.n > 1) {
            CO_CALL(c_trip, fly_to(c_trip.path[1], NULL));
            CO_EXIT(c_trip);
        }
    }
    // trading: the best destination in range and cargo for it
trading:
    // the starting ship (pulse laser) loses fights the fitted one wins: in the first hour of careers, deaths
    // per arrival were 3 of 3 at anarchies, 5 of 31 at government 3, 4 of 65 at 4, none of 69 at 7
    g_risk_mult = laser(0) == 0x8F || laser(0) == 0x97 ? 1 : g_weak_risk;
    CO_CALL(c_trip, sell_all());
    CO_CALL(c_trip, refuel());
    CO_CALL(c_trip, shop());
    // the best trade loop in the galaxy (reached by safe jumps), 16 systems a frame
    { const sys_t *h0 = here_sys(); g_loop_reach_on = g_safe_jumps && h0; if (h0) safe_reach(galaxy(), h0->num); }
    c_trip.far = -1e30;
    c_trip.a = c_trip.b = NULL;
    for (c_trip.i = 0; c_trip.i < 256; c_trip.i += 16) {
        c_trip.far = best_loop_part(galaxy(), cash(), cargo_bay() - 2, c_trip.i, c_trip.i + 16, c_trip.far, &c_trip.a, &c_trip.b);
        CO_YIELD(c_trip);
    }
    {
        // worth the trip if it beats the best loop from here by a third
        const sys_t *h = here_sys(), *a = c_trip.a, *b = c_trip.b;
        int room = cargo_bay() - 2;
        double far = c_trip.far, near = -1e30;
        const sys_t *nb[64];
        int d[64], n = h ? in_range(galaxy(), h->x, h->y, 70, nb, d, 64) : 0;
        for (int i = 0; i < n; i++)
            if (nb[i] != h) { double r = loop_rate(h, nb[i], cash(), room); if (r > near) near = r; }
        if (a && h && a != h && b != h && far > near * 4 / 3 + 100) {
            c_trip.n = route(galaxy(), here_x(), here_y(), a->x, a->y, 3.0f, c_trip.path, 24);
            if (c_trip.n > 1 && c_trip.n <= 13) {
                logf_("a better loop: %s - %s %.1f Cr a jump (here %.1f), %d jumps away", a->name, b->name, far / 10, near / 10,
                      c_trip.n - 1);
                note("{\"event\": \"relocate\", \"to\": \"%s\", \"with\": \"%s\", \"rate\": %.1f, \"here\": %.1f, \"jumps\": %d}",
                     a->name, b->name, far / 10, near / 10, c_trip.n - 1);
                CO_CALL(c_trip, fly_to(c_trip.path[1], NULL));
                CO_EXIT(c_trip);
            }
        }
    }
    {
        uint8_t avl[17], crg[17];
        for (int i = 0; i < 17; i++) { avl[i] = R(A_AVL + i); crg[i] = (uint8_t) cargo(i); }
        if (!plan_trip_seen(galaxy(), here_x(), here_y(), R(A_QQ28), R(A_QQ26), avl, cash(), fuel(), cargo_bay(), crg,
                            g_seen[galaxy()], NOVELTY, &c_trip.plan)) {
            // (curious: no trade worth flying, then bounties instead of a reload: the round-4 career, short of cash after
            // a replacement energy bomb, reloaded the same docked save for 80 game hours. Here if it's an anarchy, else on
            // to the nearest)
            if (g_curious) {
                const sys_t *h = here_sys();
                if (h && h->government == 0) {
                    logf_("nowhere to trade: bounty hunting here");
                    g_sess.t = tally_seen(), g_sess.f = now(), g_sess.kind = 0;
                    g_no_session = 0;
                    CO_CALL(c_trip, hunt(5));
                    g_sess.pending = 1;
                    CO_EXIT(c_trip);
                }
                if ((c_trip.n = bfs(is_anarchy, c_trip.path, 24)) > 1) {
                    logf_("nowhere to trade: on to the nearest anarchy");
                    CO_CALL(c_trip, fly_to(c_trip.path[1], NULL));
                    CO_EXIT(c_trip);
                }
            }
            CO_FAIL(c_trip, "nowhere to go");
        }
    }
    logf_("plan: %s %d.%d LY, expected +%.1f Cr", c_trip.plan.dest->name, c_trip.plan.dist / 10, c_trip.plan.dist % 10,
          c_trip.plan.profit / 10);
    CO_CALL(c_trip, fly_to(c_trip.plan.dest, NULL));
    CO_END(c_trip);
}

// ---- the career loop, and recovery
static int s_recover;
static int s_played;         // the career has docked once (a title screen before that is the start)        // 0 none, 1 after a death, 2 after a stuck step
static struct { CO; } c_car;
PT_THREAD(career(void)) {
    CO_BEGIN(c_car);
    for (;;) {
        if (s_recover == 1 || title() || dying()) {
            if (s_recover != 1 && s_played) {   // a death nothing else noticed (in a hyperspace jump, say)
                logf_("died (found at the title screen)");
                status_("Died: reloading the last save");
                note("{\"event\": \"died\", \"why\": \"title\", \"tally\": %.2f}", tally());
            }
            s_recover = 0;
            PT_CALL(&c_car.pt, after_death());
        } else if (s_recover == 2) {
            s_recover = 0;
            PT_CALL(&c_car.pt, reload());
        } else if (!is_docked() && !briefing()) {
            g_in_flight = 1;
            PT_CALL(&c_car.pt, dock(900));
        }
        if (g_fail) { CO_YIELD(c_car); continue; }      // (player_frame sets up the recovery)
        PT_CALL(&c_car.pt, settle_docked());
        if (g_fail) { CO_YIELD(c_car); continue; }
        mark_seen();
        s_played = 1;
        if (g_sess.kind >= 0) {         // (the last session is over, whether it came back or died)
            rate_add(g_sess.kind, tally_seen() - g_sess.t, now() - g_sess.f);
            g_sess.kind = -1, g_sess.pending = 0;
        }
        {
            static const char *rank0;
            const sys_t *h = here_sys();
            status_("Docked at %s", h ? h->name : "?");
            if (rank0 && strcmp(rank0, rank_name())) {
                status_("Promoted to %s", rank_name());
                note("{\"event\": \"rank\", \"rank\": \"%s\", \"tally\": %.2f}", rank_name(), tally());
            }
            rank0 = rank_name();
            status_("Status %s - %s · tally %.1f", rank_name(), legal_name(), tally());
        }
        logf_("== at (%d,%d) G%d cash %ld.%ld tally %.2f", here_x(), here_y(), galaxy() + 1, (long) cash() / 10,
              (long) cash() % 10, tally());
        PT_CALL(&c_car.pt, trip());
        if (!g_fail && (is_docked() || briefing())) {
            PT_CALL(&c_car.pt, settle_docked());
            if (!g_fail) note("{\"event\": \"docked\", \"g\": %d, \"x\": %d, \"y\": %d, \"cash\": %.1f, \"tally\": %.2f}", galaxy() + 1,
                              here_x(), here_y(), cash() / 10.0, tally());
        }
        CO_YIELD(c_car);
    }
    CO_END(c_car);
}

void player_init(player_io_t *io) {
    IO = io;
    fair_reset();
    g_sess.kind = -1;
    g_fail = NULL;
    g_gen++;
    c_car.pt.lc = 0;
    c_car.gen = g_gen;
}

void player_frame(void) {
    fair_begin_frame();         // (fair mode: what the player sees, and when)
    drv_frame_start();          // (a death sets g_fail)
    if (g_fail) {
        int died = !strcmp(g_fail, "died");
        logf_("%s: back to the last save", g_fail);
        if (died) status_("Died: reloading the last save");
        else status_("Stuck (%s): reloading the last save", g_fail);
        note("{\"event\": \"%s\", \"why\": \"%s\", \"tally\": %.2f}", died ? "died" : "stuck", g_fail, tally());
        s_recover = died ? 1 : 2;
        g_fail = NULL;
        g_gen++;                // every coroutine starts afresh
        g_in_flight = 0;
        g_misjump = g_hold_ctrl = g_bail_energy = 0;
        (void) 0;    // (an open session is scored at the next dock, after the reload: a death scores what the save had)
        hold_none();
        c_car.pt.lc = 0;
        c_car.gen = g_gen;
    }
    career();
    fair_end_frame();
}

// ---- the fight lab (cplayer/lab.c): from a snapshot taken at a fight's start, fight (and in
// witchspace, get out) as the career would, for up to four game minutes, as sim/eval.py did
static struct { CO; uint32_t f0, quiet; int fought; } c_lab;
int g_lab_done, g_lab_pod;
int g_lab_window = 240;     // seconds
uint32_t g_lab_cleared;
uint32_t g_lab_t0 = 0xFFFFFFFF;
int g_lab_undamped;
static int lab_threats(int rng) {
    static ship_t sh[NOSH];
    const ship_t *t[1];
    int n = see_ships(sh);
    return threats(sh, n, rng, t, 1);
}
PT_THREAD(lab(void)) {
    CO_BEGIN(c_lab);
    c_lab.f0 = now();
    g_lab_t0 = now();           // (the fight's clock starts here)
    g_in_flight = 1;
    while (now() - c_lab.f0 < (uint32_t) g_lab_window * 50) {
        if (witchspace()) {
            CO_CALL(c_lab, leave_witchspace());
            if (!g_lab_cleared) g_lab_cleared = now() - c_lab.f0;
            break;
        }
        CO_CALL(c_lab, engage(240));
        if (is_docked()) { g_lab_pod = 1; break; }
        if (!g_lab_cleared) g_lab_cleared = now() - c_lab.f0;
        for (c_lab.quiet = 0; c_lab.quiet < 150; c_lab.quiet++) {   // a little while for the next
            CO_YIELD(c_lab);
            keyset_t k = {{0}, 0};
            apply_flight(&k);
            if (lab_threats(g_cp.engage_range)) break;
        }
        if (c_lab.quiet >= 150) break;
    }
    g_lab_done = 1;
    CO_END(c_lab);
}
extern int g_depart_y;
void player_lab_init(player_io_t *io) {
    IO = io;
    fair_reset();
    g_fail = NULL;
    g_gen++;
    c_lab.pt.lc = 0;
    c_lab.gen = g_gen;
    g_lab_done = g_lab_pod = 0;
    drv_reset_keys();
    g_lab_cleared = 0;
    g_lab_t0 = 0xFFFFFFFF;
    g_misjump = g_hold_ctrl = g_bail_energy = 0;
    if (witchspace()) g_depart_y = here_y() ^ 31;     // (the snapshot is after the flip: no waiting for it)
}
const char *g_lab_why;
int player_lab_frame(void) {        // 1 when finished (or dead: g_fail "died")
    fair_begin_frame();
    drv_frame_start();
    if (g_fail) { g_lab_why = g_fail; fair_end_frame(); return 1; }
    lab();
    if (g_fail) g_lab_why = g_fail;
    fair_end_frame();
    return g_lab_done || g_fail != NULL;
}

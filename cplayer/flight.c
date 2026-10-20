#include "flight.h"
#include "docked.h"
#include "pilot.h"
#include "fair.h"
#include <stdlib.h>
#include <math.h>

static int is_space_front(void) { return view() == 0 && !is_docked(); }
static int is_front(void) { return view() == 0; }
static int is_short(void) { return view() == V_SHORT; }
static int is_counting(void) { return hyperspace_count() > 0; }

int g_undamped = 1;         // fly with keyboard damping off (set after launch, never in a fight): packs +10% pts/min
int g_launch_clear = 60;    // frames of climbing at full throttle after launch (0: none)
PT_THREAD(set_damping(int on));
static struct { CO; uint32_t until; } c_la;
PT_THREAD(launch(void)) {
    CO_BEGIN(c_la);
    CO_CALL(c_la, hold_until(K_F0, is_space_front, ev_disc_wait(5), "launch"));
    CO_CALL(c_la, wait_for(is_space_front, 15, "in space"));
    g_in_flight = 1;
    // clear the slot: ships launching after us come out along its axis, and idling there through a
    // hyperspace countdown got us rear-ended (a hit of ~145 on the aft shield, measured in the sim)
    if (g_launch_clear) {
        c_la.until = deadline(g_launch_clear / 50.0f);
        while (!expired(c_la.until)) {
            keyset_t k = {{0}, 0};
            ks_add(&k, K_SPACE);
            ks_add(&k, K_CLIMB);
            apply_flight(&k);
            CO_YIELD(c_la);
        }
        hold_none();
    }
    // damping off for the fights to come (it stays as set: once a career). Here, by the station, nothing's at us
    if (g_undamped) CO_CALL(c_la, set_damping(0));
    logf_("launched: G%d (%d,%d) cash %ld tally %.2f", galaxy() + 1, here_x(), here_y(), (long) cash() / 10, tally());
    {
        const sys_t *h = nearest_system(galaxy(), here_x(), here_y());
        status_("Launched from %s", h ? h->name : "the station");
    }
    CO_END(c_la);
}

// H, read from KL where the highest held key wins: nothing else held while pressing it. Held 10 frames:
// the game reads it once a main-loop pass, and among five or six Thargoids and their Thargons a pass takes
// several frames, so a short tap fell between reads (refused countdowns, nearly all of them in witchspace
// bail-outs; twice in one career we died re-selecting the way out). Holding it on is safe: once counting,
// hyp returns at once
static struct { CO; int i; } c_h;
PT_THREAD(press_h(void)) {
    CO_BEGIN(c_h);
    hold_none();
    CO_CALL(c_h, idle(2));
    for (c_h.i = 0; c_h.i < 4 && !is_counting(); c_h.i++) {
        CO_CALL(c_h, press(K_H, 10, 3));
        {
            static uint32_t until;
            until = deadline(1);
            CO_WAIT(c_h, is_counting() || expired(until));
        }
    }
    if (!is_counting()) CO_FAIL(c_h, "countdown");
    CO_END(c_h);
}

// Manual mis-jumps (routine ee5, flag PATG in Moxon's source): with the author credits switched on
// (pause with COPY, X, unpause with DELETE), holding CTRL as the hyperspace countdown ends drops us
// into witchspace among three Thargoids
int g_misjump;
int g_depart_y;         // where the last hyperspace left from (a mis-jump lands at y XOR 31)
static int patg_on(void) { return R(A_PATG) != 0; }
// (the pause loop reads keys with RDKEY: DELETE has to be held for about a quarter of a second to
// unpause, tried in the sim; a shorter press leaves the game paused)
static struct { CO; int tries, mc; } c_pg;
PT_THREAD(allow_misjumps(void)) {
    CO_BEGIN(c_pg);
    for (c_pg.tries = 0; !patg_on() && c_pg.tries < 3; c_pg.tries++) {
        hold_none();
        CO_CALL(c_pg, press(K_COPY, 6, 6));
        CO_CALL(c_pg, press(K_X, 3, 6));
        CO_CALL(c_pg, press(K_DELETE, 15, 10));
    }
    if (!patg_on()) CO_FAIL(c_pg, "mis-jumps not allowed");
    for (c_pg.tries = 0; c_pg.tries < 5; c_pg.tries++) {      // and running again: the loop counter moves
        c_pg.mc = mcnt();
        CO_CALL(c_pg, idle(10));
        if (mcnt() != c_pg.mc) break;
        CO_CALL(c_pg, press(K_DELETE, 15, 10));
    }
    CO_END(c_pg);
}

// keyboard damping (DAMP, the first of the configuration bytes; CAPS LOCK in the pause toggles it). On (the
// game's default), the stick creeps back 1 a game loop: a released stick returns to the centre, which docking's
// roll-matching relies on; but a roll key then moves the stick 7 less 1 a loop, and in a fight the roll stick's
// ramp was most of the time it took to get a sideways target into the sights. Set outside fights only (toggling
// mid-fight means pausing it: bad form, and each toggle cost ~2 game loops of drift). In the pause the game stands still: toggling costs no game time (g_pause_frames counts the
// frames it takes, for the lab's clocks)
extern int g_steer_log;
int g_pausing;                  // (in the pause now: the lab doesn't count these frames)
static int damping_is_(int on) { return (R(A_DAMP) == 0) == on; }
// and joystick mode (JSTK &2C5C, K in the pause): wanted when the platform has an analogue port
// and g_joystick says so (then the stick goes to the ADC, drv.c's hold), and the A key still fires with the
// joystick on (the Master's; the disc version's joystick mode fires only from the stick's button: keys there)
int g_joystick = 1;
static int joystick_ok(void) { return (R(A_JSTK) != 0) == (g_joystick && IO->analogue != NULL && g_ev.joy_fire_key); }
static int damping_is(int on) { return damping_is_(on) && joystick_ok(); }
static struct { CO; int on, tries, rep; uint32_t until; keyset_t held; } c_ud;
PT_THREAD(set_damping(int on)) {
    CO_BEGIN(c_ud);
    c_ud.on = on;
    if (damping_is(c_ud.on)) CO_EXIT(c_ud);
    g_pausing = 1;
    c_ud.held = g_flight;
    for (c_ud.tries = 0; !damping_is(c_ud.on) && c_ud.tries < 3; c_ud.tries++) {
        // COPY with the flight keys still held (the game takes the highest key down, and COPY is above them all):
        // we go on steering until the game is paused (its loop counter stops), then let go, as the pause reads
        // any key (a held roll key there is the volume). Paused, nothing moves: toggle, unpause, steer again at once
        CO_CALL(c_ud, press(K_COPY, 4, 0));
        c_ud.until = deadline(1);
        CO_WAIT(c_ud, g_frozen >= 8 || expired(c_ud.until));
        hold_none();
        // (CAPS LOCK until the setting flips, while paused: the second toggle of a fight wasn't taken from one press)
        for (c_ud.rep = 0; c_ud.rep < 4 && !damping_is_(c_ud.on); c_ud.rep++) {
            CO_CALL(c_ud, press(K_CAPS, 3, 3));
            c_ud.until = deadline(0.6f);
            CO_WAIT(c_ud, damping_is_(c_ud.on) || expired(c_ud.until));
        }
        for (c_ud.rep = 0; c_ud.rep < 4 && !joystick_ok(); c_ud.rep++) {       // (and the joystick, in the same pause)
            CO_CALL(c_ud, press(K_K, 3, 3));
            c_ud.until = deadline(0.6f);
            CO_WAIT(c_ud, joystick_ok() || expired(c_ud.until));
        }
        if (g_steer_log) dbgf_("damping toggle: CAPS pressed %d times, now %s", c_ud.rep, damping_is(c_ud.on) ? "done" : "not taken");
        // (DELETE alone until the game runs again: a flight key held as it's let go reads as a key in the pause
        // and it stays paused)
        for (c_ud.rep = 0; c_ud.rep < 4 && g_frozen; c_ud.rep++) {
            CO_CALL(c_ud, press(K_DELETE, 15, 2));
            c_ud.until = deadline(0.2f);
            CO_WAIT(c_ud, g_frozen == 0 || expired(c_ud.until));
        }
        g_flight = c_ud.held;
        hold(&g_flight);
    }
    g_pausing = 0;
    if (!damping_is(c_ud.on)) logf_("damping toggle not taken (wanted %s)", c_ud.on ? "on" : "off");     // (not a failure: fly on)
    CO_END(c_ud);
}

static struct { CO; const sys_t *dest; int x0, y0, w0, fought; uint32_t until; } c_hy;
int g_ws_fight = 1;
int g_ws_shield;        // (test)      // fight, don't run, through a witchspace bail-out countdown (they're faster)
PT_THREAD(hyperspace(const sys_t *dest)) {
    CO_BEGIN(c_hy);
    c_hy.dest = dest;
    c_hy.x0 = here_x();
    c_hy.y0 = g_depart_y = here_y();
    c_hy.w0 = witchspace();
    PT_CALL(&c_hy.pt, press_h());
    if (g_fail) {
        if (!c_hy.dest) CO_EXIT(c_hy);
        g_fail = 0;             // no countdown: the selection isn't what we meant; pick it again
        logf_("no countdown (target %d,%d): selecting %s again", target_x(), target_y(), c_hy.dest->name);
        CO_CALL(c_hy, select_by_cursor(c_hy.dest));
        CO_CALL(c_hy, hold_until(K_F0, is_front, 3, "front view"));
        PT_CALL(&c_hy.pt, press_h());
        if (g_fail) {           // refused again: the game resolves it to another system; don't plan it again
            // (unless we're short of fuel for it: then it's the tank, not the system)
            if (fuel() >= distance_qq8(here_x(), here_y(), c_hy.dest->x, c_hy.dest->y))
                g_unreachable[galaxy() & 7][c_hy.dest->num >> 3] |= (uint8_t) (1u << (c_hy.dest->num & 7));
            logf_("%s can't be selected for a jump (fuel %.1f LY): leaving it out of plans", c_hy.dest->name, fuel() / 10.0);
            status_("Jump to %s refused: planning again", c_hy.dest->name);
            g_fail = 0;             // (not a failure of ours: the caller flies on, docks, and plans without it)
            g_misjump = g_hold_ctrl = 0;
            hold(&g_flight);
            PT_EXIT(&c_hy.pt);
        }
    }
    {
        const sys_t *t = c_hy.dest ? c_hy.dest : nearest_system(galaxy(), target_x(), target_y());
        status_("Hyperspace to %s", t ? t->name : "?");
    }
    if (g_misjump) { g_hold_ctrl = 1; hold(&g_flight); }
    c_hy.until = deadline(120);
    for (;;) {                  // the countdown runs on while we wait, and while we fight
        CO_YIELD(c_hy);
        if (here_x() != c_hy.x0 || here_y() != c_hy.y0 || witchspace() != c_hy.w0) break;
        if (expired(c_hy.until)) CO_FAIL(c_hy, "arrival");
        g_phase = 3;
        // (test) the stronger shield towards them: a dead front shield and a full aft one (a mis-jump straight
        // out of a fight) means run, showing them the aft, rather than joust on the bare front
        // (curious: only with someone to fight, and not when engage() would bail out at once, below g_bail_energy
        // before the count: it started and ended a fight every other frame, "Fight over: +0.00" 35 times through
        // a countdown; run instead)
        int fightable = 1;
        extern int g_curious;
        if (g_curious && witchspace() && g_bail_energy) {
            static ship_t sh[NOSH];
            const ship_t *t[1];
            int n = see_ships(sh);
            fightable = threats(sh, n, 60000, t, 1) && !(energy() < g_bail_energy && !hyperspace_count());
        }
        if (witchspace() && g_bail_energy && g_ws_fight && !(g_ws_shield && fsh() * 2 < ash()) && fightable) {
            g_phase = 4;
            CO_CALL(c_hy, engage(3));       // (fighting through the countdown: they fly faster than we can run)
        } else if (witchspace() && g_bail_energy) {
            g_phase = 4;
            // bailing out of witchspace: run, don't fight (engage() would give up at once and leave
            // us sitting still): full speed straight away from them, jinking
            static ship_t sh[NOSH];
            int n = see_ships(sh);
            vec_t away = {0, 0, 1};
            double sx = 0, sy = 0, sz = 0;
            for (int i = 0; i < n; i++)
                if (ship_in_space(&sh[i]) && (sh[i].newb & NEWB_HOSTILE) && !ship_dead(&sh[i])) sx += sh[i].x, sy += sh[i].y, sz += sh[i].z;
            if (sx || sy || sz) away = (vec_t){-sx, -sy, -sz};
            keyset_t keys = {{0}, 0};
            steer(away, jstx(), jsty(), 3, 3, &keys);
            if ((now() / 25) & 1) ks_add(&keys, K_CLIMB);
            speed_keys(speed(), 40, &keys);
            apply_flight(&keys);
        } else
            CO_CALL(c_hy, fight_if_needed(&c_hy.fought));
    }
    g_misjump = g_hold_ctrl = 0;
    hold(&g_flight);
    logf_("arrived (%d,%d)%s", here_x(), here_y(), witchspace() ? " in witchspace" : "");
    if (witchspace()) status_("Mis-jump: trapped in witchspace");
    CO_END(c_hy);
}

// outgunned (the starting pulse laser against a pack): jump out, fighting through the countdown
// (running is no use: they fly at 1.5x their speed byte, faster than us)
static struct { CO; int x0, y0; uint32_t until; } c_hf;
PT_THREAD(hyperspace_fighting(const sys_t *dest)) {
    CO_BEGIN(c_hf);
    c_hf.x0 = here_x(), c_hf.y0 = here_y();
    CO_CALL(c_hf, select_by_cursor(dest));
    CO_CALL(c_hf, hold_until(K_F0, is_front, 3, "front view"));
    PT_CALL(&c_hf.pt, press_h());
    if (g_fail) { g_fail = 0; hold(&g_flight); CO_EXIT(c_hf); }     // (refused: fight it out here)
    status_("Outgunned: hyperspace to %s", dest->name);
    note("{\"event\": \"flee\", \"to\": \"%s\", \"tally\": %.2f}", dest->name, tally());
    c_hf.until = deadline(60);
    while (here_x() == c_hf.x0 && here_y() == c_hf.y0 && !witchspace() && !expired(c_hf.until)) {
        CO_CALL(c_hf, engage(5));
        CO_YIELD(c_hf);
    }
    hold(&g_flight);
    logf_("arrived (%d,%d)%s after fleeing", here_x(), here_y(), witchspace() ? " in witchspace" : "");
    CO_END(c_hf);
}

static struct { CO; const sys_t *dest; uint32_t until; uint8_t key; int x0, y0; } c_cur;
PT_THREAD(select_by_cursor(const sys_t *dest)) {
    CO_BEGIN(c_cur);
    c_cur.dest = dest;
    g_phase = 2;
    // no flight keys down: the game takes the highest key only, so a held roll or throttle key swallows
    // F5 and the cursor keys (we sat on a blank chart in witchspace, shot to death, four times in six hours)
    { keyset_t none = {{0}, 0}; g_flight = none; }
    hold_none();
    CO_CALL(c_cur, hold_until(K_F5, is_short, 3, "short-range chart"));
    c_cur.until = deadline(60);
    for (;;) {
        const sys_t *near = nearest_system(galaxy(), target_x(), target_y());
        if (near && near->num == c_cur.dest->num) break;
        if (expired(c_cur.until)) CO_FAIL(c_cur, "cursor");
        {
            int dx = c_cur.dest->x - target_x(), dy = c_cur.dest->y - target_y();
            c_cur.key = abs(dx) >= abs(dy) ? (dx > 0 ? K_RIGHT : K_LEFT) : (dy > 0 ? K_DOWN : K_UP);
        }
        dbgf_("cursor: target %d,%d -> %s at %d,%d key %02x view %d", target_x(), target_y(), c_cur.dest->name, c_cur.dest->x, c_cur.dest->y, c_cur.key, view());
        if (IO->analogue && R(A_JSTK)) {
            // joystick mode: the chart's cursor follows the stick, not the cursor keys. Lean it towards the
            // system, less as it nears; which way the stick moves it is learnt from how it moves (a push
            // that takes it further away flips that axis)
            static int sx = -1, sy = -1;      // (measured: the stick right moves the cursor left)
            int dx = c_cur.dest->x - target_x(), dy = c_cur.dest->y - target_y();
            int px = dx ? (dx > 0 ? 1 : -1) * (abs(dx) > 8 ? 90 : 40) : 0, py = dy ? (dy > 0 ? 1 : -1) * (abs(dy) > 8 ? 90 : 40) : 0;
            c_cur.x0 = target_x(), c_cur.y0 = target_y();
            {
                keyset_t none = {{0}, 0};
                g_want_jx = 128 + sx * px, g_want_jy = 128 + sy * py;
                hold(&none);
            }
            CO_CALL(c_cur, idle(4));
            if (g_fair & FAIR_STICK) CO_CALL(c_cur, idle(4));     // (a hand: the stick takes a few frames to get there)
            {
                int ndx = c_cur.dest->x - target_x(), ndy = c_cur.dest->y - target_y();
                if (px && abs(ndx) > abs(dx) && target_x() != c_cur.x0) sx = -sx;
                if (py && abs(ndy) > abs(dy) && target_y() != c_cur.y0) sy = -sy;
                // (fair mode: and pushed against the chart's edge, it doesn't move at all: the other way. A noisy stick
                // once taught it the wrong way and it sat at x 255 until the career gave up on it)
                if (g_fair && px && target_x() == c_cur.x0 && (target_x() <= 1 || target_x() >= 254)) sx = -sx;
                if (g_fair && py && target_y() == c_cur.y0 && (target_y() <= 1 || target_y() >= 254)) sy = -sy;
            }
        } else {
            CO_CALL(c_cur, press(c_cur.key, 2, 1));
        }
    }
    if (IO->analogue && R(A_JSTK)) { keyset_t none = {{0}, 0}; g_want_jx = g_want_jy = 128; hold(&none); }     // (the stick back to the middle)
    CO_CALL(c_cur, press(K_D, 3, 4));
    CO_CALL(c_cur, idle(10));
    CO_END(c_cur);
}

int g_chain_quick = -1;     // (-1: by mode, efficient only; CHQ)
static int chain_selected(const sys_t *d) {
    const sys_t *n = nearest_system(galaxy(), target_x(), target_y());
    return n && n->num == d->num && is_front();
}
static int thg_bail(void) { combat_thg_init(); return g_cp_thg.bail_energy; }
// chained farming: cleared, back to full strength, and the fuel covers a hop from here and a way out
// from where a mis-jump would drop us (x, y XOR 31): mis-jump again rather than fly in and dock (a
// dock costs two minutes; the fight half a minute)
int g_chain = 1;
extern int g_curious;
int g_ws_noregen = -1;      // (-1: by mode, efficient only)
static const sys_t *chain_hop(void) {
    const sys_t *c[8];
    int d[8];
    int n = in_range(galaxy(), here_x(), here_y(), fuel(), c, d, 8);
    for (int i = 0; i < n; i++) {
        const sys_t *o[1];
        int od[1];
        // (half a light year to spare: the game's distance can round against ours, and a refused
        // jump while bailing out is a death). And from where it drops us, a way out that a random
        // mis-jump (3 in 256) can't strand: that one lands back here, (x, y XOR 31 XOR 31), with
        // what's left after both. Stranded is a reload: three of six in a career came from chains
        int f1 = fuel() - d[i];
        const sys_t *e[8];
        int de[8], ne = in_range(galaxy(), here_x(), here_y() ^ 31, f1 - 5, e, de, 8);
        for (int j = 0; j < ne; j++)
            if (in_range(galaxy(), here_x(), here_y(), f1 - de[j] - 5, o, od, 1)) return c[i];
    }
    return NULL;
}
// anything that can hurt us near: hostile-flagged, or a Thargoid/Thargon whatever its flags (a freshly
// launched Thargon isn't flagged yet: we chained into one and died on the chart)
static int danger_near(void) {
    static ship_t sh[NOSH];
    int n = see_ships(sh);
    for (int i = 0; i < n; i++) {
        const ship_t *q = &sh[i];
        if (!ship_in_space(q) || ship_dead(q)) continue;
        if (!((q->newb & NEWB_HOSTILE) || q->type == 29 || q->type == 30)) continue;
        if (q->type >= 3 && q->type <= 8) continue;
        double d = sqrt((double) q->x * q->x + (double) q->y * q->y + (double) q->z * q->z);
        if (d < 20000) return 1;
    }
    return 0;
}
// chain when this strong again (the energy unit refills on the way in, 15-20 s before the Thargoids close)
int g_chain_energy = 248, g_chain_shield = 200;
static int full_strength(void) { return energy() >= g_chain_energy && fsh() >= g_chain_shield && ash() >= g_chain_shield; }
static struct { CO; const sys_t *dest; uint32_t until; int chained; } c_lw;
PT_THREAD(leave_witchspace(void)) {
    CO_BEGIN(c_lw);
    c_lw.chained = 0;
    for (;;) {
        // the way out first (a second on the chart while they close in), then fight; if it goes badly,
        // jump out mid-fight: the countdown runs while we fight on (hyperspace() keeps fighting). (Half a
        // while: MJP moves us, QQ1 XOR 31, after setting MJ, and the drawing in between spans frames)
        c_lw.until = deadline(6);
        while (here_y() != (g_depart_y ^ 31) && !expired(c_lw.until)) CO_YIELD(c_lw);
        {
            const sys_t *out[1];
            int dist[1];
            // (the way out can mis-jump as well, one in eighty, dropping us at (x, y XOR 31) with what's left:
            // prefer a way out from which that landing still reaches a system; the escape pod doesn't work
            // in witchspace, so stranded is a reload)
            const sys_t *c8[8];
            int d8[8];
            int n8 = in_range(galaxy(), here_x(), here_y(), fuel(), c8, d8, 8);
            if (!n8) {
                // stranded (no fuel for any way out): the galactic hyperdrive needs none, and Ghy doesn't
                // ask whether we're in witchspace. CTRL with H; CTRL let go once counting, as CTRL at the
                // count's end (author credits on) is a forced mis-jump
                if (!has_hyperdrive()) CO_FAIL(c_lw, "stranded in witchspace");
                status_("Stranded in witchspace: out by galactic hyperdrive");
                hold_none();
                g_hold_ctrl = 1;
                hold(&g_flight);
                CO_CALL(c_lw, press(K_H, 10, 3));
                c_lw.until = deadline(1);
                CO_WAIT(c_lw, is_counting() || expired(c_lw.until));
                g_hold_ctrl = 0;
                hold_none();
                if (!is_counting()) CO_FAIL(c_lw, "stranded in witchspace (galactic hyperdrive refused)");
                c_lw.until = deadline(30);
                CO_WAIT(c_lw, !witchspace() || expired(c_lw.until));
                logf_("galactic hyperdrive out of witchspace: galaxy %d, (%d,%d)", galaxy() + 1, here_x(), here_y());
                CO_EXIT(c_lw);
            }
            out[0] = c8[0];
            // (and not a trap: a system with no safe jump on, see jump_max)
            for (int pass = 0, done = 0; pass < 2 && !done; pass++)
                for (int i = 0; i < n8; i++) {
                    const sys_t *o[1];
                    int od[1];
                    if (pass == 0 && jump_trap(galaxy(), c8[i]->num)) continue;
                    if (in_range(galaxy(), here_x(), here_y() ^ 31, fuel() - d8[i], o, od, 1)) { out[0] = c8[i]; done = 1; break; }
                }
            (void) dist;
            c_lw.dest = out[0];
        }
        logf_("way out of witchspace: %s", c_lw.dest->name);
        CO_CALL(c_lw, select_by_cursor(c_lw.dest));
        CO_CALL(c_lw, hold_until(K_F0, is_front, 3, "front view"));
        {
            static ship_t sh[NOSH];
            const ship_t *t[1];
            int n = see_ships(sh);
            g_bail_energy = thg_bail();
            // (no military laser: three Thargoids at 240 energy each are out of reach, so don't fight
            // at all: out at once, running through the countdown. A random mis-jump early in the career
            // put a pulse laser among them, and without saves that was the career)
            if (threats(sh, n, 60000, t, 1) && laser(0) == 0x97) CO_CALL(c_lw, engage(600));
            g_bail_energy = 0;
        }
        if (laser(0) != 0x97) {
            logf_("leaving witchspace for %s at once (no military laser)", c_lw.dest->name);
            g_bail_energy = 256;
            CO_CALL(c_lw, hyperspace(c_lw.dest));
            g_bail_energy = 0;
            CO_EXIT(c_lw);
        }
        // (no chain to come: leave now, not after getting our strength back here, ~6 s a session; the energy unit
        // refills on the long way to the station. Efficient mode; the lab's witchspace pts/min +7.6% on eff300)
        if (!g_chain || energy() < thg_bail() || ((g_ws_noregen < 0 ? !g_curious : g_ws_noregen) && !chain_hop())) break;
        // cleared: get our strength back where we are (new arrivals: fight them, as above)
        c_lw.until = deadline(100);
        {
            static ship_t sh[NOSH];
            const ship_t *t[1];
            for (;;) {
                int n = see_ships(sh);
                if (threats(sh, n, 60000, t, 1) || danger_near() || full_strength() || expired(c_lw.until)) break;
                if (now() % 50 == 0) for (int i = 0; i < n; i++) if (ship_in_space(&sh[i]))
                    dbgf_("regen: slot %d type %d dist %.0f newb %02x flags %02x E %d", sh[i].slot, sh[i].type,
                          sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z), sh[i].newb, sh[i].flags31, energy());
                {
                    // hold still, but clear of anything close: debris of the last kill (alloys, canisters,
                    // flagged as exploding, so collision avoidance skipped it) killed us at speed 40
                    keyset_t k = {{0}, 0};
                    const ship_t *near = NULL;
                    double nd = 800;
                    for (int i = 0; i < n; i++) {
                        if (!ship_is_ship(&sh[i]) || sh[i].type == SHIP_SST) continue;
                        double d = sqrt((double) sh[i].x * sh[i].x + (double) sh[i].y * sh[i].y + (double) sh[i].z * sh[i].z);
                        if (d < nd) nd = d, near = &sh[i];
                    }
                    if (near) {
                        steer((vec_t){-near->x, -near->y, -near->z}, jstx(), jsty(), 3, 3, &k);
                        speed_keys(speed(), 12, &k);
                    } else speed_keys(speed(), 0, &k);
                    apply_flight(&k);
                    if (now() % 25 == 0) dbgf_("regen hold: speed %d near %.0f keys %d", speed(), nd, k.n);
                }
                CO_YIELD(c_lw);
            }
            if (threats(sh, see_ships(sh), 60000, t, 1) || danger_near()) continue;
        }
        { keyset_t k = {{0}, 0}; apply_flight(&k); }       // (let go: a held flight key blocks the view keys)
        if (!full_strength() || danger_near()) break;
        if (!chain_hop()) break;
        c_lw.dest = chain_hop();
        status_("Chaining: another mis-jump, towards %s (fuel %.1f LY)", c_lw.dest->name, fuel() / 10.0);
        // (efficient: the way out chosen on arrival is often this same system, and the chart's selection stays: no
        // second visit to the chart, where we neither steer nor shoot)
        if (!((g_chain_quick < 0 ? !g_curious : g_chain_quick) && chain_selected(c_lw.dest))) {
            CO_CALL(c_lw, select_by_cursor(c_lw.dest));
            CO_CALL(c_lw, hold_until(K_F0, is_front, 3, "front view"));
        }
        CO_CALL(c_lw, allow_misjumps());
        g_misjump = 1;
        CO_CALL(c_lw, hyperspace(c_lw.dest));
        c_lw.chained++;
        if (!witchspace()) CO_EXIT(c_lw);       // (it didn't take: we're in a system; dock there)
    }
    logf_("leaving witchspace for %s", c_lw.dest->name);
    g_bail_energy = energy() < thg_bail() ? thg_bail() : 0;      // (still losing: hyperspace() runs rather than fights)
    CO_CALL(c_lw, hyperspace(c_lw.dest));
    g_bail_energy = 0;
    CO_END(c_lw);
}

static struct { CO; int g0, i; uint32_t until; } c_gj;
PT_THREAD(galactic_jump(void)) {
    CO_BEGIN(c_gj);
    c_gj.g0 = galaxy();
    { extern int saving(void); if (saving()) CO_CALL(c_gj, save_commander(NULL)); }
    CO_CALL(c_gj, launch());
    hold_none();
    CO_CALL(c_gj, idle(2));
    for (c_gj.i = 0; c_gj.i < 4 && galaxy() == c_gj.g0; c_gj.i++) {
        { keyset_t k = {{K_CTRL}, 1}; hold(&k); }
        CO_CALL(c_gj, idle(2));
        CO_CALL(c_gj, press(K_H, 6, 2));
        hold_none();
        c_gj.until = deadline(20);
        CO_WAIT(c_gj, galaxy() != c_gj.g0 || expired(c_gj.until));
    }
    logf_("galactic jump: galaxy %d -> %d at (%d,%d)", c_gj.g0 + 1, galaxy() + 1, here_x(), here_y());
    status_("Galactic jump to galaxy %d", galaxy() + 1);
    note("{\"event\": \"galaxy\", \"galaxy\": %d, \"tally\": %.2f}", galaxy(), tally());
    CO_CALL(c_gj, dock(900));
    CO_END(c_gj);
}

#include <stdio.h>
#include <string.h>
#include "fair.h"
#include "docked.h"
#include "galaxy.h"

int g_bought;
extern int g_curious;

// curious mode lingers on the screens it goes through, so someone watching can read them
static struct { CO; } c_lg;
static PT_THREAD(linger(float seconds)) {
    CO_BEGIN(c_lg);
    if (g_curious) CO_CALL(c_lg, idle((int) (seconds * 50)));
    CO_END(c_lg);
}

static int qq29(void) { return R(A_QQ29); }
static int qq25(void) { return R(A_QQ25); }
static int is_any(void) { return 1; }
static int is_status(void) { return view() == V_STATUS; }
static int is_buy(void) { return view() == V_BUY; }
static int is_sell(void) { return view() == V_SELL; }
static int is_equip(void) { return view() == V_EQUIP; }
static int is_long(void) { return view() == V_LONG; }
static int is_menu(void) { return view() == 1; }
static int is_not_sell(void) { return view() != V_SELL; }
static int is_docked_status(void) { return is_docked() && view() == V_STATUS; }
static int is_title1(void) { return title() == 1; }
static int is_title2(void) { return title() == 2; }
static int is_title_or_docked(void) { return title() || (is_docked() && view()); }

// ---- Buy Cargo (f1): item by item (QQ29) for those in stock: the quantity and RETURN, or RETURN
// for none; a letter leaves (or the game does, after the last item)
static const char *const UNITS[17] = {"t", "t", "t", "t", "t", "t", "t", "t", "t", "t", "t", "t", "t", "kg", "kg", "g", "t"};
static struct { CO; uint8_t want[17]; int item, before; char num[8];  int32_t cash0; } c_buy;
static int is_bought(void) { return cargo(c_buy.item) != c_buy.before || qq29() != c_buy.item; }
static int is_next_item(void) { return qq29() != c_buy.item; }
static int buy_remaining(void) { for (int i = 0; i < 17; i++) if (c_buy.want[i]) return 1; return 0; }
PT_THREAD(buy(const uint8_t want[17])) {
    CO_BEGIN(c_buy);
    memcpy(c_buy.want, want, 17);
    if (!buy_remaining()) CO_EXIT(c_buy);
    CO_CALL(c_buy, hold_until(K_F1, is_buy, 3, "buy screen"));
    while (buy_remaining()) {
        CO_CALL(c_buy, settle(is_any, 6, 8, "buy prompt"));
        if (view() != V_BUY) break;
        c_buy.item = qq29();
        if (c_buy.item >= 17) break;
        {
            int n = c_buy.want[c_buy.item];
            if (n > qq25()) n = qq25();
            c_buy.before = cargo(c_buy.item);
            snprintf(c_buy.num, sizeof c_buy.num, "%d", n);
            c_buy.want[c_buy.item] = 0;
        }
        if (strcmp(c_buy.num, "0")) {
            logf_("buy %s of %d", c_buy.num, c_buy.item);
            c_buy.cash0 = cash();
            CO_CALL(c_buy, type_text(c_buy.num));
            CO_CALL(c_buy, press(K_RETURN, 3, 3));
            CO_CALL(c_buy, wait_for(is_bought, 5, "bought"));
            {
                int got = cargo(c_buy.item) - c_buy.before;
                if (got > 0)
                    status_("Bought %s %d%s at %.1f Cr/%s", ITEM_NAMES[c_buy.item], got, UNITS[c_buy.item],
                            (c_buy.cash0 - cash()) / 10.0 / got, UNITS[c_buy.item]);
            }
        } else {
            CO_CALL(c_buy, press(K_RETURN, 3, 3));
        }
        CO_CALL(c_buy, wait_for(is_next_item, 5, "next item"));
    }
    CO_CALL(c_buy, settle(is_any, 10, 8, "buy screen settles"));
    if (view() == V_BUY) CO_CALL(c_buy, linger(3));
    if (view() == V_BUY) {
        CO_CALL(c_buy, press(K_B, 3, 3));
        CO_CALL(c_buy, wait_for(is_status, 5, "inventory"));
    }
    CO_END(c_buy);
}

// ---- Sell Cargo (f2): Y for each item held (the game only asks about those)
static struct { CO; uint8_t asked[17]; uint8_t held[17]; int item; int32_t cash0; } c_sell;
static int is_sell_prompt(void) { return view() != V_SELL || (qq29() < 17 && !c_sell.asked[qq29()]); }
PT_THREAD(sell_all(void)) {
    CO_BEGIN(c_sell);
    {
        int any = 0;
        for (int i = 0; i < 17; i++) { c_sell.held[i] = (uint8_t) cargo(i); c_sell.asked[i] = 0; any |= cargo(i); }
        if (!any) CO_EXIT(c_sell);
    }
    c_sell.cash0 = cash();
    CO_CALL(c_sell, hold_until(K_F2, is_sell, 3, "sell screen"));
    while (view() == V_SELL) {
        CO_CALL(c_sell, settle(is_sell_prompt, 5, 4, "sell prompt"));
        if (view() != V_SELL) break;
        c_sell.item = qq29();
        if (c_sell.item >= 17 || !c_sell.held[c_sell.item]) break;
        c_sell.asked[c_sell.item] = 1;
        CO_CALL(c_sell, press(K_Y, 3, 4));
    }
    CO_CALL(c_sell, wait_for(is_not_sell, 5, "sell done"));
    {
        char line[160];
        int n = 0;
        for (int i = 0; i < 17 && n < (int) sizeof line - 40; i++) {
            int sold = c_sell.held[i] - cargo(i);
            if (sold > 0) n += snprintf(line + n, sizeof line - (size_t) n, "%s%s %d%s", n ? ", " : "", ITEM_NAMES[i], sold, UNITS[i]);
        }
        if (n) status_("Sold %s for %.1f Cr (cash %.1f Cr)", line, (cash() - c_sell.cash0) / 10.0, cash() / 10.0);
    }
    if (view() != V_STATUS) CO_CALL(c_sell, hold_until(K_F8, is_status, 3, "status"));
    CO_END(c_sell);
}

// ---- Equip Ship (f3): the item's number (item + 1) and RETURN, the view for a laser
static struct { CO; int item, view; int32_t cash0; char num[12]; } c_eq;
static int is_eq_result(void) { return cash() != c_eq.cash0 || view() == V_STATUS || cursor_y() < 4; }
PT_THREAD(equip(int item, int lview)) {
    CO_BEGIN(c_eq);
    c_eq.item = item;
    c_eq.view = lview;
    c_eq.cash0 = cash();
    g_bought = 0;
    CO_CALL(c_eq, hold_until(K_F3, is_equip, 3, "equip screen"));
    CO_CALL(c_eq, settle(is_equip, 8, 8, "equip prompt"));
    if (c_eq.item != EQ_FUEL) CO_CALL(c_eq, linger(2.5));
    if (c_eq.item + 1 > qq25()) {
        logf_("equip: item %d not sold here", c_eq.item);
        CO_CALL(c_eq, press(K_RETURN, 3, 4));
        CO_CALL(c_eq, wait_for(is_status, 5, "status"));
        CO_EXIT(c_eq);
    }
    snprintf(c_eq.num, sizeof c_eq.num, "%d", c_eq.item + 1);
    CO_CALL(c_eq, type_text(c_eq.num));
    CO_CALL(c_eq, press(K_RETURN, 3, 4));
    if (c_eq.item == EQ_PULSE || c_eq.item == EQ_BEAM || c_eq.item == EQ_MILITARY || c_eq.item == EQ_MINING) {
        CO_CALL(c_eq, idle(25));
        CO_CALL(c_eq, press((uint8_t) key_for_char((char) ('0' + c_eq.view)), 3, 4));
    }
    CO_CALL(c_eq, wait_for(is_eq_result, 8, "equip result"));
    g_bought = cash() < c_eq.cash0;
    if (view() != V_STATUS) {
        CO_CALL(c_eq, idle(30));
        CO_CALL(c_eq, settle(is_equip, 8, 8, "equip again"));
        CO_CALL(c_eq, press(K_RETURN, 3, 4));
        CO_CALL(c_eq, wait_for(is_status, 5, "status"));
    }
    logf_("equip %d: %s, cash %ld.%ld", c_eq.item, g_bought ? "bought" : "no", (long) cash() / 10, (long) cash() % 10);
    CO_END(c_eq);
}

static struct { CO; } c_ref;
PT_THREAD(refuel(void)) {
    CO_BEGIN(c_ref);
    if (fuel() < 70) CO_CALL(c_ref, equip(EQ_FUEL, 0));
    CO_END(c_ref);
}

// ---- the long-range chart, F, the name, RETURN: the game selects it (QQ9/QQ10, QQ8)
static struct { CO; char name[12]; } c_sel;
PT_THREAD(select_system(const char *name)) {
    CO_BEGIN(c_sel);
    {
        int i = 0;
        for (; name[i] && i < 11; i++) c_sel.name[i] = name[i];
        c_sel.name[i] = 0;
    }
    CO_CALL(c_sel, hold_until(K_F4, is_long, 3, "chart"));
    CO_CALL(c_sel, settle(is_long, 8, 8, "chart"));
    CO_CALL(c_sel, press(K_F, 3, 4));
    CO_CALL(c_sel, idle(12));
    CO_CALL(c_sel, type_text(c_sel.name));
    CO_CALL(c_sel, press(K_RETURN, 3, 4));
    CO_CALL(c_sel, idle(20));
    CO_CALL(c_sel, linger(1));
    CO_END(c_sel);
}

// ---- f6 Data on System for the selected system (economy, government, species, the description), held
// for a while in curious mode. It runs TT111 on the crosshair first, which is on the selection already.
static struct { CO; float s; } c_dat;
static int is_data(void) { return view() == V_DATA; }
PT_THREAD(show_system_data(float seconds)) {
    CO_BEGIN(c_dat);
    c_dat.s = seconds;
    if (!g_curious) CO_EXIT(c_dat);
    CO_CALL(c_dat, hold_until(K_F6, is_data, 3, "data on system"));
    CO_CALL(c_dat, linger(c_dat.s));
    CO_END(c_dat);
}

// ---- titles, saving, reloading

static struct { CO; } c_ft;
PT_THREAD(from_title(void)) {
    CO_BEGIN(c_ft);
    hold_none();
    CO_CALL(c_ft, wait_for(is_title_or_docked, 30, "title or docked"));
    if (title() == 1) {
        CO_CALL(c_ft, idle(10));
        CO_CALL(c_ft, hold_until(K_N, is_title2, 8, "title 2"));
    }
    if (title() == 2) {
        CO_CALL(c_ft, idle(10));
        CO_CALL(c_ft, hold_until(K_SPACE, is_docked_status, 5, "docked"));
    }
    g_in_flight = 0;
    CO_END(c_ft);
}

static struct { CO; char name[8]; } c_save;
static int is_saved_or_menu(void) { return view() == V_STATUS; }
PT_THREAD(save_commander(const char *name)) {
    CO_BEGIN(c_save);
    c_save.name[0] = 0;
    if (name) { strncpy(c_save.name, name, 7); c_save.name[7] = 0; }
    CO_CALL(c_save, hold_until(K_AT, is_menu, 3, "disc menu"));
    CO_CALL(c_save, settle(is_menu, 8, 8, "disc menu"));
    CO_CALL(c_save, press(K_2, 3, 4));
    CO_CALL(c_save, idle(12));                       // (GTNMEW waits 8 vsyncs before it reads)
    if (c_save.name[0]) CO_CALL(c_save, type_text(c_save.name));
    CO_CALL(c_save, press(K_RETURN, 3, 4));
    CO_CALL(c_save, idle(6));
    CO_CALL(c_save, press(K_0, 3, 4));              // WHICH DRIVE?
    // a failed disc write (read-only disc) waits for a key and shows the menu again: 6 leaves
    {
        static uint32_t until;
        until = deadline(6);
        CO_WAIT(c_save, is_saved_or_menu() || expired(until));
    }
    if (view() != V_STATUS) {
        CO_CALL(c_save, press(K_SPACE, 3, 4));
        CO_CALL(c_save, idle(20));
        CO_CALL(c_save, settle(is_menu, 8, 8, "disc menu again"));
        CO_CALL(c_save, press(K_6, 3, 4));
        CO_CALL(c_save, wait_for(is_status, 10, "out of the disc menu"));
    }
    // the game copies the commander to NA% (what a death restores) before the disc write
    if (memcmp(IO->ram + A_NA_PC + 8, IO->ram + A_TP, 73)) logf_("save: the copy doesn't match");
    note("{\"event\": \"save\", \"tally\": %.2f, \"cash\": %.1f}", tally(), cash() / 10.0);
    fair_saved();
    CO_END(c_save);
}

// loading a commander from the disc: @ 1 name RETURN 0, as the save (g_load_name, once, when next docked:
// a career put on the Presto's save disc, "agent load <name>", or run_sim LOAD=name)
char g_load_name[8];
static struct { CO; } c_load;
PT_THREAD(load_commander(const char *name)) {
    CO_BEGIN(c_load);
    CO_CALL(c_load, hold_until(K_AT, is_menu, 3, "disc menu"));
    CO_CALL(c_load, settle(is_menu, 8, 8, "disc menu"));
    CO_CALL(c_load, press(K_1, 3, 4));
    CO_CALL(c_load, idle(12));                       // (GTNMEW waits 8 vsyncs before it reads)
    CO_CALL(c_load, type_text(name));
    CO_CALL(c_load, press(K_RETURN, 3, 4));
    CO_CALL(c_load, idle(6));
    CO_CALL(c_load, press(K_0, 3, 4));              // WHICH DRIVE?
    {
        static uint32_t until;
        until = deadline(6);
        CO_WAIT(c_load, !memcmp(IO->ram + A_NAME, name, strlen(name)) || expired(until));
    }
    // (loaded: the commander's name is the one asked for; the screen is the Status page, though the game's
    // view byte doesn't say so yet. Not found: the menu waits for a key; out of it)
    if (memcmp(IO->ram + A_NAME, name, strlen(name))) {
        CO_CALL(c_load, press(K_SPACE, 3, 4));
        CO_CALL(c_load, idle(20));
        CO_CALL(c_load, settle(is_menu, 8, 8, "disc menu again"));
        CO_CALL(c_load, press(K_6, 3, 4));
        CO_CALL(c_load, wait_for(is_status, 10, "out of the disc menu"));
    }
    logf_("loaded commander %s: tally %.2f cash %.1f", name, tally(), cash() / 10.0);
    note("{\"event\": \"loaded\", \"name\": \"%s\", \"tally\": %.2f, \"cash\": %.1f}", name, tally(), cash() / 10.0);
    CO_END(c_load);
}

static struct { CO; } c_rl;
static int is_t1(void) { return title() == 1; }
PT_THREAD(reload(void)) {
    CO_BEGIN(c_rl);
    hold_none();
    if (is_docked() && view() != V_STATUS) {
        CO_CALL(c_rl, press(K_B, 3, 6));          // (a prompt reads COPY as a letter: leave it first)
        CO_CALL(c_rl, hold_until(K_F8, is_status, 3, "status"));
    }
    if (!title()) {
        CO_CALL(c_rl, press(K_COPY, 8, 6));
        {
            static int i;
            for (i = 0; i < 5 && !is_t1(); i++) {
                CO_CALL(c_rl, press(K_ESCAPE, 4, 3));
                CO_CALL(c_rl, idle(50));
            }
        }
        if (!is_t1()) CO_FAIL(c_rl, "quit to title");
        hold_none();
    }
    CO_CALL(c_rl, from_title());
    fair_reloaded();
    CO_END(c_rl);
}

static struct { CO; } c_ad;
PT_THREAD(after_death(void)) {
    CO_BEGIN(c_ad);
    hold_none();
    CO_CALL(c_ad, wait_for(is_title1, 30, "title after death"));
    CO_CALL(c_ad, from_title());
    fair_reloaded();
    CO_END(c_ad);
}

static struct { CO; int tries; uint32_t until; int n; } c_sd;
PT_THREAD(settle_docked(void)) {
    CO_BEGIN(c_sd);
    for (c_sd.tries = 0; c_sd.tries < 40; c_sd.tries++) {
        // a steady Status screen for half a second
        c_sd.until = deadline(1.5);
        c_sd.n = 0;
        while (c_sd.n < 25 && !expired(c_sd.until)) {
            CO_YIELD(c_sd);
            c_sd.n = is_docked_status() ? c_sd.n + 1 : 0;
        }
        if (c_sd.n >= 25) {
            if (!g_load_name[0]) CO_EXIT(c_sd);
            {
                static char name[8];
                memcpy(name, g_load_name, sizeof name);
                g_load_name[0] = 0;          // (once: a failed load isn't retried)
                CO_CALL(c_sd, load_commander(name));
            }
            continue;
        }
        if ((is_docked() && view() != V_STATUS) || briefing()) {
            CO_CALL(c_sd, press(K_SPACE, 3, 4));        // through a mission briefing's pages
        } else if (title()) {
            CO_CALL(c_sd, from_title());
        }
    }
    CO_FAIL(c_sd, "status screen after docking");
    CO_END(c_sd);
}

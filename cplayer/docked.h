// The docked screens by keys, and the game's flow: titles, saving, reloading.
#pragma once
#include "drv.h"

enum { V_DATA = 1, V_BUY = 2, V_SELL = 4, V_STATUS = 8, V_MARKET = 16, V_EQUIP = 32, V_LONG = 64, V_SHORT = 128 };
enum { EQ_FUEL, EQ_MISSILE, EQ_CARGO_BAY, EQ_ECM, EQ_PULSE, EQ_BEAM, EQ_SCOOPS, EQ_ESCAPE_POD, EQ_BOMB,
       EQ_ENERGY_UNIT, EQ_DOCK_COMPUTER, EQ_HYPERDRIVE, EQ_MILITARY, EQ_MINING };

PT_THREAD(buy(const uint8_t want[17]));
PT_THREAD(sell_all(void));
PT_THREAD(equip(int item, int view));        // sets g_bought
PT_THREAD(refuel(void));
PT_THREAD(select_system(const char *name));  // F on the long-range chart, the name, RETURN
extern int g_bought;

PT_THREAD(from_title(void));                  // title 1 -> N -> title 2 -> SPACE -> docked
PT_THREAD(save_commander(const char *name)); // @ 2 [name] RETURN 0
PT_THREAD(reload(void));                      // pause, ESCAPE (to the title), then from_title
PT_THREAD(after_death(void));
PT_THREAD(settle_docked(void));               // through any briefing to a steady Status screen

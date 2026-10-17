// The Elite agent on the Presto: one implementation of the platform the player needs (the other is
// sim/, the development build). The firmware is rp2350-bbc-micro's, built with this file and
// cplayer/ through core/machine_hook.h (see presto/build.sh). Once per emulated frame, between
// frames: the player reads the Master's main RAM, then the keys it holds go into the keyboard
// matrix. Nothing else of the machine is touched. It runs only on the interface version and the
// machine it was built for (MACHINE_HOOK_VERSION; the Master 128 with MOS 3.20).
// Commands (over USB "!agent ...", or the web page's buttons through POST /api/command; "player"
// works as well as "agent"):
//   player                       start or stop it (it starts by itself ELITE_AUTOSTART frames after boot)
//   player start | stop
//   player mode efficient|curious   how it plays: straight for Elite, or seeing the galaxy
//   player log <0|1|2>           its USB log: STATUS lines only, + PLAYER (default), + DEBUG
//   player status                a line: rank, tally, play time, kills, deaths
// The web page shows its panel (machine_program_json): progress to Elite, where it is, the totals.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "machine_hook.h"
#include "player.h"
#include "keyq.h"
#include "drv.h"
#include "state.h"
#include "galaxy.h"

#ifndef ELITE_AUTOSTART
#define ELITE_AUTOSTART 2500        // frames: 50 s, Elite booted from the disc and at its title
#endif

static const struct machine_hook_io *s_hook;
static bool s_refused;
static player_io_t s_io;
static bool s_on;
static uint32_t s_boot_frames, s_us_max, s_us_sum, s_n;
static uint32_t s_last_frame;
static int s_level = PLOG_INFO;
extern int g_curious;

// the totals, from the player's journal and status lines (it keeps playing; these only watch): for
// this career (this commander) and since boot. In RAM only: nothing is written to flash but what
// the game saves to its disc. A new career when the commander changes (someone starts a new one
// on the web page, say) or the tally falls without a death to explain it.
#define TOT_MAGIC 0x45414732u       // "EAG2"
typedef struct { uint32_t frames, kills, deaths, fights, pods, docks; } counts_t;
static struct {
    uint32_t magic;
    char name[8];                   // the agent's name (agent name ...)
    counts_t life, car;
    uint32_t careers, boots, resets;
    char cmdr[8];                   // this career's commander
    float best;                     // and the best tally it's had
} s_tot;
#define COUNT(f, n) (s_tot.life.f += (n), s_tot.car.f += (n))
static uint32_t s_saved_at, s_resets_seen, s_died_at;
static void save_totals(void) {}       // (nothing kept across a reboot: see above)
static bool cmdr_name(char out[8])
{
    // the commander's name in the game: 1 to 7 letters or digits, ended by a CR (anything else: not a
    // commander in play, so no career decision on it)
    int i = 0;
    for (; i < 7; i++) {
        uint8_t c = s_hook->ram[0x122C + i];
        if (c == 13) break;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        out[i] = (char) c;
    }
    out[i] = 0;
    return i > 0 && (i == 7 || s_hook->ram[0x122C + i] == 13);
}
#define LOGN 8
static char s_log[LOGN][96];
static int s_logn;
static void remember(const char *s)
{
    snprintf(s_log[s_logn % LOGN], sizeof s_log[0], "%s", s);
    s_logn++;
}
static const char *rank_of(double t)
{
    // the combat ranks by kill tally (Elite's TALLY thresholds)
    return t >= 6400 ? "Elite" : t >= 2560 ? "Deadly" : t >= 512 ? "Dangerous" : t >= 128 ? "Competent" :
           t >= 64 ? "Above Average" : t >= 32 ? "Average" : t >= 16 ? "Poor" : t >= 8 ? "Mostly Harmless" : "Harmless";
}

static void key(int code, int down)
{
    s_hook->key((code >> 4) & 7, code & 15, down != 0);      // (the player's codes: row << 4 | column)
}
static void p_hold(void *ctx, const uint8_t *c, int n) { (void) ctx; keyq_hold(s_io.frame, c, n); }
static void p_tap(void *ctx, uint8_t c, int down, int up) { (void) ctx; keyq_tap(s_io.frame, c, (uint8_t) down, (uint8_t) up); }
static void p_log(void *ctx, int level, const char *s)
{
    (void) ctx;
    static const char *const TAG[] = {"STATUS", "PLAYER", "DEBUG"};
    if (level == PLOG_STATUS) {
        remember(s);
        if (!strncmp(s, "Escape pod launched", 19)) COUNT(pods, 1);
    }
    if (level <= s_level) printf("%s %lu %s\n", TAG[level], (unsigned long) s_io.frame, s);
}
static void p_analogue(void *ctx, int ch, uint16_t v) { (void) ctx; if (s_hook->analogue) s_hook->analogue(ch, v); }
static void p_note(void *ctx, const char *s)
{
    (void) ctx;
    const char *k;
    if (strstr(s, "\"fight_end\"")) { COUNT(fights, 1); if ((k = strstr(s, "\"kills\": "))) COUNT(kills, (uint32_t) atoi(k + 9)); }
    else if (strstr(s, "\"died\"")) { COUNT(deaths, 1); s_died_at = s_tot.life.frames; }
    else if (strstr(s, "\"docked\"")) {
        COUNT(docks, 1);
        if (s_tot.life.frames - s_saved_at >= 5 * 60 * 50) save_totals();     // (docked: at most every 5 minutes)
    }
    printf("NOTE %lu %s\n", (unsigned long) s_io.frame, s);
}

static void set_on(bool on)
{
    if (on == s_on) return;
    s_on = on;
    if (on) {
        memset(&s_io, 0, sizeof s_io);
        s_io.ram = s_hook->ram;
        s_io.hold = p_hold, s_io.tap = p_tap, s_io.log = p_log, s_io.note = p_note, s_io.analogue = p_analogue;
        keyq_reset();
        player_init(&s_io);
        s_us_max = s_us_sum = s_n = 0;
    } else {
        keyq_release(key);
    }
    printf("PLAYER %s\n", on ? "on" : "off");
}

void machine_frame_hook(const struct machine_hook_io *io)
{
    if (s_refused) return;
    if (!s_hook) {
        // (the player reads the Master's memory map: on anything else it would misread it)
        if (io->version != MACHINE_HOOK_VERSION || io->model != MACHINE_MODEL_MASTER_128 || io->mos != 320 || io->ram_size < 0x8000) {
            printf("PLAYER not started: built for hook interface %d on a Master 128 (MOS 3.20), this is interface %lu, model %lu, MOS %lu\n",
                   MACHINE_HOOK_VERSION, (unsigned long) io->version, (unsigned long) io->model, (unsigned long) io->mos);
            s_refused = true;
            return;
        }
        s_hook = io;
        memset(&s_tot, 0, sizeof s_tot);
        strcpy(s_tot.name, "CLAUDE");
        s_tot.magic = TOT_MAGIC;
        s_tot.boots = 1;
        s_resets_seen = io->resets;
    }
    if (io->resets != s_resets_seen) {
        // the machine was reset (BREAK, a disc booted): its RAM and our idea of the game start over
        s_tot.resets += io->resets - s_resets_seen;
        s_resets_seen = io->resets;
        if (s_on) { s_on = false; set_on(true); }
    }
    if (!s_on && s_boot_frames < ELITE_AUTOSTART && ++s_boot_frames == ELITE_AUTOSTART) set_on(true);
    if (!s_on) return;
    // (a reset: the frame count starts again, and the keys it held went with the old machine)
    if (io->frame < s_last_frame) keyq_reset();
    s_last_frame = io->frame;
    uint32_t t0 = time_us_32();
    player_frame();
    uint32_t us = time_us_32() - t0;
    s_io.taps_done += (uint32_t) keyq_step(s_io.frame, key);
    s_io.frame++;
    COUNT(frames, 1);
    // whose career this is: checked every second, once a commander is in play
    char c[8];
    if (s_io.frame % 50 == 0 && !title() && !dying() && is_docked() && cmdr_name(c)) {     // (docked: the commander's settled)
        float t = (float) tally();
        bool other = s_tot.cmdr[0] && strcmp(c, s_tot.cmdr) && !(strcmp(s_tot.cmdr, "JAMESON") == 0 && !strcmp(c, s_tot.name));
        bool fell = t < s_tot.best - 1 && s_tot.life.frames - s_died_at > 60 * 50;     // (not a reload after a death)
        if (!s_tot.cmdr[0] || other || fell) {
            if (s_tot.cmdr[0]) {
                char m[96];
                snprintf(m, sizeof m, "New career: %s, tally %.1f (was %s, best %.1f)", c, t, s_tot.cmdr, s_tot.best);
                remember(m);
                printf("STATUS %lu %s\n", (unsigned long) s_io.frame, m);
            }
            memset(&s_tot.car, 0, sizeof s_tot.car);
            s_tot.careers++;
            s_tot.best = t;
            save_totals();
        }
        memcpy(s_tot.cmdr, c, sizeof c);
        if (t > s_tot.best) s_tot.best = t;
    }
    if (us > s_us_max) s_us_max = us;
    s_us_sum += us, s_n++;
    if (s_n == 3000) {      // every minute: how long it takes
        printf("PLAYER time avg %lu us max %lu us\n", (unsigned long) (s_us_sum / s_n), (unsigned long) s_us_max);
        s_us_max = s_us_sum = s_n = 0;
    }
}

static void hms(char *out, size_t n, uint32_t frames)
{
    uint32_t s = frames / 50;
    snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long) (s / 3600), (unsigned long) (s / 60 % 60), (unsigned long) (s % 60));
}

bool machine_command_hook(const char *cmd, char *reply, uint32_t reply_size)
{
    int w = !strncmp(cmd, "agent", 5) ? 5 : !strncmp(cmd, "player", 6) ? 6 : 0;
    if (!w || (cmd[w] && cmd[w] != ' ')) return false;
    const char *a = cmd[w] ? cmd + w + 1 : "";
    bool started = s_hook != NULL;
    if (!*a || !strcmp(a, "start") || !strcmp(a, "stop")) {
        if (!started) { snprintf(reply, reply_size, "AGENT not running here (see the boot message)"); return true; }
        set_on(*a ? !strcmp(a, "start") : !s_on);
        s_boot_frames = ELITE_AUTOSTART;       // (asked for: no autostart after this)
        snprintf(reply, reply_size, "AGENT %s", s_on ? "on" : "off");
    } else if (!strncmp(a, "mode ", 5)) {
        if (!strcmp(a + 5, "curious")) g_curious = 1;
        else if (!strcmp(a + 5, "efficient")) g_curious = 0;
        else { snprintf(reply, reply_size, "AGENT mode efficient|curious"); return true; }
        snprintf(reply, reply_size, "AGENT mode %s (from its next decision)", g_curious ? "curious" : "efficient");
    } else if (!strncmp(a, "log ", 4)) {
        int l = atoi(a + 4);
        s_level = l < 0 ? 0 : l > PLOG_DEBUG ? PLOG_DEBUG : l;
        snprintf(reply, reply_size, "AGENT log level %d", s_level);
    } else if (!strncmp(a, "name ", 5)) {
        // (a commander's name in Elite: up to 7 letters and digits; it saves under this name)
        char n[8] = "";
        int k = 0;
        for (const char *q = a + 5; *q && k < 7; q++)
            if ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9')) n[k++] = (char) (*q >= 'a' && *q <= 'z' ? *q - 32 : *q);
        if (!k) { snprintf(reply, reply_size, "AGENT name: 1 to 7 letters or digits"); return true; }
        memcpy(s_tot.name, n, sizeof n);
        save_totals();
        snprintf(reply, reply_size, "AGENT name %s", s_tot.name);
    } else if (!strcmp(a, "save")) {
        save_totals();
        snprintf(reply, reply_size, "AGENT totals saved");
    } else if (!strcmp(a, "status")) {
        char t[16];
        hms(t, sizeof t, s_tot.car.frames);
        double ty = s_on ? tally() : 0;
        snprintf(reply, reply_size, "AGENT %s %s %s, %s, tally %.1f of 6400, played %s, kills %lu, deaths %lu, pods %lu",
                 s_tot.name, s_on ? "on" : "off", g_curious ? "curious" : "efficient", rank_of(ty), ty, t,
                 (unsigned long) s_tot.car.kills, (unsigned long) s_tot.car.deaths, (unsigned long) s_tot.car.pods);
    } else {
        snprintf(reply, reply_size, "AGENT commands: start, stop, mode efficient|curious, name <NAME>, log 0|1|2, status, save");
    }
    return true;
}

// the web page's panel: how it's doing, and its buttons
static void jstr(char *o, size_t n, const char *s)
{
    size_t k = 0;
    o[k++] = '"';
    for (; *s && k + 3 < n; s++) {
        if (*s == '"' || *s == '\\') o[k++] = '\\';
        o[k++] = (unsigned char) *s < 0x20 ? ' ' : *s;
    }
    o[k++] = '"';
    o[k] = 0;
}
const char *machine_program_json(void)
{
    static char j[2200];
    if (!s_hook) return NULL;
    char t[16], where[48] = "-", doing[24] = "-", q[200];
    double ty = 0, cr = 0;
    int in_game = s_on && !title() && (is_docked() || g_in_flight);
    if (s_on && !in_game) snprintf(doing, sizeof doing, "starting the game");
    if (in_game) {
        ty = tally(), cr = cash() / 10.0;
        const sys_t *h = nearest_system(galaxy(), here_x(), here_y());
        snprintf(where, sizeof where, "%s, galaxy %d", witchspace() ? "witchspace" : h ? h->name : "?", galaxy() + 1);
        snprintf(doing, sizeof doing, "%s", is_docked() ? "docked" : g_phase == 1 ? "fighting" : witchspace() ? "in witchspace" : "flying");
    }
    char tl[16];
    hms(tl, sizeof tl, s_tot.life.frames);
    hms(t, sizeof t, s_tot.car.frames);
    int n = snprintf(j, sizeof j,
        "{\"title\":\"Elite agent: %s\",\"fields\":[[\"Commander\",\"%s\"],[\"Rank\",\"%s\"],[\"Tally\",\"%.1f\"],[\"Credits\",\"%.1f Cr\"],"
        "[\"Where\",\"%s\"],[\"Doing\",\"%s\"],[\"Mode\",\"%s\"],[\"Career played\",\"%s\"],[\"Kills\",\"%lu\"],"
        "[\"Deaths\",\"%lu\"],[\"Escape pods\",\"%lu\"],[\"Fights\",\"%lu\"],[\"Docked\",\"%lu times\"],"
        "[\"Careers since boot\",\"%lu, %s played\"],[\"All kills, deaths\",\"%lu, %lu\"],[\"Resets\",\"%lu\"]],"
        "\"progress\":{\"label\":\"To Elite: %.1f of 6400\",\"value\":%.1f,\"max\":6400},"
        "\"actions\":[{\"label\":\"%s\",\"cmd\":\"agent %s\",\"on\":%s},"
        "{\"label\":\"Efficient\",\"cmd\":\"agent mode efficient\",\"on\":%s},"
        "{\"label\":\"Curious\",\"cmd\":\"agent mode curious\",\"on\":%s}],\"log\":[",
        s_tot.name, in_game ? s_tot.cmdr : "-", rank_of(ty), ty, cr, where, doing, g_curious ? "curious" : "efficient", t,
        (unsigned long) s_tot.car.kills, (unsigned long) s_tot.car.deaths, (unsigned long) s_tot.car.pods,
        (unsigned long) s_tot.car.fights, (unsigned long) s_tot.car.docks,
        (unsigned long) s_tot.careers, tl, (unsigned long) s_tot.life.kills, (unsigned long) s_tot.life.deaths,
        (unsigned long) s_tot.resets, ty, ty,
        s_on ? "Stop" : "Start", s_on ? "stop" : "start", s_on ? "true" : "false",
        g_curious ? "false" : "true", g_curious ? "true" : "false");
    for (int i = s_logn - 1, k = 0; i >= 0 && k < LOGN && n < (int) sizeof j - 220; i--, k++) {
        jstr(q, sizeof q, s_log[i % LOGN]);
        n += snprintf(j + n, sizeof j - n, "%s%s", k ? "," : "", q);
    }
    snprintf(j + n, sizeof j - n, "]}");
    return j;
}

extern const struct machine_boot_image elite_splash;             // (the loading screen: presto/elite_splash.c)
const struct machine_boot_image *machine_boot_image(void) { return &elite_splash; }

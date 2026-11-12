// The C player on a real BBC Micro Model B, through the Pi1MHz RAM probe (Pi1MHz PR #51,
// docs/dev/ramprobe.md there; docs/PROBE.md here).  The probe stops the BBC for a moment,
// reads or writes its RAM over HTTP, and lets it go on: no ROM, no keyboard rig.
//   run_probe HOST[:PORT] [minutes] [journal.jsonl]
// BOOT=1 loads Elite from the disc first, from BASIC's prompt with the probe installed
// (X%=23:CALL &FC88).  Model B disc Elite (flicker-free) only.
//
// Each pass: one stop reads the RAM the player looks at, the player decides, and a second stop
// (only when something changed) writes the keys down.  Keys reach Elite through its own key
// routine, DKS4, patched to look up a list of held keys; the commander's name, which Elite
// reads through the OS (OSWORD 0), goes into the OS's keyboard buffer as well.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "player.h"
#include "keyq.h"
#include "elite_ram.h"
#include "keys.h"

// ---- HTTP: one request at a time, the connection kept while the server keeps it

static char s_host[128];
static int s_port = 80, s_sock = -1;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "run_probe: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static int connect_host(void) {
    struct addrinfo hints = {0}, *res;
    char port[8];
    snprintf(port, sizeof port, "%d", s_port);
    hints.ai_family = AF_INET, hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(s_host, port, &hints, &res)) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen)) close(fd), fd = -1;
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        struct timeval tv = {15, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    return fd;
}

static int send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k <= 0) return -1;
        p += k, n -= (size_t) k;
    }
    return 0;
}

// method path (with its query) -> the HTTP status, the body in out (NUL-terminated)
static int http(const char *method, const char *path, char *out, size_t outsz) {
    static char buf[16384];
    for (int attempt = 0; attempt < 20; attempt++) {      // (WiFi drops a request now and then: retry for ~10 s)
        if (attempt) usleep(500000);
        if (s_sock < 0 && (s_sock = connect_host()) < 0) continue;
        char req[4096];
        int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Length: 0\r\n\r\n", method, path, s_host);
        if (n >= (int) sizeof req) die("request too long: %s", path);
        if (send_all(s_sock, req, (size_t) n)) { close(s_sock), s_sock = -1; continue; }
        size_t got = 0;
        char *body = NULL;
        while (!body) {
            ssize_t k = recv(s_sock, buf + got, sizeof buf - 1 - got, 0);
            if (k <= 0) break;
            got += (size_t) k, buf[got] = 0;
            char *e = strstr(buf, "\r\n\r\n");
            if (e) body = e + 4;
        }
        if (!body) { close(s_sock), s_sock = -1; continue; }
        int status = atoi(buf + 9);
        char *cl = strcasestr(buf, "Content-Length:");
        size_t want = cl && cl < body ? (size_t) atol(cl + 15) : 0;
        int keep = !(strcasestr(buf, "Connection: close") && strcasestr(buf, "Connection: close") < body);
        size_t head = (size_t) (body - buf);
        while (got - head < want) {
            if (got >= sizeof buf - 1) die("answer too long for %s", path);
            ssize_t k = recv(s_sock, buf + got, sizeof buf - 1 - got, 0);
            if (k <= 0) break;
            got += (size_t) k, buf[got] = 0;
        }
        size_t len = got - head < want ? got - head : want;
        if (len >= outsz) len = outsz - 1;
        memcpy(out, body, len), out[len] = 0;
        if (!keep) close(s_sock), s_sock = -1;
        return status;
    }
    die("no answer from %s:%d", s_host, s_port);
    return 0;
}

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + t.tv_nsec / 1e9;
}

// a string field of the probe's status JSON ("" if absent), and a number field (-1 if absent or null)
static void json_str(const char *j, const char *key, char *out, size_t n) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(j, pat);
    size_t i = 0;
    if (p) for (p += strlen(pat); *p && *p != '"' && i + 1 < n; p++) out[i++] = *p;
    out[i] = 0;
}
static long json_num(const char *j, const char *key) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(j, pat);
    if (!p || !isdigit((unsigned char) p[strlen(pat)])) return -1;
    return atol(p + strlen(pat));
}

// ---- the probe (Pi1MHz docs/dev/ramprobe.md: HTTP routes)

static double s_stopped;        // seconds the BBC has spent stopped for us, which is no game time
static double s_read_at;        // when the last peek's stop ended: the RAM the player sees is from then
// timings, summed over the passes since the last report: the peek's stop, fetching its data, the
// player, the poke's stop, and the game time from the read to the keys being written
static double s_t_peek, s_t_data, s_t_player, s_t_poke, s_t_lat, s_t_peek_max;
static int s_n_lat;
static char s_st[2048];

typedef struct { uint16_t at, n; } range_t;

static void ranges_param(char *q, size_t qn, const range_t *r, int nr) {
    size_t at = 0;
    for (int i = 0; i < nr; i++) at += (size_t) snprintf(q + at, qn - at, "%s0x%X:%u", i ? "," : "", r[i].at, r[i].n);
}

// a request with rung=1&wait=1 (stop, work, go); its time counts as stopped. 0 or the probe's error
static int act(const char *path_query, char *err, size_t errn) {
    double t = now_s();
    int st = http("POST", path_query, s_st, sizeof s_st);
    s_stopped += now_s() - t;
    json_str(s_st, "error", err, errn);
    if (st != 200 && !*err) snprintf(err, errn, "HTTP %d", st);
    return st == 200 && !*err ? 0 : -1;
}

// the ranges' bytes, back to back into out; 0, or -1 with err
static int peek(const range_t *r, int nr, uint8_t *out, char *err, size_t errn) {
    char q[1024], path[1200];
    ranges_param(q, sizeof q, r, nr);
    snprintf(path, sizeof path, "/bbc/peek?ranges=%s&rung=1&wait=1", q);
    double t0 = now_s();
    if (act(path, err, errn)) return -1;
    s_read_at = now_s();
    s_t_peek += s_read_at - t0;
    if (s_read_at - t0 > s_t_peek_max) s_t_peek_max = s_read_at - t0;
    int total = 0;
    for (int i = 0; i < nr; i++) total += r[i].n;
    static char hex[8200];
    for (int off = 0; off < total; off += 4096) {
        int n = total - off < 4096 ? total - off : 4096;
        snprintf(path, sizeof path, "/bbc/data?offset=%d&len=%d", off, n);
        if (http("GET", path, hex, sizeof hex) != 200 || (int) strlen(hex) < 2 * n) { snprintf(err, errn, "short data"); return -1; }
        for (int i = 0; i < n; i++) {
            unsigned v;
            sscanf(hex + 2 * i, "%2x", &v);
            out[off + i] = (uint8_t) v;
        }
    }
    s_t_data += now_s() - s_read_at;
    return 0;
}

static int poke(const range_t *r, int nr, const uint8_t *data, char *err, size_t errn) {
    char q[1024];
    static char path[4096];
    ranges_param(q, sizeof q, r, nr);
    int total = 0;
    for (int i = 0; i < nr; i++) total += r[i].n;
    if (total > 512) die("poke of %d bytes", total);
    int at = snprintf(path, sizeof path, "/bbc/poke?ranges=%s&rung=1&wait=1&hex=", q);
    for (int i = 0; i < total; i++) at += snprintf(path + at, sizeof path - (size_t) at, "%02X", data[i]);
    return act(path, err, errn);
}

static void must(int ok, const char *what, const char *err) { if (ok) die("%s: %s", what, err); }

// ---- Elite's key routine, DKS4 (every key Elite reads goes through it: Moxon's flight and
// docked sources), and the patch that has it look up the keys we hold instead of the keyboard

// the original: LDA #3: SEI: STA &FE40: LDA #&7F: STA &FE43: STX &FE4F: LDX &FE4F: LDA #&0B:
// STA &FE40: CLI: TXA: RTS.  At &4431 in the flight code (D.CODE), &3C6F in the docked (T.CODE).
static const uint8_t DKS4[25] = {0xA9, 0x03, 0x78, 0x8D, 0x40, 0xFE, 0xA9, 0x7F, 0x8D, 0x43, 0xFE, 0x8E, 0x4F,
                                 0xFE, 0xAE, 0x4F, 0xFE, 0xA9, 0x0B, 0x8D, 0x40, 0xFE, 0x58, 0x8A, 0x60};
static const uint16_t DKS4_AT[2] = {0x4431, 0x3C6F};
#define NLIST 6          // keys held at once: the player holds six at most
#define LIST_AT 18       // where the list starts in the patched routine

// the same 25 bytes, which return the key number in A and X with bit 7 set if it's in the list
// at +18 (&FF for none).  The flight keys come with bit 7 set (KYTB), which the keyboard
// ignores, so it's masked off first.  Y is kept, as the original does; the last byte stays the
// RTS that DKS1 branches to.
static void dks4_patch(uint16_t at, uint8_t *p) {
    uint16_t list = (uint16_t) (at + LIST_AT);
    const uint8_t code[LIST_AT] = {
        0x8A,                                        // TXA
        0x29, 0x7F,                                  // AND #&7F
        0xA2, NLIST - 1,                             // LDX #5
        0xDD, (uint8_t) list, (uint8_t) (list >> 8), // .l CMP list,X
        0xF0, 0x04,                                  // BEQ down
        0xCA,                                        // DEX
        0x10, 0xF8,                                  // BPL l
        0x2C,                                        // BIT &8009 (steps over the ORA)
        0x09, 0x80,                                  // .down ORA #&80
        0xAA, 0x60,                                  // TAX: RTS
    };
    memcpy(p, code, LIST_AT);
    memset(p + LIST_AT, 0xFF, NLIST);
    p[24] = 0x60;
}

// ---- the OS's keyboard buffer (MOS 1.20): Elite reads the commander's name with OSWORD 0
#define KEYBUF_START 0x02D8
#define KEYBUF_END 0x02E1
#define KEYBUF_FIRST 0xE0          // the buffer is &03E0-&03FF

// the ASCII for a key the player types at a prompt: letters, digits, SPACE (keys.h's
// key_for_char, the other way) and RETURN; 0 for the rest
static int key_ascii(int code) {
    if (code == K_RETURN) return '\r';
    static const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    for (const char *c = chars; *c; c++) if (key_for_char(*c) == code) return *c;
    return 0;
}

// ---- the player's side

static player_io_t io;
static FILE *journal;
static uint8_t s_ram[0x8000];
static uint8_t s_down[16];             // the keys down now, a bit each
static char s_typed[64];               // characters for the OS's buffer, not yet put in
static int s_ntyped;
static int s_dirty, s_toomany;

static void p_hold(void *ctx, const uint8_t *c, int n) { (void) ctx; keyq_hold(io.frame, c, n); }
static void p_tap(void *ctx, uint8_t c, int down, int up) { (void) ctx; keyq_tap(io.frame, c, down, up); }
static void p_log(void *ctx, int level, const char *s) {
    (void) ctx;
    static const char *TAG[] = {"", "  ", "    "};
    if (level <= PLOG_INFO) printf("%7.1f %s%s\n", io.frame / 50.0 / 60.0, TAG[level], s), fflush(stdout);
}
static void p_note(void *ctx, const char *s) {
    (void) ctx;
    if (journal) fprintf(journal, "{\"frame\": %u, \"e\": %s}\n", io.frame, s), fflush(journal);
}
static void key(int code, int down) {
    uint8_t bit = (uint8_t) (1u << (code & 7));
    if (down) s_down[(code & 127) >> 3] |= bit;
    else s_down[(code & 127) >> 3] &= (uint8_t) ~bit;
    s_dirty = 1;
    int ch = down ? key_ascii(code) : 0;
    if (ch && s_ntyped < (int) sizeof s_typed) s_typed[s_ntyped++] = (char) ch;
}

// ---- booting Elite: *LOAD ELITE2, one instruction of its vector copy changed, CALL &2F23

// put text in the OS's keyboard buffer, 30 characters at a time, each lot once the OS has taken the last
static void type_text(const char *text) {
    char err[200];
    for (const char *p = text; *p; ) {
        uint8_t ptr[10];
        range_t r = {KEYBUF_START, 10};
        must(peek(&r, 1, ptr, err, sizeof err), "keyboard buffer", err);
        if (ptr[0] != ptr[9]) { usleep(50000); continue; }
        uint8_t data[40];
        range_t w[33];
        int nw = 0, nd = 0, y = ptr[9];
        for (; *p && nd < 30 && nw < 31; p++) {
            w[nw++] = (range_t) {(uint16_t) (0x300 + y), 1};
            data[nd++] = (uint8_t) *p;
            y = y == 0xFF ? KEYBUF_FIRST : y + 1;
        }
        w[nw++] = (range_t) {KEYBUF_END, 1};
        data[nd++] = (uint8_t) y;
        must(poke(w, nw, data, err, sizeof err), "typing", err);
    }
}

static void boot(void) {
    char err[200];
    uint8_t v[2];
    range_t r = {0xFFFC, 2};
    must(peek(&r, 1, v, err, sizeof err), "reset vector", err);
    if (v[0] != 0xCD || v[1] != 0xD9) die("not a Model B with MOS 1.20 (reset vector &%02X%02X)", v[1], v[0]);
    printf("boot: *LOAD ELITE2\n"), fflush(stdout);
    // ELITE2 copies the MOS's default vectors over &0200-&0235, the probe's hooks on IRQ2V and
    // EVNTV with them; then it loads the rest through the DFS, which takes the NMI's &0D00, so the
    // probe could never stop the game again.  Its copy is cut to &0200-&0205 (USERV, BRKV,
    // IRQ1V): CPY &FFB6 (the table's length, &36) becomes CPY #6.  The other vectors keep what
    // BASIC and the DFS gave them.
    type_text("*LOAD ELITE2\r");
    uint8_t e2[256];
    range_t r2 = {0x2F00, 256};
    double until = now_s() + 30;
    int at = -1;
    while (at < 0) {
        if (now_s() > until) die("boot: no ELITE2 at &2F00 (is the Elite disc in drive 0?)");
        usleep(300000);
        if (peek(&r2, 1, e2, err, sizeof err)) continue;
        for (int i = 0; i + 6 <= 256; i++)      // LDA (&01),Y: STA &0200,Y: INY: CPY &FFB6
            if (!memcmp(e2 + i, (const uint8_t[]) {0xB1, 0x01, 0x99, 0x00, 0x02, 0xC8, 0xCC, 0xB6, 0xFF}, 9)) at = i + 6;
    }
    range_t w = {(uint16_t) (0x2F00 + at), 3};
    must(poke(&w, 1, (const uint8_t[]) {0xC0, 0x06, 0xEA}, err, sizeof err), "boot: patching ELITE2", err);
    printf("boot: CALL &2F23 (ELITE2's copy of the vectors cut to &0200-&0205)\n"), fflush(stdout);
    type_text("CALL &2F23\r");
}

// ---- each pass

// what the player reads on disc Elite, every pass (elite_ver.c's disc table): zero page; page 3
// (the workspace, and the OS's keyboard buffer at &03E0); the OS's buffer pointers; K%, the ship
// blocks; LASX to ALTIT; NA%, the last save; DKS4 in both codes.  Plus 256 bytes of the rest in
// turn, so the whole 32 KB is read every 128 passes (the version check reads &0400, &2000, &3000).
static range_t s_hot[] = {
    {0x0000, 256}, {0x02D8, 10}, {0x0300, 256}, {0x0900, 0x1E2}, {0x0FCE, 5}, {0x1181, 0x54},
    {0x4431, 25}, {0x3C6F, 25},
};
#define NHOT (int) (sizeof s_hot / sizeof s_hot[0])

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: run_probe HOST[:PORT] [minutes] [journal.jsonl]\n");
        return 1;
    }
    snprintf(s_host, sizeof s_host, "%s", argv[1]);
    char *colon = strchr(s_host, ':');
    if (colon) *colon = 0, s_port = atoi(colon + 1);
    double minutes = argc > 2 ? atof(argv[2]) : 60;
    journal = fopen(argc > 3 ? argv[3] : "/dev/null", "w");
    if (getenv("NAME")) { extern char g_name[8]; snprintf(g_name, sizeof g_name, "%s", getenv("NAME")); }
    { extern int g_save; if (getenv("SAVE")) g_save = atoi(getenv("SAVE")); }

    char st[2048], err[200];
    if (http("GET", "/bbc", st, sizeof st) != 200) die("no probe at %s:%d", s_host, s_port);
    if (!strstr(st, "\"installed\":true")) die("the probe isn't installed: type X%%=23:CALL &FC88 at the BBC's BASIC prompt");
    if (getenv("BOOT")) boot();

    io.ram = s_ram;
    io.hold = p_hold, io.tap = p_tap, io.log = p_log, io.note = p_note;
    io.analogue = NULL;            // (disc Elite on the Model B fires only from the stick's button: it plays on keys)
    io.machine = 1;
    keyq_reset();
    player_init(&io);

    uint8_t hot[2048];
    int hot_n = 0;
    for (int i = 0; i < NHOT; i++) hot_n += s_hot[i].n;
    if (hot_n + 256 > (int) sizeof hot) die("hot ranges too big");
    double t0 = now_s(), last_report = t0;
    uint32_t end = (uint32_t) (minutes * 60 * 50), roll = 0, passes = 0, errors = 0, pokes = 0;
    int live = -1;                   // which DKS4 is patched and in use (0 flight, 1 docked), -1 none
    uint8_t list_now[NLIST];
    memset(list_now, 0xFF, sizeof list_now);
    s_stopped = 0;
    while (io.frame < end) {
        // read
        range_t r[NHOT + 1];
        memcpy(r, s_hot, sizeof s_hot);
        r[NHOT] = (range_t) {(uint16_t) (roll * 256), 256};
        if (peek(r, NHOT + 1, hot, err, sizeof err)) {
            if (++errors % 20 == 1) fprintf(stderr, "peek: %s\n", err);
            usleep(20000);
            continue;
        }
        roll = (roll + 1) & 127;
        int off = 0;
        for (int i = 0; i <= NHOT; i++) memcpy(s_ram + r[i].at, hot + off, r[i].n), off += r[i].n;
        passes++;

        // the game's time: the wall's, less the stops
        uint32_t game = (uint32_t) ((now_s() - t0 - s_stopped) * 50);
        while (io.frame < game) io.taps_done += (uint32_t) keyq_step(io.frame, key), io.frame++;
        double tp = now_s();
        player_frame();
        s_t_player += now_s() - tp;

        // write: the patch where a fresh DKS4 has been loaded (each launch and dock loads the code
        // again), the held keys into its list, and typed characters into the OS's buffer
        range_t w[40];
        uint8_t data[512];
        int nw = 0, nd = 0, patch_at = -1;
        live = -1;
        for (int i = 0; i < 2; i++) {
            const uint8_t *b = s_ram + DKS4_AT[i];
            uint8_t want[25];
            dks4_patch(DKS4_AT[i], want);
            if (!memcmp(b, want, LIST_AT)) live = i, memcpy(list_now, b + LIST_AT, NLIST);
            else if (!memcmp(b, DKS4, 25)) patch_at = i;
        }
        if (patch_at >= 0) {
            // not while the BBC is inside DKS4: stop it, check where, patch, go on
            if (!act("/bbc/halt?rung=1&wait=1", err, sizeof err)) {
                long pc = json_num(s_st, "pc");
                uint16_t at = DKS4_AT[patch_at];
                if (pc >= at && pc < at + 25) {
                    act("/bbc/continue?wait=1", err, sizeof err);
                } else {
                    uint8_t p[25];
                    dks4_patch(at, p);
                    char path[200];
                    int a = snprintf(path, sizeof path, "/bbc/poke?addr=%u&wait=1&hex=", at);
                    for (int i = 0; i < 25; i++) a += snprintf(path + a, sizeof path - (size_t) a, "%02X", p[i]);
                    if (act(path, err, sizeof err)) fprintf(stderr, "patch: %s\n", err);
                    else printf("%7.1f patched DKS4 at &%04X (%s code)\n", io.frame / 3000.0, at, patch_at ? "docked" : "flight");
                    act("/bbc/continue?wait=1", err, sizeof err);
                    live = patch_at, memset(list_now, 0xFF, sizeof list_now);
                    s_dirty = 1;
                }
            }
        }
        if (live >= 0 && s_dirty) {
            uint8_t list[NLIST];
            int n = 0;
            for (int c = 0; c < 128; c++)
                if (s_down[c >> 3] >> (c & 7) & 1) {
                    if (n < NLIST) list[n++] = (uint8_t) c;
                    else if (!s_toomany++) fprintf(stderr, "more than %d keys held: key &%02X left out\n", NLIST, c);
                }
            while (n < NLIST) list[n++] = 0xFF;
            if (memcmp(list, list_now, NLIST)) {
                w[nw++] = (range_t) {(uint16_t) (DKS4_AT[live] + LIST_AT), NLIST};
                memcpy(data + nd, list, NLIST), nd += NLIST;
                memcpy(list_now, list, NLIST);
            }
            s_dirty = 0;
        }
        if (s_ntyped) {
            int start = s_ram[KEYBUF_START], y = s_ram[KEYBUF_END], used = (y - start + 32) & 31, n = 0;
            for (; n < s_ntyped && used + n < 30 && nw < 38; n++) {
                w[nw++] = (range_t) {(uint16_t) (0x300 + y), 1};
                data[nd++] = (uint8_t) s_typed[n];
                y = y == 0xFF ? KEYBUF_FIRST : y + 1;
            }
            if (getenv("PROBELOG")) printf("%7.1f typed %d of %d into the OS's buffer (start %02X end %02X)\n", io.frame / 3000.0, n, s_ntyped, start, s_ram[KEYBUF_END]);
            if (n) {
                w[nw++] = (range_t) {KEYBUF_END, 1};
                data[nd++] = (uint8_t) y;
            }
            s_ntyped = 0;      // (what didn't fit is dropped: Elite flushes the buffer before it reads a line)
        }
        if (nw) {
            double tw = now_s();
            s_t_lat += tw - s_read_at, s_n_lat++;
            if (poke(w, nw, data, err, sizeof err)) fprintf(stderr, "poke: %s\n", err);
            else pokes++;
            s_t_poke += now_s() - tw;
        }

        double t = now_s();
        if (t - last_report > (getenv("PROBEREPORT") ? atof(getenv("PROBEREPORT")) : 60)) {
            last_report = t;
            printf("%7.1f probe: %u passes, %.1f a game second; stopped %.0f%% of the time; %u pokes, %u errors, %d too many keys\n",
                   io.frame / 3000.0, passes, passes / ((t - t0 - s_stopped) > 0 ? (t - t0 - s_stopped) : 1),
                   100 * s_stopped / (t - t0), pokes, errors, s_toomany);
            static uint32_t passes_was, pokes_was;
            uint32_t np = passes - passes_was, nk = pokes - pokes_was;
            if (np && nk)
                printf("        a pass: peek's stop %.1f ms (longest %.0f), its data %.1f ms, the player %.2f ms; a poke's stop %.1f ms; "
                       "read to keys written %.1f ms of game time\n",
                       1e3 * s_t_peek / np, 1e3 * s_t_peek_max, 1e3 * s_t_data / np, 1e3 * s_t_player / np, 1e3 * s_t_poke / nk, 1e3 * s_t_lat / s_n_lat);
            passes_was = passes, pokes_was = pokes;
            s_t_peek = s_t_data = s_t_player = s_t_poke = s_t_lat = s_t_peek_max = 0, s_n_lat = 0;
            fflush(stdout);
        }
        // at most one pass a frame
        double next = t0 + s_stopped + (io.frame + 1) / 50.0;
        if (next > t) usleep((useconds_t) ((next - t) * 1e6));
    }
    printf("done: %.1f game minutes, %u passes\n", io.frame / 3000.0, passes);
    return 0;
}

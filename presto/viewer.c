// The AI viewer, on the Presto's own panel (the firmware's app screen: app_api.h): a tap anywhere swaps it with the game.
// The network flying now, from the same senses and the same weights as the web demo's panel (web/panel.json, viewer_nets.h),
// in the panel's 8 colours; the agent's log when no network is flying. It only reads, about 10 times a second, from the
// app's frame hook: it touches nothing the player reads, and draws nothing on the player's stack.
#include "viewer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "viewer_nets.h"

enum { BLUE = 1, GREEN = 2, CYAN = 3, RED = 4, MAGENTA = 5, YELLOW = 6, WHITE = 7 };
// The rows: a blank one, four lines of text, the bars and the net's slots, each a row for the cells and a row for the gap under
// it (the links run through both), stretched by the map to 6 lines and 2: 123 rows, 30 KB, of the SRAM's scarce heap
enum { R_BLANK = 0, R_L1 = 1, R_L2 = 8, R_L3 = 15, R_LAB = 22, R_BAR = 29, R_NET = 31, SLOTS = 46, NET_ROWS = SLOTS * 2, ROWS = R_NET + NET_ROWS, NET_Y = 112, LOG_PITCH = 18, LOG_LINES = 13 };
static const unsigned char FONT[96][5] = {
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},
{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},
{0,1,2,4,0},{32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},
{124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{8,4,8,16,8},{0,0,0,0,0}};
static const struct app_api *s_api;
static uint32_t *s_fb;
static int s_rw;
static bool s_ok, s_logmap, s_mapped;
static uint32_t s_next, s_us_max, s_us_sum, s_n, s_dec;
static const vnet_t *s_drawn;
static float s_a[440];
static char s_line[3][44];

static inline void px(int x, int y, int c) { uint32_t *w = s_fb + y * s_rw + (x >> 3); int sh = (x & 7) * 4; *w = (*w & ~(0xFu << sh)) | ((uint32_t) c << sh); }
// a span of a row: x0 up to x1, in a pattern of 8 nibbles
static void span(int x0, int x1, int y, uint32_t pat)
{
    if (x0 < 0) x0 = 0;
    if (x1 > 480) x1 = 480;
    uint32_t *r = s_fb + y * s_rw;
    for (int w = x0 >> 3; w <= (x1 - 1) >> 3 && x1 > x0; w++) {
        uint32_t m = 0xFFFFFFFFu;
        if (w == x0 >> 3) m &= 0xFFFFFFFFu << ((x0 & 7) * 4);
        if (w == (x1 - 1) >> 3 && ((x1 - 1) & 7) != 7) m &= ~(0xFFFFFFFFu << ((((x1 - 1) & 7) + 1) * 4));
        r[w] = (r[w] & ~m) | (pat & m);
    }
}
static inline uint32_t solid(int c) { return (uint32_t) c * 0x11111111u; }
static void text(int x, int row, const char *s, int c)
{
    for (; *s && x + 5 * 2 <= 480; s++) {
        unsigned ch = (unsigned char) *s; if (ch >= 0x80) { if (ch != 0xB7) continue; ch = '-'; } if (ch < 32) ch = '?';      // (UTF-8: the log's middle dot, C2 B7, as a dash)
        const unsigned char *g = FONT[ch - 32];
        for (int i = 0; i < 5; i++) for (int b = 0; b < 7; b++) { int on = g[i] >> b & 1; px(x + i * 2, row + b, on ? c : 0); px(x + i * 2 + 1, row + b, on ? c : 0); }
        x += 12;
    }
}
static void text_line(int n, int row, const char *s, int c)
{
    if (!strcmp(s_line[n], s)) return;
    snprintf(s_line[n], sizeof s_line[n], "%s", s);
    for (int y = 0; y < 7; y++) span(8, 480, row + y, 0);
    text(8, row, s_line[n], c);
}
static void map_view(bool log)
{
    if (s_mapped && s_logmap == log) return;
    s_mapped = true, s_logmap = log;
    for (int y = 0; y < 480; y++) {
        int r = R_BLANK;
#define BAND(top, n, row, k) if (y >= (top) && y < (top) + (n) * (k)) r = (row) + (y - (top)) / (k)
        BAND(4, 7, R_L1, 2); BAND(22, 7, R_L2, 2); BAND(40, 7, R_L3, 2); BAND(62, 7, R_LAB, 2); BAND(78, 2, R_BAR, 8);
        if (log) { if (y >= NET_Y) { int k = (y - NET_Y) / LOG_PITCH, o = (y - NET_Y) % LOG_PITCH; if (k < LOG_LINES && o < 14) r = R_NET + k * 7 + o / 2; } }
        else if (y >= NET_Y) { int o = (y - NET_Y) % 8; r = R_NET + (y - NET_Y) / 8 * 2 + (o >= 6); }
        s_api->view_map(y, r);
    }
}
static void net_clear(void) { for (int y = 0; y < NET_ROWS; y++) span(0, 480, R_NET + y, 0); }

// a forward pass, sparse, every layer's activations in s_a: the senses first
static void forward(const vnet_t *N, const float *f)
{
    memcpy(s_a, f, N->size[0] * sizeof(float));
    int in = 0, out = N->size[0], u = 0;
    for (int l = 0; l < N->nl; l++) {
        for (int o = 0; o < N->size[l + 1]; o++, u++) {
            float s = N->b[u];
            for (int k = N->row[u]; k < N->row[u + 1]; k++) s += N->w[k] * s_a[in + N->idx[k]];
            s_a[out + o] = l < N->nl - 1 && s < 0 ? 0 : s;
        }
        in = out, out += N->size[l + 1];
    }
}
static void draw_net(const vnet_t *N, int col)
{
    static uint16_t lvl[16];
    int off = 0;
    for (int l = 0; l <= N->nl; l++) lvl[l] = off, off += N->size[l];
    int rows = 0;
    for (int c = 0; c < N->ncol; c++) if (N->colsize[c] > rows) rows = N->colsize[c];
    const int gap = N->ncol > 5 ? 14 : 22, cw = (464 - (N->ncol - 1) * gap) / N->ncol;
#define SLOT(r) (((r) * SLOTS / rows) * 2)
    static float lit[6][46];
    const vcell_t *ce = N->cell;
    const uint32_t full = solid(col), half = (uint32_t) col * 0x01010101u, quart = (uint32_t) col * 0x00010001u, idle = (uint32_t) BLUE * 0x00010001u;
    for (int c = 0; c < N->ncol; c++) {
        const int x = 8 + c * (cw + gap);
        for (int r = 0; r < N->colsize[c]; r++, ce++) {
            const float *A = s_a + lvl[ce->lvl];
            float v = ce->u2 != 255 ? A[ce->u2] - A[ce->u] + A[ce->u3] : A[ce->u];
            float L = fabsf(v) / ce->scale; if (L > 1) L = 1;
            lit[c][r] = L;
            uint32_t p = L > 0.6f ? full : L > 0.3f ? half : L > 0.04f ? quart : idle;
            span(x, x + cw, R_NET + SLOT(r), p);
        }
    }
    // the lit links, 1 px, across the gaps
    for (int c = 0; c + 1 < N->ncol; c++) { const int x = 8 + c * (cw + gap) + cw; for (int y = 0; y < NET_ROWS; y++) span(x, x + gap, R_NET + y, 0); }
    for (int i = 0; i < N->nlink; i++) {
        const vlink_t *K = &N->link[i];
        float L = lit[K->c][K->a] * (0.4f + 0.6f * lit[K->c + 1][K->b]);
        if (L < 0.25f) continue;
        int x0 = 8 + K->c * (cw + gap) + cw, y0 = R_NET + SLOT(K->a), y1 = R_NET + SLOT(K->b);
        for (int k = 0; k < gap; k++) px(x0 + k, y0 + (y1 - y0) * k / gap, col);
    }
    // the four controls
    static const char *const NAME[2][4] = {{"ROLL", "PITCH", "THROTTLE", "FIRE"}, {"ROLL", "PITCH", "THROTTLE", "JUMP"}};
    const float *O = s_a + lvl[N->nl];
    for (int i = 0; i < 4; i++) {
        const int x = 8 + i * 117, W = 112;
        for (int y = 0; y < 7; y++) span(x, x + W, R_LAB + y, 0);
        text(x, R_LAB, NAME[N == &V_DOCK][i], WHITE);
        float fr = O[N->out[i]] / N->outscale[i]; fr = fr > 1 ? 1 : fr < -1 ? -1 : fr;
        const bool centred = i < 2;
        int a = centred ? x + W / 2 : x, b = a + (int) (fr * (centred ? W / 2 : W));
        if (b < a) { int t = a; a = b; b = t; }
        for (int y = 0; y < 2; y++) { span(x, x + W, R_BAR + y, idle); if (b > a) span(a, b + 1, R_BAR + y, solid(col)); }
    }
}
bool viewer_init(const struct app_api *api)
{
    s_api = api;
    if (api->size < offsetof(struct app_api, view_errors) + sizeof api->view_errors || !api->view_open) return false;
    int dr = 0;
    return s_ok = api->view_open(ROWS, &s_rw, &dr) && dr == 480;
}
bool viewer_due(void)
{
    if (!s_ok) return false;
    uint32_t *fb = s_api->view_buffer();
    if (!fb) { s_fb = NULL; return false; }
    if (!s_fb) {        // (just shown: blank again; everything anew)
        s_fb = fb, s_mapped = false, s_drawn = (const vnet_t *) -1, s_line[1][0] = s_line[2][0] = 0;
        s_next = s_api->time_us();
        text(8, R_L1, "8bitkick/elite-agent", WHITE);
    }
    uint32_t now = s_api->time_us();
    if ((int32_t) (now - s_next) < 0) return false;
    s_next = now + 100000;
    return true;
}
void viewer_draw(const viewer_info_t *I)
{
    uint32_t t0 = s_api->time_us();
    extern float g_wsp_last_f[], g_dock_last_f[];
    extern uint32_t g_wsp_last_frame, g_dock_last_frame;
    extern int g_wsp_last_net;
    uint32_t f = I->frame;
    int fight = g_wsp_last_frame > 0 && f - g_wsp_last_frame < 30, dock = !fight && g_dock_last_frame > 0 && f - g_dock_last_frame < 30;
    const vnet_t *N = NULL; const float *v = NULL; int col = WHITE; const char *who = "AGENT LOG";
    if (fight) {
        v = g_wsp_last_f;
        if (g_wsp_last_net == 1) N = &V_CAMERA, col = MAGENTA, who = "CAMERA AI";
        else if (g_wsp_last_net == 2) N = &V_INTERCEPTOR, col = RED, who = "INTERCEPTOR AI";
#if !NO_FAIR
        else N = &V_FIGHT, col = YELLOW, who = "DOGFIGHT AI";
#endif
    } else if (dock) N = &V_DOCK, v = g_dock_last_f, col = CYAN, who = "DOCKING AI";
    char b[44];
    snprintf(b, sizeof b, "%s  %s  %.0f", I->time, I->rank, I->tally);
    text_line(1, R_L2, b, WHITE);
    text_line(2, R_L3, I->log_n ? I->log(I->log_n - 1) : "", GREEN);
    { int n = (int) strlen(who), x = 472 - n * 12; for (int y = 0; y < 7; y++) span(256, 480, R_L1 + y, 0); text(x, R_L1, who, col); }
    map_view(N == NULL);
    if (N != s_drawn) { net_clear(); s_drawn = N; memset(s_line[0], 0, sizeof s_line[0]); for (int y = 0; y < 9; y++) span(0, 480, R_LAB + y, 0); }
    if (N) { forward(N, v); draw_net(N, col); s_dec++; }
    else {
        net_clear();
        int n = I->log_n < LOG_LINES ? I->log_n : LOG_LINES;
        for (int i = 0; i < n; i++) text(8, R_NET + i * 7, I->log(I->log_n - n + i), i == n - 1 ? WHITE : GREEN);
    }
    uint32_t us = s_api->time_us() - t0;
    if (us > s_us_max) s_us_max = us;
    s_us_sum += us, s_n++;
}
int viewer_status(char *reply, int n)
{
    if (!s_ok) return snprintf(reply, n, "AGENT view off: the firmware has no app screen, or no RAM for it");
    int k = snprintf(reply, n, "AGENT view %s, taps %lu, edges %lu, touch errors %lu, drawn %lu times, avg %lu us, max %lu us", s_api->view_shown() ? "shown" : "hidden", (unsigned long) s_api->view_taps(), (unsigned long) s_api->view_edges(), (unsigned long) s_api->view_errors(), (unsigned long) s_n, (unsigned long) (s_n ? s_us_sum / s_n : 0), (unsigned long) s_us_max);
    s_us_sum = s_us_max = s_n = 0;
    return k;
}
void viewer_set(bool on) { if (s_ok) s_api->view_set(on); }

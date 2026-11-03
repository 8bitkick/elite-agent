// The weight-space pilot's forward pass. Three fixed parts and the network between them:
//  - the senses (wsp_features): the target's sightline as a pilot reads it, how far off the nose and at what
//    clock position, how fast it moves across the sky, how big it looks, the range, the stick under the hand.
//    Coordinate changes only; no constants that decide anything.
//  - the network: every decision, in the weights (wsp/elite_wsp/author.py writes them, wsp/evolve tunes them).
//  - the hands (jstx_for, jsty_for): a roll and pitch rate to the stick position the game turns into that rate,
//    the inverse of Elite's own stick maps (pilot.c has the same two).
// The weights are compiled in (pilot_weights.h); WSPW=file loads another set at run time (the evolution's
// candidates), in the text format wsp/elite_wsp/export.py writes (not in the Presto app, which has no files).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "wsp_pilot.h"
#include "pilot_weights.h"

#define MAXL 8
#define MAXW 256
static int s_nl, s_shape[MAXL + 1];
static const float *s_W[MAXL], *s_B[MAXL];

#if !APP_LOADED     // (the Presto app has no files or heap: the compiled-in weights only)
static int read_net(const char *path, int *nl_out, int *shape, const float **Ws, const float **Bs) {
    FILE *f = fopen(path, "r");
    int nl;
    if (!f || fscanf(f, "WSP2 %d", &nl) != 1 || nl < 1 || nl > MAXL) { fprintf(stderr, "%s: not a weights file\n", path); exit(1); }
    for (int l = 0; l < nl; l++) {
        int r, c;
        if (fscanf(f, "%d %d", &r, &c) != 2 || r > MAXW || c > MAXW) { fprintf(stderr, "%s: bad layer %d\n", path, l); exit(1); }
        if (l == 0) shape[0] = c;
        shape[l + 1] = r;
        float *W = malloc(sizeof(float) * r * c), *B = malloc(sizeof(float) * r);
        for (int i = 0; i < r * c; i++) if (fscanf(f, "%f", &W[i]) != 1) { fprintf(stderr, "%s: short\n", path); exit(1); }
        for (int i = 0; i < r; i++) if (fscanf(f, "%f", &B[i]) != 1) { fprintf(stderr, "%s: short\n", path); exit(1); }
        Ws[l] = W, Bs[l] = B;
    }
    fclose(f);
    if (shape[0] != WSP_NF || shape[nl] != 4) { fprintf(stderr, "%s: %d inputs, %d outputs, want %d and 4\n", path, shape[0], shape[nl], WSP_NF); exit(1); }
    *nl_out = nl;
    return 1;
}

#endif

// reinforcement learning (wsp/evolve/ppo.py): WSPR=file a residual network added to the written one's outputs;
// WSPN="sa sb sf ss hold seed" explores: Gaussian on the roll and pitch rates and the speed, the laser a coin
// (p = sigmoid(8 (f - 0.5) )), each sampled action held for `hold` frames
static int s_rnl, s_rshape[MAXL + 1];
static const float *s_RW[MAXL], *s_RB[MAXL];
static double s_sa, s_sb, s_sf, s_ss;
static int s_hold = 1;
static uint64_t s_rng = 88172645463325252ULL;

static void load(void) {
    static int done;
    if (done) return;
    done = 1;
    s_nl = WSP_LAYERS;
    for (int i = 0; i <= s_nl; i++) s_shape[i] = WSP_SHAPE[i];
    for (int i = 0; i < s_nl; i++) s_W[i] = WSP_W[i], s_B[i] = WSP_B[i];
#if !APP_LOADED
    const char *path = getenv("WSPW");
    if (path && *path) read_net(path, &s_nl, s_shape, s_W, s_B);
    const char *rp = getenv("WSPR");
    if (rp && *rp) read_net(rp, &s_rnl, s_rshape, s_RW, s_RB);
#endif
    const char *n = getenv("WSPN");
    if (n && *n) {
        unsigned long long seed = 1;
        if (sscanf(n, "%lf %lf %lf %lf %d %llu", &s_sa, &s_sb, &s_sf, &s_ss, &s_hold, &seed) < 5) { fprintf(stderr, "WSPN: sa sb sf ss hold [seed]\n"); exit(1); }
        s_rng ^= seed * 0x9E3779B97F4A7C15ULL;
        if (s_hold < 1) s_hold = 1;
    }
}

void wsp_features(const wsp_in_t *in, float f[WSP_NF]) {
    double d = sqrt(in->x * in->x + in->y * in->y + in->z * in->z);
    if (d < 1) d = 1;
    double ux = in->x / d, uy = in->y / d, uz = in->z / d;
    double rx = in->vx, ry = in->vy, rz = in->vz - in->own;          // relative velocity
    double rd = rx * ux + ry * uy + rz * uz;
    double wx = (rx - rd * ux) / d, wy = (ry - rd * uy) / d;          // sightline rate, rad a loop
    double h = sqrt(ux * ux + uy * uy);
    double th = acos(uz < -1 ? -1 : uz > 1 ? 1 : uz);
    f[0] = (float) ux, f[1] = (float) uy, f[2] = (float) uz;
    f[3] = (float) (wx * 16), f[4] = (float) (wy * 16);
    f[5] = (float) (th / M_PI);
    f[6] = h > 1e-6 ? (float) (ux / h) : 0.0f;
    f[7] = h > 1e-6 ? (float) (uy / h) : 1.0f;
    f[8] = (float) (in->r / d * 16);
    f[9] = (float) fmin(d / 4096, 8);
    f[10] = (float) (-rd / 64);
    f[11] = (float) (fabs(in->own) / 64);
    f[12] = in->laser_temp / 255.0f;
    f[13] = in->energy / 255.0f;
    f[14] = in->witchspace ? 1.0f : 0.0f;
    f[15] = (in->jstx - 128) / 128.0f;
    f[16] = (in->jsty - 128) / 128.0f;
    f[17] = 1.0f;       // a target (the splice only asks with one)
    f[18] = 1.0f;       // the bias
    f[19] = in->heading_seen ? 1.0f : 0.0f;
    f[20] = (float) fmin(in->age / 50.0, 4.0);            // seconds stale, to 4
    f[21] = (float) fmin(in->range_sd * 4, 4.0);          // 1.0 = a 25% range error
    f[22] = (float) fmin(in->vel_sd / 16, 4.0);           // 1.0 = 16 units a loop (a Sidewinder's top speed is 55)
}

static int jstx_for(double alpha) {
    int a = (int) lround(alpha);
    if (a > 31) a = 31;
    if (a < -31) a = -31;
    int m = abs(a), d = m == 0 ? 0 : (m <= 3 ? m * 8 + 4 : (m * 4 + 2 > 32 ? m * 4 + 2 : 32));
    return 128 + (a > 0 ? d : -d);
}
static int jsty_for(double beta) {
    int b = (int) lround(beta);
    if (b > 8) b = 8;
    if (b < -8) b = -8;
    int m = abs(b), e = m == 0 ? 0 : (m <= 2 ? 36 : (m * 16 + 6 < 127 ? m * 16 + 6 : 127));
    return 128 + (b > 0 ? -e : e);
}

static void forward(int nl, const int *shape, const float **Ws, const float **Bs, const float *f, float out[4]) {
    float a[MAXW], b[MAXW];
    const float *h = f;
    float *o = a;
    for (int l = 0; l < nl; l++) {
        int ni = shape[l], no = shape[l + 1];
        const float *W = Ws[l];
        for (int j = 0; j < no; j++) {
            float s = Bs[l][j];
            for (int i = 0; i < ni; i++) s += W[j * ni + i] * h[i];
            o[j] = (l < nl - 1 && s < 0) ? 0 : s;
        }
        h = o, o = (o == a) ? b : a;
    }
    for (int j = 0; j < 4; j++) out[j] = h[j];
}

static wsp_out_t hands(const float *act) {
    float sp = act[3] < 0 ? 0 : act[3] > 40 ? 40 : act[3];
    return (wsp_out_t){(uint8_t) jstx_for(act[0]), (uint8_t) jsty_for(act[1]), act[2] > 0.5f, (uint8_t) (sp + 0.5f), act[0], act[1]};
}

static double unif(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17; return ((s_rng >> 11) + 0.5) / 9007199254740992.0; }
static double gauss(void) { return sqrt(-2 * log(unif())) * cos(2 * M_PI * unif()); }

static float s_act[4];
static uint32_t s_last = 0xFFFFFFFFu;
void wsp_episode_reset(void) { s_last = 0xFFFFFFFFu; }
float wsp_speed_sample(void) { return s_act[3]; }

wsp_out_t wsp_pilot_at(const wsp_in_t *in, uint32_t frame, int *decided, float f[WSP_NF]) {
    load();
    wsp_features(in, f);
    if (s_last != 0xFFFFFFFFu && frame >= s_last && frame - s_last < (uint32_t) s_hold) { *decided = 0; return hands(s_act); }
    float m[4], r[4];
    forward(s_nl, s_shape, s_W, s_B, f, m);
    // (the written outputs clamped to what the hands can do, so a learned correction is never lost in the clip)
    m[0] = m[0] > 31 ? 31 : m[0] < -31 ? -31 : m[0];
    m[1] = m[1] > 8 ? 8 : m[1] < -8 ? -8 : m[1];
    m[3] = m[3] > 40 ? 40 : m[3] < 0 ? 0 : m[3];
    if (s_rnl) { forward(s_rnl, s_rshape, s_RW, s_RB, f, r); for (int j = 0; j < 4; j++) m[j] += r[j]; }
    s_act[0] = (float) (m[0] + s_sa * gauss());
    s_act[1] = (float) (m[1] + s_sb * gauss());
    s_act[2] = s_sf > 0 ? (unif() < 1 / (1 + exp(-8 * (m[2] - 0.5))) ? 1.0f : 0.0f) : (m[2] > 0.5f ? 1.0f : 0.0f);
    s_act[3] = (float) (m[3] + s_ss * gauss());
    s_last = frame, *decided = 1;
    return hands(s_act);
}

wsp_out_t wsp_pilot(const wsp_in_t *in) {
    float f[WSP_NF];
    int d;
    s_last = 0xFFFFFFFFu;
    return wsp_pilot_at(in, 0, &d, f);
}

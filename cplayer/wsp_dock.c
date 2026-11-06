// The docking network: hyperspace exit to docked as weights (wsp/elite_wsp/dock_author.py), compiled in sparse
// (dock_weights.h); WSPAPPW=file loads another set. Once a frame: from the senses (wsp_dock_features: the sightlines
// to the planet, the slot, the marks on its axis, the field heading, the escape heading; the station as drawn; our
// state; the bits carried from the last frame) the roll and pitch rates, the speed, the jump, the final and the
// commit. The senses do the trigonometry, as the dogfight network's do; the network every decision.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wsp_dock.h"
#include "dock_weights.h"
#include "dock_opt_weights.h"      // Optimal mode's set (wsp/MODES.md: the jump asked for sooner); Curious flies fair mode's

#define MAXL 8
#define MAXW 256
typedef struct { int nl, shape[MAXL + 1]; const int *I[MAXL]; const float *V[MAXL], *B[MAXL]; int N[MAXL]; } net_t;
static net_t s_net;
static double s_G[5];       // the senses' geometry: lead, chi, k_min, k_side, c_side (dock_features.SENSES)

static void load(void) {
    static int done;
    if (done) return;
    done = 1;
    net_t *n = &s_net;
    // the mode's own set: Optimal's (the same network with its own numbers), else fair mode's (Curious flies it too)
    extern int g_fair, g_curious;
    const int *SHAPE = DOCK_SHAPE, *const *I = DOCK_I, *N = DOCK_N; const float *const *V = DOCK_V, *const *B = DOCK_B; const double *S = DOCK_SENSES; int NL = DOCK_LAYERS;
    if (!g_fair && !g_curious) { SHAPE = DOCKO_SHAPE, I = DOCKO_I, N = DOCKO_N, V = DOCKO_V, B = DOCKO_B, S = DOCKO_SENSES, NL = DOCKO_LAYERS; }
    n->nl = NL;
    for (int i = 0; i <= n->nl; i++) {
        n->shape[i] = SHAPE[i];
#if !APP_LOADED     // (the Presto app has no exit: its compiled-in weights fit, as this build's do)
        if (n->shape[i] > MAXW) { fprintf(stderr, "docking network: layer %d has %d units, MAXW %d\n", i, n->shape[i], MAXW); exit(1); }
#endif
    }
    for (int i = 0; i < n->nl; i++) n->I[i] = I[i], n->V[i] = V[i], n->B[i] = B[i], n->N[i] = N[i];
    for (int i = 0; i < 5; i++) s_G[i] = S[i];
#if !APP_LOADED
    const char *path = getenv("WSPAPPW");
    if (path && *path) {
        FILE *f = fopen(path, "r"); int nl;
        if (!f || fscanf(f, "WSPD %d", &nl) != 1 || nl < 1 || nl > MAXL) { fprintf(stderr, "%s: not a docking weights file\n", path); exit(1); }
        if (fscanf(f, " SENSES %lf %lf %lf %lf %lf", &s_G[0], &s_G[1], &s_G[2], &s_G[3], &s_G[4]) != 5) { fprintf(stderr, "%s: no SENSES line\n", path); exit(1); }
        n->nl = nl;
        for (int l = 0; l < nl; l++) {
            int r, c, nz;
            if (fscanf(f, "%d %d %d", &r, &c, &nz) != 3 || r > MAXW || c > MAXW) { fprintf(stderr, "docking weights: bad layer\n"); exit(1); }
            if (l == 0) n->shape[0] = c;
            n->shape[l + 1] = r;
            int *I = malloc(sizeof(int) * (nz + 1)); float *V = malloc(sizeof(float) * (nz + 1)), *B = malloc(sizeof(float) * r);
            for (int i = 0; i < nz; i++) if (fscanf(f, "%d %f", &I[i], &V[i]) != 2) { fprintf(stderr, "docking weights: short\n"); exit(1); }
            for (int i = 0; i < r; i++) if (fscanf(f, "%f", &B[i]) != 1) { fprintf(stderr, "docking weights: short\n"); exit(1); }
            n->I[l] = I, n->V[l] = V, n->B[l] = B, n->N[l] = nz;
        }
        fclose(f);
        if (n->shape[0] != WSPD_N || n->shape[nl] != 7) { fprintf(stderr, "%s: %d inputs and %d outputs, want %d and 7\n", path, n->shape[0], n->shape[nl], WSPD_N); exit(1); }
    }
#endif
}

static void forward(const net_t *n, const float *in, float *out) {
    static float a[MAXW], b[MAXW];
    float *x = a, *y = b;
    memcpy(x, in, sizeof(float) * n->shape[0]);
    for (int l = 0; l < n->nl; l++) {
        int r = n->shape[l + 1], c = n->shape[l];
        for (int o = 0; o < r; o++) y[o] = n->B[l][o];
        const int *I = n->I[l]; const float *V = n->V[l];
        for (int k = 0; k < n->N[l]; k++) y[I[k] / c] += V[k] * x[I[k] % c];
        if (l < n->nl - 1) for (int o = 0; o < r; o++) if (y[o] < 0) y[o] = 0;
        float *t = x; x = y; y = t;
    }
    memcpy(out, x, sizeof(float) * n->shape[n->nl]);
}

// ---- the senses (dock_features.py, in the same order)
#define U 4096.0
static double vlen3(const double *v) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static double vdot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void unit3(float *f, const double *v) { double n = vlen3(v); for (int i = 0; i < 3; i++) f[i] = n > 1e-9 ? (float) (v[i] / n) : 0.0f; }
enum { SLOT = 160, INSIDE = 200 };

void wsp_dock_features(const wsp_dock_in_t *in, int jsty, float f[WSPD_N]) {
    memset(f, 0, sizeof(float) * WSPD_N);
    unit3(f, in->P); f[3] = (float) (in->altitude / 255.0); f[4] = (float) (in->own / 40.0); f[5] = in->station ? 1.0f : 0.0f;
    if (in->station) {
        const double *S = in->S, *nv = in->nv, *rv = in->rv;
        double lead = s_G[0], chi = s_G[1], k_min = s_G[2], k_side = s_G[3], c_side = s_G[4];
        double rel[3] = {-S[0], -S[1], -S[2]};
        double along = vdot3(rel, nv);
        double lv[3] = {rel[0] - along * nv[0], rel[1] - along * nv[1], rel[2] - along * nv[2]};
        double lat = vlen3(lv), w[3] = {0, 0, 0};
        if (lat > 1e-9) for (int i = 0; i < 3; i++) w[i] = lv[i] / lat;
        double dist = vlen3(rel), slot[3], mark[3];
        double so = along > INSIDE ? SLOT : 0.0;
        for (int i = 0; i < 3; i++) slot[i] = S[i] + so * nv[i];
        double ahead = fmax(lead, lat / tan(chi * M_PI / 180)), k = fmax(k_min, along - ahead);
        unit3(f + 6, slot);
        for (int i = 0; i < 3; i++) mark[i] = S[i] + k * nv[i]; unit3(f + 9, mark);
        for (int i = 0; i < 3; i++) mark[i] = S[i] + k_side * nv[i] + c_side * w[i]; unit3(f + 12, mark);
        double z = fmax(1.0, slot[2]);
        f[19] = (float) (along / U); f[20] = (float) (lat / U); f[21] = (float) fmin(dist / U, 16.0);
        f[22] = (float) fmax(-0.5, fmin(0.5, slot[0] / z)); f[23] = (float) fmax(-0.5, fmin(0.5, slot[1] / z));
        f[24] = (float) (fmod(atan2(rv[1], rv[0]) + M_PI / 2 + 4 * M_PI, M_PI) - M_PI / 2);
        f[25] = in->spin >= 0 ? 1.0f : -1.0f;
        f[26] = (float) (atan2(hypot(nv[0], nv[1]), -nv[2]) / M_PI);
        f[27] = (float) (atan2(hypot(S[0], S[1]), S[2]) / M_PI);
        f[28] = (float) (atan2(hypot(slot[0], slot[1]), slot[2]) / M_PI);
    }
    if (in->col) {
        double m = hypot(in->miss[0], in->miss[1]), mx = m > 1 ? in->miss[0] / m : 0, my = m > 1 ? in->miss[1] / m : 1;
        double e[3] = {mx * 4000, my * 4000, 1500};
        unit3(f + 15, e); f[18] = 1.0f;
    }
    f[29] = in->clear ? 1.0f : 0.0f; f[30] = (float) (in->fsh / 255.0); f[31] = (float) (in->ash / 255.0);
    f[32] = (float) ((jsty - 128) / 128.0);
    f[33] = in->up ? 1.0f : 0.0f; f[34] = in->final ? 1.0f : 0.0f; f[35] = in->commit ? 1.0f : 0.0f;
    f[36] = 1.0f;
    static const int V0[5] = {0, 6, 9, 12, 15};
    for (int k = 0; k < 5; k++) {
        double h = hypot(f[V0[k]], f[V0[k] + 1]);
        f[37 + 2 * k] = h > 1e-6 ? (float) (f[V0[k]] / h) : 0.0f; f[38 + 2 * k] = h > 1e-6 ? (float) (f[V0[k] + 1] / h) : 1.0f;
    }
}

wsp_dock_out_t wsp_dock_at(const wsp_dock_in_t *in, int jsty, float f[WSPD_N]) {
    load();
    wsp_dock_features(in, jsty, f);
    float o[7];
    forward(&s_net, f, o);
    wsp_dock_out_t r;
    if (in->station) { double so = -in->S[2] * 0 + (vdot3((double[]){-in->S[0], -in->S[1], -in->S[2]}, in->nv) > INSIDE ? SLOT : 0.0); for (int i = 0; i < 3; i++) r.goal[i] = in->S[i] + so * in->nv[i]; }
    else for (int i = 0; i < 3; i++) r.goal[i] = in->P[i];
    r.alpha = o[0]; r.beta = o[1];
    r.speed = (int) (o[2] + 0.5f); if (r.speed < 0) r.speed = 0; if (r.speed > 40) r.speed = 40;
    r.jump = o[3] > 0.5f; r.up = o[4] > 0.5f; r.final = o[5] > 0.5f; r.commit = o[6] > 0.5f;
    return r;
}

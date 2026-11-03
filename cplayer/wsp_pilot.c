// The authored pilot's forward pass: 13 features in, the stick, fire and speed out.
// About 1.4k multiply-adds a frame; well inside the 2.4 ms the C player spends.
#include <stdint.h>
#include "pilot_weights.h"

#include "wsp_pilot.h"

static void wsp_forward(const float *in, float *out)
{
    float a[WSP_MAXW], b[WSP_MAXW];
    const float *h = in; float *o = a;
    for (int l = 0; l < WSP_LAYERS; l++) {
        int ni = WSP_SHAPE[l], no = WSP_SHAPE[l + 1];
        const float *W = WSP_W[l];
        for (int j = 0; j < no; j++) {
            float s = WSP_B[l][j];
            for (int i = 0; i < ni; i++) s += W[j * ni + i] * h[i];
            o[j] = (l < WSP_LAYERS - 1 && s < 0) ? 0 : s;
        }
        h = o; o = (o == a) ? b : a;
    }
    for (int j = 0; j < WSP_SHAPE[WSP_LAYERS]; j++) out[j] = h[j];
}

static uint8_t clamp_stick(float v) { return v < 1 ? 1 : v > 255 ? 255 : (uint8_t)(v + 0.5f); }

// Features as elite_wsp/features.py lays them out. The caller decodes the K% block (ships.c does).
wsp_out_t wsp_pilot(int32_t tx, int32_t ty, int32_t tz, const float nosev[3], int tspeed,
                    int own_speed, int energy, int fsh, int laser_temp, float hit_radius, int seen)
{
    float f[WSP_IN] = {0};
    f[11] = 1.0f;
    f[6] = own_speed / 64.0f; f[7] = energy / 255.0f; f[8] = fsh / 255.0f; f[9] = laser_temp / 255.0f;
    if (seen) {
        float sp = 1.5f * tspeed;
        int32_t p[3] = {tx, ty, tz};
        for (int i = 0; i < 3; i++) {
            float v = p[i] / 4096.0f; f[i] = v < -8 ? -8 : v > 8 ? 8 : v;
            f[3 + i] = nosev[i] * sp / 64.0f;
        }
        f[10] = hit_radius / 256.0f; f[12] = 1.0f;
    }
    float out[4];
    wsp_forward(f, out);
    return (wsp_out_t){clamp_stick(out[0]), clamp_stick(out[1]), out[2] > 0.5f, (uint8_t)(out[3] + 0.5f)};
}

#if 0
#include <stdio.h>
int main(void) {   // the same ship as tests/test_pilot.py's fire cylinder
    float n[3] = {0, 0, 1};
    wsp_out_t o = wsp_pilot(100, -300, 5000, n, 20, 30, 200, 200, 0, 80, 1);
    printf("%d %d %d %d\n", o.jstx, o.jsty, o.fire, o.speed);
    return 0;
}
#endif

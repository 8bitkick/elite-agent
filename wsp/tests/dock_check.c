// The docking network's C senses and forward pass on inputs from stdin, for tests/test_dock.py: a line an input
// (P alt station S nv rv spin own fsh ash clear col miss jsty up final commit), the senses and the outputs out.
#include <stdio.h>
#include "../../cplayer/wsp_dock.h"
int main(void) {
    wsp_dock_in_t in; int jsty; float f[WSPD_N];
    while (scanf("%lf %lf %lf %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf %d %d %d %d %d %d %lf %lf %lf %d %d %d %d",
                 &in.P[0], &in.P[1], &in.P[2], &in.altitude, &in.station, &in.S[0], &in.S[1], &in.S[2], &in.nv[0], &in.nv[1], &in.nv[2],
                 &in.rv[0], &in.rv[1], &in.rv[2], &in.spin, &in.own, &in.fsh, &in.ash, &in.clear, &in.col, &in.miss[0], &in.miss[1], &in.miss[2],
                 &jsty, &in.up, &in.final, &in.commit) == 27) {
        wsp_dock_out_t o = wsp_dock_at(&in, jsty, f);
        for (int i = 0; i < WSPD_N; i++) printf("%.9g ", f[i]);
        printf("| %.6f %.6f %d %d %d %d %d\n", o.alpha, o.beta, o.speed, o.jump, o.up, o.final, o.commit);
    }
    return 0;
}

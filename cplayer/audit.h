// The fair-mode leak audit (docs/FAIR.md §6), for run_sim and the lab built with -DREAD_AUDIT: every RAM read
// the player makes through R()/R16() while it's playing (g_audit_on), by address and call site, written out at the
// end (AUDIT lines). Reads straight from IO->ram (ships_read in fair.c's perception, the commander's name) are
// listed in FAIR.md by hand. Also SCRAMBLE=1: the player is handed a copy of RAM with the bytes it must not use
// overwritten with random values each frame (ships' AI, energy, NEWB but the scooped bit; ships' speed, roll and
// pitch counters; SCRAMBLE=2 also the tally's low byte and fraction): its play must not change.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "elite_ram.h"
#ifdef READ_AUDIT
int g_audit_on;
static struct { uint32_t n; char site[200]; } s_audit[0x8000];
void read_audit(uint16_t a, const char *file, int line) {
    if (!g_audit_on || a >= 0x8000) return;
    char one[40];
    const char *b = strrchr(file, '/');
    snprintf(one, sizeof one, " %s:%d", b ? b + 1 : file, line);
    if (!strstr(s_audit[a].site, one) && strlen(s_audit[a].site) + strlen(one) < sizeof s_audit[a].site) strcat(s_audit[a].site, one);
    s_audit[a].n++;
}
static void audit_dump(void) {
    for (int a = 0; a < 0x8000; a++) if (s_audit[a].n) fprintf(stderr, "AUDIT %04X %u%s\n", a, s_audit[a].n, s_audit[a].site);
}
#else
static int g_audit_on;
static void audit_dump(void) {}
#endif
static uint8_t s_scram[0x8000];
static uint32_t s_scram_rng = 777;
// a copy of RAM with the hidden bytes scrambled (level 0: none: returns ram itself)
static const uint8_t *scramble(const uint8_t *ram, int level) {
    if (!level) return ram;
    memcpy(s_scram, ram, sizeof s_scram);
    for (int i = 0; i < 12 && ram[A_FRIN + i]; i++) {
        if (ram[A_FRIN + i] >= 128 || ram[A_FRIN + i] == 2) continue;      // (not the planet, sun or station)
        uint8_t *b = s_scram + A_K_PC + g_ev.ni * i;
        int k[] = {27, 28, 29, 30, 32, 35};
        for (int j = 0; j < 6; j++) { s_scram_rng = s_scram_rng * 1103515245u + 12345u; b[k[j]] = (uint8_t) (s_scram_rng >> 16); }
        s_scram_rng = s_scram_rng * 1103515245u + 12345u;
        b[36] = (uint8_t) ((b[36] & 0x80) | ((s_scram_rng >> 16) & 0x7F));
    }
    if (level >= 2) {
        s_scram_rng = s_scram_rng * 1103515245u + 12345u;
        s_scram[A_TALLY] = (uint8_t) (s_scram_rng >> 16);       // TALLY's low byte
        if (A_TALLYL) s_scram[A_TALLYL] = (uint8_t) (s_scram_rng >> 8);        // TALLYL
    }
    return s_scram;
}

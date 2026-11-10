// A 6502 Second Processor on the Tube, for Kieran Connell's Master core: the Tube ULA (after jsbeeb's tube.js, Matt
// Godbolt's jsbeeb, GPL-3.0) and a 65C02 parasite with a flat 64K, the core's own cpu6502.c compiled a second time
// under other names. The host reaches the ULA at &FEE0-&FEFF (kc-beeb-tube.patch puts it in SHEILA); the
// parasite at &FEF8-&FEFF. The machine runs the two in short slices, the parasite at twice the host's clock
// (4 MHz, as the Master's internal 65C102): machine_run() in the patched machine.c calls tube_par_run().
#include <stdint.h>
#include <string.h>
#include "cpu6502.h"

uint8_t tube_host_irq;          // the host's IRQ from the ULA (R4 data with Q set)
int tube_on;                    // a parasite is attached

static struct cpu6502 P;
static uint8_t pmem[0x10000];
static uint8_t prom[0x800];     // the client ROM at &F800 while booting
static uint8_t *p_rd[256], *p_wr[256];
static int rom_paged, nmi_level;

// ---- the ULA
enum { R1, R2, R3, R4 };
#define F_AVAIL 0x80
#define F_NOTFULL 0x40
#define S_Q 0x01
#define S_I 0x02
#define S_J 0x04
#define S_M 0x08
#define S_V 0x10
#define S_P 0x20
#define S_T 0x40
#define S_S 0x80
static uint8_t isr, hst[4], pst[4];
static uint8_t p2h1[24], p2h[4][2], h2p[4][2];
static int n_p2h1, n_p2h3, n_h2p3;

static void par_reset(void);

static void ints(struct cpu6502 *c)
{
    tube_host_irq = (hst[R4] & F_AVAIL) && (isr & S_Q);
    int irq = ((pst[R1] & F_AVAIL) && (isr & S_I)) || ((pst[R4] & F_AVAIL) && (isr & S_J));
    int r3 = (isr & S_V) ? 2 : 1;
    int nmi = (isr & S_M) && (n_h2p3 >= r3 || n_p2h3 == 0);
    if ((uint32_t) irq != c->irq_line) {
        // (the history brought up to now on the old level first: cpu6502.c's cpu_hist_update)
        uint32_t n = c->cyc - c->last_sync, fill = c->irq_line ? 0xffffffffu : 0;
        c->irq_hist = n >= 16 ? fill : ((c->irq_hist << n) | (fill & ((1u << n) - 1)));
        c->last_sync = c->cyc;
        c->irq_line = (uint32_t) irq;
    }
    if (nmi && !nmi_level) { c->nmi_pending = 1; c->nmi_cycle = c->cyc; }
    nmi_level = nmi;
}

static void ula_reset(int hard)
{
    if (hard) isr = 0;
    for (int i = 0; i < 4; i++) hst[i] = pst[i] = F_NOTFULL;
    // (register 3 holds one insignificant byte from the parasite, so that there is no NMI straight after a reset)
    hst[R3] |= F_AVAIL; p2h[R3][0] = 0;
    n_p2h1 = 0; n_p2h3 = 1; n_h2p3 = 0;
}

uint8_t tube_host_read(uint16_t a)
{
    uint8_t v = 0xfe;
    if (!tube_on) return v;
    switch (a & 7) {
    case 0: v = (hst[R1] & (F_AVAIL | F_NOTFULL)) | (isr & ~(F_AVAIL | F_NOTFULL)); break;
    case 1:
        v = p2h1[0];
        if (hst[R1] & F_AVAIL) {
            memmove(p2h1, p2h1 + 1, 23);
            pst[R1] |= F_NOTFULL;
            if (--n_p2h1 == 0) hst[R1] &= ~F_AVAIL;
        }
        break;
    case 2: v = hst[R2]; break;
    case 3: v = p2h[R2][0]; pst[R2] |= F_NOTFULL; hst[R2] &= ~F_AVAIL; break;
    case 4: v = hst[R3]; break;
    case 5:
        v = p2h[R3][0];
        if (hst[R3] & F_AVAIL) {
            p2h[R3][0] = p2h[R3][1];
            pst[R3] |= F_NOTFULL;
            if (--n_p2h3 == 0) hst[R3] &= ~F_AVAIL;
        }
        break;
    case 6: v = hst[R4]; break;
    case 7: v = p2h[R4][0]; pst[R4] |= F_NOTFULL; hst[R4] &= ~F_AVAIL; break;
    }
    ints(&P);
    return v;
}

void tube_host_write(uint16_t a, uint8_t v)
{
    if (!tube_on) return;
    switch (a & 7) {
    case 0:
        if (v & S_S) isr |= v & ~(F_AVAIL | F_NOTFULL);
        else isr &= ~(v & ~(F_AVAIL | F_NOTFULL));
        if (v & S_T) ula_reset(0);
        if ((v & S_P) && (isr & S_P)) par_reset();
        break;
    case 1: if (hst[R1] & F_NOTFULL) { h2p[R1][0] = v; pst[R1] |= F_AVAIL; hst[R1] &= ~F_NOTFULL; } break;
    case 3: if (hst[R2] & F_NOTFULL) { h2p[R2][0] = v; pst[R2] |= F_AVAIL; hst[R2] &= ~F_NOTFULL; } break;
    case 5:
        if (hst[R3] & F_NOTFULL) {
            if (isr & S_V) {
                if (n_h2p3 < 2) h2p[R3][n_h2p3++] = v;
                if (n_h2p3 == 2) { pst[R3] |= F_AVAIL; hst[R3] &= ~F_NOTFULL; }
            } else {
                h2p[R3][0] = v; n_h2p3 = 1; pst[R3] |= F_AVAIL; hst[R3] &= ~F_NOTFULL;
            }
        }
        break;
    case 7: if (hst[R4] & F_NOTFULL) { h2p[R4][0] = v; pst[R4] |= F_AVAIL; hst[R4] &= ~F_NOTFULL; } break;
    }
    ints(&P);
}

static uint8_t par_read(struct cpu6502 *c, uint16_t a)
{
    uint8_t v = 0;
    switch (a & 7) {
    case 0: v = pst[R1]; break;
    case 1: v = h2p[R1][0]; hst[R1] |= F_NOTFULL; pst[R1] &= ~F_AVAIL; break;
    case 2: v = pst[R2]; break;
    case 3: v = h2p[R2][0]; hst[R2] |= F_NOTFULL; pst[R2] &= ~F_AVAIL; break;
    case 4: v = pst[R3]; break;
    case 5:
        v = h2p[R3][0];
        if (pst[R3] & F_AVAIL) {
            h2p[R3][0] = h2p[R3][1];
            if (--n_h2p3 == 0) { hst[R3] |= F_NOTFULL; pst[R3] &= ~F_AVAIL; }
        }
        break;
    case 6: v = pst[R4]; break;
    case 7: v = h2p[R4][0]; hst[R4] |= F_NOTFULL; pst[R4] &= ~F_AVAIL; break;
    }
    ints(c);
    return v;
}

static void par_write(struct cpu6502 *c, uint16_t a, uint8_t v)
{
    switch (a & 7) {
    case 1:
        if (pst[R1] & F_NOTFULL) {
            p2h1[n_p2h1++] = v; hst[R1] |= F_AVAIL;
            if (n_p2h1 == 24) pst[R1] &= ~F_NOTFULL;
        }
        break;
    case 3: if (pst[R2] & F_NOTFULL) { p2h[R2][0] = v; hst[R2] |= F_AVAIL; pst[R2] &= ~F_NOTFULL; } break;
    case 5:
        if (pst[R3] & F_NOTFULL) {
            if (isr & S_V) {
                if (n_p2h3 < 2) p2h[R3][n_p2h3++] = v;
                if (n_p2h3 == 2) { hst[R3] |= F_AVAIL; pst[R3] &= ~F_NOTFULL; }
            } else {
                p2h[R3][0] = v; n_p2h3 = 1; hst[R3] |= F_AVAIL; pst[R3] &= ~F_NOTFULL;
            }
        }
        break;
    case 7: if (pst[R4] & F_NOTFULL) { p2h[R4][0] = v; hst[R4] |= F_AVAIL; pst[R4] &= ~F_NOTFULL; } break;
    }
    ints(c);
}

// ---- the parasite: page &FE goes through the I/O hooks (the ULA at &FEF8-&FEFF, RAM elsewhere), &F800 up reads the
// ROM until the first read of &FEF8
static void map(void)
{
    for (int i = 0; i < 256; i++) p_rd[i] = p_wr[i] = pmem + i * 256;
    if (rom_paged) for (int i = 0xf8; i < 0x100; i++) p_rd[i] = prom + (i - 0xf8) * 256;
    p_rd[0xfe] = p_wr[0xfe] = NULL;
}

// the agent's view of the game's zero page: Solid Elite's frame layer (F_FRAME) borrows &06-&27 and &D2-&E1 while it
// draws, keeping the game's in f_zsv; between hook_on and hook_off the game's copy is there
uint16_t tube_hook_on, tube_hook_off;
int tube_zp_held;
#define PAR_HOOK(c) do { if ((c)->pc == tube_hook_on) tube_zp_held = 1; else if ((c)->pc == tube_hook_off) tube_zp_held = 0; } while (0)

static uint8_t par_io_rd(struct cpu6502 *c, uint16_t a);
static void par_io_wr(struct cpu6502 *c, uint16_t a, uint8_t v);
#define CPU6502_FN(name) par_##name
#define CPU6502_CMOS_ONLY 1
#define CPU6502_IO_RD(c, a) par_io_rd(c, a)
#define CPU6502_IO_WR(c, a, v) par_io_wr(c, a, v)
#define CPU6502_TRACE(c) PAR_HOOK(c)
#include "cpu6502.c"

static uint8_t par_io_rd(struct cpu6502 *c, uint16_t a)
{
    c->cyc++;
    if ((a & 0xfff8) == 0xfef8) {
        if ((a & 7) == 0 && rom_paged) {
            rom_paged = 0; map();
            c->rd_tab = c->rd_tabs[0];
        }
        return par_read(c, a);
    }
    return rom_paged && a >= 0xf800 ? prom[a - 0xf800] : pmem[a];
}

static void par_io_wr(struct cpu6502 *c, uint16_t a, uint8_t v)
{
    c->cyc++;
    if ((a & 0xfff8) == 0xfef8) par_write(c, a, v);
    else pmem[a] = v;
}

static void par_reset(void)
{
    rom_paged = 1; map();
    par_cpu6502_reset(&P);
    P.wlo = 1; P.whi = 0;            // (no write needs a catch-up)
    nmi_level = 0;
    ula_reset(1);                    // (as jsbeeb: the parasite's reset clears the ULA, its flags too)
    ints(&P);
}

void tube_init(const uint8_t *rom2k)
{
    memcpy(prom, rom2k, sizeof prom);
    memset(pmem, 0, sizeof pmem);
    P.cmos = 1;
    P.rd_tabs[0] = P.rd_tabs[1] = p_rd;
    P.wr_tabs[0] = P.wr_tabs[1] = p_wr;
    P.cycles = 0; P.cyc = 0;
    tube_on = 1;
    par_reset();
}

// the parasite up to `host_cycles` of the host's 2 MHz clock
void tube_par_run(uint64_t host_cycles)
{
    uint64_t until = host_cycles * 2;
    if (P.cycles >= until) return;
    P.sync_due = P.cyc + 0x40000000u;
    par_cpu6502_run(&P, until);
}

uint8_t *tube_mem(void) { return pmem; }

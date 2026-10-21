// The Elite versions the player knows (elite_ver.h, docs/DUAL.md): their addresses, from Mark Moxon's
// assembled sources (the Master's: elite-source-code-bbc-master, flicker-free branch; the disc
// version's: elite-source-code-bbc-micro-disc, flicker-free branch, its docked and flight code agreeing
// on every address here; Solid Elite's: its own assembled listing), and how each is recognised in RAM.
#include <string.h>
#include "elite_ver.h"

elite_ver_t g_ev;

static const elite_ver_t MASTER = {
    .id = EV_MASTER, .name = "BBC Master Elite (flicker-free SNG47)",
    .A_ALTIT = 0x1221, .A_ASH = 0x00D6, .A_AVL = 0x1269, .A_BOMB = 0x125E, .A_BST = 0x125D, .A_CABTMP = 0x0E74,
    .A_CASH = 0x123D, .A_COMC = 0x2C40, .A_CRGO = 0x124A, .A_DAMP = 0x2C56, .A_DELTA = 0x0075, .A_DKCMP = 0x1260,
    .A_ECM = 0x125C, .A_ECMA = 0x0046, .A_ENERGY = 0x00D7, .A_ENGY = 0x125F, .A_ESCP = 0x1262, .A_FIST = 0x1268,
    .A_FRIN = 0x0E41, .A_FSH = 0x00D5, .A_GCNT = 0x1243, .A_GHYP = 0x1261, .A_GNTMP = 0x0E79, .A_JSTX = 0x00C1,
    .A_JSTY = 0x00C2, .A_JUNK = 0x0E70, .A_K_PC = 0x0400, .A_LASCT = 0x0E78, .A_LASER = 0x1244, .A_LASX = 0x121E,
    .A_MANY = 0x0E4E, .A_MCNT = 0x0084, .A_MJ = 0x0E73, .A_MSAR = 0x0E76, .A_MSTG = 0x005B, .A_NA_PC = 0x3468,
    .A_NAME = 0x122C, .A_NOMSL = 0x1267, .A_PATG = 0x2C58, .A_QQ0 = 0x1235, .A_QQ1 = 0x1236, .A_QQ10 = 0x00E2,
    .A_QQ11 = 0x0081, .A_QQ12 = 0x0087, .A_QQ14 = 0x1241, .A_QQ20 = 0x124B, .A_QQ22 = 0x0044, .A_QQ24 = 0x1292,
    .A_QQ25 = 0x1293, .A_QQ26 = 0x127A, .A_QQ28 = 0x1294, .A_QQ29 = 0x1295, .A_QQ8 = 0x00DF, .A_QQ9 = 0x00E1,
    .A_SSPR = 0x0E50, .A_SVC = 0x127D, .A_TALLY = 0x127B, .A_TALLYL = 0x1266, .A_TP = 0x1234, .A_VIEW = 0x0E77,
    .A_de = 0x0E7D, .A_distaway = 0x1229, .A_tek = 0x1297,
    .A_JSTK = 0x2C5C, .A_JSTE = 0x2C5B, .A_JSTGY = 0x2C5A, .A_DJD = 0x2C57, .A_ECMP = 0x0E72, .A_BETA = 0x0042,
    .A_XC = 0x0010, .A_YC = 0x0012, .A_RAND = 0x0002, .A_TYPE = 0x0085,
    .ni = 37, .save_cmp = 73, .title_type = {0, 0}, .kills_by_type = 1, .joy_fire_key = 1,
};

// Eben Upton's Solid Elite (github.com/ebenupton/solid-elite-bbc-master): the Master's game logic, iteration for
// iteration, laid out over sideways RAM, HAZEL and shadow RAM for a filled renderer. Its workspace WP is the
// Master's &0E41-&12A9 moved down by &600 (each byte keeps its place in its page); the variables in the main
// code (NA%, and the configuration from COMC) moved with the code; zero page and K% are where they were.
// Addresses from its assembled listing (3-assembled-output/compile.txt).
static const elite_ver_t SOLID = {
    .id = EV_SOLID, .name = "BBC Master Solid Elite (Eben Upton's port of SNG47)",
    .A_ALTIT = 0x0C21, .A_ASH = 0x00D6, .A_AVL = 0x0C69, .A_BOMB = 0x0C5E, .A_BST = 0x0C5D, .A_CABTMP = 0x0874,
    .A_CASH = 0x0C3D, .A_COMC = 0x2800, .A_CRGO = 0x0C4A, .A_DAMP = 0x2816, .A_DELTA = 0x0075, .A_DKCMP = 0x0C60,
    .A_ECM = 0x0C5C, .A_ECMA = 0x0046, .A_ENERGY = 0x00D7, .A_ENGY = 0x0C5F, .A_ESCP = 0x0C62, .A_FIST = 0x0C68,
    .A_FRIN = 0x0841, .A_FSH = 0x00D5, .A_GCNT = 0x0C43, .A_GHYP = 0x0C61, .A_GNTMP = 0x0879, .A_JSTX = 0x00C1,
    .A_JSTY = 0x00C2, .A_JUNK = 0x0870, .A_K_PC = 0x0400, .A_LASCT = 0x0878, .A_LASER = 0x0C44, .A_LASX = 0x0C1E,
    .A_MANY = 0x084E, .A_MCNT = 0x0084, .A_MJ = 0x0873, .A_MSAR = 0x0876, .A_MSTG = 0x005B, .A_NA_PC = 0x347A,
    .A_NAME = 0x0C2C, .A_NOMSL = 0x0C67, .A_PATG = 0x2818, .A_QQ0 = 0x0C35, .A_QQ1 = 0x0C36, .A_QQ10 = 0x00E2,
    .A_QQ11 = 0x0081, .A_QQ12 = 0x0087, .A_QQ14 = 0x0C41, .A_QQ20 = 0x0C4B, .A_QQ22 = 0x0044, .A_QQ24 = 0x0C92,
    .A_QQ25 = 0x0C93, .A_QQ26 = 0x0C7A, .A_QQ28 = 0x0C94, .A_QQ29 = 0x0C95, .A_QQ8 = 0x00DF, .A_QQ9 = 0x00E1,
    .A_SSPR = 0x0850, .A_SVC = 0x0C7D, .A_TALLY = 0x0C7B, .A_TALLYL = 0x0C66, .A_TP = 0x0C34, .A_VIEW = 0x0877,
    .A_de = 0x087D, .A_distaway = 0x0C29, .A_tek = 0x0C97,
    .A_JSTK = 0x281C, .A_JSTE = 0x281B, .A_JSTGY = 0x281A, .A_DJD = 0x2817, .A_ECMP = 0x0872, .A_BETA = 0x0042,
    .A_XC = 0x0010, .A_YC = 0x0012, .A_RAND = 0x0002, .A_TYPE = 0x0085,
    .ni = 37, .save_cmp = 73, .title_type = {0, 0}, .kills_by_type = 1, .joy_fire_key = 1,
};

// (the disc version keeps its workspace in zero page, page 3 and K% at &0900; it loads its docked code
// (T.CODE) and flight code (D.CODE) over each other at &11E3, and none of these addresses is in them)
static const elite_ver_t DISC = {
    .id = EV_BBC_DISC, .name = "BBC Micro disc Elite (flicker-free)",
    .A_ALTIT = 0x0FD1, .A_ASH = 0x03A6, .A_AVL = 0x038D, .A_BOMB = 0x0382, .A_BST = 0x0381, .A_CABTMP = 0x0342,
    .A_CASH = 0x0361, .A_COMC = 0x03C3, .A_CRGO = 0x036E, .A_DAMP = 0x03C5, .A_DELTA = 0x007D, .A_DKCMP = 0x0384,
    .A_ECM = 0x0380, .A_ECMA = 0x0030, .A_ENERGY = 0x03A7, .A_ENGY = 0x0383, .A_ESCP = 0x0386, .A_FIST = 0x038C,
    .A_FRIN = 0x0311, .A_FSH = 0x03A5, .A_GCNT = 0x0367, .A_GHYP = 0x0385, .A_GNTMP = 0x0347, .A_JSTX = 0x034C,
    .A_JSTY = 0x034D, .A_JUNK = 0x033E, .A_K_PC = 0x0900, .A_LASCT = 0x0346, .A_LASER = 0x0368, .A_LASX = 0x0FCE,
    .A_MANY = 0x031E, .A_MCNT = 0x008A, .A_MJ = 0x0341, .A_MSAR = 0x0344, .A_MSTG = 0x0045, .A_NA_PC = 0x1181,
    .A_NAME = 0x0350, .A_NOMSL = 0x038B, .A_PATG = 0x03C7, .A_QQ0 = 0x0359, .A_QQ1 = 0x035A, .A_QQ10 = 0x03C0,
    .A_QQ11 = 0x0087, .A_QQ12 = 0x008E, .A_QQ14 = 0x0365, .A_QQ20 = 0x036F, .A_QQ22 = 0x002E, .A_QQ24 = 0x03AA,
    .A_QQ25 = 0x03AB, .A_QQ26 = 0x039E, .A_QQ28 = 0x03AC, .A_QQ29 = 0x0004, .A_QQ8 = 0x03BD, .A_QQ9 = 0x03BF,
    .A_SSPR = 0x0320, .A_SVC = 0x03A1, .A_TALLY = 0x039F, .A_TALLYL = 0, .A_TP = 0x0358, .A_VIEW = 0x0345,
    .A_de = 0x034B, .A_distaway = 0, .A_tek = 0x03AE,
    .A_JSTK = 0x03CB, .A_JSTE = 0x03CA, .A_JSTGY = 0x03C9, .A_DJD = 0x03C6, .A_ECMP = 0x0340, .A_BETA = 0x002A,
    .A_XC = 0x002C, .A_YC = 0x002D, .A_RAND = 0x0000, .A_TYPE = 0x008C,
    .ni = 37, .save_cmp = 73, .title_type = {11, 19}, .kills_by_type = 0, .joy_fire_key = 0,
};

// Recognising them: FNV-1a hashes of 256-byte stretches of code (and the disc version's text tokens)
// that nothing writes to, from Moxon's assembled binaries, checked on the machines
static uint32_t fnv(const uint8_t *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static int is_master(const uint8_t *ram)
{
    // BCODE (loaded at &1300) at &4000 and &5000
    return fnv(ram + 0x4000, 256) == 0x1A133E45u && fnv(ram + 0x5000, 256) == 0x9C47B9DDu;
}
static int is_solid(const uint8_t *ram)
{
    // BCODE (loaded at &1EDC): the log table at &2500 and the main code at &3000 (SNG47's &4000 is
    // Solid Elite's screen)
    return fnv(ram + 0x2500, 256) == 0x98D3C278u && fnv(ram + 0x3000, 256) == 0xEE89DF85u;
}
static int is_disc(const uint8_t *ram)
{
    // WORDS (the text tokens, at &0400), and whichever of the docked code (T.CODE at &11E3: &3000)
    // or the flight code (D.CODE at &11E3: &2000) is loaded
    return fnv(ram + 0x0400, 256) == 0x15E4FC6Du &&
           (fnv(ram + 0x3000, 256) == 0xFC80B4CAu || fnv(ram + 0x2000, 256) == 0x3000A91Cu);
}

int elite_detect(int model, const uint8_t *ram)
{
    if (g_ev.id != EV_NONE) return g_ev.id;
    if (!ram) return EV_NONE;
    if ((model == EV_MODEL_MASTER_128 || model == EV_MODEL_UNKNOWN) && is_master(ram)) g_ev = MASTER;
    else if ((model == EV_MODEL_MASTER_128 || model == EV_MODEL_UNKNOWN) && is_solid(ram)) g_ev = SOLID;
    else if ((model == EV_MODEL_BBC_B || model == EV_MODEL_UNKNOWN) && is_disc(ram)) g_ev = DISC;
    return g_ev.id;
}

int g_zp_held;
uint8_t g_zp_keep[50];
int g_view_latch = -1;
void elite_forget(void) { memset(&g_ev, 0, sizeof g_ev); g_zp_held = 0; g_view_latch = -1; }

void elite_zp_frame(const uint8_t *ram)
{
    static uint8_t view, since;
    if (g_ev.id != EV_SOLID) { g_zp_held = 0; g_view_latch = -1; return; }
    // (the briefings, BRIEF, PAUSE and PAS1, draw their Constrictor as one of the renderer's frames, with QQ11 0
    // while it's recorded and drawn and the view put back after (its LL9B): caught then, the player saw no
    // briefing and waited for the status screen. So a 0 within 75 frames of a 13 or a 1, with the briefing's
    // Constrictor in slot 0 (FRIN 31), reads as that view. (The view is put back once a game loop: seen in about
    // one frame in ten. Without the Constrictor it caught other screens, and a reload looped 1,190 times)
    int q = ram[g_ev.A_QQ11];
    if ((q == 13 || q == 1) && ram[g_ev.A_FRIN] == 31) view = q, since = 0;
    else if (q == 0 && view && ram[g_ev.A_FRIN] == 31 && ++since <= 75) ;
    else view = 0;
    g_view_latch = q == 0 && view ? view : -1;
    // (&F4 always matches ROMSEL: the frame layer is bank 6, the rasteriser bank 5 or &85 (ANDY over 5);
    // the game is bank 4 or the cold area's 7. Bank 6 also prints text, with zero page intact: a frame's staleness)
    int bank = ram[0xF4];
    g_zp_held = bank == 5 || bank == 6 || bank == 0x85;
    if (!g_zp_held) memcpy(g_zp_keep, ram + 0x06, 34), memcpy(g_zp_keep + 34, ram + 0xD2, 16);
}
void elite_assume_master(void) { g_ev = MASTER; }

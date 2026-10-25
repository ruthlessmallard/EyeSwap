#include "dig.h"
#include <string.h>

#define DG_W 60
#define DG_H 60
#define DG_MAX_DEPTH 40
#define DG_CRACK_MAX 1000
#define DG_MAX_VEINS 96
#define DG_MAX_CRACK_CELLS 384
#define DG_X0 15
#define DG_X1 44
#define DG_CEIL_TOP 5
#define DG_CEIL_BOT 8
#define DG_FEET 39
#define DG_MINER_TOP 31
#define DG_COLLAPSE_MIN_DEPTH 8

struct DgState {
    uint32_t masterSeed;
    uint32_t rng;
    uint32_t layoutSeed;
    uint32_t runIndex;
    uint32_t dustSeed;
    int state;
    int depth;
    int haul;
    int banked;
    int crack;
    int crackAcc;
    int idleMs;
    int climbMs;
    int climbDur;
    int crushMs;
    int bankMs;
    int swingMs;
    int sparkleMs;
    int swingFrame;
    int banner;
    int violations;
    int lastCrackDrawn;
    int veinN;
    uint8_t veinDepth[DG_MAX_VEINS];
    uint8_t veinX[DG_MAX_VEINS];
    uint8_t veinW[DG_MAX_VEINS];
    uint8_t veinRich[DG_MAX_VEINS];
    int crackN;
    uint8_t crackX[DG_MAX_CRACK_CELLS];
    uint8_t crackY[DG_MAX_CRACK_CELLS];
    uint8_t grid[DG_W * DG_H];
};

static DgState gs;
static int g_sx = 0;
static int g_sy = 0;
static int g_noRender = 0;
static uint8_t g_crackTaken[DG_W * DG_H];

static const uint8_t FONT5X3[37][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6},
    {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2},
    {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5},
    {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5},
    {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2},
    {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
    {0, 0, 0, 0, 0}
};

static uint32_t hash32(uint32_t a) {
    a ^= a >> 16;
    a *= 0x7feb352du;
    a ^= a >> 15;
    a *= 0x846ca68bu;
    a ^= a >> 16;
    return a;
}

static uint32_t dgRng(uint32_t *s) {
    uint32_t x = *s;
    if (x == 0u) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int fontIndex(char c) {
    unsigned char u = (unsigned char)c;
    if (u >= '0' && u <= '9') return (int)(u - '0');
    if (u >= 'A' && u <= 'Z') return 10 + (int)(u - 'A');
    return 36;
}

static int textLen(const char *s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

static void putRaw(int x, int y, uint8_t c) {
    if (x >= 0 && x < DG_W && y >= 0 && y < DG_H) {
        gs.grid[y * DG_W + x] = c;
    }
}

static void put(int x, int y, uint8_t c) {
    putRaw(x + g_sx, y + g_sy, c);
}

static void drawTextRaw(int x, int y, const char *s, uint8_t col) {
    int cx = x;
    while (*s != 0) {
        const uint8_t *g = FONT5X3[fontIndex(*s)];
        int r;
        for (r = 0; r < 5; r++) {
            uint8_t bits = g[r];
            if (bits & 4) putRaw(cx, y + r, col);
            if (bits & 2) putRaw(cx + 1, y + r, col);
            if (bits & 1) putRaw(cx + 2, y + r, col);
        }
        cx += 4;
        s++;
    }
}

static int appendNum(char *b, int v) {
    char t[12];
    int k = 0;
    int i;
    if (v < 0) {
        b[0] = '-';
        return 1 + appendNum(b + 1, -v);
    }
    if (v == 0) {
        b[0] = '0';
        return 1;
    }
    while (v > 0 && k < 12) {
        t[k++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = 0; i < k; i++) b[i] = t[k - 1 - i];
    return k;
}

static int crackDrawn(void) {
    if (gs.crackN <= 0) return 0;
    return (gs.crack * gs.crackN) / DG_CRACK_MAX;
}

static void checkCrackMono(void) {
    int c = crackDrawn();
    if (c < gs.lastCrackDrawn) gs.violations++;
    gs.lastCrackDrawn = c;
}

static uint8_t rockAt(int x, int y) {
    uint32_t h = hash32((uint32_t)(x * 374761393) ^
                        (uint32_t)((y + gs.depth * 5) * 668265263) ^
                        (gs.layoutSeed * 2246822519u));
    int v = (int)(h % 100u);
    if (v < 55) return 11;
    if (v < 85) return 12;
    return 13;
}

static void crackAdd(int x, int y) {
    int idx;
    if (x < 3 || x > 56 || y < DG_CEIL_TOP || y > 46) return;
    idx = y * DG_W + x;
    if (g_crackTaken[idx] != 0) return;
    if (gs.crackN >= DG_MAX_CRACK_CELLS) return;
    g_crackTaken[idx] = 1;
    gs.crackX[gs.crackN] = (uint8_t)x;
    gs.crackY[gs.crackN] = (uint8_t)y;
    gs.crackN++;
}

static void genLayout(void) {
    uint32_t lr = hash32(gs.layoutSeed ^ 0x51ED270Bu);
    int d, i;
    gs.veinN = 0;
    for (d = 1; d <= DG_MAX_DEPTH && gs.veinN < DG_MAX_VEINS - 2; d++) {
        int cnt = 1 + (int)(dgRng(&lr) % 2u);
        for (i = 0; i < cnt && gs.veinN < DG_MAX_VEINS; i++) {
            int side = (int)(dgRng(&lr) % 2u);
            int x;
            uint32_t richRoll = dgRng(&lr) % 5u;
            if (side != 0) {
                x = (DG_X0 + 1) + (int)(dgRng(&lr) % 7u);
            } else {
                x = (DG_X1 - 1) - (int)(dgRng(&lr) % 7u);
            }
            gs.veinDepth[gs.veinN] = (uint8_t)d;
            gs.veinX[gs.veinN] = (uint8_t)x;
            gs.veinW[gs.veinN] = (uint8_t)(1 + (dgRng(&lr) % 2u));
            gs.veinRich[gs.veinN] = (uint8_t)((d >= 18 || richRoll == 0u) ? 1 : 0);
            gs.veinN++;
        }
    }

    memset(g_crackTaken, 0, sizeof(g_crackTaken));
    gs.crackN = 0;
    {
        int origins = 1 + (int)(dgRng(&lr) % 3u);
        for (i = 0; i < origins; i++) {
            int ox = (DG_X0 - 7) + (int)(dgRng(&lr) % 44u);
            int oy = DG_CEIL_TOP + (int)(dgRng(&lr) % 4u);
            crackAdd(ox, oy);
        }
    }
    {
        int guard = 0;
        while (gs.crackN < DG_MAX_CRACK_CELLS && guard < 40000) {
            uint32_t r;
            int idx;
            int x, y, dx, dy;
            guard++;
            if (gs.crackN <= 0) break;
            idx = (int)(dgRng(&lr) % (uint32_t)gs.crackN);
            x = (int)gs.crackX[idx];
            y = (int)gs.crackY[idx];
            r = dgRng(&lr) % 100u;
            dx = 0;
            dy = 1;
            if (r < 30u) {
                dx = -1; dy = 1;
            } else if (r < 60u) {
                dx = 1; dy = 1;
            } else if (r < 75u) {
                dx = -1; dy = 0;
            } else if (r < 90u) {
                dx = 1; dy = 0;
            } else if (r >= 96u) {
                dx = 0; dy = 2;
            }
            crackAdd(x + dx, y + dy);
        }
    }
}

static void drawMiner(int cx, int topY, int lampOn) {
    static const uint8_t widths[6] = {7, 9, 11, 11, 11, 11};
    int r, i;
    for (r = 0; r < 6; r++) {
        int w = (int)widths[r];
        for (i = 0; i < w; i++) put(cx - w / 2 + i, topY + r, 1);
    }
    for (i = 0; i < 15; i++) put(cx - 7 + i, topY + 6, 1);
    put(cx - 3, topY + 7, 1);
    put(cx + 3, topY + 7, 1);
    put(cx - 4, topY + 8, 1);
    put(cx - 3, topY + 8, 1);
    put(cx + 3, topY + 8, 1);
    put(cx + 4, topY + 8, 1);
    if (lampOn) {
        put(cx - 1, topY + 3, 3);
        put(cx, topY + 3, 3);
        put(cx - 1, topY + 4, 3);
        put(cx, topY + 4, 3);
    }
}

static void drawPick(int cx, int topY) {
    int phase = ((200 - gs.swingMs) / 80) & 1;
    if (phase == 0) {
        put(cx + 8, topY + 5, 1);
        put(cx + 8, topY + 4, 1);
        put(cx + 9, topY + 3, 1);
        put(cx + 9, topY + 2, 1);
        put(cx + 10, topY + 2, 1);
    } else {
        put(cx + 9, topY + 4, 1);
        put(cx + 9, topY + 3, 1);
        put(cx + 10, topY + 2, 1);
        put(cx + 10, topY + 1, 1);
        put(cx + 11, topY + 1, 1);
    }
}

static void drawSurface(void) {
    int x, y;
    for (y = DG_CEIL_TOP; y < DG_H; y++) {
        for (x = 0; x < DG_W; x++) {
            if (y >= 40) put(x, y, rockAt(x, y));
            else put(x, y, 0);
        }
    }
    put(48, 11, 3);
    put(47, 11, 3);
    put(49, 11, 3);
    put(48, 10, 3);
    put(48, 12, 3);
}

static void drawShaft(void) {
    int x, y, i;
    for (y = DG_CEIL_TOP; y < DG_H; y++) {
        for (x = 0; x < DG_W; x++) {
            if (x >= DG_X0 && x <= DG_X1 && y > DG_CEIL_BOT) put(x, y, 0);
            else put(x, y, rockAt(x, y));
        }
    }
    for (y = DG_CEIL_BOT + 1; y < DG_H; y++) {
        put(DG_X0 - 1, y, 11);
        put(DG_X1 + 1, y, 11);
    }
    for (i = 0; i < gs.veinN; i++) {
        int sy = DG_FEET + ((int)gs.veinDepth[i] - gs.depth) * 5;
        int vx = (int)gs.veinX[i];
        uint8_t c = gs.veinRich[i] ? 14 : 2;
        if (sy < DG_CEIL_BOT + 1 || sy >= DG_H) continue;
        put(vx, sy, c);
        if (gs.veinW[i] > 1) put(vx + 1, sy, c);
        if (vx < DG_X1) put(vx, sy - 1, 2);
    }
    {
        int nc = crackDrawn();
        for (i = 0; i < nc; i++) put((int)gs.crackX[i], (int)gs.crackY[i], 4);
    }
}

static void drawBanner(void) {
    const char *s;
    int w, x, y, xx, yy;
    if (gs.banner == 1) s = "TINY HAUL";
    else if (gs.banner == 2) s = "DECENT";
    else s = "BIG HAUL";
    w = textLen(s) * 4 - 1;
    x = (DG_W - w) / 2;
    if (x < 1) x = 1;
    y = 25;
    for (yy = y - 2; yy <= y + 6; yy++) {
        for (xx = x - 2; xx <= x + w + 1; xx++) putRaw(xx, yy, 11);
    }
    for (xx = x - 2; xx <= x + w + 1; xx++) {
        putRaw(xx, y - 2, 13);
        putRaw(xx, y + 6, 13);
    }
    drawTextRaw(x, y, s, 15);
}

static void drawHUD(void) {
    char buf[48];
    int n = 0;
    buf[n++] = 'D';
    n += appendNum(&buf[n], gs.depth);
    buf[n++] = ' ';
    buf[n++] = 'H';
    n += appendNum(&buf[n], gs.haul);
    if (gs.state == DG_SURFACE || gs.state == DG_BANK) {
        buf[n++] = ' ';
        buf[n++] = 'B';
        n += appendNum(&buf[n], gs.banked);
    }
    buf[n] = 0;
    drawTextRaw(0, 0, buf, 15);
}

static void dgRender(void) {
    if (g_noRender) return;
    memset(gs.grid, 0, sizeof(gs.grid));
    g_sx = 0;
    g_sy = 0;
    if (gs.state == DG_CRUSH) {
        g_sx = ((gs.crushMs / 45) % 2) ? 1 : -1;
        g_sy = (int)((gs.crushMs / 65) % 3) - 1;
    }
    if (gs.state == DG_SURFACE || gs.state == DG_BANK) {
        drawSurface();
        drawMiner(30, DG_MINER_TOP, 1);
    } else {
        drawShaft();
        if (gs.state == DG_DIG) {
            int cx = 30;
            if (gs.swingMs > 0) {
                int ty = DG_MINER_TOP;
                int best = -1;
                int bd = 99;
                int i;
                cx -= 2;
                drawMiner(cx, ty, 1);
                drawPick(cx, ty);
                if (gs.sparkleMs > 0) {
                    for (i = 0; i < gs.veinN; i++) {
                        int dd = (int)gs.veinDepth[i] - gs.depth;
                        if (dd < 0) dd = -dd;
                        if (dd < bd) {
                            bd = dd;
                            best = i;
                        }
                    }
                    if (best >= 0 && bd <= 2) {
                        int sy = DG_FEET + ((int)gs.veinDepth[best] - gs.depth) * 5;
                        int vx = (int)gs.veinX[best];
                        put(vx, sy, 3);
                        put(vx, sy - 1, 3);
                        put(vx + 1, sy, 3);
                    }
                }
            } else {
                drawMiner(30, DG_MINER_TOP, 1);
            }
        } else if (gs.state == DG_CLIMB) {
            int prog = (gs.climbDur > 0) ? (gs.climbMs * 22) / gs.climbDur : 22;
            int bob = (gs.climbMs / 100) % 2;
            int ty = DG_MINER_TOP - prog - bob;
            if (ty < 6) ty = 6;
            drawMiner(30, ty, 1);
        } else if (gs.state == DG_CRUSH) {
            int count, i;
            drawMiner(30, DG_MINER_TOP, 0);
            count = gs.crushMs * 2;
            if (count > 2400) count = 2400;
            for (i = 0; i < count; i++) {
                uint32_t h = hash32(gs.dustSeed + (uint32_t)i * 2654435761u +
                                    (uint32_t)(gs.crushMs / 25));
                int dx = DG_X0 + (int)(h % (uint32_t)(DG_X1 - DG_X0 + 1));
                int dy = DG_CEIL_BOT + 1 +
                         (int)((h >> 9) % (uint32_t)(DG_H - DG_CEIL_BOT - 1));
                put(dx, dy, 5);
            }
        }
    }
    drawHUD();
    if (gs.state == DG_BANK) drawBanner();
}

static void startRun(void) {
    gs.runIndex++;
    gs.layoutSeed = hash32(gs.masterSeed ^ (gs.runIndex * 0x9E3779B9u));
    gs.rng = gs.layoutSeed ^ 0x85EBCA6Bu;
    if (gs.rng == 0u) gs.rng = 1u;
    gs.dustSeed = gs.layoutSeed ^ 0xA5A5A5A5u;
    gs.depth = 0;
    gs.haul = 0;
    gs.crack = 0;
    gs.crackAcc = 0;
    gs.idleMs = 0;
    gs.climbMs = 0;
    gs.climbDur = 0;
    gs.crushMs = 0;
    gs.bankMs = 0;
    gs.swingMs = 0;
    gs.sparkleMs = 0;
    gs.banner = 0;
    gs.lastCrackDrawn = 0;
    genLayout();
    gs.state = DG_DIG;
}

static void doSwing(void) {
    int value;
    int add;
    if (gs.state != DG_DIG) return;
    if (gs.depth < DG_MAX_DEPTH) gs.depth++;
    value = 1 + gs.depth / 2 + (int)(dgRng(&gs.rng) % 3u);
    gs.haul += value;
    add = 60 + (int)(dgRng(&gs.rng) % 81u);
    gs.crack += add;
    if (gs.crack > DG_CRACK_MAX) gs.crack = DG_CRACK_MAX;
    gs.idleMs = 0;
    gs.swingMs = 200;
    gs.swingFrame ^= 1;
    gs.sparkleMs = 140;
    checkCrackMono();
}

static void startClimb(void) {
    gs.state = DG_CLIMB;
    gs.climbMs = 0;
    gs.climbDur = 600 + 20 * gs.depth;
    if (gs.climbDur > 1600) gs.climbDur = 1600;
    gs.swingMs = 0;
    gs.sparkleMs = 0;
}

static void enterCrush(void) {
    gs.state = DG_CRUSH;
    gs.crushMs = 0;
    gs.idleMs = 0;
    gs.climbMs = 0;
    gs.swingMs = 0;
    gs.sparkleMs = 0;
    gs.dustSeed = gs.layoutSeed ^ 0xC001D00Du;
}

static void softSurface(void) {
    gs.state = DG_SURFACE;
    gs.depth = 0;
    gs.haul = 0;
    gs.crack = 0;
    gs.crackAcc = 0;
    gs.idleMs = 0;
    gs.climbMs = 0;
    gs.climbDur = 0;
    gs.crushMs = 0;
    gs.bankMs = 0;
    gs.swingMs = 0;
    gs.sparkleMs = 0;
    gs.banner = 0;
    gs.lastCrackDrawn = 0;
}

static void doBank(void) {
    int h = gs.haul;
    if (h < 10) gs.banner = 1;
    else if (h < 30) gs.banner = 2;
    else gs.banner = 3;
    gs.banked += h;
    gs.haul = 0;
    gs.depth = 0;
    gs.crack = 0;
    gs.crackAcc = 0;
    gs.climbMs = 0;
    gs.lastCrackDrawn = 0;
    gs.state = DG_BANK;
    gs.bankMs = 0;
}

static void crackRise(uint32_t dtMs) {
    int64_t add;
    if (dtMs == 0u) return;
    add = (int64_t)(200 + gs.depth * 15) * (int64_t)dtMs + (int64_t)gs.crackAcc;
    gs.crack += (int)(add / 1000);
    gs.crackAcc = (int)(add % 1000);
    if (gs.crack > DG_CRACK_MAX) gs.crack = DG_CRACK_MAX;
    checkCrackMono();
}

static int collapseReady(void) {
    if (gs.crack < DG_CRACK_MAX) return 0;
    if (gs.state == DG_CLIMB) return 1;
    return gs.depth >= DG_COLLAPSE_MIN_DEPTH;
}

void dgInit(uint32_t seed) {
    memset(&gs, 0, sizeof(gs));
    gs.masterSeed = seed;
    gs.layoutSeed = hash32(seed ^ 0xA5A5A5A5u);
    gs.rng = (seed != 0u) ? seed : 0x9E3779B9u;
    gs.dustSeed = seed;
    gs.state = DG_SURFACE;
    gs.banner = 0;
    g_sx = 0;
    g_sy = 0;
    dgRender();
}

void dgTick(uint32_t dtMs) {
    int dt = (dtMs > 1000000u) ? 1000000 : (int)dtMs;
    int haulBefore = gs.haul;

    if (gs.swingMs > 0) {
        gs.swingMs -= dt;
        if (gs.swingMs < 0) gs.swingMs = 0;
    }
    if (gs.sparkleMs > 0) {
        gs.sparkleMs -= dt;
        if (gs.sparkleMs < 0) gs.sparkleMs = 0;
    }

    switch (gs.state) {
        case DG_SURFACE:
            break;
        case DG_DIG:
            crackRise((uint32_t)dt);
            gs.idleMs += dt;
            if (collapseReady()) {
                enterCrush();
            } else if (gs.idleMs >= 1000) {
                startClimb();
            }
            break;
        case DG_CLIMB:
            crackRise((uint32_t)dt);
            gs.climbMs += dt;
            if (collapseReady()) {
                enterCrush();
            } else if (gs.climbMs >= gs.climbDur) {
                doBank();
            }
            break;
        case DG_CRUSH:
            gs.crushMs += dt;
            if (gs.crushMs >= 1400) softSurface();
            break;
        case DG_BANK:
            gs.bankMs += dt;
            if (gs.bankMs >= 1600) {
                gs.state = DG_SURFACE;
                gs.depth = 0;
                gs.crack = 0;
                gs.crackAcc = 0;
                gs.banner = 0;
                gs.lastCrackDrawn = 0;
            }
            break;
        default:
            softSurface();
            break;
    }

    if (gs.haul > haulBefore) gs.violations++;
    if (gs.depth < 0 || gs.depth > DG_MAX_DEPTH) {
        gs.violations++;
        if (gs.depth < 0) gs.depth = 0;
        else gs.depth = DG_MAX_DEPTH;
    }
    dgRender();
}

void dgInteract(void) {
    if (gs.state == DG_SURFACE) {
        startRun();
        doSwing();
    } else if (gs.state == DG_DIG) {
        doSwing();
    }
    dgRender();
}

uint8_t dgCell(int x, int y) {
    if (x < 0 || x >= DG_W || y < 0 || y >= DG_H) return 0;
    return gs.grid[y * DG_W + x];
}

void dgStats(int *depth, int *haul, int *banked, int *crack, int *state, int *violations) {
    if (depth != 0) *depth = gs.depth;
    if (haul != 0) *haul = gs.haul;
    if (banked != 0) *banked = gs.banked;
    if (crack != 0) *crack = gs.crack;
    if (state != 0) *state = gs.state;
    if (violations != 0) *violations = gs.violations;
}

static uint32_t probeHash(uint32_t seed) {
    uint32_t h = 2166136261u;
    int i;
    dgInit(seed);
    for (i = 0; i < 220; i++) {
        if (i == 0 || i == 18 || i == 36 || i == 54 || i == 72 || i == 90 || i == 108) {
            dgInteract();
        }
        dgTick(16);
        h = (h ^ (uint32_t)(gs.depth + gs.haul * 3 + gs.crack * 7 +
                            gs.state * 11 + gs.banked * 13)) * 16777619u;
        if ((i % 20) == 0) {
            int k;
            g_noRender = 0;
            dgRender();
            g_noRender = 1;
            for (k = 0; k < DG_W * DG_H; k += 17) {
                h = (h ^ (uint32_t)gs.grid[k]) * 16777619u;
            }
        }
    }
    return h;
}

int dgSelfCheck(void) {
    static DgState backup;
    int fail = 0;
    int i;
    int b0;
    int reached;
    int bankedSaved;
    int last;
    int mono;
    int hb;
    int cb;
    uint32_t h1;
    uint32_t h2;

    memcpy(&backup, &gs, sizeof(gs));
    g_noRender = 1;

    for (i = 0; i < DG_W * DG_H; i++) {
        uint8_t v = gs.grid[i];
        if (!(v == 0 || v == 1 || v == 2 || v == 3 || v == 4 || v == 5 ||
              v == 11 || v == 12 || v == 13 || v == 14 || v == 15)) {
            fail |= 1;
            break;
        }
    }
    if (dgCell(-1, 0) != 0u || dgCell(0, -1) != 0u ||
        dgCell(DG_W, 0) != 0u || dgCell(0, DG_H) != 0u) {
        fail |= 1;
    }

    dgInit(0x12345u);
    for (i = 0; i < 300; i++) dgTick(16);
    if (gs.depth != 0 || gs.haul != 0 || gs.state != DG_SURFACE) fail |= 2;

    dgInteract();
    if (gs.depth != 1 || gs.state != DG_DIG || gs.haul < 1 || gs.haul > 3) fail |= 4;
    dgTick(999);
    if (gs.state != DG_DIG || gs.depth != 1) fail |= 4;
    dgTick(1);
    if (gs.state != DG_CLIMB) fail |= 4;

    b0 = gs.banked;
    reached = 0;
    for (i = 0; i < 500; i++) {
        dgTick(16);
        if (gs.state == DG_BANK) {
            reached = 1;
            break;
        }
    }
    if (!reached) fail |= 4;
    if (gs.banner != 1) fail |= 4;
    if (!(gs.banked > b0 && (gs.banked - b0) < 10)) fail |= 4;
    for (i = 0; i < 300 && gs.state != DG_SURFACE; i++) dgTick(16);
    if (gs.state != DG_SURFACE) fail |= 4;
    bankedSaved = gs.banked;

    dgInteract();
    last = 0;
    mono = 1;
    for (i = 0; i < 200 && gs.state != DG_CRUSH; i++) {
        int cc;
        dgInteract();
        dgTick(30);
        cc = (gs.crack * gs.crackN) / DG_CRACK_MAX;
        if (cc < last) mono = 0;
        last = cc;
    }
    if (gs.state != DG_CRUSH) fail |= 8;
    if (!mono) fail |= 32;
    if (gs.banked != bankedSaved) fail |= 8;
    dgTick(1400);
    if (gs.state != DG_SURFACE || gs.haul != 0 || gs.banked != bankedSaved ||
        gs.depth != 0) {
        fail |= 8;
    }
    dgInteract();
    if (gs.state != DG_DIG || gs.depth != 1 || gs.haul < 1) fail |= 8;

    hb = gs.haul;
    cb = gs.crack;
    dgTick(300);
    if (gs.haul != hb) fail |= 128;
    if (gs.crack <= cb) fail |= 32;
    if (gs.state != DG_DIG) fail |= 4;

    dgInit(0xBEEFu);
    dgInteract();
    gs.depth = 39;
    gs.crack = 0;
    gs.crackAcc = 0;
    gs.haul = 0;
    doSwing();
    doSwing();
    if (gs.depth != DG_MAX_DEPTH) fail |= 256;

    h1 = probeHash(0x31415926u);
    h2 = probeHash(0x31415926u);
    if (h1 != h2) fail |= 16;

    memcpy(&gs, &backup, sizeof(gs));
    g_noRender = 0;
    dgRender();
    return fail;
}

#include "slab.h"
#include <stdint.h>
#include <string.h>

#define GW 60
#define GH 60
#define MAX_VEINS 192
#define MAX_CRACK 480

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t rich;
} Vein;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint16_t at;
} Crack;

static uint8_t g_grid[GW * GH];
static int g_state;
static int g_depth;
static int g_haul;
static int g_banked;
static int g_crack;
static int g_crackAcc;
static int g_ai;
static int g_violations;
static uint32_t g_now;
static uint32_t g_lastScoop;
static uint32_t g_sinceInput;
static uint32_t g_stateTimer;
static uint32_t g_backoutDur;
static uint32_t g_banner;
static uint32_t g_armTimer;
static uint32_t g_flashTimer;
static uint32_t g_aiSwingTimer;
static uint32_t g_aiNextSwing;
static uint32_t g_aiThreshold;
static uint32_t g_aiStopped;
static uint32_t g_aiRoundTimer;
static uint32_t g_rngStone;
static uint32_t g_rngPlay;
static uint32_t g_rngAi;
static int g_crackCountPrev;
static int g_shake;
static int g_pen;
static int g_initialized;
static int g_scoopFlag;
static int g_bankFlag;
static int g_resetFlag;
static Vein g_veins[MAX_VEINS];
static int g_nVeins;
static Crack g_cracks[MAX_CRACK];
static int g_nCracks;

static uint32_t rngNext(uint32_t *s)
{
    uint32_t x = *s;
    if (x == 0u) {
        x = 0x1234567u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void put(int x, int y, uint8_t v)
{
    x += g_shake;
    if (x < 0 || x >= GW || y < 0 || y >= GH) {
        return;
    }
    g_grid[y * GW + x] = v;
}

static int faceX(void)
{
    return 22 + (g_depth * 20) / 40;
}

static uint32_t hashAt(int x, int y, uint32_t salt)
{
    uint32_t h = (uint32_t)(x + 11) * 0x9E3779B1u;
    h ^= (uint32_t)(y + 7) * 0x85EBCA77u;
    h ^= salt * 0xC2B2AE3Du;
    return mix32(h);
}

static int inBore(int x, int y)
{
    int f = faceX();
    if (y < 17 || y > 39) {
        return 0;
    }
    if (x < 0 || x > f) {
        return 0;
    }
    if (x >= f - 1) {
        uint32_t h = hashAt(x, y, (uint32_t)g_depth + 1u);
        if ((h % 100u) < 28u) {
            return 0;
        }
    }
    return 1;
}

static uint8_t rockCell(int x, int y)
{
    uint32_t h = hashAt(x / 3, y / 2, (uint32_t)g_depth + 1u);
    uint32_t p = h % 10u;
    if (p == 0u) {
        return 13;
    }
    if (p <= 3u) {
        return 12;
    }
    return 11;
}

static void addVein(int x, int y, int rich)
{
    if (g_nVeins >= MAX_VEINS) {
        return;
    }
    if (x < 1 || x > GW - 2 || y < 6 || y > GH - 3) {
        return;
    }
    g_veins[g_nVeins].x = (uint8_t)x;
    g_veins[g_nVeins].y = (uint8_t)y;
    g_veins[g_nVeins].rich = (uint8_t)(rich ? 1 : 0);
    g_nVeins++;
}

static void generateVeins(void)
{
    int i;
    int seams;
    g_nVeins = 0;
    seams = 10 + (int)(rngNext(&g_rngStone) % 6u);
    for (i = 0; i < seams; i++) {
        int x;
        int y;
        int rich;
        int len;
        int j;
        int dx;
        int dy;
        if (i < 3) {
            x = 3 + (int)(rngNext(&g_rngStone) % 14u);
            if (rngNext(&g_rngStone) & 1u) {
                y = 6 + (int)(rngNext(&g_rngStone) % 8u);
            } else {
                y = 41 + (int)(rngNext(&g_rngStone) % 12u);
            }
        } else {
            x = 25 + (int)(rngNext(&g_rngStone) % 32u);
            y = 6 + (int)(rngNext(&g_rngStone) % 50u);
        }
        rich = (x >= 38) ? 1 : 0;
        switch (rngNext(&g_rngStone) % 4u) {
        case 0:
            dx = 1;
            dy = 0;
            break;
        case 1:
            dx = 0;
            dy = 1;
            break;
        case 2:
            dx = 1;
            dy = 1;
            break;
        default:
            dx = -1;
            dy = 1;
            break;
        }
        len = 4 + (int)(rngNext(&g_rngStone) % 4u);
        for (j = 0; j < len; j++) {
            addVein(x, y, rich);
            if ((rngNext(&g_rngStone) % 100u) < 30u) {
                addVein(x + ((rngNext(&g_rngStone) & 1u) ? 1 : -1), y, rich);
            }
            x += dx;
            y += dy;
            if ((rngNext(&g_rngStone) % 100u) < 25u) {
                x += (int)(rngNext(&g_rngStone) % 3u) - 1;
                y += (int)(rngNext(&g_rngStone) % 3u) - 1;
            }
            if (x < 2) {
                x = 2;
            }
            if (x > GW - 3) {
                x = GW - 3;
            }
            if (y < 6) {
                y = 6;
            }
            if (y > GH - 4) {
                y = GH - 4;
            }
        }
    }
}

static int crackExists(int x, int y)
{
    int i;
    for (i = 0; i < g_nCracks; i++) {
        if ((int)g_cracks[i].x == x && (int)g_cracks[i].y == y) {
            return 1;
        }
    }
    return 0;
}

static void crackAdd(int x, int y, uint16_t at)
{
    if (g_nCracks >= MAX_CRACK) {
        return;
    }
    if (x < 1 || x > GW - 2 || y < 6 || y > 14) {
        return;
    }
    if (crackExists(x, y)) {
        return;
    }
    g_cracks[g_nCracks].x = (uint8_t)x;
    g_cracks[g_nCracks].y = (uint8_t)y;
    g_cracks[g_nCracks].at = at;
    g_nCracks++;
}

static void generateCracks(void)
{
    int origins;
    int o;
    g_nCracks = 0;
    origins = 1 + (int)(rngNext(&g_rngStone) % 3u);
    for (o = 0; o < origins; o++) {
        int ox = 5 + (int)(rngNext(&g_rngStone) % 50u);
        int oy = 6 + (int)(rngNext(&g_rngStone) % 4u);
        int b;
        crackAdd(ox, oy, 0);
        for (b = 0; b < 3; b++) {
            int x = ox;
            int y = oy;
            int len = 16 + (int)(rngNext(&g_rngStone) % 8u);
            int i;
            for (i = 0; i < len; i++) {
                int d = (int)(rngNext(&g_rngStone) % 100u);
                if (d < 52) {
                    y++;
                } else if (d < 72) {
                    if (b == 0) {
                        x--;
                    } else if (b == 1) {
                        x++;
                    } else {
                        x += (rngNext(&g_rngStone) & 1u) ? 1 : -1;
                    }
                } else if (d < 88) {
                    y++;
                    x += (rngNext(&g_rngStone) & 1u) ? 1 : -1;
                } else {
                    x += (rngNext(&g_rngStone) & 1u) ? 1 : -1;
                }
                if (y > 14) {
                    y = 14;
                    x += (rngNext(&g_rngStone) & 1u) ? 1 : -1;
                }
                if (y < 6) {
                    y = 6;
                }
                if (x < 2) {
                    x = 2;
                }
                if (x > GW - 3) {
                    x = GW - 3;
                }
                crackAdd(x, y, 0);
            }
        }
    }
    /* Appearance order: the k-th crack cell pops in at roughly k/n of the meter. */
    {
        int i;
        int n = g_nCracks > 1 ? g_nCracks - 1 : 1;
        for (i = 0; i < g_nCracks; i++) {
            uint32_t at = 15u + (uint32_t)i * 965u / (uint32_t)n;
            g_cracks[i].at = (uint16_t)at;
        }
    }
}

static int countCrackCells(void)
{
    int i;
    int n = 0;
    for (i = 0; i < GW * GH; i++) {
        if (g_grid[i] == 4u) {
            n++;
        }
    }
    return n;
}

static void drawLoader(void)
{
    int ox = 10;
    uint8_t body;
    int x;
    int y;
    int ext;
    int len;
    int ax;
    int sx;
    uint8_t scoop;

    if (g_state == SB_BACKOUT && g_backoutDur > 0u) {
        int sh = (int)((g_stateTimer * 5u) / g_backoutDur);
        ox -= sh;
    } else if (g_state == SB_BANK) {
        ox -= 5;
    }

    body = (g_state == SB_CRUSH) ? 11u : 1u;

    for (y = 27; y <= 32; y++) {
        for (x = ox; x < ox + 10; x++) {
            put(x, y, body);
        }
    }
    for (y = 24; y <= 26; y++) {
        for (x = ox + 2; x <= ox + 6; x++) {
            put(x, y, body);
        }
    }
    for (x = ox + 1; x <= ox + 3; x++) {
        put(x, 33, 4u);
        put(x, 34, 4u);
    }
    for (x = ox + 6; x <= ox + 8; x++) {
        put(x, 33, 4u);
        put(x, 34, 4u);
    }
    if (g_state != SB_CRUSH) {
        put(ox + 7, 25, 3u);
    } else {
        put(ox + 7, 25, 11u);
    }

    ext = (g_armTimer > 0u) ? 1 : 0;
    len = ext ? 6 : 3;
    ax = ox + 10;
    for (x = ax; x < ax + len; x++) {
        put(x, 29, body);
        put(x, 30, body);
    }
    sx = ax + len;
    scoop = (g_flashTimer > 0u && g_state != SB_CRUSH) ? 3u : body;
    put(sx, 28, scoop);
    put(sx, 29, scoop);
}

static const uint8_t FONT3x5[37][5] = {
    {2, 5, 7, 5, 5}, /* A */
    {6, 5, 6, 5, 6}, /* B */
    {3, 4, 4, 4, 3}, /* C */
    {6, 5, 5, 5, 6}, /* D */
    {7, 4, 6, 4, 7}, /* E */
    {7, 4, 6, 4, 4}, /* F */
    {3, 4, 5, 5, 3}, /* G */
    {5, 5, 7, 5, 5}, /* H */
    {7, 2, 2, 2, 7}, /* I */
    {1, 1, 1, 5, 2}, /* J */
    {5, 5, 6, 5, 5}, /* K */
    {4, 4, 4, 4, 7}, /* L */
    {5, 7, 7, 5, 5}, /* M */
    {5, 7, 7, 7, 5}, /* N */
    {2, 5, 5, 5, 2}, /* O */
    {6, 5, 6, 4, 4}, /* P */
    {2, 5, 5, 6, 3}, /* Q */
    {6, 5, 6, 5, 5}, /* R */
    {3, 4, 2, 1, 6}, /* S */
    {7, 2, 2, 2, 2}, /* T */
    {5, 5, 5, 5, 7}, /* U */
    {5, 5, 5, 5, 2}, /* V */
    {5, 5, 7, 7, 5}, /* W */
    {5, 5, 2, 5, 5}, /* X */
    {5, 5, 2, 2, 2}, /* Y */
    {7, 1, 2, 4, 7}, /* Z */
    {7, 5, 5, 5, 7}, /* 0 */
    {2, 6, 2, 2, 7}, /* 1 */
    {6, 1, 2, 4, 7}, /* 2 */
    {6, 1, 2, 1, 6}, /* 3 */
    {5, 5, 7, 1, 1}, /* 4 */
    {7, 4, 6, 1, 6}, /* 5 */
    {3, 4, 6, 5, 2}, /* 6 */
    {7, 1, 2, 2, 2}, /* 7 */
    {2, 5, 2, 5, 2}, /* 8 */
    {2, 5, 3, 1, 6}, /* 9 */
    {0, 0, 0, 0, 0}  /* space */
};

static int fontIndex(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= '0' && c <= '9') {
        return 26 + (c - '0');
    }
    return 36;
}

static void drawChar(int x, int y, char c, uint8_t slot)
{
    int idx = fontIndex(c);
    int row;
    for (row = 0; row < 5; row++) {
        uint8_t bits = FONT3x5[idx][row];
        if (bits & 4u) {
            put(x, y + row, slot);
        }
        if (bits & 2u) {
            put(x + 1, y + row, slot);
        }
        if (bits & 1u) {
            put(x + 2, y + row, slot);
        }
    }
}

static int drawText(int x, int y, const char *s, uint8_t slot, int pitch)
{
    while (*s != '\0') {
        drawChar(x, y, *s, slot);
        x += pitch;
        s++;
    }
    return x;
}

static int drawNum(int x, int y, int v, uint8_t slot, int pitch)
{
    char b[12];
    int n = 0;
    int i;
    if (v < 0) {
        v = 0;
    }
    if (v == 0) {
        b[n++] = '0';
    } else {
        while (v > 0 && n < 11) {
            b[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    for (i = n - 1; i >= 0; i--) {
        drawChar(x, y, b[i], slot);
        x += pitch;
    }
    return x;
}

static void drawHud(void)
{
    int x = 1;
    int y = 0;
    if (g_state == SB_SURFACE) {
        if (g_ai) {
            x = drawText(x, y, "AI", 15u, 3);
            x += 3;
        }
        x = drawText(x, y, "BANK", 15u, 3);
        x += 3;
        drawNum(x, y, g_banked, 15u, 3);
        return;
    }
    if (g_ai) {
        x = drawText(x, y, "AI", 15u, 3);
        x += 3;
    }
    x = drawText(x, y, "DEPTH", 15u, 3);
    x += 3;
    x = drawNum(x, y, g_depth, 15u, 3);
    x += 3;
    x = drawText(x, y, "HAUL", 15u, 3);
    x += 3;
    drawNum(x, y, g_haul, 15u, 3);
}

static void drawBanner(void)
{
    const char *s;
    int n;
    int w;
    int x;
    int xx;
    int yy;
    if (g_banner == 0u) {
        s = "TINY HAUL";
    } else if (g_banner == 1u) {
        s = "DECENT";
    } else {
        s = "BIG HAUL";
    }
    n = (int)strlen(s);
    w = n * 4 - 1;
    x = (GW - w) / 2;
    for (yy = 24; yy <= 30; yy++) {
        for (xx = 6; xx < 54; xx++) {
            put(xx, yy, 0u);
        }
    }
    drawText(x, 25, s, 15u, 4);
}

static void render(void)
{
    int x;
    int y;
    int i;
    int f;

    g_shake = 0;
    if (g_state == SB_CRUSH) {
        g_shake = (((g_stateTimer / 70u) & 1u) != 0u) ? 1 : -1;
    }

    memset(g_grid, 0, sizeof(g_grid));
    f = faceX();

    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++) {
            if (!inBore(x, y)) {
                put(x, y, rockCell(x, y));
            }
        }
    }

    if (g_state != SB_SURFACE) {
        for (i = 0; i < g_nVeins; i++) {
            if (!inBore((int)g_veins[i].x, (int)g_veins[i].y)) {
                put((int)g_veins[i].x, (int)g_veins[i].y,
                    g_veins[i].rich ? 14u : 2u);
            }
        }
    }

    if (g_state == SB_DIG || g_state == SB_BACKOUT) {
        for (i = 0; i < g_nCracks; i++) {
            if ((int)g_cracks[i].at <= g_crack) {
                put((int)g_cracks[i].x, (int)g_cracks[i].y, 4u);
            }
        }
    }

    if (g_state == SB_CRUSH) {
        for (y = 17; y <= 39; y++) {
            for (x = 0; x <= f; x++) {
                uint32_t h = hashAt(x, y, 0x51ED270Bu);
                if ((h % 5u) != 0u) {
                    put(x, y, 5u);
                } else {
                    put(x, y, 11u);
                }
            }
        }
    }

    drawLoader();

    if (g_state == SB_BANK) {
        drawBanner();
    }
    drawHud();

    if (g_state == SB_DIG || g_state == SB_BACKOUT) {
        int cc = countCrackCells();
        if (cc < g_crackCountPrev) {
            g_violations++;
        }
        g_crackCountPrev = cc;
    } else if (g_state == SB_SURFACE) {
        g_crackCountPrev = 0;
    }
}

static void enterCrush(void)
{
    if (g_state == SB_CRUSH) {
        return;
    }
    g_state = SB_CRUSH;
    g_stateTimer = 0u;
    g_armTimer = 0u;
    g_flashTimer = 0u;
}

static void addCrackRise(uint32_t dt)
{
    uint32_t rate;
    uint32_t total;
    if (g_state != SB_DIG && g_state != SB_BACKOUT) {
        return;
    }
    rate = 150u + (uint32_t)g_depth * 10u;
    total = (uint32_t)g_crackAcc + rate * dt;
    g_crackAcc = (int)(total % 1000u);
    g_crack += (int)(total / 1000u);
    if (g_crack >= 1000) {
        g_crack = 1000;
        enterCrush();
    }
}

static void doScoop(void)
{
    int value;
    if (g_state != SB_DIG) {
        return;
    }
    if (g_depth < 40) {
        g_depth++;
    }
    value = 1 + g_depth / 2 + (int)(rngNext(&g_rngPlay) % 3u);
    g_haul += value;
    g_crack += 25 + (int)(rngNext(&g_rngPlay) % 41u);
    g_lastScoop = g_now;
    g_armTimer = 120u;
    g_flashTimer = 140u;
    g_scoopFlag = 1;
    if (g_crack >= 1000) {
        g_crack = 1000;
        enterCrush();
    }
}

static void startRun(void)
{
    g_state = SB_DIG;
    g_depth = 0;
    g_haul = 0;
    g_crack = 0;
    g_crackAcc = 0;
    g_banner = 0u;
    g_lastScoop = g_now;
    g_stateTimer = 0u;
    g_armTimer = 0u;
    g_flashTimer = 0u;
    g_aiSwingTimer = 0u;
    g_aiStopped = 0u;
    g_crackCountPrev = 0;
    generateVeins();
    generateCracks();
}

static void aiStartRound(void)
{
    g_aiThreshold = 450u + rngNext(&g_rngAi) % 350u;
    g_ai = 1;
    startRun();
    g_aiNextSwing = 700u + rngNext(&g_rngAi) % 401u;
    doScoop();
}

static void enterBackout(void)
{
    uint32_t d;
    g_state = SB_BACKOUT;
    g_stateTimer = 0u;
    d = 600u + 20u * (uint32_t)g_depth;
    if (d > 1600u) {
        d = 1600u;
    }
    g_backoutDur = d;
    g_armTimer = 0u;
    g_flashTimer = 0u;
}

static void bankNow(void)
{
    int h = g_haul;
    g_banked += h;
    g_haul = 0;
    if (h < 10) {
        g_banner = 0u;
    } else if (h < 30) {
        g_banner = 1u;
    } else {
        g_banner = 2u;
    }
    g_state = SB_BANK;
    g_stateTimer = 0u;
    g_bankFlag = 1;
}

static void toSurface(void)
{
    g_state = SB_SURFACE;
    g_depth = 0;
    g_haul = 0;
    g_crack = 0;
    g_crackAcc = 0;
    g_stateTimer = 0u;
    g_lastScoop = g_now;
    g_armTimer = 0u;
    g_flashTimer = 0u;
    g_crackCountPrev = 0;
    g_aiRoundTimer = g_ai ? 350u : 0u;
    g_resetFlag = 1;
}

void sbInit(uint32_t seed)
{
    memset(g_grid, 0, sizeof(g_grid));
    memset(g_veins, 0, sizeof(g_veins));
    memset(g_cracks, 0, sizeof(g_cracks));

    g_rngStone = mix32(seed ^ 0x9E3779B9u);
    g_rngPlay = mix32(seed + 0x85EBCA6Bu);
    g_rngAi = mix32(seed ^ 0xC2B2AE35u);
    if (g_rngStone == 0u) {
        g_rngStone = 0xA341316Cu;
    }
    if (g_rngPlay == 0u) {
        g_rngPlay = 0x1B873593u;
    }
    if (g_rngAi == 0u) {
        g_rngAi = 0xCC9E2D51u;
    }

    g_state = SB_SURFACE;
    g_depth = 0;
    g_haul = 0;
    g_banked = 0;
    g_crack = 0;
    g_crackAcc = 0;
    g_ai = 0;
    g_violations = 0;
    g_now = 0u;
    g_lastScoop = 0u;
    g_sinceInput = 0u;
    g_stateTimer = 0u;
    g_backoutDur = 0u;
    g_banner = 0u;
    g_armTimer = 0u;
    g_flashTimer = 0u;
    g_aiSwingTimer = 0u;
    g_aiNextSwing = 0u;
    g_aiThreshold = 0u;
    g_aiStopped = 0u;
    g_aiRoundTimer = 0u;
    g_crackCountPrev = 0;
    g_shake = 0;
    g_pen = 0;
    g_scoopFlag = 0;
    g_bankFlag = 0;
    g_resetFlag = 0;
    g_nVeins = 0;
    g_nCracks = 0;
    g_initialized = 1;
    render();
}

void sbTick(uint32_t dtMs)
{
    uint32_t dt = dtMs;
    int haul0;
    int crack0;

    if (!g_initialized) {
        sbInit(0u);
    }

    g_now += dt;
    if (dt > 0u) {
        g_sinceInput += dt;
    }
    haul0 = g_haul;
    crack0 = g_crack;
    g_scoopFlag = 0;
    g_bankFlag = 0;
    g_resetFlag = 0;

    if (g_state == SB_SURFACE) {
        if (!g_ai && g_sinceInput >= 30000u) {
            g_ai = 1;
            g_aiRoundTimer = 0u;
            g_aiStopped = 0u;
        }
        if (g_ai) {
            if (g_aiRoundTimer > dt) {
                g_aiRoundTimer -= dt;
            } else {
                g_aiRoundTimer = 0u;
            }
            if (g_aiRoundTimer == 0u) {
                aiStartRound();
            }
        }
    } else if (g_state == SB_DIG || g_state == SB_BACKOUT) {
        addCrackRise(dt);
        if (g_state != SB_CRUSH && g_state == SB_DIG) {
            if (g_ai) {
                if (!g_aiStopped) {
                    g_aiSwingTimer += dt;
                    if (g_aiSwingTimer >= g_aiNextSwing) {
                        if (g_crack >= (int)g_aiThreshold) {
                            g_aiStopped = 1u;
                        } else {
                            g_aiSwingTimer = 0u;
                            g_aiNextSwing = 700u + rngNext(&g_rngAi) % 401u;
                            doScoop();
                        }
                    }
                }
                if (g_state == SB_DIG && g_aiStopped &&
                    (g_now - g_lastScoop) >= 1000u) {
                    enterBackout();
                }
            } else {
                if ((g_now - g_lastScoop) >= 1000u) {
                    enterBackout();
                }
            }
        }
        if (g_state == SB_BACKOUT) {
            g_stateTimer += dt;
            if (g_stateTimer >= g_backoutDur) {
                bankNow();
            }
        }
    } else if (g_state == SB_CRUSH) {
        g_stateTimer += dt;
        if (g_stateTimer >= 1400u) {
            toSurface();
        }
    } else if (g_state == SB_BANK) {
        g_stateTimer += dt;
        if (g_stateTimer >= 1600u) {
            toSurface();
        }
    }

    if (g_armTimer > 0u) {
        g_armTimer = (dt >= g_armTimer) ? 0u : g_armTimer - dt;
    }
    if (g_flashTimer > 0u) {
        g_flashTimer = (dt >= g_flashTimer) ? 0u : g_flashTimer - dt;
    }

    if (g_haul > haul0 && !g_scoopFlag) {
        g_violations++;
    }
    if (g_haul < haul0 && !g_bankFlag && !g_resetFlag) {
        g_violations++;
    }
    if ((g_state == SB_DIG || g_state == SB_BACKOUT) && g_crack < crack0) {
        g_violations++;
    }
    if (g_depth < 0 || g_depth > 40) {
        g_violations++;
    }
    if (g_state < 0 || g_state > 4) {
        g_violations++;
    }

    render();
}

void sbInteract(void)
{
    int haul0;

    if (!g_initialized) {
        sbInit(0u);
    }

    g_sinceInput = 0u;
    if (g_ai) {
        g_ai = 0;
        g_aiStopped = 0u;
    }
    haul0 = g_haul;
    g_scoopFlag = 0;

    if (g_state == SB_SURFACE) {
        startRun();
        doScoop();
    } else if (g_state == SB_DIG) {
        doScoop();
    }

    if (g_haul > haul0 && !g_scoopFlag) {
        g_violations++;
    }
    render();
}

void sbCell(int x, int y)
{
    x += g_shake;
    if (x < 0 || x >= GW || y < 0 || y >= GH) {
        return;
    }
    g_grid[y * GW + x] = (uint8_t)g_pen;
}

uint8_t sbCellAt(int x, int y)
{
    if (x < 0 || x >= GW || y < 0 || y >= GH) {
        return 0u;
    }
    return g_grid[y * GW + x];
}

void sbStats(int *depth, int *haul, int *banked, int *crack,
             int *state, int *ai, int *violations)
{
    if (depth != 0) {
        *depth = g_depth;
    }
    if (haul != 0) {
        *haul = g_haul;
    }
    if (banked != 0) {
        *banked = g_banked;
    }
    if (crack != 0) {
        *crack = g_crack;
    }
    if (state != 0) {
        *state = g_state;
    }
    if (ai != 0) {
        *ai = g_ai;
    }
    if (violations != 0) {
        *violations = g_violations;
    }
}

typedef struct {
    uint8_t grid[GW * GH];
    Vein veins[MAX_VEINS];
    Crack cracks[MAX_CRACK];
    int nVeins;
    int nCracks;
    int state;
    int depth;
    int haul;
    int banked;
    int crack;
    int crackAcc;
    int ai;
    int violations;
    int crackCountPrev;
    int initialized;
    int pen;
    uint32_t now;
    uint32_t lastScoop;
    uint32_t sinceInput;
    uint32_t stateTimer;
    uint32_t backoutDur;
    uint32_t banner;
    uint32_t armTimer;
    uint32_t flashTimer;
    uint32_t aiSwingTimer;
    uint32_t aiNextSwing;
    uint32_t aiThreshold;
    uint32_t aiStopped;
    uint32_t aiRoundTimer;
    uint32_t rngStone;
    uint32_t rngPlay;
    uint32_t rngAi;
} Snap;

static Snap g_snap;

static void snapSave(void)
{
    memcpy(g_snap.grid, g_grid, sizeof(g_grid));
    memcpy(g_snap.veins, g_veins, sizeof(g_veins));
    memcpy(g_snap.cracks, g_cracks, sizeof(g_cracks));
    g_snap.nVeins = g_nVeins;
    g_snap.nCracks = g_nCracks;
    g_snap.state = g_state;
    g_snap.depth = g_depth;
    g_snap.haul = g_haul;
    g_snap.banked = g_banked;
    g_snap.crack = g_crack;
    g_snap.crackAcc = g_crackAcc;
    g_snap.ai = g_ai;
    g_snap.violations = g_violations;
    g_snap.crackCountPrev = g_crackCountPrev;
    g_snap.initialized = g_initialized;
    g_snap.pen = g_pen;
    g_snap.now = g_now;
    g_snap.lastScoop = g_lastScoop;
    g_snap.sinceInput = g_sinceInput;
    g_snap.stateTimer = g_stateTimer;
    g_snap.backoutDur = g_backoutDur;
    g_snap.banner = g_banner;
    g_snap.armTimer = g_armTimer;
    g_snap.flashTimer = g_flashTimer;
    g_snap.aiSwingTimer = g_aiSwingTimer;
    g_snap.aiNextSwing = g_aiNextSwing;
    g_snap.aiThreshold = g_aiThreshold;
    g_snap.aiStopped = g_aiStopped;
    g_snap.aiRoundTimer = g_aiRoundTimer;
    g_snap.rngStone = g_rngStone;
    g_snap.rngPlay = g_rngPlay;
    g_snap.rngAi = g_rngAi;
}

static void snapLoad(void)
{
    memcpy(g_grid, g_snap.grid, sizeof(g_grid));
    memcpy(g_veins, g_snap.veins, sizeof(g_veins));
    memcpy(g_cracks, g_snap.cracks, sizeof(g_cracks));
    g_nVeins = g_snap.nVeins;
    g_nCracks = g_snap.nCracks;
    g_state = g_snap.state;
    g_depth = g_snap.depth;
    g_haul = g_snap.haul;
    g_banked = g_snap.banked;
    g_crack = g_snap.crack;
    g_crackAcc = g_snap.crackAcc;
    g_ai = g_snap.ai;
    g_violations = g_snap.violations;
    g_crackCountPrev = g_snap.crackCountPrev;
    g_initialized = g_snap.initialized;
    g_pen = g_snap.pen;
    g_now = g_snap.now;
    g_lastScoop = g_snap.lastScoop;
    g_sinceInput = g_snap.sinceInput;
    g_stateTimer = g_snap.stateTimer;
    g_backoutDur = g_snap.backoutDur;
    g_banner = g_snap.banner;
    g_armTimer = g_snap.armTimer;
    g_flashTimer = g_snap.flashTimer;
    g_aiSwingTimer = g_snap.aiSwingTimer;
    g_aiNextSwing = g_snap.aiNextSwing;
    g_aiThreshold = g_snap.aiThreshold;
    g_aiStopped = g_snap.aiStopped;
    g_aiRoundTimer = g_snap.aiRoundTimer;
    g_rngStone = g_snap.rngStone;
    g_rngPlay = g_snap.rngPlay;
    g_rngAi = g_snap.rngAi;
    g_shake = 0;
}

static uint32_t probeHash(uint32_t seed)
{
    uint32_t h = 2166136261u;
    uint32_t t;
    int i;
    sbInit(seed);
    for (t = 0u; t < 8000u; t += 20u) {
        if (t == 0u || t == 300u || t == 600u || t == 900u ||
            t == 1200u || t == 1500u || t == 1800u || t == 2100u ||
            t == 2400u) {
            sbInteract();
        }
        sbTick(20u);
        for (i = 0; i < GW * GH; i++) {
            h ^= (uint32_t)g_grid[i];
            h *= 16777619u;
        }
    }
    h ^= (uint32_t)g_depth * 0x9E3779B1u;
    h ^= (uint32_t)g_haul * 0x85EBCA77u;
    h ^= (uint32_t)g_banked * 0xC2B2AE3Du;
    h ^= (uint32_t)g_state * 0x27D4EB2Fu;
    return h;
}

int sbSelfCheck(void)
{
    int fail = 0;
    int sawSurface = 0;
    int sawDig = 0;
    int sawBack = 0;
    int sawBank = 0;
    int sawCrush = 0;
    int i;
    uint32_t t;
    uint32_t h1;
    uint32_t h2;

    snapSave();

    for (i = 0; i < GW * GH; i++) {
        uint8_t v = g_grid[i];
        if (!(v <= 5u || (v >= 11u && v <= 15u))) {
            fail++;
        }
    }
    if (sbCellAt(-1, 0) != 0u || sbCellAt(GW, GH) != 0u ||
        sbCellAt(0, -1) != 0u) {
        fail++;
    }

    /* TINY HAUL: one scoop, wait, bank, surface. */
    sbInit(0x1111u);
    sbInteract();
    if (g_state == SB_DIG) {
        sawDig = 1;
    } else {
        fail++;
    }
    for (t = 0u; t < 1800u; t += 10u) {
        sbTick(10u);
        if (g_state == SB_BACKOUT) {
            sawBack = 1;
        }
        if (g_state == SB_BANK) {
            sawBank = 1;
        }
    }
    if (g_state != SB_BANK) {
        fail++;
    }
    if (g_depth != 1 || g_haul != 0 || g_banked <= 0 || g_banked >= 10) {
        fail++;
    }
    if (g_banner != 0u) {
        fail++;
    }
    for (t = 0u; t < 2200u; t += 10u) {
        sbTick(10u);
    }
    if (g_state == SB_SURFACE) {
        sawSurface = 1;
    } else {
        fail++;
    }

    /* Crush: bank first, then bury a run; banked must survive. */
    sbInit(0x2222u);
    sbInteract();
    for (t = 0u; t < 3600u; t += 10u) {
        sbTick(10u);
    }
    if (g_state != SB_SURFACE) {
        fail++;
    }
    {
        int bankedBefore = g_banked;
        int guard = 0;
        if (bankedBefore <= 0) {
            fail++;
        }
        sbInteract();
        while (g_state != SB_CRUSH && guard < 90) {
            sbInteract();
            sbTick(10u);
            guard++;
        }
        if (g_state == SB_CRUSH) {
            sawCrush = 1;
        } else {
            fail++;
        }
        for (t = 0u; t < 1600u; t += 10u) {
            sbTick(10u);
        }
        if (g_state != SB_SURFACE) {
            fail++;
        }
        if (g_banked != bankedBefore || g_haul != 0) {
            fail++;
        }
        sbInteract();
        if (g_state != SB_DIG || g_depth != 1 || g_haul <= 0) {
            fail++;
        }
    }

    /* AI reachable by elapsed time, reaches depth, returns to surface. */
    sbInit(0x3333u);
    for (t = 0u; t < 30200u; t += 100u) {
        sbTick(100u);
    }
    if (!g_ai) {
        fail++;
    }
    {
        int maxDepth = 0;
        int sawAiSurface = 0;
        for (t = 0u; t < 60000u; t += 50u) {
            sbTick(50u);
            if (g_ai) {
                if (g_depth > maxDepth) {
                    maxDepth = g_depth;
                }
                if (g_state == SB_SURFACE) {
                    sawAiSurface = 1;
                }
            }
        }
        if (maxDepth < 3) {
            fail++;
        }
        if (!sawAiSurface) {
            fail++;
        }
    }

    /* Handoff: tap during AI changes control and keeps the round. */
    {
        int guard = 0;
        while (guard < 400 && !(g_ai && g_state == SB_DIG)) {
            sbTick(50u);
            guard++;
        }
        if (!(g_ai && g_state == SB_DIG)) {
            fail++;
        } else {
            int d0 = g_depth;
            int h0 = g_haul;
            int c0 = g_crack;
            sbInteract();
            if (g_ai != 0) {
                fail++;
            }
            if (g_depth < d0 || g_haul < h0 || g_crack < c0) {
                fail++;
            }
            for (t = 0u; t < 6000u; t += 10u) {
                sbTick(10u);
            }
            if (g_state != SB_SURFACE) {
                fail++;
            }
        }
    }

    /* Determinism of two identical probe runs. */
    h1 = probeHash(0x77AA55C3u);
    h2 = probeHash(0x77AA55C3u);
    if (h1 != h2) {
        fail++;
    }

    if (!sawDig || !sawBack || !sawBank || !sawCrush || !sawSurface) {
        fail++;
    }

    snapLoad();
    return fail;
}

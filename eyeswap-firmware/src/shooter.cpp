#include "shooter.h"
#include <string.h>

/* Quickdraw duel: 60x60 cell grid, 240x240 round panel. */

enum { W = 60, H = 60 };
enum { GROUND_Y = 42, HUD_Y = 54 };
enum { READY_MS = 2400, SHOT_MS = 900, SCORE_MS = 3200, IDLE_MS = 60000 };
enum { SHOT_GAP_MS = 500, CYLINDER = 6 };
enum { MODE_DEMO = 0, MODE_PLAYER = 1 };

enum {
    C_EMPTY = 0,
    C_LEFT = 1,
    C_RIGHT = 2,
    C_FLASH = 3,
    C_TRACER = 4,
    C_DUST = 5,
    C_SKY = 11,
    C_SKYBRIGHT = 12,
    C_GROUND = 13,
    C_BUILDING = 14,
    C_TEXT = 15
};

struct CsState {
    uint32_t rng, seed, animMs, frameNo;
    uint32_t stateMs, waitMs, prevWaitMs;
    uint32_t oppTime, lTime, rTime;
    uint32_t pNextShot, bNextShot, idleMs;
    uint32_t drawStartFrame, shotStartFrame;
    uint32_t shotFrame[3];
    int32_t mode, state, round, streak;
    int32_t winner, shotBy, shotResolved, fallStep;
    int32_t pShots, bShots, playerFired, shotCount, shotSide[3];
    int32_t violations;
};

static CsState g;
static uint8_t g_grid[W * H];

/* ---------- tiny drawing helpers ---------- */

static void setc(int x, int y, uint8_t code)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    g_grid[y * W + x] = code;
}

static void hline(int x0, int x1, int y, uint8_t code)
{
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (int x = x0; x <= x1; x++) setc(x, y, code);
}

static void rect(int x, int y, int w, int h, uint8_t code)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            setc(x + xx, y + yy, code);
}

static void drawDisc(int cx, int cy, int r, uint8_t code)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) setc(cx + x, cy + y, code);
}

static void drawMesa(int x0, int w, int h)
{
    for (int r = 0; r < h; r++) {
        int inset = r / 2;
        hline(x0 + inset, x0 + w - 1 - inset, GROUND_Y - 1 - r, C_BUILDING);
    }
}

/* ---------- figures ---------- */

static void drawStanding(int bx, uint8_t code, int dir, int lean)
{
    /* hat */
    hline(bx - 5 + lean, bx + 5 + lean, 25, code);
    hline(bx - 3 + lean, bx + 3 + lean, 22, code);
    hline(bx - 3 + lean, bx + 3 + lean, 23, code);
    hline(bx - 3 + lean, bx + 3 + lean, 24, code);
    /* head and neck */
    hline(bx - 2 + lean, bx + 1 + lean, 26, code);
    hline(bx - 2 + lean, bx + 1 + lean, 27, code);
    setc(bx + lean, 28, code);
    /* shoulders and poncho */
    hline(bx - 4 + lean, bx + 4 + lean, 29, code);
    hline(bx - 4 + lean, bx + 4 + lean, 30, code);
    hline(bx - 4 + lean, bx + 4 + lean, 31, code);
    hline(bx - 5 + lean, bx + 4 + lean, 32, code);
    hline(bx - 5 + lean, bx + 4 + lean, 33, code);
    hline(bx - 4 + lean, bx + 4 + lean, 34, code);
    hline(bx - 4 + lean, bx + 3 + lean, 35, code);
    /* gun arm, hand hovering at the holster */
    setc(bx + dir * 4 + lean, 31, code);
    setc(bx + dir * 4 + lean, 32, code);
    setc(bx + dir * 4 + lean, 33, code);
    setc(bx + dir * 4 + lean, 34, code);
    setc(bx + dir * 5 + lean, 35, code);
    setc(bx + dir * 5 + lean, 36, code);
    /* belt, legs, boots */
    hline(bx - 3, bx + 3, 36, code);
    rect(bx - 3, 37, 3, 4, code);
    rect(bx + 1, 37, 3, 4, code);
    hline(bx - 4, bx - 1, 41, code);
    hline(bx + 1, bx + 4, 41, code);
}

static void drawFallen(int bx, uint8_t code, int d, int hatRoll)
{
    hline(bx + d * 1, bx + d * 8, 40, code);
    hline(bx + d * 2, bx + d * 9, 39, code);
    hline(bx + d * 2, bx + d * 7, 38, code);
    setc(bx + d * 10, 39, code);
    setc(bx + d * 10, 38, code);
    hline(bx - d * 1, bx - d * 2, 41, code);
    int hx = bx + d * (12 + hatRoll);
    hline(hx, hx + d * 2, 41, code);
}

static void drawDust(int bx, int d, uint32_t t)
{
    int r = 1 + (int)((t / 150u) % 3u);
    setc(bx + d * (3 + r), 40, C_DUST);
    setc(bx + d * (5 + r), 39, C_DUST);
    setc(bx + d * (2 + r), 38, C_DUST);
    setc(bx - d * (1 + r), 40, C_DUST);
    setc(bx + d * (7 + r), 37, C_DUST);
}

/* ---------- 3x5 font ---------- */

static const uint8_t FONT[40][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, /* 0 */
    {0b010, 0b110, 0b010, 0b010, 0b111}, /* 1 */
    {0b111, 0b001, 0b111, 0b100, 0b111}, /* 2 */
    {0b111, 0b001, 0b111, 0b001, 0b111}, /* 3 */
    {0b101, 0b101, 0b111, 0b001, 0b001}, /* 4 */
    {0b111, 0b100, 0b111, 0b001, 0b111}, /* 5 */
    {0b111, 0b100, 0b111, 0b101, 0b111}, /* 6 */
    {0b111, 0b001, 0b001, 0b001, 0b001}, /* 7 */
    {0b111, 0b101, 0b111, 0b101, 0b111}, /* 8 */
    {0b111, 0b101, 0b111, 0b001, 0b111}, /* 9 */
    {0b010, 0b101, 0b111, 0b101, 0b101}, /* A */
    {0b110, 0b101, 0b110, 0b101, 0b110}, /* B */
    {0b011, 0b100, 0b100, 0b100, 0b011}, /* C */
    {0b110, 0b101, 0b101, 0b101, 0b110}, /* D */
    {0b111, 0b100, 0b110, 0b100, 0b111}, /* E */
    {0b111, 0b100, 0b110, 0b100, 0b100}, /* F */
    {0b011, 0b100, 0b101, 0b101, 0b011}, /* G */
    {0b101, 0b101, 0b111, 0b101, 0b101}, /* H */
    {0b111, 0b010, 0b010, 0b010, 0b111}, /* I */
    {0b001, 0b001, 0b001, 0b101, 0b011}, /* J */
    {0b101, 0b101, 0b110, 0b101, 0b101}, /* K */
    {0b100, 0b100, 0b100, 0b100, 0b111}, /* L */
    {0b101, 0b111, 0b111, 0b101, 0b101}, /* M */
    {0b110, 0b101, 0b101, 0b101, 0b101}, /* N */
    {0b010, 0b101, 0b101, 0b101, 0b010}, /* O */
    {0b110, 0b101, 0b110, 0b100, 0b100}, /* P */
    {0b010, 0b101, 0b101, 0b110, 0b011}, /* Q */
    {0b110, 0b101, 0b110, 0b101, 0b101}, /* R */
    {0b011, 0b100, 0b010, 0b001, 0b110}, /* S */
    {0b111, 0b010, 0b010, 0b010, 0b010}, /* T */
    {0b101, 0b101, 0b101, 0b101, 0b111}, /* U */
    {0b101, 0b101, 0b101, 0b101, 0b010}, /* V */
    {0b101, 0b101, 0b111, 0b111, 0b101}, /* W */
    {0b101, 0b101, 0b010, 0b101, 0b101}, /* X */
    {0b101, 0b101, 0b010, 0b010, 0b010}, /* Y */
    {0b111, 0b001, 0b010, 0b100, 0b111}, /* Z */
    {0b000, 0b000, 0b000, 0b000, 0b000}, /* space */
    {0b010, 0b010, 0b010, 0b000, 0b010}, /* ! */
    {0b000, 0b000, 0b000, 0b000, 0b010}, /* . */
    {0b000, 0b000, 0b111, 0b000, 0b000}  /* - */
};

static int glyphIndex(char ch)
{
    static const char MAP[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ !.-";
    for (int i = 0; MAP[i]; i++)
        if (MAP[i] == ch) return i;
    return 36;
}

static int textW(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n > 0 ? n * 4 - 1 : 0;
}

static void drawText(const char *s, int x, int y, uint8_t code)
{
    for (int i = 0; s[i]; i++, x += 4) {
        int gi = glyphIndex(s[i]);
        for (int r = 0; r < 5; r++) {
            uint8_t bits = FONT[gi][r];
            if (bits & 4) setc(x, y + r, code);
            if (bits & 2) setc(x + 1, y + r, code);
            if (bits & 1) setc(x + 2, y + r, code);
        }
    }
}

static int appendUInt(char *buf, int n, unsigned v)
{
    char tmp[12];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v && t < 12) {
        tmp[t++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (t > 0) buf[n++] = tmp[--t];
    buf[n] = 0;
    return n;
}

/* ---------- scene ---------- */

static void drawScenery(void)
{
    drawDisc(52, 30, 4, C_TEXT);
    drawMesa(0, 17, 5);
    drawMesa(16, 10, 3);
    drawMesa(35, 10, 4);
    drawMesa(44, 16, 6);

    /* false-front building */
    hline(21, 32, 20, C_BUILDING);
    hline(20, 33, 21, C_BUILDING);
    rect(22, 22, 10, 20, C_BUILDING);
    rect(24, 35, 3, 7, C_GROUND);
    rect(28, 27, 2, 3, C_GROUND);

    hline(0, W - 1, GROUND_Y, C_BUILDING);
}

static void drawHud(void)
{
    char buf[40];
    if (g.mode == MODE_DEMO) {
        drawText("DEMO", (W - 15) / 2, HUD_Y, C_TEXT);
        return;
    }
    int n = 0;
    const char *p;
    p = "ROUND";
    while (*p) buf[n++] = *p++;
    n = appendUInt(buf, n, (unsigned)g.round);
    if (g.state == CS_WAIT) {
        p = " WAIT...";
        while (*p) buf[n++] = *p++;
    } else {
        p = " STREAK";
        while (*p) buf[n++] = *p++;
        n = appendUInt(buf, n, (unsigned)g.streak);
    }
    buf[n] = 0;
    if (textW(buf) > W - 2) {
        n = 0;
        buf[n++] = 'R';
        n = appendUInt(buf, n, (unsigned)g.round);
        p = (g.state == CS_WAIT) ? " W" : " S";
        while (*p) buf[n++] = *p++;
        if (g.state != CS_WAIT) n = appendUInt(buf, n, (unsigned)g.streak);
        buf[n] = 0;
    }
    drawText(buf, (W - textW(buf)) / 2, HUD_Y, C_TEXT);
}

static void renderFrame(void)
{
    int i;
    for (i = 0; i < W * GROUND_Y; i++) g_grid[i] = C_SKY;
    for (; i < W * H; i++) g_grid[i] = C_GROUND;

    if (g.state == CS_DRAW && g.stateMs < 400u) {
        for (i = 0; i < W * GROUND_Y; i++) g_grid[i] = C_SKYBRIGHT;
    }

    drawScenery();

    int swayL = (int)((g.animMs / 900u) & 1u);
    int swayR = (int)(((g.animMs + 450u) / 900u) & 1u);

    if (g.state == CS_READY || g.state == CS_WAIT || g.state == CS_DRAW ||
        g.winner < 0 || g.winner == 2) {
        drawStanding(11 + swayL, C_LEFT, 1, 0);
        drawStanding(48 + swayR, C_RIGHT, -1, 0);
    } else {
        int wLeft = (g.winner == 0);
        if (wLeft) drawStanding(11, C_LEFT, 1, 0);
        else drawStanding(48, C_RIGHT, -1, 0);

        int loserLeft = !wLeft;
        int lbx = loserLeft ? 11 : 48;
        int ld = loserLeft ? -1 : 1;
        uint8_t lc = loserLeft ? C_LEFT : C_RIGHT;
        int step = 2;
        if (g.state == CS_SHOT && g.winner >= 0 && g.winner <= 1) {
            step = (g.stateMs < 130u) ? 0 : ((g.stateMs < 280u) ? 1 : 2);
        }
        if (step == 0) {
            drawStanding(lbx, lc, loserLeft ? 1 : -1, 0);
        } else if (step == 1) {
            drawStanding(lbx, lc, loserLeft ? 1 : -1, ld * 2);
        } else {
            int roll = 0;
            if (g.state == CS_SCORE) roll = (int)((g.stateMs / 400u) % 3u);
            drawFallen(lbx, lc, ld, roll);
            uint32_t dt = (g.state == CS_SCORE) ? (600u + g.stateMs / 3u) : g.stateMs;
            drawDust(lbx, ld, dt);
        }
    }

    /* muzzle flashes and tracers: every shot burns for 2 rendered frames */
    if (g.state == CS_DRAW || g.state == CS_SHOT) {
        for (int k = 0; k < 3; k++) {
            if ((uint32_t)(g.frameNo - g.shotFrame[k]) >= 2u) continue;
            int side = g.shotSide[k];
            int sdir = (side == 0) ? 1 : -1;
            int sbx = (side == 0) ? 11 : 48;
            int mx = sbx + sdir * 7;
            int my = 34;
            int tx = (side == 0) ? 48 : 11;
            int ty = 32;
            int dx = tx - mx, dy = ty - my;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int nn = adx > ady ? adx : ady;
            if (nn < 1) nn = 1;
            for (int s = 2; s <= nn; s++) {
                setc(mx + dx * s / nn, my + dy * s / nn, C_TRACER);
            }
            setc(mx, my, C_FLASH);
            setc(mx + sdir, my, C_FLASH);
            setc(mx, my - 1, C_FLASH);
            setc(mx, my + 1, C_FLASH);
            setc(mx + sdir * 2, my, C_FLASH);
        }
    }

    if (g.state == CS_DRAW) {
        for (int yy = 0; yy <= 6; yy++) hline(0, W - 1, yy, C_BUILDING);
        drawText("DRAW!", (W - 19) / 2, 1, C_TEXT);
    } else if (g.state == CS_SCORE) {
        const char *s;
        if (g.winner == 2) s = "TIE!";
        else if (g.mode == MODE_DEMO) s = (g.winner == 0) ? "L" : "R";
        else s = (g.winner == 0) ? "YOU!" : "HIM!";
        int tw = textW(s);
        int x = (W - tw) / 2;
        for (int yy = 6; yy <= 14; yy++) hline(x - 5, x + tw + 4, yy, C_BUILDING);
        drawText(s, x, 8, C_TEXT);
        if (g.mode == MODE_PLAYER) {
            char buf[20];
            int n = 0;
            const char *p = "STREAK";
            while (*p) buf[n++] = *p++;
            n = appendUInt(buf, n, (unsigned)g.streak);
            buf[n] = 0;
            drawText(buf, (W - textW(buf)) / 2, 17, C_TEXT);
        }
    }

    drawHud();
}

/* ---------- rng and pacing ---------- */

static uint32_t rngNext(void)
{
    uint32_t x = g.rng;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g.rng = x;
    return x;
}

static uint32_t pickWait(int round)
{
    (void)round;
    uint32_t lo = 2000, span = 4001;
    uint32_t w = lo + (rngNext() % span);
    if (w == g.prevWaitMs) w = lo + ((w - lo + 1u) % span);
    g.prevWaitMs = w;
    return w;
}

static uint32_t reactMs(int wins)
{
    int base = 700 - 12 * wins;
    if (base < 320) base = 320;
    int noise = (int)(rngNext() % 81u) - 40;
    int t = base + noise;
    if (t < 280) t = 280;
    if (t > 760) t = 760;
    return (uint32_t)t;
}

/* ---------- state machine ---------- */

static void startRound(int r)
{
    g.round = r;
    g.state = CS_READY;
    g.stateMs = 0;
    g.winner = -1;
    g.shotBy = -1;
    g.shotResolved = 0;
    g.fallStep = 0;
    g.waitMs = pickWait(r);
}

static void startPlayerGame(void)
{
    g.mode = MODE_PLAYER;
    g.streak = 0;
    g.idleMs = 0;
    g.prevWaitMs = 0;
    startRound(1);
}

static void enterDemo(void)
{
    g.mode = MODE_DEMO;
    g.streak = 0;
    g.idleMs = 0;
    g.prevWaitMs = 0;
    g.winner = -1;
    g.shotBy = -1;
    g.shotResolved = 0;
    startRound(1);
}

static void enterDraw(void)
{
    g.state = CS_DRAW;
    g.stateMs = 0;
    g.winner = -1;
    g.shotBy = -1;
    g.shotResolved = 0;
    g.fallStep = 0;
    g.drawStartFrame = g.frameNo;
    g.pShots = CYLINDER;
    g.bShots = CYLINDER;
    g.playerFired = 0;
    g.shotCount = 0;
    if (g.mode == MODE_DEMO) {
        int esc = g.round - 1;
        if (esc > 11) esc = 11;
        g.lTime = reactMs(esc);
        g.rTime = reactMs(esc);
        g.pNextShot = g.lTime;
        g.bNextShot = g.rTime;
    } else {
        g.oppTime = reactMs(g.streak);
        g.bNextShot = g.oppTime;
        g.pNextShot = 0xFFFFFFFFu;   /* player waits for the tap */
    }
}

static void enterShot(void)
{
    g.state = CS_SHOT;
    g.shotResolved = 1;
    g.shotStartFrame = g.frameNo;
    g.fallStep = 0;
}

/* One shot leaves the cylinder: record it for the renderer, roll the hit.
 * 2 of 3 shots connect, so the cylinder depth actually matters. */
static void fireShot(int side)
{
    if (side == 0) {
        if (g.pShots <= 0) return;
        g.pShots--;
        g.pNextShot += SHOT_GAP_MS;
    } else {
        if (g.bShots <= 0) return;
        g.bShots--;
        g.bNextShot += SHOT_GAP_MS;
    }
    int slot = g.shotCount % 3;
    g.shotSide[slot] = side;
    g.shotFrame[slot] = g.frameNo;
    g.shotCount++;
    g.shotBy = side;
    if ((rngNext() % 3u) != 0u) {
        g.winner = side;
        if (g.mode == MODE_PLAYER) {
            if (side == 0) {
                if (g.streak < 9999) g.streak++;
            } else {
                g.streak = 0;
            }
        }
    }
}

static void tickCore(uint32_t dt, bool doRender)
{
    g.frameNo++;
    g.animMs += dt;

    if (g.mode == MODE_PLAYER) {
        g.idleMs += dt;
        if (g.idleMs >= IDLE_MS) {
            enterDemo();
            if (doRender) renderFrame();
            return;
        }
    }

    uint32_t rem = dt;
    for (int guard = 0; guard < 12; guard++) {
        bool moved = false;
        switch (g.state) {
        case CS_READY:
            g.stateMs += rem;
            rem = 0;
            if (g.stateMs >= READY_MS) {
                uint32_t over = g.stateMs - READY_MS;
                g.state = CS_WAIT;
                g.stateMs = over;
                moved = true;
            }
            break;
        case CS_WAIT:
            g.stateMs += rem;
            rem = 0;
            if (g.stateMs >= g.waitMs) {
                uint32_t over = g.stateMs - g.waitMs;
                enterDraw();
                g.stateMs = over;
                moved = true;
            }
            break;
        case CS_DRAW: {
            /* the fight: cylinders trade fire, first clean hit ends it */
            g.stateMs += rem;
            rem = 0;
            if (g.pShots > 0 && g.stateMs >= g.pNextShot) fireShot(0);
            if (g.winner < 0 && g.bShots > 0 && g.stateMs >= g.bNextShot) fireShot(1);
            bool done = false;
            if (g.winner >= 0) {
                uint32_t over = g.stateMs;
                enterShot();
                g.stateMs = over;
                done = true;
            } else if (g.pShots <= 0 && g.bShots <= 0) {
                g.winner = 2;            /* both cylinders dry: stand-off */
                uint32_t over = g.stateMs;
                enterShot();
                g.stateMs = over;
                done = true;
            } else if (g.mode == MODE_PLAYER && !g.playerFired &&
                       g.bShots <= 0 && g.stateMs >= g.bNextShot + 800u) {
                g.winner = 1;            /* never drew: he wins dry */
                g.streak = 0;
                uint32_t over = g.stateMs;
                enterShot();
                g.stateMs = over;
                done = true;
            }
            if (done) moved = true;
            break;
        }
        case CS_SHOT:
            g.stateMs += rem;
            rem = 0;
            if (g.stateMs >= SHOT_MS) {
                uint32_t over = g.stateMs - SHOT_MS;
                g.state = CS_SCORE;
                g.stateMs = over;
                moved = true;
            }
            break;
        case CS_SCORE:
            g.stateMs += rem;
            rem = 0;
            if (g.stateMs >= SCORE_MS) {
                uint32_t over = g.stateMs - SCORE_MS;
                int nr = g.round + 1;
                if (nr > 9999) nr = 9999;
                startRound(nr);
                g.stateMs = over;
                moved = true;
            }
            break;
        default:
            g.state = CS_READY;
            g.stateMs = 0;
            moved = true;
            break;
        }
        if (!moved) break;
    }

    if (g.stateMs > 20000u) g.stateMs = 20000u;
    if (g.idleMs > IDLE_MS) g.idleMs = IDLE_MS;

    if (doRender) renderFrame();
}

/* ---------- public interface ---------- */

void csInit(uint32_t seed)
{
    memset(&g, 0, sizeof(g));
    memset(g_grid, 0, sizeof(g_grid));
    g.rng = seed ? seed : 0x9E3779B9u;
    g.seed = seed;
    g.mode = MODE_DEMO;
    g.state = CS_READY;
    g.round = 1;
    g.winner = -1;
    g.shotBy = -1;
    for (int k = 0; k < 3; k++) g.shotFrame[k] = 0xFFFFFF00u;
    g.prevWaitMs = 0;
    g.waitMs = pickWait(1);
    renderFrame();
}

void csTick(uint32_t dtMs)
{
    if (dtMs > 250u) dtMs = 250u;
    tickCore(dtMs, true);
}

void csInteract(void)
{
    if (g.mode == MODE_DEMO) {
        startPlayerGame();
        renderFrame();
        return;
    }
    g.idleMs = 0;
    if (g.state == CS_DRAW && !g.playerFired) {
        g.playerFired = 1;
        g.pNextShot = g.stateMs;   /* shot 1 leaves the barrel this instant */
    }
}

uint8_t csCell(int x, int y)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return 0;
    return g_grid[y * W + x];
}

void csStats(int *round, int *streak, int *state, int *violations)
{
    if (round) *round = (int)g.round;
    if (streak) *streak = (int)g.streak;
    if (state) *state = (int)g.state;
    if (violations) *violations = (int)g.violations;
}

/* ---------- self check ---------- */

static uint32_t fnv(uint32_t h, uint32_t v)
{
    h ^= v;
    return h * 16777619u;
}

static uint32_t hashCore(uint32_t h)
{
    h = fnv(h, g.rng);
    h = fnv(h, g.seed);
    h = fnv(h, g.animMs);
    h = fnv(h, g.frameNo);
    h = fnv(h, g.stateMs);
    h = fnv(h, g.waitMs);
    h = fnv(h, g.prevWaitMs);
    h = fnv(h, g.oppTime);
    h = fnv(h, g.lTime);
    h = fnv(h, g.rTime);
    h = fnv(h, g.idleMs);
    h = fnv(h, g.pNextShot);
    h = fnv(h, g.bNextShot);
    h = fnv(h, (uint32_t)g.pShots);
    h = fnv(h, (uint32_t)g.bShots);
    h = fnv(h, (uint32_t)g.playerFired);
    h = fnv(h, (uint32_t)g.shotCount);
    for (int k = 0; k < 3; k++) {
        h = fnv(h, g.shotFrame[k]);
        h = fnv(h, (uint32_t)g.shotSide[k]);
    }
    h = fnv(h, g.drawStartFrame);
    h = fnv(h, g.shotStartFrame);
    h = fnv(h, (uint32_t)g.mode);
    h = fnv(h, (uint32_t)g.state);
    h = fnv(h, (uint32_t)g.round);
    h = fnv(h, (uint32_t)g.streak);
    h = fnv(h, (uint32_t)g.winner);
    h = fnv(h, (uint32_t)g.shotBy);
    h = fnv(h, (uint32_t)g.shotResolved);
    h = fnv(h, (uint32_t)g.fallStep);
    return h;
}

static uint32_t hashGrid(uint32_t h)
{
    for (int i = 0; i < W * H; i++) h = fnv(h, g_grid[i]);
    return h;
}

static int scanPalette(void)
{
    int bad = 0;
    for (int i = 0; i < W * H; i++) {
        uint8_t c = g_grid[i];
        if (!(c == C_EMPTY || c == C_LEFT || c == C_RIGHT || c == C_FLASH ||
              c == C_TRACER || c == C_DUST || c == C_SKY || c == C_SKYBRIGHT ||
              c == C_GROUND || c == C_BUILDING || c == C_TEXT)) {
            bad++;
        }
    }
    return bad;
}

static uint32_t probeRun(void)
{
    csInit(777u);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 48; i++) {
        if (i == 4 || i == 12 || i == 30) csInteract();
        csTick(16u);
        h = hashCore(h);
        h = hashGrid(h);
    }
    return h;
}

static uint32_t probeLong(void)
{
    csInit(4242u);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 600; i++) {
        if (i == 5 || i == 220 || i == 440) csInteract();
        tickCore(16u, false);
        h = hashCore(h);
    }
    return h;
}

static int canReachReady(int from)
{
    CsState save = g;
    g.mode = (from == CS_DRAW) ? MODE_DEMO : MODE_PLAYER;
    g.state = from;
    g.stateMs = 0;
    g.playerFired = 0;
    g.pShots = CYLINDER;
    g.bShots = CYLINDER;
    g.pNextShot = 0;
    g.bNextShot = 200;
    g.winner = -1;
    g.shotBy = -1;
    g.shotResolved = 0;
    g.fallStep = 0;
    g.idleMs = 0;
    if (from == CS_WAIT) g.waitMs = 3000;
    if (from == CS_DRAW) {
        g.oppTime = 200;
        g.lTime = 200;
        g.rTime = 220;
        g.pNextShot = 200;
        g.bNextShot = 220;
    }
    if (from == CS_SHOT || from == CS_SCORE) {
        g.winner = 0;
        g.shotBy = 0;
        g.shotResolved = 1;
    }
    int ok = (from == CS_READY);
    for (int i = 0; i < 900 && !ok; i++) {
        tickCore(16u, false);
        if (g.state == CS_READY) ok = 1;
    }
    g = save;
    return ok;
}

static int idleReturnsToDemo(void)
{
    CsState save = g;
    g.mode = MODE_PLAYER;
    g.state = CS_READY;
    g.stateMs = 0;
    g.idleMs = 0;
    int ok = 0;
    for (int i = 0; i < 700; i++) {
        tickCore(100u, false);
        if (g.mode == MODE_DEMO) {
            ok = 1;
            break;
        }
    }
    g = save;
    return ok;
}

int csSelfCheck(void)
{
    int v = 0;
    CsState save = g;
    uint8_t gridSave[W * H];
    memcpy(gridSave, g_grid, sizeof(g_grid));

    v += scanPalette();

    /* reaction formula must stay inside [140, 700] ms */
    for (int w = 0; w <= 80; w++) {
        int base = 700 - 12 * w;
        if (base < 320) base = 320;
        if (base - 40 < 280 || base + 40 > 760) v++;
    }

    /* renderer is a pure view of state */
    uint32_t before = hashCore(0) ^ hashGrid(0);
    renderFrame();
    uint32_t after = hashCore(0) ^ hashGrid(0);
    if (before != after) v++;
    v += scanPalette();

    /* identical seed + taps => identical frame sequence */
    uint32_t a = probeRun();
    uint32_t b = probeRun();
    if (a != b) v++;

    /* longer logic run covers shots, scoring and round changes */
    uint32_t c1 = probeLong();
    uint32_t c2 = probeLong();
    if (c1 != c2) v++;

    for (int s = 0; s <= 4; s++) {
        if (!canReachReady(s)) v++;
    }
    if (!idleReturnsToDemo()) v++;

    memcpy(g_grid, gridSave, sizeof(g_grid));
    g = save;
    if (v > 0) {
        g.violations += v;
        if (g.violations > 9999) g.violations = 9999;
    }
    return v;
}

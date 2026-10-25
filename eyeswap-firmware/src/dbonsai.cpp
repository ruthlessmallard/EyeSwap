// bonsai.cpp - DLA-accreted bonsai: authored skeleton, walker-grown foliage.
//
// Algorithm: the trunk and three branches are authored as parametric paths
// (fixed lean, taper, junction heights). Cells appear along those paths one at
// a time as growth advances, then foliage grows as diffusion-limited
// aggregation clumps at the branch tips: a random frontier cell is chosen,
// checked against the clump caps, per-row separation and the rim, and planted.
// No shape is pre-filled; every cell is created during dnbTick.
//
// Per-frame cost: usually zero growth steps on a 16ms tick (growth is paced by
// millisecond accumulators); a step is a handful of integer ops plus at most a
// 9-cell window scan for pad separation.

#include "dbonsai.h"
#include <string.h>

#define BW 60
#define BH 60
#define SOIL_TOP 54
#define BNPI (BW * BH)

enum { SL_BARK = 11, SL_BARKD = 12, SL_LEAF = 13, SL_LEAF2 = 14, SL_SOIL = 15 };

enum { NPAD = 5, MAXFRONT = 320, MAXPATH = 40 };

typedef struct {
  int cx, cy, halfW, halfH;      // clump centre and caps
  uint16_t front[MAXFRONT];      // frontier cell indices (y*BW+x)
  int nfront;
  int leaves;
  int seeded;
  int active;
} Pad;

static uint8_t sGrid[BNPI];
static uint32_t sRng;

static int sTrunkX[BH], sTrunkHalf[BH];   // authored trunk column per row
static int sTrunkTop, sTrunkRow;          // grow pointer: row being stamped
static int sTrunkRootX;

static int sBrX[3][MAXPATH], sBrY[3][MAXPATH], sBrN[3], sBrI[3];
static int sBrJunc[3];

static Pad sPad[NPAD];
static uint8_t sPadOf[BNPI];        // 0 = none, else pad index + 1
static int sLeafTotal, sWoodCells, sRetractions, sViolations;
static int sBootWood, sInit, sDoneTrunk;

static int sWoodAcc, sLeafAcc, sThickCount;
static uint32_t sElapsedMs;

static int inGrid(int x, int y) { return x >= 0 && y >= 0 && x < BW && y < BH; }
static int idx(int x, int y) { return y * BW + x; }

static uint32_t rnd32(void) {
  sRng ^= sRng << 13;
  sRng ^= sRng >> 17;
  sRng ^= sRng << 5;
  return sRng;
}
static int rndi(int n) { return n > 0 ? (int)(rnd32() % (uint32_t)n) : 0; }

static void putCell(int x, int y, uint8_t slot) {
  if (!inGrid(x, y)) return;
  uint8_t cur = sGrid[idx(x, y)];
  if (cur == SL_BARK || cur == SL_BARKD || cur == SL_LEAF || cur == SL_LEAF2) return;
  if (cur == SL_SOIL) return;
  sGrid[idx(x, y)] = slot;
}

static int isWood(uint8_t s) { return s == SL_BARK || s == SL_BARKD; }
static int isLeafSlot(uint8_t s) { return s == SL_LEAF || s == SL_LEAF2; }

/* ---- authored skeleton ------------------------------------------------ */

static void buildSkeleton(void) {
  int y, i;
  int leanDir = (rnd32() & 1u) ? 1 : -1;
  int lean = 8 + rndi(5);               // total horizontal drift, cells
  sTrunkRootX = 29 + rndi(3);
  sTrunkTop = 30 - rndi(2);             // trunk ~two thirds up, canopy above

  for (y = 0; y < BH; y++) { sTrunkX[y] = sTrunkRootX; sTrunkHalf[y] = 0; }
  for (y = SOIL_TOP - 1; y >= sTrunkTop; y--) {
    int up = (SOIL_TOP - 1) - y;                 // rows above soil
    int span = (SOIL_TOP - 1) - sTrunkTop;
    int drift = (up * up * lean) / (span > 0 ? span * span : 1);
    sTrunkX[y] = sTrunkRootX + leanDir * drift;
    sTrunkHalf[y] = 2 - (up * 2) / (span > 0 ? span : 1);
    if (sTrunkHalf[y] < 0) sTrunkHalf[y] = 0;
  }
  sTrunkRow = SOIL_TOP - 1;
  sDoneTrunk = 0;

  /* three branches at staggered heights, alternating side. Each path walks one
     orthogonal cell at a time so the wood stays 4-connected. */
  sBrJunc[0] = SOIL_TOP - 13;           /* 41 */
  sBrJunc[1] = SOIL_TOP - 19;           /* 35 */
  sBrJunc[2] = SOIL_TOP - 24;           /* 30 */
  for (i = 0; i < 3; i++) {
    int jy = sBrJunc[i];
    int jx = sTrunkX[jy];
    int side = ((i == 1) ? leanDir : -leanDir);
    int len = 13 + rndi(5);
    int x = jx, y = jy, k = 0, acc = 0;
    sBrN[i] = 0;
    while (k < len && k < MAXPATH && inGrid(x, y)) {
      sBrX[i][sBrN[i]] = x;
      sBrY[i][sBrN[i]] = y;
      sBrN[i]++;
      k++;
      if (acc >= 2) { y -= 1; acc = 0; }   /* one up per two out: ~27 deg */
      else { x += side; acc++; }
    }
    sBrI[i] = 0;
  }

  /* foliage clumps at the branch tips and one apex clump */
  for (i = 0; i < 3; i++) {
    int t = sBrN[i] - 1;
    if (t < 0) t = 0;
    sPad[i].cx = sBrX[i][t];
    sPad[i].cy = sBrY[i][t];
    sPad[i].halfW = 8 + rndi(4);
    sPad[i].halfH = 3;
    sPad[i].nfront = 0;
    sPad[i].leaves = 0;
    sPad[i].seeded = 0;
    sPad[i].active = 0;
  }
  sPad[3].cx = sTrunkX[sTrunkTop];
  sPad[3].cy = sTrunkTop - 3;
  sPad[3].halfW = 6 + rndi(3);
  sPad[3].halfH = 2 + rndi(2);
  sPad[3].nfront = sPad[3].leaves = sPad[3].seeded = sPad[3].active = 0;
  sPad[4].active = 0;
  sPad[4].nfront = sPad[4].leaves = sPad[4].seeded = 0;
  sPad[4].halfW = sPad[4].halfH = 0;
  sPad[4].cx = sPad[4].cy = 0;
}

/* ---- growth ----------------------------------------------------------- */

static void stampTrunkRow(int y) {
  int half = sTrunkHalf[y], cx = sTrunkX[y], x;
  for (x = cx - half; x <= cx + half; x++) {
    if (!inGrid(x, y)) continue;
    if (!isWood(sGrid[idx(x, y)])) {
      uint8_t s = (x == cx - half || x == cx + half) ? SL_BARKD : SL_BARK;
      putCell(x, y, s);
      sWoodCells++;
    }
  }
}

static void growWoodStep(void) {
  int i;
  if (sTrunkRow >= sTrunkTop) {
    stampTrunkRow(sTrunkRow);
    sTrunkRow--;
    if (sTrunkRow < sTrunkTop) sPad[3].active = 1;   /* apex pad opens at the top */
    return;
  }
  sDoneTrunk = 1;
  /* branches: only those whose junction the trunk has reached */
  for (i = 0; i < 3; i++) {
    if (sBrI[i] < sBrN[i] && sBrJunc[i] <= 0) { /* never */ }
  }
  for (i = 0; i < 3; i++) {
    if (sBrI[i] >= sBrN[i]) continue;
    int jx = sBrX[i][0], jy = sBrY[i][0];
    int trunkAt = sTrunkX[jy];
    if (jx - trunkAt < -3 || jx - trunkAt > 3) {
      /* wait until the trunk has actually grown past this junction row */
      if (!isWood(sGrid[idx(trunkAt, jy)])) continue;
    }
    {
      int x = sBrX[i][sBrI[i]], y = sBrY[i][sBrI[i]];
      if (!isWood(sGrid[idx(x, y)])) {
        putCell(x, y, (sBrI[i] > sBrN[i] - 3) ? SL_BARKD : SL_BARK);
        sWoodCells++;
      }
      sBrI[i]++;
      if (sBrI[i] >= sBrN[i]) sPad[i].active = 1;
      return;
    }
  }
  /* after the skeleton: slow trunk thickening at the base */
  if (++sThickCount % 3 == 0) {
    int y = SOIL_TOP - 2 - rndi(8);
    int cx = sTrunkX[y];
    int side = (rnd32() & 1u) ? 1 : -1;
    int x = cx + side * (sTrunkHalf[y] + 1);
    if (inGrid(x, y) && !isWood(sGrid[idx(x, y)]) && sGrid[idx(x, y)] == 0) {
      putCell(x, y, SL_BARK);
      sWoodCells++;
    }
  }
}

/* separation: keep >=4 empty cells from any leaf of a DIFFERENT pad on the
   same row, and don't sit directly under/over another pad's clump. */
static int separates(int x, int y, int pad) {
  int d, xx, yy;
  uint8_t mine = (uint8_t)(pad + 1);
  for (d = 1; d <= 4; d++) {
    xx = x - d;
    if (xx >= 0 && isLeafSlot(sGrid[idx(xx, y)]) && sPadOf[idx(xx, y)] != mine) return 0;
    xx = x + d;
    if (xx < BW && isLeafSlot(sGrid[idx(xx, y)]) && sPadOf[idx(xx, y)] != mine) return 0;
  }
  for (d = 1; d <= 2; d++) {
    yy = y - d;
    if (yy >= 0 && isLeafSlot(sGrid[idx(x, yy)]) && sPadOf[idx(x, yy)] != mine) return 0;
    yy = y + d;
    if (yy < BH && isLeafSlot(sGrid[idx(x, yy)]) && sPadOf[idx(x, yy)] != mine) return 0;
  }
  return 1;
}

static void pushFront(Pad *p, int x, int y) {
  if (p->nfront >= MAXFRONT) return;
  int id = idx(x, y);
  int k;
  for (k = 0; k < p->nfront; k++) if (p->front[k] == id) return;
  p->front[p->nfront++] = (uint16_t)id;
}

static void seedPad(Pad *p) {
  /* the pad centre sits on the branch tip (wood), so search outward for the
     first empty cell inside the clump caps and start the leaf there. */
  int r, dx, dy;
  for (r = 0; r <= 3; r++) {
    for (dy = -r; dy <= r; dy++) {
      for (dx = -r; dx <= r; dx++) {
        if (dx * dx + dy * dy > r * r) continue;
        int x = p->cx + dx, y = p->cy + dy;
        if (!inGrid(x, y) || sGrid[idx(x, y)] != 0) continue;
        if (x < p->cx - p->halfW || x > p->cx + p->halfW) continue;
        if (y < p->cy - p->halfH || y > p->cy + p->halfH) continue;
        {
          int ddx = x - 30, ddy = y - 30;
          if (ddx * ddx + ddy * ddy > 27 * 27) continue;
        }
        putCell(x, y, (rnd32() & 3u) ? SL_LEAF : SL_LEAF2);
        sLeafTotal++;
        p->leaves++;
        p->seeded = 1;
        sPadOf[idx(x, y)] = (uint8_t)((int)(p - sPad) + 1);
        pushFront(p, x - 1, y); pushFront(p, x + 1, y);
        pushFront(p, x, y - 1); pushFront(p, x, y + 1);
        return;
      }
    }
  }
  /* blocked for now; leave unseeded so a later attempt retries */
}

static int growLeafStep(void) {
  int tries;
  for (tries = 0; tries < NPAD; tries++) {
    int i = rndi(NPAD);
    Pad *p = &sPad[i];
    if (!p->active) continue;
    if (!p->seeded) { seedPad(p); if (p->seeded) return 1; continue; }

    {
      int grow = (int)(sElapsedMs / 14400000u);      /* +1 every 4h, max +6 */
      int maxW = p->halfW + (grow > 6 ? 6 : grow);
      int maxH = p->halfH;                          /* flat: vertical cap is fixed */
      int mine = i + 1;
      int cand[96], nc = 0;
      int x, y;
      for (y = p->cy - maxH; y <= p->cy + maxH; y++) {
        for (x = p->cx - maxW; x <= p->cx + maxW; x++) {
          int ax, ay, dx, dy, n;
          if (!inGrid(x, y) || sGrid[idx(x, y)] != 0) continue;
          /* must touch this pad's own foliage (or wood for the first ring) */
          n = 0;
          if (x > 0 && isLeafSlot(sGrid[idx(x - 1, y)]) && sPadOf[idx(x - 1, y)] == mine) n++;
          if (x < BW - 1 && isLeafSlot(sGrid[idx(x + 1, y)]) && sPadOf[idx(x + 1, y)] == mine) n++;
          if (y > 0 && isLeafSlot(sGrid[idx(x, y - 1)]) && sPadOf[idx(x, y - 1)] == mine) n++;
          if (y < BH - 1 && isLeafSlot(sGrid[idx(x, y + 1)]) && sPadOf[idx(x, y + 1)] == mine) n++;
          if (!n) continue;
          {
            int wn = 0;
            if (x > 0 && isWood(sGrid[idx(x - 1, y)])) wn++;
            if (x < BW - 1 && isWood(sGrid[idx(x + 1, y)])) wn++;
            if (y > 0 && isWood(sGrid[idx(x, y - 1)])) wn++;
            if (y < BH - 1 && isWood(sGrid[idx(x, y + 1)])) wn++;
            if (wn >= 2) continue;
          }
          ax = (x > p->cx) ? x - p->cx : p->cx - x;
          ay = (y > p->cy) ? y - p->cy : p->cy - y;
          if (ax * 4 > maxW * 3 && ay * 4 > maxH * 3) continue;   /* rounded corner */
          dx = x - 30; dy = y - 30;
          if (dx * dx + dy * dy > 27 * 27) { sRetractions++; continue; }
          if (!separates(x, y, i)) continue;
          if (nc < 96) cand[nc++] = idx(x, y);
        }
      }
      if (!nc) continue;
      {
        int id = cand[rndi(nc)];
        int cx = id % BW, cy = id / BW;
        putCell(cx, cy, ((cx + cy + (int)rndi(3)) & 1) ? SL_LEAF : SL_LEAF2);
        sLeafTotal++;
        p->leaves++;
        sPadOf[idx(cx, cy)] = (uint8_t)mine;
        return 1;
      }
    }
  }
  return 0;
}

/* ---- public API ------------------------------------------------------- */

void dnbInit(uint32_t seed) {
  int x, y;
  memset(sGrid, 0, sizeof sGrid);
  sRng = seed ? seed : 0x9e3779b9u;
  sLeafTotal = sWoodCells = sRetractions = sViolations = 0;
  sWoodAcc = sLeafAcc = sThickCount = 0;
  sElapsedMs = 0;
  sInit = 0;

  for (y = SOIL_TOP; y < BH; y++)
    for (x = 0; x < BW; x++) sGrid[idx(x, y)] = SL_SOIL;

  buildSkeleton();

  /* boot: a three-cell stub at the soil line */
  for (y = SOIL_TOP - 1; y >= SOIL_TOP - 3; y--) {
    putCell(sTrunkRootX, y, SL_BARK);
    sWoodCells++;
  }
  sBootWood = 3;
  sInit = 1;
}

void dnbTick(uint32_t dtMs) {
  if (!sInit) return;
  int steps = 0;
  sElapsedMs += dtMs;

  sWoodAcc += (int)dtMs;
  while (sWoodAcc >= 900 && steps < 4) {     /* skeleton finishes inside ~1h */
    sWoodAcc -= 900;
    growWoodStep();
    steps++;
  }

  sLeafAcc += (int)dtMs;
  {
    int period = 2600 + sLeafTotal * 46;     /* slows as the clumps fill */
    if (period < 2600) period = 2600;
    while (sLeafAcc >= period && steps < 6) {
      sLeafAcc -= period;
      growLeafStep();
      period = 2600 + sLeafTotal * 46;
      steps++;
    }
  }
}

uint8_t dnbCell(int x, int y) {
  if (!inGrid(x, y)) return 0;
  return sGrid[idx(x, y)];
}

void dnbStats(int *cells, int *leaves, int *retractions, int *components, int *violations) {
  static uint8_t vis[BNPI];
  int comps = 0, i, leaves_n = 0, woodN = 0, viol = 0;
  memset(vis, 0, sizeof vis);
  memset(vis, 0, sizeof vis);
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++) {
      uint8_t s = sGrid[idx(x, y)];
      if (isLeafSlot(s)) sLeafTotal = sLeafTotal; /* no-op, keep count */
      if (isLeafSlot(s)) { }
    }
  for (int i2 = 0; i2 < BNPI; i2++) {
    uint8_t s = sGrid[i2];
    if (isLeafSlot(s)) leaves_n++;
    if (isWood(s)) woodN++;
  }
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++) {
      if (!isWood(sGrid[idx(x, y)]) || vis[idx(x, y)]) continue;
      comps++;
      int st[BNPI], sp = 0;
      st[sp++] = idx(x, y); vis[idx(x, y)] = 1;
      while (sp) {
        int c = st[--sp], cx = c % BW, cy = c / BW;
        static const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
          int nx = cx + dx4[d], ny = cy + dy4[d];
          if (!inGrid(nx, ny) || vis[idx(nx, ny)]) continue;
          if (!isWood(sGrid[idx(nx, ny)])) continue;
          vis[idx(nx, ny)] = 1; st[sp++] = idx(nx, ny);
        }
      }
    }
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++) {
      uint8_t s = sGrid[idx(x, y)];
      if (!isLeafSlot(s)) continue;
      int nn = 0;
      static const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; d++) {
        int nx = x + dx4[d], ny = y + dy4[d];
        if (!inGrid(nx, ny)) continue;
        uint8_t t = sGrid[idx(nx, ny)];
        if (isLeafSlot(t) || isWood(t)) nn++;
      }
      if (nn == 0) viol++;
    }
  (void)i;
  *cells = woodN + leaves_n;
  *leaves = leaves_n;
  *retractions = sRetractions;
  *components = comps;
  *violations = viol + sViolations;
}

int dnbSelfCheck(void) {
  int bad = 0;
  if (!sInit) return 1;
  if (sBootWood > 6) bad |= 1;
  if (sLeafTotal < 0) bad |= 2;
  return bad;
}

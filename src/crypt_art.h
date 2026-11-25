// Crypt: the palette and the procedurally generated art (wall / floor textures,
// sprites, the first-person claw). Everything is drawn once into RAM when the
// game opens (about 24 KB, freed when it closes), so the art costs almost no
// flash. Images are 8-bit palette indices stored column by column (a vertical
// screen slice reads one contiguous column); index 0 is transparent in sprites.
// Included by crypt.cpp only.
#pragma once
#include <Arduino.h>

namespace cryptgame {

// ---------------- palette ----------------
// 64 entries in ramps, dark -> light
enum {
  CI_STONE = 1,  // 8
  CI_MOSS = 9,   // 6
  CI_BRICK = 15, // 6
  CI_CORAL = 21, // 6
  CI_WOOD = 27,  // 6
  CI_GOLD = 33,  // 4
  CI_BLUE = 37,  // 6
  CI_GLOW = 43,  // 4, emissive (cycled: portals, runes, pearls)
  CI_RED = 47,   // 4
  CI_BONE = 51,  // 4
  CI_INK = 55,   // 4
  CI_FIRE = 59,  // 4, emissive (flickers)
  CI_WHITE = 63, // emissive
  CI_COUNT = 64
};

static uint8_t crPal[CI_COUNT][3];

static void crRamp(int i0, int n, int r0, int g0, int b0, int r1, int g1, int b1) {
  for (int k = 0; k < n; k++) {
    crPal[i0 + k][0] = r0 + (r1 - r0) * k / (n - 1);
    crPal[i0 + k][1] = g0 + (g1 - g0) * k / (n - 1);
    crPal[i0 + k][2] = b0 + (b1 - b0) * k / (n - 1);
  }
}

static void crBuildPalette() {
  crPal[0][0] = crPal[0][1] = crPal[0][2] = 0;
  crRamp(CI_STONE, 8, 16, 20, 28, 158, 168, 180);
  crRamp(CI_MOSS, 6, 10, 30, 14, 118, 178, 70);
  crRamp(CI_BRICK, 6, 40, 20, 28, 178, 112, 90);
  crRamp(CI_CORAL, 6, 92, 20, 46, 255, 168, 132);
  crRamp(CI_WOOD, 6, 34, 20, 10, 160, 104, 54);
  crRamp(CI_GOLD, 4, 124, 78, 0, 255, 234, 122);
  crRamp(CI_BLUE, 6, 6, 22, 66, 100, 190, 255);
  crRamp(CI_GLOW, 4, 0, 104, 128, 190, 255, 255);
  crRamp(CI_RED, 4, 92, 4, 14, 255, 92, 60);
  crRamp(CI_BONE, 4, 112, 102, 84, 242, 236, 212);
  crRamp(CI_INK, 4, 20, 6, 32, 130, 66, 170);
  crRamp(CI_FIRE, 4, 214, 52, 0, 255, 250, 164);
  crPal[CI_WHITE][0] = crPal[CI_WHITE][1] = crPal[CI_WHITE][2] = 255;
}

static inline bool crEmissive(int i) { return (i >= CI_GLOW && i < CI_GLOW + 4) || i >= CI_FIRE; }

// ---------------- image directory ----------------
enum {
  TX_STONE, TX_MOSS, TX_CORAL, TX_RUNE, TX_DOOR, TX_GDOOR, TX_BDOOR, TX_EXIT,   // walls = cell code - 1
  TX_FLOOR, TX_SAND, TX_CEIL, TX_COUNT
};
static const int CR_TEX = 32;                  // textures are 32 x 32
static const int CR_TEX_BYTES = CR_TEX * CR_TEX;

enum {
  SP_DROWN0, SP_DROWN1, SP_JELLY0, SP_JELLY1, SP_LURK0, SP_LURK1,     // 32 x 32
  SP_HEALTH, SP_MANA, SP_GKEY, SP_BKEY, SP_GOLD, SP_WAND,             // 16 x 16
  SP_TORCH0, SP_TORCH1, SP_PILLAR, SP_KELP0, SP_KELP1,                // 16 x 32
  SP_BARREL, SP_BONES, SP_BOLT, SP_INK, SP_PUFF,                      // 16 x 16
  SP_HAND,                                                            // 48 x 48
  SP_COUNT
};
static const uint8_t SP_SIZE[SP_COUNT][2] = {
  {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32},
  {16, 16}, {16, 16}, {16, 16}, {16, 16}, {16, 16}, {16, 16},
  {16, 32}, {16, 32}, {16, 32}, {16, 32}, {16, 32},
  {16, 16}, {16, 16}, {16, 16}, {16, 16}, {16, 16},
  {48, 48}};

static int crSpriteBytes(uint16_t *off) {
  int o = 0;
  for (int i = 0; i < SP_COUNT; i++) {
    if (off) off[i] = o;
    o += SP_SIZE[i][0] * SP_SIZE[i][1];
  }
  return o;
}

// ---------------- noise ----------------
static uint32_t crHash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
static int crRnd(int x, int y, int seed) {
  return crHash((uint32_t)x * 374761393U + (uint32_t)y * 668265263U + (uint32_t)seed * 2246822519U) & 255;
}
// value noise that tiles every 32 px; c = lattice cell size (divides 32)
static int crNoise(int x, int y, int c, int seed) {
  int n = CR_TEX / c, gx = x / c, gy = y / c, fx = x % c, fy = y % c;
  int a = crRnd(gx % n, gy % n, seed), b = crRnd((gx + 1) % n, gy % n, seed);
  int d = crRnd(gx % n, (gy + 1) % n, seed), e = crRnd((gx + 1) % n, (gy + 1) % n, seed);
  int top = a + (b - a) * fx / c, bot = d + (e - d) * fx / c;
  return top + (bot - top) * fy / c;
}
static int crFbm(int x, int y, int seed) {
  return (crNoise(x, y, 16, seed) * 2 + crNoise(x, y, 8, seed + 1) + crNoise(x, y, 4, seed + 2)) / 4;
}
static inline uint8_t crShadeOf(int base, int n, int v) { return base + (v < 0 ? 0 : v >= n ? n - 1 : v); }

// ---------------- textures ----------------
static inline void crTput(uint8_t *t, int x, int y, uint8_t c) { t[(x & 31) * CR_TEX + (y & 31)] = c; }
static inline uint8_t crTget(const uint8_t *t, int x, int y) { return t[(x & 31) * CR_TEX + (y & 31)]; }

// big stone blocks, two per row, staggered
static void crGenStone(uint8_t *t, int seed) {
  for (int y = 0; y < CR_TEX; y++) {
    int row = y >> 3, off = (row & 1) * 8 + (crRnd(row, 0, seed) & 3);
    for (int x = 0; x < CR_TEX; x++) {
      int bx = (x + off) & 31, blk = bx >> 4, ex = bx & 15, ey = y & 7;
      int n = crFbm(x, y, seed), v;
      if (ey == 0 || ex == 0) v = n > 150 ? 1 : 0;           // mortar
      else {
        v = 3 + crRnd(blk, row, seed + 7) % 3 + (n - 128) / 48;
        if (ey == 1 || ex == 1) v++;                            // lit top-left edge
        if (ey == 7 || ex == 15) v--;                           // shaded bottom-right edge
        if (crRnd(x, y, seed + 3) < 22) v--;                    // pits
      }
      crTput(t, x, y, crShadeOf(CI_STONE, 8, v));
    }
  }
  // one hairline crack
  for (int k = 0; k < 1; k++) {
    int x = crRnd(k, 9, seed) & 31, y = crRnd(k, 11, seed) & 31;
    for (int i = 0; i < 5; i++) {
      crTput(t, x, y, CI_STONE);
      x += (crRnd(i, k, seed + 5) % 3) - 1;
      y++;
    }
  }
}

// small bricks with moss creeping down from the top
static void crGenMoss(uint8_t *t) {
  for (int y = 0; y < CR_TEX; y++) {
    int row = y >> 2, off = (row & 1) * 4;
    for (int x = 0; x < CR_TEX; x++) {
      int bx = (x + off) & 31, ex = bx & 7, ey = y & 3;
      int n = crFbm(x, y, 21), v;
      uint8_t c;
      if (ey == 0 || ex == 0) c = crShadeOf(CI_STONE, 8, 1 + (n > 160));
      else {
        v = 2 + crRnd(bx >> 3, row, 23) % 2 + (n - 128) / 56;
        if (ey == 1) v++;
        c = crShadeOf(CI_BRICK, 6, v);
      }
      int drip = crRnd(x, 0, 41) > 150 ? (crRnd(x, 1, 41) >> 3) : 0;
      int m = crNoise(x, y, 8, 40) + (12 - y) * 9 + drip * 6;
      if (m > 160) c = crShadeOf(CI_MOSS, 6, (m - 160) / 22 + (crRnd(x, y, 42) > 200));
      crTput(t, x, y, c);
    }
  }
}

// organic coral growth: overlapping lit blobs with polyp holes
static void crGenCoral(uint8_t *t) {
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) crTput(t, x, y, crShadeOf(CI_INK, 4, crNoise(x, y, 8, 30) / 100));
  for (int b = 0; b < 16; b++) {
    int cx = crRnd(b, 1, 31) & 31, cy = crRnd(b, 2, 31) & 31, r = 4 + crRnd(b, 3, 31) % 4;
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        int d2 = dx * dx + dy * dy;
        if (d2 > r * r) continue;
        // light from the top-left
        int v = 5 - d2 * 4 / (r * r) - (dx + dy > 0 ? 1 : 0);
        uint8_t c = crShadeOf(CI_CORAL, 6, v);
        if (((cx + dx) * 7 + (cy + dy) * 3) % 11 == 0 && d2 < r * r - 2) c = CI_CORAL;   // polyps
        crTput(t, cx + dx, cy + dy, c);
      }
  }
}

// carved stone with a glowing glyph (its glow is palette-cycled)
static void crGenRune(uint8_t *t) {
  crGenStone(t, 77);
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) {
      int dx = x - 16, dy = y - 15, d2 = dx * dx + dy * dy;
      bool g = (d2 >= 36 && d2 <= 56) ||                         // ring
               (x == 16 && y >= 5 && y <= 26) ||                 // stem
               ((y == 10 || y == 21) && dx >= -3 && dx <= 3) ||  // bars
               (y == 15 && (dx == -9 || dx == 9));               // ticks
      if (g) crTput(t, x, y, CI_GLOW + ((x + y) >> 1 & 3));
      else if (d2 >= 30 && d2 <= 64) crTput(t, x, y, CI_STONE);  // carved groove
    }
}

// plank door with iron bands; emblem 0 = plain, else a key plate in that ramp
static void crGenDoor(uint8_t *t, int emblem) {
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) {
      uint8_t c;
      if (x < 2 || x > 29 || y < 2) c = crShadeOf(CI_STONE, 8, (x == 0 || x == 31 || y == 0) ? 1 : 3 + (crRnd(x, y, 60) > 180));
      else {
        int p = (x - 2) % 7;
        if (p == 0) c = CI_WOOD;
        else {
          int g = crNoise((x * 5) & 31, y, 4, 61);
          c = crShadeOf(CI_WOOD, 6, 2 + ((x - 2) / 7 & 1) + (g - 128) / 60 + (p == 1));
        }
        if ((y >= 6 && y <= 8) || (y >= 23 && y <= 25)) {
          c = crShadeOf(CI_STONE, 8, y == 6 || y == 23 ? 5 : 3);
          if (p == 3 && (y == 7 || y == 24)) c = CI_STONE + 7;   // rivets
        }
      }
      crTput(t, x, y, c);
    }
  if (!emblem) return;
  int n = emblem == CI_GOLD ? 4 : 6;
  for (int y = 11; y <= 20; y++)
    for (int x = 11; x <= 20; x++) {
      int v = n - 2 + (x == 11 || y == 11) - (x == 20 || y == 20) * 2;
      crTput(t, x, y, crShadeOf(emblem, n, v));
    }
  for (int y = 13; y <= 18; y++)
    for (int x = 14; x <= 17; x++) {
      int dx = x * 2 - 31, dy = y * 2 - 29;
      if (dx * dx + dy * dy <= 10 || (y >= 15 && (x == 15 || x == 16))) crTput(t, x, y, CI_INK);
    }
}

// the exit: a gold-trimmed arch around a swirling portal (palette-cycled)
static void crGenExit(uint8_t *t) {
  crGenStone(t, 99);
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) {
      float dx = x - 15.5f, dy = y - 13.0f;
      float r = sqrtf(dx * dx + dy * dy);
      bool in = (y >= 13 && x >= 6 && x <= 25) || (y < 13 && r <= 10.0f);
      bool rim = (y >= 13 && (x == 4 || x == 5 || x == 26 || x == 27)) || (y < 13 && r > 10.0f && r <= 12.0f);
      if (y >= 31) { in = false; rim = x >= 4 && x <= 27; }
      if (in) {
        int k = (int)(r * 0.9f + atan2f(dy, dx) * 2.55f + 32) & 3;
        crTput(t, x, y, r < 2.5f ? CI_WHITE : CI_GLOW + k);
      } else if (rim) crTput(t, x, y, crShadeOf(CI_GOLD, 4, (x + y) % 3 == 0 ? 3 : 1));
    }
}

// wet flagstones with puddles
static void crGenFloor(uint8_t *t) {
  for (int y = 0; y < CR_TEX; y++) {
    int row = y >> 4, off = (row & 1) * 8;
    for (int x = 0; x < CR_TEX; x++) {
      int bx = (x + off) & 31, ex = bx & 15, ey = y & 15;
      int n = crFbm(x, y, 50);
      uint8_t c;
      if (ex == 0 || ey == 0) c = CI_STONE;
      else c = crShadeOf(CI_STONE, 8, 2 + crRnd(bx >> 4, row, 51) % 2 + (n - 128) / 50 + (ex == 1 || ey == 1));
      int w = crFbm(x, y, 55);
      if (w > 172) c = w > 190 ? CI_BLUE + 2 : CI_BLUE + 1;
      crTput(t, x, y, c);
    }
  }
}

// rippled sand with pebbles and shell bits
static void crGenSand(uint8_t *t) {
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) {
      int n = crFbm(x, y, 70);
      int rip = ((y * 2 + (n >> 5)) % 8) < 2;
      uint8_t c = rip ? CI_BONE + 1 : (n > 140 ? CI_STONE + 4 : CI_BONE);
      int s = crRnd(x, y, 71);
      if (s < 6) c = CI_STONE + 3;
      else if (s < 9) c = CI_CORAL + 4;
      crTput(t, x, y, c);
    }
}

// rough dark rock overhead with moss drips
static void crGenCeil(uint8_t *t) {
  for (int y = 0; y < CR_TEX; y++)
    for (int x = 0; x < CR_TEX; x++) {
      int n = crFbm(x, y, 80);
      uint8_t c = crShadeOf(CI_STONE, 8, (n - 60) / 50);
      if (crNoise(x, y, 4, 81) > 215) c = CI_STONE;
      if (crRnd(x, y, 82) < 10) c = CI_MOSS + 1;
      crTput(t, x, y, c);
    }
}

static void crGenTextures(uint8_t *tex) {
  crGenStone(tex + TX_STONE * CR_TEX_BYTES, 11);
  crGenMoss(tex + TX_MOSS * CR_TEX_BYTES);
  crGenCoral(tex + TX_CORAL * CR_TEX_BYTES);
  crGenRune(tex + TX_RUNE * CR_TEX_BYTES);
  crGenDoor(tex + TX_DOOR * CR_TEX_BYTES, 0);
  crGenDoor(tex + TX_GDOOR * CR_TEX_BYTES, CI_GOLD);
  crGenDoor(tex + TX_BDOOR * CR_TEX_BYTES, CI_BLUE);
  crGenExit(tex + TX_EXIT * CR_TEX_BYTES);
  crGenFloor(tex + TX_FLOOR * CR_TEX_BYTES);
  crGenSand(tex + TX_SAND * CR_TEX_BYTES);
  crGenCeil(tex + TX_CEIL * CR_TEX_BYTES);
}

// ---------------- sprite canvas ----------------
struct CrCv {
  uint8_t *p;
  int w, h;
};
static inline void cvPut(CrCv &c, int x, int y, uint8_t v) {
  if ((unsigned)x < (unsigned)c.w && (unsigned)y < (unsigned)c.h) c.p[x * c.h + y] = v;
}
// filled ellipse; n > 1 shades it as a lit ball over ramp [base, base + n)
static void cvEll(CrCv &c, float cx, float cy, float rx, float ry, int base, int n, float yMax = 1e9f) {
  for (int y = (int)(cy - ry); y <= (int)(cy + ry) + 1; y++) {
    if (y + 0.5f > yMax) break;
    for (int x = (int)(cx - rx); x <= (int)(cx + rx) + 1; x++) {
      float nx = (x + 0.5f - cx) / rx, ny = (y + 0.5f - cy) / ry, d = nx * nx + ny * ny;
      if (d > 1.0f) continue;
      if (n <= 1) { cvPut(c, x, y, base); continue; }
      float l = -0.45f * nx - 0.55f * ny + 0.70f * sqrtf(1.0f - d);
      int v = (int)((l + 0.35f) / 1.3f * n);
      cvPut(c, x, y, crShadeOf(base, n, v));
    }
  }
}
static void cvLine(CrCv &c, float x0, float y0, float x1, float y1, float r, uint8_t v) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf(dx * dx + dy * dy) * 2) + 1;
  for (int i = 0; i <= n; i++) {
    float x = x0 + dx * i / n, y = y0 + dy * i / n;
    if (r < 0.75f) cvPut(c, (int)x, (int)y, v);
    else cvEll(c, x, y, r, r, v, 1);
  }
}
static void cvRect(CrCv &c, int x0, int y0, int x1, int y1, uint8_t v) {
  for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++) cvPut(c, x, y, v);
}
// dark outline around the opaque pixels, so sprites read against busy walls
static void cvOutline(CrCv &c, uint8_t v) {
  for (int x = 0; x < c.w; x++)
    for (int y = 0; y < c.h; y++) {
      if (c.p[x * c.h + y]) continue;
      bool near = (x > 0 && c.p[(x - 1) * c.h + y] && c.p[(x - 1) * c.h + y] != 255) ||
                  (x < c.w - 1 && c.p[(x + 1) * c.h + y] && c.p[(x + 1) * c.h + y] != 255) ||
                  (y > 0 && c.p[x * c.h + y - 1] && c.p[x * c.h + y - 1] != 255) ||
                  (y < c.h - 1 && c.p[x * c.h + y + 1] && c.p[x * c.h + y + 1] != 255);
      if (near) c.p[x * c.h + y] = 255;      // mark first, so marks don't spread
    }
  for (int i = 0; i < c.w * c.h; i++) if (c.p[i] == 255) c.p[i] = v;
}

// ---------------- sprites ----------------
// shambling drowned sailor: melee
static void crDrawDrowner(CrCv &c, int f) {
  float s = f ? 1.8f : -1.8f;
  cvLine(c, 13, 21, 12 + s, 29, 1.6f, CI_MOSS + 1);
  cvLine(c, 19, 21, 20 - s, 29, 1.6f, CI_MOSS + 1);
  cvEll(c, 12 + s, 30.2f, 2.6f, 1.4f, CI_BONE, 1);
  cvEll(c, 20 - s, 30.2f, 2.6f, 1.4f, CI_BONE, 1);
  cvEll(c, 16, 15.5f, 7.0f, 7.5f, CI_MOSS + 1, 5);
  for (int i = 0; i < 7; i++)                                    // rags
    cvLine(c, 10 + i * 2, 19, 10 + i * 2 + (i * 7) % 3 - 1, 22 + (i * 5) % 4, 0.6f, CI_INK + 1);
  float a = f ? -2.0f : 1.0f;                                    // arms reaching out
  cvLine(c, 10, 12, 5, 18 + a, 1.5f, CI_MOSS + 2);
  cvLine(c, 22, 12, 27, 18 - a, 1.5f, CI_MOSS + 2);
  cvEll(c, 5, 19.5f + a, 1.9f, 1.9f, CI_BONE + 1, 1);
  cvEll(c, 27, 19.5f - a, 1.9f, 1.9f, CI_BONE + 1, 1);
  cvEll(c, 16, 7, 5, 5, CI_BONE, 4);                             // head
  cvRect(c, 13, 6, 14, 7, CI_FIRE + 3);                          // glowing eyes
  cvRect(c, 18, 6, 19, 7, CI_FIRE + 3);
  cvLine(c, 14, 10, 18, 10, 0.5f, CI_INK);
  cvLine(c, 12, 3, 10, 13, 0.5f, CI_MOSS + 3);                   // seaweed hair
  cvLine(c, 20, 3, 23, 12, 0.5f, CI_MOSS + 2);
  cvLine(c, 16, 2, 14, 5, 0.5f, CI_MOSS + 4);
  cvOutline(c, CI_INK);
}

// ghostly jellyfish: floats, spits ink
static void crDrawJelly(CrCv &c, int f) {
  for (int i = 0; i < 5; i++) {
    float x0 = 8 + i * 4;
    for (int y = 15; y < 31; y++) {
      float x = x0 + sinf((y + f * 3 + i * 2) * 0.55f) * 1.6f;
      cvPut(c, (int)x, y, (i & 1) ? CI_INK + 3 : CI_BLUE + 3);
      if (y < 22) cvPut(c, (int)x + 1, y, CI_INK + 2);
    }
  }
  cvEll(c, 16, 14, 12, 11, CI_INK + 1, 3, 16.0f);
  for (int y = 6; y < 16; y++)                                   // see-through shimmer
    for (int x = 6; x < 27; x++)
      if (((x + y) & 1) == 0 && c.p[x * c.h + y] && y > 9) cvPut(c, x, y, CI_BLUE + 4);
  cvLine(c, 5, 15, 27, 15, 0.5f, CI_BLUE + 5);                   // rim
  cvEll(c, 10, 8, 1.2f, 1.2f, CI_GLOW + 2, 1);
  cvEll(c, 22, 7, 1.2f, 1.2f, CI_GLOW + 1, 1);
  cvEll(c, 16, 5, 1.2f, 1.2f, CI_GLOW + 3, 1);
  cvEll(c, 12, 11.5f, 2.2f, 2.6f, CI_INK, 1);                    // hollow eyes
  cvEll(c, 20, 11.5f, 2.2f, 2.6f, CI_INK, 1);
  cvPut(c, 12, 12, CI_GLOW + 3);
  cvPut(c, 20, 12, CI_GLOW + 3);
  cvOutline(c, CI_INK);
}

// lantern-lure lurker: tough, spits ink, bites
static void crDrawLurker(CrCv &c, int f) {
  cvLine(c, 5, 19, 1, 12, 1.3f, CI_CORAL + 1);                   // fins
  cvLine(c, 27, 19, 31, 13, 1.3f, CI_CORAL + 1);
  cvEll(c, 16, 18, 13, 11, CI_BLUE, 4);
  for (int y = 21; y < 29; y++)
    for (int x = 7; x < 26; x++)
      if (((x + y) & 1) && c.p[x * c.h + y]) cvPut(c, x, y, CI_BONE);
  float m = 2.5f + f * 1.6f;
  cvEll(c, 16, 22.5f, 9, m, CI_INK, 1);                          // jaws
  for (int x = 9; x <= 23; x += 2) {
    cvPut(c, x, (int)(22.5f - m) + 1, CI_BONE + 3);
    cvPut(c, x, (int)(22.5f - m) + 2, CI_BONE + 2);
    cvPut(c, x + 1, (int)(22.5f + m) - 1, CI_BONE + 3);
  }
  cvEll(c, 21, 12.5f, 3.4f, 3.4f, CI_BONE + 2, 2);               // eye
  cvEll(c, 21.8f, 12.5f, 1.4f, 2.0f, CI_FIRE + 2, 1);
  for (int i = 0; i <= 12; i++) {                                // lure stalk
    float u = i / 12.0f;
    cvPut(c, (int)(13 - 7 * u), (int)(8 - 6 * sinf(u * 2.8f)), CI_STONE + 5);
  }
  cvEll(c, 6, 3, 2.6f, 2.6f, CI_GLOW + 2, 1);
  cvPut(c, 6, 3, CI_WHITE);
  cvOutline(c, CI_INK);
}

static void crDrawKey(CrCv &c, int base, int n) {
  for (int a = 0; a < 40; a++) {
    float t = a * 6.2832f / 40;
    cvPut(c, (int)(4.5f + cosf(t) * 3.3f), (int)(8 + sinf(t) * 3.3f), crShadeOf(base, n, a < 20 ? n - 1 : n - 2));
  }
  cvLine(c, 8, 8, 15, 8, 0.9f, base + n - 2);
  cvRect(c, 12, 9, 13, 11, base + n - 3);
  cvRect(c, 14, 9, 15, 10, base + n - 3);
  cvOutline(c, CI_INK);
}

static void crDrawTorch(CrCv &c, int f) {
  cvRect(c, 7, 13, 8, 30, CI_STONE + 4);
  cvRect(c, 9, 13, 9, 30, CI_STONE + 2);
  cvLine(c, 8, 27, 3, 31, 0.6f, CI_STONE + 3);
  cvLine(c, 8, 27, 13, 31, 0.6f, CI_STONE + 3);
  cvEll(c, 8, 12.5f, 6, 2.6f, CI_STONE + 2, 5);
  cvEll(c, 8, 11.5f, 4.5f, 1.2f, CI_FIRE, 1);
  float s = f ? 0.9f : -0.9f;
  cvEll(c, 8 + s * 0.5f, 6.0f, 5.0f, 6.5f, CI_FIRE, 1, 11.5f);
  cvEll(c, 8 + s, 7.0f, 3.4f, 4.6f, CI_FIRE + 1, 1, 11.5f);
  cvEll(c, 8, 8.5f, 2.0f, 3.0f, CI_FIRE + 3, 1, 11.5f);
  cvPut(c, (int)(8 + s * 2), 0, CI_FIRE + 1);
  cvOutline(c, CI_INK);
}

// a shaded tube from (x0, y0) to (x1, y1), radius r0 -> r1
static void cvTube(CrCv &c, float x0, float y0, float x1, float y1, float r0, float r1, int base, int n) {
  float dx = x1 - x0, dy = y1 - y0;
  int steps = (int)(sqrtf(dx * dx + dy * dy) * 2) + 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    cvEll(c, x0 + dx * t, y0 + dy * t, r0 + (r1 - r0) * t, r0 + (r1 - r0) * t, base, n);
  }
}

// first person: Tigo's big claw gripping the coral wand
static void crDrawHand(CrCv &c) {
  cvTube(c, 33, 47, 20, 10, 2.6f, 1.8f, CI_CORAL + 1, 5);        // the wand: a coral branch
  cvTube(c, 25, 26, 17, 18, 1.2f, 0.8f, CI_CORAL + 2, 4);
  cvTube(c, 27, 31, 33, 22, 1.1f, 0.7f, CI_CORAL + 1, 4);
  cvEll(c, 19.5f, 8, 5, 5, CI_GLOW, 1);                          // the pearl on top
  cvEll(c, 19, 7.5f, 3.6f, 3.6f, CI_GLOW + 2, 1);
  cvEll(c, 18.3f, 6.6f, 1.6f, 1.6f, CI_WHITE, 1);
  cvEll(c, 37, 41, 13, 10, CI_RED, 4);                           // claw palm
  cvTube(c, 30, 38, 19, 33, 5.0f, 2.2f, CI_RED, 4);              // fixed finger under the wand
  cvTube(c, 36, 32, 23, 26, 4.6f, 1.8f, CI_RED + 1, 3);          // moving finger over it
  cvTube(c, 23, 26, 20, 29, 1.8f, 1.2f, CI_RED + 1, 3);          // its tip hooks down
  cvPut(c, 40, 36, CI_CORAL + 5); cvPut(c, 44, 40, CI_CORAL + 5); cvPut(c, 35, 44, CI_CORAL + 5);
  cvPut(c, 30, 30, CI_CORAL + 5); cvPut(c, 25, 35, CI_CORAL + 5); cvPut(c, 41, 37, CI_CORAL + 4);
  cvOutline(c, CI_INK);
}

static void crGenSprites(uint8_t *mem, const uint16_t *off) {
  for (int i = 0; i < SP_COUNT; i++) {
    CrCv c = {mem + off[i], SP_SIZE[i][0], SP_SIZE[i][1]};
    memset(c.p, 0, c.w * c.h);
    switch (i) {
      case SP_DROWN0: case SP_DROWN1: crDrawDrowner(c, i - SP_DROWN0); break;
      case SP_JELLY0: case SP_JELLY1: crDrawJelly(c, i - SP_JELLY0); break;
      case SP_LURK0: case SP_LURK1: crDrawLurker(c, i - SP_LURK0); break;
      case SP_HEALTH:                                            // red tonic flask
        cvEll(c, 8, 10.5f, 5, 5, CI_RED, 4);
        cvRect(c, 7, 3, 9, 6, CI_BONE + 1);
        cvRect(c, 6, 1, 10, 2, CI_WOOD + 3);
        cvPut(c, 6, 8, CI_WHITE);
        cvOutline(c, CI_INK);
        break;
      case SP_MANA:                                              // glowing pearl in a shell
        cvEll(c, 8, 13, 6.5f, 2.5f, CI_CORAL + 2, 3);
        cvEll(c, 8, 8.5f, 5, 5, CI_BLUE + 1, 5);
        cvEll(c, 6.5f, 7, 1.7f, 1.7f, CI_GLOW + 3, 1);
        cvPut(c, 12, 3, CI_WHITE);
        cvOutline(c, CI_INK);
        break;
      case SP_GKEY: crDrawKey(c, CI_GOLD, 4); break;
      case SP_BKEY: crDrawKey(c, CI_BLUE, 6); break;
      case SP_GOLD:                                              // coins and a ruby
        cvEll(c, 8, 13, 7, 2.6f, CI_GOLD, 4);
        cvEll(c, 8, 10.5f, 5.2f, 2.6f, CI_GOLD, 4);
        cvEll(c, 8, 8, 3.4f, 2.2f, CI_GOLD, 4);
        cvEll(c, 8, 6, 2, 2, CI_RED, 4);
        cvPut(c, 11, 4, CI_WHITE);
        cvPut(c, 3, 11, CI_WHITE);
        cvOutline(c, CI_INK);
        break;
      case SP_WAND:                                              // a glowing coral branch
        cvLine(c, 8, 15, 8, 4, 1.1f, CI_CORAL + 3);
        cvLine(c, 8, 10, 3.5f, 5, 0.8f, CI_CORAL + 4);
        cvLine(c, 8, 8, 12.5f, 3, 0.8f, CI_CORAL + 2);
        cvEll(c, 8, 3, 1.8f, 1.8f, CI_GLOW + 3, 1);
        cvEll(c, 3.5f, 4.5f, 1.4f, 1.4f, CI_GLOW + 2, 1);
        cvEll(c, 12.5f, 2.5f, 1.4f, 1.4f, CI_GLOW + 1, 1);
        cvPut(c, 1, 9, CI_WHITE);
        cvPut(c, 14, 8, CI_WHITE);
        cvOutline(c, CI_INK);
        break;
      case SP_TORCH0: case SP_TORCH1: crDrawTorch(c, i - SP_TORCH0); break;
      case SP_PILLAR:
        for (int x = 3; x <= 12; x++) {
          int v = 1 + (int)(5.5f * sinf((x - 2.5f) / 10.0f * 3.1416f));
          for (int y = 3; y <= 28; y++) {
            uint8_t col = crShadeOf(CI_STONE, 8, v + (crRnd(x, y, 90) > 220 ? -1 : 0));
            if (crNoise(x * 2, y, 4, 91) > 200) col = CI_MOSS + 1 + (x > 6);
            cvPut(c, x, y, col);
          }
        }
        cvRect(c, 1, 0, 14, 2, CI_STONE + 4);
        cvRect(c, 1, 2, 14, 2, CI_STONE + 2);
        cvRect(c, 1, 29, 14, 31, CI_STONE + 3);
        break;
      case SP_KELP0: case SP_KELP1:
        for (int s = 0; s < 3; s++)
          for (int y = 31; y >= 3 + s * 4; y--) {
            float x = 4 + s * 4 + sinf(y * 0.32f + (i - SP_KELP0) * 1.3f + s * 2) * (31 - y) * 0.08f;
            cvPut(c, (int)x, y, CI_MOSS + 2 + (s & 1));
            cvPut(c, (int)x + 1, y, CI_MOSS + 1);
            if (y % 6 == s) { cvPut(c, (int)x + 2, y, CI_MOSS + 4); cvPut(c, (int)x - 1, y + 1, CI_MOSS + 3); }
          }
        break;
      case SP_BARREL:
        cvEll(c, 8, 9, 6, 7, CI_WOOD, 5);
        cvLine(c, 2, 5, 14, 5, 0.5f, CI_STONE + 3);
        cvLine(c, 2, 13, 14, 13, 0.5f, CI_STONE + 3);
        cvEll(c, 8, 3, 4.5f, 1.3f, CI_WOOD + 5, 1);
        cvOutline(c, CI_INK);
        break;
      case SP_BONES:
        cvLine(c, 2, 15, 14, 11, 0.8f, CI_BONE + 2);
        cvLine(c, 2, 11, 14, 15, 0.8f, CI_BONE + 1);
        cvEll(c, 8, 10, 3.8f, 3.2f, CI_BONE, 4);
        cvPut(c, 7, 10, CI_INK);
        cvPut(c, 9, 10, CI_INK);
        cvOutline(c, CI_INK);
        break;
      case SP_BOLT:
        for (int y = 0; y < 16; y++)
          for (int x = 0; x < 16; x++) {
            int d2 = (x * 2 - 15) * (x * 2 - 15) + (y * 2 - 15) * (y * 2 - 15);
            if (d2 < 30) cvPut(c, x, y, CI_WHITE);
            else if (d2 < 80) cvPut(c, x, y, CI_GLOW + 3);
            else if (d2 < 150) cvPut(c, x, y, CI_GLOW + 1);
            else if (d2 < 220 && ((x + y) & 1)) cvPut(c, x, y, CI_GLOW);
          }
        break;
      case SP_INK:
        for (int y = 0; y < 16; y++)
          for (int x = 0; x < 16; x++) {
            int d2 = (x * 2 - 15) * (x * 2 - 15) + (y * 2 - 15) * (y * 2 - 15);
            if (d2 < 20) cvPut(c, x, y, CI_FIRE + 1);
            else if (d2 < 70) cvPut(c, x, y, CI_INK + 3);
            else if (d2 < 150) cvPut(c, x, y, CI_INK + 2);
            else if (d2 < 210 && ((x + y) & 1)) cvPut(c, x, y, CI_INK + 1);
          }
        break;
      case SP_PUFF:
        for (int k = 0; k < 8; k++) {
          float a = k * 0.785f;
          cvLine(c, 8 + cosf(a) * 2, 8 + sinf(a) * 2, 8 + cosf(a) * 7, 8 + sinf(a) * 7, 0.5f, k & 1 ? CI_GLOW + 2 : CI_WHITE);
        }
        break;
      case SP_HAND: crDrawHand(c); break;
    }
  }
}

}  // namespace cryptgame

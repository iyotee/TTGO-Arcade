// Abyx: art (included by abyx.cpp only). Tigo's pixel art, item art, biome
// palettes, the procedural tile cache and the parallax background.
#pragma once
#include "abyx_gfx.h"

namespace abyx {

// ---------------- Tigo (pixel art, facing right) ----------------
// Layers: body (22x16), legs (22x2 under it), claw (front). Slot chars below.
static const char TIGO_CHARS[] = "osdbBhrRlwkcCpPn";   // slot 1..16
static const int TG_W = 24, TG_H = 17;
static const char TG_BODY[] =
  "......................"
  "...........ooo.ooo...."
  "..........owwwowwwo..."
  "..........owwkowwko..."
  "..........owwkowwko..."
  "....oooo...ooo.ooo...."
  "..oossssoo..R..R......"
  ".osshhssso..R..R......"
  "osshbbbbssooRooRo....."
  "osbbsssbbsorrrrrro...."
  "osbsbbbsbsorrlrrnro..."
  "osbsbsdsbborrrrrrro..."
  "osbssbbbsdoRrrrrro...."
  "odbbbbbsddoRRRRRo....."
  ".oddsssddooooooo......"
  "..oooooo..............";
// eyes closed (blink): rows 1..4 replaced
static const char TG_BLINK[] =
  "......................"
  "......................"
  "...........ooo.ooo...."
  "..........owwwowwwo..."
  "..........ooooooooo...";
static const char TG_LEGS[5][45] = {
  "..........p.p.p......." "..........P.P.P.......",      // idle
  ".........p..p..p......" "........P..P..P.......",      // run 1
  "..........p.p.p......." "..........P.P.P.......",      // run 2
  "...........p..p..p...." "............P..P..P...",      // run 3
  "..........pp.pp......." "......................",      // tucked (air)
};
static const char TG_CLAW[] =      // 6 x 5, drawn at (15, 8)
  "..oooo"
  ".oCCCo"
  "occoo."
  "occCCo"
  ".oooo.";
static const char TG_CLAW_OPEN[] = // 8 x 7, drawn at (15, 7): firing
  "...ooooo"
  "..oCCCCo"
  ".occooo."
  "occo...."
  ".occooo."
  "..oCCCCo"
  "...oooo.";

static_assert(sizeof(TG_BODY) == 22 * 16 + 1, "Tigo body art size");
static_assert(sizeof(TG_BLINK) == 22 * 5 + 1, "Tigo blink art size");
static_assert(sizeof(TG_CLAW) == 6 * 5 + 1 && sizeof(TG_CLAW_OPEN) == 8 * 7 + 1, "Tigo claw art size");

// slot palettes: normal, neon (hot pink, violet shell, shades)
static const uint16_t TG_PAL_SRC[2][17] = {
  {0, 0x38E7, 0xF6B4, 0xD58F, 0x2D95, 0x7F1A, 0xFFDB, 0xFB49, 0xDA25, 0xFCAE, 0xFFFF, 0x18C3, 0xE245, 0xFCF0, 0xFD91, 0xDC4C, 0xFBD1},
  {0, 0x2026, 0x915C, 0x60B5, 0xF955, 0xFF87, 0xDDFF, 0xFA4D, 0xC8EC, 0xFCB5, 0x0822, 0x073F, 0xF1AB, 0xFE47, 0xFD12, 0xC8EC, 0xFAD9},
};
static uint16_t tgPal[17];

// ---------------- small item / shot art ----------------
static const char ITEM_CHARS[] = "oOwWpPyYrRgGkK";
static const char ART_SHRIMP[] =   // 8 x 5
  "..ppp..."
  ".pPPPp.."
  "pPwPPPpp"
  ".pPPPpPp"
  "..ppp.p.";
static const char ART_JUMBO[] =    // 12 x 7
  "...pppp....."
  "..pPPPPpp..."
  ".pPPwPPPPp.."
  "pPPPPPPPPPpp"
  ".pPPPPPPpPPp"
  "..pPPPPp.pp."
  "...ppp......";
static const char ART_HEART[] =    // 9 x 8
  ".oo...oo."
  "orro.orro"
  "orwrorrro"
  "orrrrrrro"
  ".orrrrro."
  "..orrro.."
  "...oro..."
  "....o....";
static const char ART_POWER[] =    // 9 x 9
  "....o...."
  "...oyo..."
  ".ooyYyoo."
  "oyyYwYyyo"
  ".oyYYYyo."
  "oyyYYYyyo"
  ".ooyyyoo."
  "...oyo..."
  "....o....";

static_assert(sizeof(ART_SHRIMP) == 8 * 5 + 1 && sizeof(ART_JUMBO) == 12 * 7 + 1, "shot art size");
static_assert(sizeof(ART_HEART) == 9 * 8 + 1 && sizeof(ART_POWER) == 9 * 9 + 1, "item art size");

// ---------------- biomes ----------------
enum { BI_CORAL, BI_KELP, BI_RUINS, BI_ABYSS, BI_LAVA, NUM_BIOMES };
struct BiomeDef {
  uint16_t water0, water1, far, mid;           // background (plain RGB565)
  uint16_t deep, rock, rock2, rim, rimHi, edge; // tiles
  uint16_t plat, platD, spike, accent, accent2;
  uint8_t rays;                                // light shaft strength 0..3
  const char *name;
};
// plain RGB565, written as rgb() values computed by hand (see rgb())
#define RGB_(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const BiomeDef BIOME_SRC[NUM_BIOMES] = {
  {RGB_(36, 128, 168), RGB_(12, 36, 92), RGB_(46, 84, 140), RGB_(30, 50, 104),
   RGB_(44, 18, 54), RGB_(120, 46, 86), RGB_(150, 64, 104), RGB_(255, 124, 112), RGB_(255, 200, 168), RGB_(64, 22, 56),
   RGB_(248, 160, 120), RGB_(170, 80, 80), RGB_(150, 70, 200), RGB_(255, 156, 64), RGB_(120, 255, 230), 2, "CORAL CAVES"},
  {RGB_(52, 150, 112), RGB_(10, 46, 52), RGB_(40, 104, 84), RGB_(22, 70, 60),
   RGB_(18, 36, 30), RGB_(56, 84, 52), RGB_(76, 104, 56), RGB_(136, 224, 80), RGB_(210, 255, 140), RGB_(22, 40, 30),
   RGB_(150, 104, 60), RGB_(92, 60, 36), RGB_(200, 220, 90), RGB_(255, 232, 120), RGB_(170, 255, 120), 3, "KELP FOREST"},
  {RGB_(48, 70, 136), RGB_(14, 14, 50), RGB_(48, 54, 104), RGB_(32, 34, 76),
   RGB_(28, 30, 58), RGB_(88, 98, 140), RGB_(110, 120, 164), RGB_(200, 212, 244), RGB_(255, 255, 255), RGB_(26, 26, 56),
   RGB_(200, 170, 110), RGB_(120, 96, 60), RGB_(170, 120, 90), RGB_(255, 210, 90), RGB_(120, 200, 255), 1, "DROWNED RUINS"},
  {RGB_(6, 20, 52), RGB_(0, 2, 12), RGB_(10, 22, 50), RGB_(6, 12, 34),
   RGB_(6, 8, 22), RGB_(24, 30, 58), RGB_(34, 42, 74), RGB_(60, 230, 255), RGB_(210, 255, 255), RGB_(4, 4, 18),
   RGB_(70, 90, 140), RGB_(30, 40, 80), RGB_(120, 255, 240), RGB_(255, 80, 220), RGB_(80, 255, 230), 0, "ABYSSAL TRENCH"},
  {RGB_(28, 8, 14), RGB_(120, 34, 14), RGB_(60, 18, 20), RGB_(36, 12, 16),
   RGB_(24, 14, 18), RGB_(56, 38, 42), RGB_(72, 48, 48), RGB_(255, 140, 40), RGB_(255, 232, 120), RGB_(14, 6, 8),
   RGB_(110, 80, 70), RGB_(60, 40, 40), RGB_(40, 30, 34), RGB_(255, 100, 20), RGB_(255, 220, 80), 0, "LAVA VENTS"},
};

// the palettes in use: BIOME_SRC, or its neon take when theme().neon (darker
// water and rock, glowing rims and plants, like the launcher's neon theme)
static BiomeDef BIOMES[NUM_BIOMES];
static int artBiome = -1, bgBiome = -1;
static void setBiomePalettes(bool neon) {
  for (int i = 0; i < NUM_BIOMES; i++) {
    BiomeDef b = BIOME_SRC[i];
    if (neon) {
      const uint16_t night = RGB_(10, 0, 28);
      b.water0 = mix565(b.water0, night, 18);
      b.water1 = mix565(b.water1, night, 22);
      b.far = mix565(b.far, RGB_(60, 0, 90), 14);
      b.mid = mix565(b.mid, night, 16);
      b.deep = mix565(b.deep, 0x0000, 18);
      b.rock = mix565(b.rock, night, 16);
      b.rock2 = mix565(b.rock2, night, 12);
      b.edge = mix565(b.edge, 0x0000, 20);
      b.rim = mix565(b.rim, b.accent2, 14);
      b.rimHi = mix565(b.rimHi, 0xFFFF, 16);
      b.plat = mix565(b.plat, b.accent, 12);
      b.spike = mix565(b.spike, RGB_(255, 40, 200), 16);
    }
    BIOMES[i] = b;
  }
  artBiome = bgBiome = -1;                         // regenerate the cached art
}

static uint32_t hash32(uint32_t n) {
  n ^= n >> 16; n *= 0x7FEB352D; n ^= n >> 15; n *= 0x846CA68B; n ^= n >> 16;
  return n;
}
static inline uint32_t hash2(int x, int y) { return hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u); }

// ---------------- tile cache ----------------
// 32 solid tiles (4 open-side bits x 2 textures), then the special tiles.
enum { TA_PLAT = 32, TA_SPIKE, TA_SPIKE_DN, TA_CRACK, TA_FRINGE, TA_FRINGE2, TA_DEEP, TA_DEEP2, NUM_TA };
static const uint16_t KEY = 0x2010;                 // transparent pixel in tile art (swapped value)
static uint16_t tileArt[NUM_TA][64];

// texture of the rock itself (no edges): per biome motif, two variants
static uint16_t rockTex(int x, int y, int var, const BiomeDef &b, int biome) {
  uint32_t h = hash2(x + var * 31, y + var * 7 + 5);
  uint16_t c = (h % 6 == 0) ? b.rock2 : b.rock;
  switch (biome) {
    case BI_CORAL: {                                   // coral pores: dark holes with a light rim
      static const int8_t P[2][2][2] = {{{2, 2}, {5, 6}}, {{6, 1}, {1, 5}}};
      for (int k = 0; k < 2; k++) {
        int dx = x - P[var][k][0], dy = y - P[var][k][1], d = dx * dx + dy * dy;
        if (d == 0) return mix565(b.deep, b.edge, 16);
        if (d <= 2) return dy < 0 || dx < 0 ? mix565(b.rock2, b.rim, 6) : mix565(b.rock, b.deep, 10);
      }
      if ((h & 63) == 1) return mix565(b.rock2, b.accent, 14);
      break;
    }
    case BI_RUINS: {                                   // masonry
      int row = (y + var * 4) & 7, off = (row < 4) ? 0 : 4;
      if (row == 3 || row == 7 || ((x + off) & 7) == 0) return mix565(b.rock, b.edge, 12);
      if (row == 0 || row == 4) return b.rock2;
      if ((h & 15) == 0) return mix565(b.rock, b.deep, 8);
      break;
    }
    case BI_LAVA:                                      // basalt with glowing veins
      if (((x + y * 2 + var * 3) % 9 == 0 && (h & 1)) || (h & 63) == 3) return mix565(b.rock, b.accent, 12 + (h & 7));
      break;
    case BI_ABYSS:                                     // dark stone, a rare glowing speck
      if ((h & 127) == 3) return b.accent2;
      if ((h & 7) == 0) return mix565(b.rock, b.deep, 12);
      break;
    default: {                                         // kelp: pebbles with a lit side
      static const int8_t S[2][3][2] = {{{1, 1}, {5, 3}, {2, 6}}, {{4, 0}, {0, 4}, {5, 5}}};
      for (int k = 0; k < 3; k++) {
        int dx = x - S[var][k][0], dy = y - S[var][k][1];
        if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1) return (dx + dy == 0) ? mix565(b.rock2, b.rim, 8) : b.rock2;
        if (dx == 2 && dy >= 0 && dy <= 2) return mix565(b.rock, b.deep, 12);
      }
      break;
    }
  }
  return c;
}

static void genSolid(uint16_t *t, int m, int var, const BiomeDef &b, int biome) {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int dN = (m & 1) ? y : 9, dE = (m & 2) ? 7 - x : 9, dS = (m & 4) ? 7 - y : 9, dW = (m & 8) ? x : 9;
      int d = min(min(dN, dS), min(dE, dW));
      uint32_t h = hash2(x + var * 31, y + m * 17);
      // rounded outer corners
      bool cut = ((m & 9) == 9 && x + y < 2) || ((m & 3) == 3 && (7 - x) + y < 2) ||
                 ((m & 12) == 12 && x + (7 - y) < 2) || ((m & 6) == 6 && (7 - x) + (7 - y) < 2);
      if (cut) { t[y * 8 + x] = KEY; continue; }
      uint16_t c = rockTex(x, y, var, b, biome);
      if (d >= 4) c = mix565(c, b.deep, 8);
      // surfaces: a lit lip on top, a dark underside, outlined sides
      if (dN == 0) c = (h & 3) == 0 ? b.rimHi : b.rim;
      else if (dN == 1) c = (h & 1) ? b.rim : mix565(b.rim, c, 14);
      else if (dN == 2) c = mix565(c, b.rim, (h & 1) ? 10 : 4);
      else if (dS == 0) c = b.edge;
      else if (dS == 1) c = mix565(c, b.edge, 14);
      else if (dE == 0 || dW == 0) c = mix565(c, b.edge, 18);
      else if (dE == 1 || dW == 1) c = mix565(c, b.rim, (m & 1) && y < 4 ? 8 : 3);
      t[y * 8 + x] = sw16(c);
    }
}

// rock far from any open water: darker, so caves read as solid masses
static void genDeep(uint16_t *t, int var, const BiomeDef &b, int biome) {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) t[y * 8 + x] = sw16(mix565(rockTex(x, y, var, b, biome), b.deep, 20));
}

static void genTiles(int biome) {
  if (biome == artBiome) return;
  artBiome = biome;
  const BiomeDef &b = BIOMES[biome];
  for (int v = 0; v < 2; v++)
    for (int m = 0; m < 16; m++) genSolid(tileArt[v * 16 + m], m, v, b, biome);
  // one-way ledge: a thick slab, lit on top, dark below, with a few drips
  genDeep(tileArt[TA_DEEP], 0, b, biome);
  genDeep(tileArt[TA_DEEP2], 1, b, biome);
  uint16_t *t = tileArt[TA_PLAT];
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      uint16_t c = KEY;
      if (y == 0) c = sw16((x & 3) == 1 ? b.rimHi : b.rim);
      else if (y == 1) c = sw16(mix565(b.rim, b.plat, 16));
      else if (y == 2) c = sw16((x + 1) % 4 == 0 ? b.platD : b.plat);
      else if (y == 3) c = sw16(b.platD);
      else if (y == 4 && (x == 1 || x == 2 || x == 6)) c = sw16(b.platD);
      else if (y == 5 && x == 2) c = sw16(mix565(b.platD, b.edge, 16));
      t[y * 8 + x] = c;
    }
  // spikes (pointing up) and the same flipped
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int k = x & 3, hgt = (k == 1 || k == 2) ? 7 : 3;   // two spines per tile
      int top = 8 - hgt + (k == 2 ? 1 : 0);
      uint16_t c = KEY;
      if (y >= top) c = sw16(y == top ? b.rimHi : y < top + 2 ? b.spike : mix565(b.spike, b.edge, 16));
      if (y == 7) c = sw16(b.edge);
      tileArt[TA_SPIKE][y * 8 + x] = c;
      tileArt[TA_SPIKE_DN][(7 - y) * 8 + x] = c;
    }
  // cracked block: a rock block split by a glowing seam
  static const uint8_t SEAM[8] = {0x10, 0x10, 0x20, 0x38, 0x44, 0x04, 0x08, 0x08};
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      uint16_t c = rockTex(x, y, 1, b, biome);
      bool seam = SEAM[y] & (0x80 >> x);
      bool nearSeam = (x > 0 && (SEAM[y] & (0x80 >> (x - 1)))) || (x < 7 && (SEAM[y] & (0x80 >> (x + 1))));
      if (seam) c = (y & 1) ? b.accent2 : mix565(b.accent2, 0xFFFF, 14);
      else if (nearSeam) c = mix565(c, b.accent2, 10);
      if (y == 0 || x == 0) c = mix565(c, b.rimHi, 12);
      if (y == 7 || x == 7) c = b.edge;
      tileArt[TA_CRACK][y * 8 + x] = sw16(c);
    }
  // surface fringe drawn above open-topped rock: polyps, grass, moss, glow tips, embers
  for (int v = 0; v < 2; v++) {
    uint16_t *f = tileArt[TA_FRINGE + v];
    for (int i = 0; i < 64; i++) f[i] = KEY;
    for (int x = 0; x < 8; x++) {
      uint32_t h = hash2(x + v * 8, 99);
      int hg = 0;
      uint16_t c = b.rim, tip = b.rimHi;
      if (biome == BI_CORAL) { hg = (h % 3 == 0) ? 3 : (h % 3 == 1) ? 1 : 0; tip = (h & 4) ? b.accent : b.rimHi; }
      else if (biome == BI_KELP) { hg = 1 + h % 4; c = mix565(b.rim, b.edge, 8); tip = b.rimHi; }
      else if (biome == BI_RUINS) { hg = (h % 4 == 0) ? 1 : 0; c = RGB_(90, 140, 90); tip = c; }
      else if (biome == BI_ABYSS) { hg = (h % 4 == 0) ? 2 : 0; c = b.rock2; tip = (h & 8) ? b.accent : b.accent2; }
      else { hg = (h % 5 == 0) ? 1 : 0; c = b.accent; tip = b.accent2; }
      for (int k = 0; k < hg; k++) f[(7 - k) * 8 + x] = sw16(k == hg - 1 ? tip : c);
    }
  }
}

// ---------------- background ----------------
static int8_t bgFar[256], bgMid[256];
static uint8_t rayTab[128];
static uint16_t waterLut[240][4], farLut[240], midLut[240];
static int bgH = 0;

static void genBackground(int biome, int h) {
  if (biome == bgBiome && h == bgH) return;
  bgBiome = biome;
  bgH = h;
  const BiomeDef &b = BIOMES[biome];
  for (int i = 0; i < 256; i++) {
    float a = i * 6.2831853f / 256;
    float f = 0, m = 0;
    switch (biome) {
      case BI_CORAL:   // rounded coral mounds
        f = 20 + 8 * sinf(a * 3 + 1) + 5 * sinf(a * 7) + 3 * sinf(a * 13 + 2);
        m = 10 + 7 * sinf(a * 4 + 2) + 4 * fabsf(sinf(a * 11));
        break;
      case BI_KELP: {  // tall thin strands
        float s = fabsf(sinf(a * 9)), s2 = fabsf(sinf(a * 14 + 1));
        f = 6 + 46 * powf(s, 12) + 8 * sinf(a * 2);
        m = 4 + 34 * powf(s2, 16);
        break;
      }
      case BI_RUINS: { // columns and broken walls
        int c = i % 32, k = i / 32;
        f = (c < 9) ? 30 + (k * 7) % 14 : 10 + ((c / 4) & 1) * 3;
        int c2 = (i + 11) % 48;
        m = (c2 < 7) ? 18 + (k * 5) % 9 : 4;
        break;
      }
      case BI_ABYSS:   // jagged ridges
        f = 14 + 10 * fabsf(sinf(a * 5)) + 6 * fabsf(sinf(a * 17 + 1)) + 3 * ((hash32(i) & 3));
        m = 6 + 8 * fabsf(sinf(a * 8 + 2)) + 2 * (hash32(i + 9) & 3);
        break;
      default:         // volcanic spires
        f = 12 + 30 * powf(fabsf(sinf(a * 6)), 5) + 4 * sinf(a * 19);
        m = 6 + 20 * powf(fabsf(sinf(a * 9 + 1)), 7);
        break;
    }
    bgFar[i] = (int8_t)constrain((int)f, 0, 100);
    bgMid[i] = (int8_t)constrain((int)m, 0, 100);
  }
  for (int i = 0; i < 128; i++) {
    float s = sinf(i * 6.2831853f / 128 * 3);
    int v = s > 0.55f ? (s > 0.85f ? 3 : 2) : s > 0.25f ? 1 : 0;
    rayTab[i] = (uint8_t)min(v, (int)b.rays);
  }
  uint16_t light = mix565(b.water0, 0xFFFF, 18);
  for (int y = 0; y < h && y < 240; y++) {
    int t = y * 32 / max(1, h - 1);
    uint16_t w = mix565(b.water0, b.water1, t);
    // light shafts fade out with depth
    int k = max(0, 32 - y * 40 / max(1, h));
    for (int r = 0; r < 4; r++) waterLut[y][r] = sw16(mix565(w, light, r * k / 10));
    farLut[y] = sw16(mix565(b.far, b.water1, t / 2));
    midLut[y] = sw16(mix565(b.mid, b.deep, t / 2));
  }
}

// Parallax layers: far and mid silhouettes hang from the top and rise from the
// bottom of the screen; water with light shafts in between. Drawn column by
// column as runs, so the inner loops are plain stores.
static void drawBackground(int camX, int camY, int roomPxH, uint32_t t) {
  const int W = fb.w, H = fb.h;
  const int biome = bgBiome;
  int ox1 = camX >> 2, ox2 = camX >> 1;
  int vy1 = constrain((camY - (roomPxH - H) / 2) / 8, -14, 14), vy2 = vy1 * 2;
  int phase = (camX >> 2) + (int)(t >> 6);
  int csh = biome == BI_KELP ? 2 : biome == BI_LAVA ? 1 : 1;     // ceiling silhouettes: height >> csh
  bool rays = BIOMES[biome].rays > 0;
  const int32_t s = fb.w;                            // column runs: one row down
  for (int x = 0; x < W; x++) {
    int f = bgFar[(x + ox1) & 255], m = bgMid[(x + ox2) & 255];
    int fc = bgFar[(x + ox1 + 97) & 255] >> csh, mc = bgMid[(x + ox2 + 151) & 255] >> csh;
    int mt = constrain(mc - 4 + vy2, 0, H), ft = constrain(fc + 6 + vy1, 0, H);
    int fb0 = constrain(H - 10 - f + vy1, 0, H), mb = constrain(H - m + vy2, 0, H);
    uint16_t *p = fbAt(x, 0);
    int y = 0;
    for (; y < mt; y++, p += s) *p = midLut[y];
    for (; y < ft; y++, p += s) *p = farLut[y];
    int wEnd = min(fb0, mb);
    if (rays) {
      int base = x + phase;
      for (; y < wEnd; y++, p += s) *p = waterLut[y][rayTab[(base + (y >> 1)) & 127]];
    } else {
      for (; y < wEnd; y++, p += s) *p = waterLut[y][0];
    }
    for (; y < mb; y++, p += s) *p = farLut[y];
    for (; y < H; y++, p += s) *p = midLut[y];
  }
  // drifting plankton specks (abyss and lava get more, they are the light there)
  int n = (biome == BI_ABYSS) ? 46 : (biome == BI_LAVA ? 26 : 18);
  uint16_t c1 = BIOMES[biome].accent2, c2 = BIOMES[biome].accent;
  for (int i = 0; i < n; i++) {
    uint32_t h = hash32(i * 7919 + biome * 131);
    int x = (int)((h & 1023) + 2048 - (camX / 3) + (t >> 7) * ((h >> 20) & 1)) % 256;
    int y = (int)(((h >> 10) & 255) - (camY / 3) - (int)(t >> 6) * (biome == BI_LAVA ? 1 : 0)) & 255;
    if (x >= W) continue;
    y = y * H / 256;
    int tw = (int)((t >> 5) + (h >> 24)) & 31;
    int a = tw < 16 ? tw : 31 - tw;
    if (a < 3) continue;
    pmix(x, y, (h & 4) ? sw16(c1) : sw16(c2), a + 8);
    if (biome == BI_ABYSS && a > 12) glow(x, y, 3, (h & 4) ? c1 : c2, 10);
  }
}

}  // namespace abyx

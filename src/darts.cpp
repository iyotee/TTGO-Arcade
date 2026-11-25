// Darts (Pub Darts, 8 rounds of 3 darts)
//
// Controls (two buttons):
//   Tap L or R : stop the aim line sweeping across, then the one sweeping down
//                (the second stop throws the dart)
// A standard board: singles, the outer double ring, the inner triple ring, the
// outer bull (25) and the bull (50). The hand wobbles a little more every round
// and the aim sweeps a little faster. Three treble 20s make 180.
//
// The board is drawn once per game into the screen sprite (per-pixel, with
// supersampled edges) and never redrawn: everything that moves over it (aim
// lines, darts, confetti) saves the pixels under it first and puts them back at
// the start of the next frame. The header and the panel below the board are
// cleared and redrawn every frame.

#include "common.h"
#include "themeui.h"

namespace darts {

// ---------------- board geometry (px at full size) ----------------
static const int CX = 67, CY = 107;                // board centre
static const int ZONE_T = 39, ZONE_B = 175;        // rows holding the persistent board image
static const float R_IB = 3.6f, R_OB = 8.2f;       // bull 50 / outer bull 25
static const float R_TI = 28.0f, R_TO = 34.0f;     // treble ring
static const float R_DI = 48.0f, R_DO = 54.0f;     // double ring (edge of the scoring area)
static const float R_RIM = 64.6f, R_EDGE = 66.0f, R_NUM = 60.5f;
static const float WIRE_W = 0.55f;                 // spider wire width
static const float GLOW_W = 4.0f;                  // neon halo around the board
static const float SEG = 0.31415927f;              // 18 degrees
static const float TWO_PI_F = 6.2831853f;
static const uint8_t ORDER[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

// colours (RGB888, converted when drawn)
static const uint32_t C_DARK = 0x1C1C1E, C_CREAM = 0xF0E2BE, C_RED = 0xD42A30, C_GREEN = 0x1E8C42;
static const uint32_t C_RING = 0x151517, C_WIRE = 0xC4C4CC, C_RIM_LO = 0x45454D, C_RIM_HI = 0xD8D8E0;
static const uint32_t C_WALL = 0x1E2430;

// ---------------- game ----------------
static const int ROUNDS = 8, DARTS = 3;
static const float SWEEP = 61.0f;                  // the aim lines travel the centre +- this
static const int FLY_T = 18;                       // ticks of the throw animation
static const int MAX_CONF = 24;
static const uint32_t TICK_MS = 16;
static const float DART_ANG = 1.2f;                // tip -> flights direction (down, a bit right)
static const float DART_S = 1.2f;                  // size of a dart stuck in the board

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { AIM_X, AIM_Y, FLY, HIT, SHOW };
static State state = TITLE;
static Phase phase = AIM_X;

struct Hit { uint8_t num, mult; };                 // num 0 = miss, 25 = bull (mult 2 = 50)
struct Dart { float x, y; int landT; };
struct Conf { float x, y, vx, vy; uint16_t col; };

static Dart dart[DARTS];
static Hit hits[DARTS];
static int nThrown, roundNo, roundPts, phaseT, gameT, showLen;
static float aimX, aimY, aimV, driftX, wob, ph1, ph2;
static float tgtX, tgtY, flyX0, flyY0;
static bool big180;
static Conf conf[MAX_CONF];
static char msg[16];
static uint16_t msgCol;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false, boardOK = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }
static float frnd() { return (esp_random() % 10001) / 10000.0f; }   // 0 .. 1

static uint16_t to565(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
static uint32_t mix888(uint32_t a, uint32_t b, float t) {
  int r = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
  int g = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
  int bl = (int)((a & 255) * (1 - t) + (b & 255) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}
static uint32_t shade(uint32_t c, float f) {
  int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)((c & 255) * f);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint16_t wallCol() { return theme().neon ? theme().bg : to565(C_WALL); }

// ---------------- scoring ----------------
static float clockAngle(float u, float v) {        // 0 at the top, clockwise
  float a = atan2f(u, -v);
  return a < 0 ? a + TWO_PI_F : a;
}

static Hit evalHit(float u, float v) {
  Hit h = {0, 0};
  float r = sqrtf(u * u + v * v);
  if (r <= R_IB) { h.num = 25; h.mult = 2; return h; }
  if (r <= R_OB) { h.num = 25; h.mult = 1; return h; }
  if (r > R_DO) return h;
  h.num = ORDER[(int)(clockAngle(u, v) / SEG + 0.5f) % 20];
  h.mult = (r > R_TI && r <= R_TO) ? 3 : r > R_DI ? 2 : 1;
  return h;
}

static int points(const Hit &h) { return h.num * h.mult; }

static void shortLabel(const Hit &h, char *buf, size_t n) {
  if (!h.num) snprintf(buf, n, "Miss");
  else if (h.num == 25) snprintf(buf, n, h.mult == 2 ? "Bull" : "25");
  else snprintf(buf, n, "%s%d", h.mult == 3 ? "T" : h.mult == 2 ? "D" : "", h.num);
}

// ---------------- the board ----------------
// colour of the board at (u, v) board units from the centre; false off the board
static bool sampleBoard(float u, float v, uint32_t &col, bool wires) {
  float r2 = u * u + v * v;
  if (r2 > R_EDGE * R_EDGE) return false;
  float r = sqrtf(r2);
  float light = r > 0.01f ? -(u * 0.6f + v * 0.8f) / r : 0;   // -1 .. 1, light from the top left
  if (r > R_RIM) { col = mix888(C_RIM_LO, C_RIM_HI, (light + 1) * 0.5f); return true; }
  uint32_t c;
  if (r > R_DO) c = C_RING;
  else if (r <= R_IB) c = C_RED;
  else if (r <= R_OB) c = C_GREEN;
  else {
    float f = clockAngle(u, v) / SEG + 0.5f;
    int i = (int)f;
    float fr = f - i;
    bool dark = (i % 20) % 2 == 0;                 // 20 is a dark segment with red rings
    bool ring = (r > R_TI && r <= R_TO) || r > R_DI;
    c = ring ? (dark ? C_RED : C_GREEN) : (dark ? C_DARK : C_CREAM);
    if (wires && (fr < 0.5f ? fr : 1 - fr) * SEG * r < WIRE_W * 0.5f) c = C_WIRE;
  }
  if (wires && r > R_OB - 1 && r < R_DO + 1) {
    const float rings[6] = {R_IB, R_OB, R_TI, R_TO, R_DI, R_DO};
    for (int k = 0; k < 6; k++)
      if (fabsf(r - rings[k]) < WIRE_W * 0.5f) c = C_WIRE;
  }
  // soft light from the top left and a slight darkening towards the rim
  col = shade(c, 1.0f + 0.07f * light * r / R_EDGE - 0.10f * r2 / (R_EDGE * R_EDGE));
  return true;
}

// true when a boundary is within tol board units: that pixel gets supersampled
static bool nearEdge(float u, float v, float r, float tol) {
  const float rad[8] = {R_IB, R_OB, R_TI, R_TO, R_DI, R_DO, R_RIM, R_EDGE};
  for (int k = 0; k < 8; k++)
    if (fabsf(r - rad[k]) < tol) return true;
  if (r > R_OB - tol && r < R_DO + tol) {
    float f = clockAngle(u, v) / SEG + 0.5f;
    float fr = f - floorf(f);
    if ((fr < 0.5f ? fr : 1 - fr) * SEG * r < tol) return true;
  }
  return false;
}

// the board centred on (cx, cy), scaled by s (1 = full size, R_EDGE px radius);
// partly covered pixels blend with what is already in the sprite
static void renderBoard(int cx, int cy, float s, bool dim, bool glow) {
  const float inv = 1.0f / s;
  const int ss = s > 0.3f ? 4 : 2;
  const bool wires = s > 0.5f;
  const float redge = R_EDGE * s, gw = glow ? GLOW_W : 0;
  const uint16_t gcol = theme().frameSel;
  const int ext = (int)(redge + gw) + 2;
  const float tol = 0.75f * inv + (wires ? WIRE_W * 0.5f : 0);
  for (int y = cy - ext; y <= cy + ext; y++) {
    if (y < 0 || y >= 240) continue;
    for (int x = cx - ext; x <= cx + ext; x++) {
      if (x < 0 || x >= 135) continue;
      float u = (x - cx) * inv, v = (y - cy) * inv;
      float r = sqrtf(u * u + v * v), rp = r * s;
      if (rp > redge + 1.0f) {
        if (glow && rp < redge + gw) {
          float a = 1.0f - (rp - redge) / gw;
          spr.drawPixel(x, y, tft.alphaBlend((uint8_t)(a * a * 150), gcol, spr.readPixel(x, y)));
        }
        continue;
      }
      uint32_t c, rs = 0, gs = 0, bs = 0;
      int n = 0, tot = 1;
      if (!nearEdge(u, v, r, tol)) {
        if (sampleBoard(u, v, c, wires)) { rs = c >> 16; gs = (c >> 8) & 255; bs = c & 255; n = 1; }
      } else {
        tot = ss * ss;
        for (int j = 0; j < ss; j++)
          for (int i = 0; i < ss; i++) {
            float su = u + ((i + 0.5f) / ss - 0.5f) * inv, sv = v + ((j + 0.5f) / ss - 0.5f) * inv;
            if (!sampleBoard(su, sv, c, wires)) continue;
            rs += c >> 16; gs += (c >> 8) & 255; bs += c & 255; n++;
          }
      }
      if (!n) continue;
      uint16_t col = to565(((rs / n) << 16) | ((gs / n) << 8) | (bs / n));
      if (dim) col = tft.alphaBlend(80, col, TFT_BLACK);
      if (n < tot) {
        uint16_t under = spr.readPixel(x, y);
        if (glow) under = tft.alphaBlend(150, gcol, under);
        col = tft.alphaBlend((uint8_t)(n * 255 / tot), col, under);
      }
      spr.drawPixel(x, y, col);
    }
  }
}

static void drawNumbers(int cx, int cy, float rad) {
  char buf[4];
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(to565(0xECECEC));
  for (int k = 0; k < 20; k++) {
    float a = k * SEG;
    snprintf(buf, sizeof buf, "%d", ORDER[k]);
    spr.drawString(buf, cx + (int)floorf(sinf(a) * rad + 0.5f) + 1, cy - (int)floorf(cosf(a) * rad + 0.5f) + 1, 1);
  }
}

// ---------------- overlays: save the board under them, restore next frame ----------------
static const int POOL = 4096, MAX_SAVES = 24;   // the worst frame needs ~3300 px
struct Save { int16_t x, y, w, h; int32_t off; };
// allocated only while Darts is open (8 KB of heap is precious with WiFi or Bluetooth up)
static uint16_t *pool = nullptr;
static Save saves[MAX_SAVES];
static int nSaves = 0, poolUsed = 0;

static void freePool() {
  free(pool);
  pool = nullptr;
  nSaves = poolUsed = 0;
}

// only the board rows are saved: the header and the panel are redrawn anyway.
// false when the pool is full (the overlay is then not drawn)
static bool saveRect(int x, int y, int w, int h) {
  int x0 = x < 0 ? 0 : x, y0 = y < ZONE_T ? ZONE_T : y;
  int x1 = x + w > 135 ? 135 : x + w, y1 = y + h > ZONE_B ? ZONE_B : y + h;
  if (x1 <= x0 || y1 <= y0) return true;
  int n = (x1 - x0) * (y1 - y0);
  if (!pool || nSaves >= MAX_SAVES || poolUsed + n > POOL) return false;   // no buffer: skip the overlay
  Save &s = saves[nSaves++];
  s.x = x0; s.y = y0; s.w = x1 - x0; s.h = y1 - y0; s.off = poolUsed;
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) pool[poolUsed++] = spr.readPixel(xx, yy);
  return true;
}

static void restoreAll() {
  while (nSaves > 0) {
    const Save &s = saves[--nSaves];
    int k = s.off;
    for (int yy = s.y; yy < s.y + s.h; yy++)
      for (int xx = s.x; xx < s.x + s.w; xx++) spr.drawPixel(xx, yy, pool[k++]);
  }
  poolUsed = 0;
}

// the persistent board image (rows ZONE_T .. ZONE_B - 1)
static void buildBoard() {
  nSaves = poolUsed = 0;                           // whatever they covered is gone
  spr.fillRect(0, ZONE_T, 135, ZONE_B - ZONE_T, wallCol());
  renderBoard(CX, CY, 1.0f, false, theme().neon);
  drawNumbers(CX, CY, R_NUM);
  boardOK = true;
}

// ---------------- darts ----------------
static uint16_t flightCol() { return theme().neon ? theme().frameSel : 0x249F; }
static uint16_t flightEdge() { return theme().neon ? 0xFBDF : 0x9E7F; }

// a dart with its tip at (x, y), the body along angle ang, s = size (1 = stuck in the board)
static void drawDart(float x, float y, float s, float ang, bool dim) {
  const uint32_t BL = 0x00FFFFFF;                  // smooth primitives: blend with the sprite
  float ux = cosf(ang), uy = sinf(ang), nx = -uy, ny = ux;
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  #define DP(d) x + ux * (d) * s, y + uy * (d) * s
  // flights first: the shaft and barrel overlap their front
  float fx[5], fy[5];
  const float fd[5] = {12.5f, 18.0f, 20.0f, 20.0f, 18.0f}, fw[5] = {0, 3.6f, 3.0f, -3.0f, -3.6f};
  for (int i = 0; i < 5; i++) {
    fx[i] = x + (ux * fd[i] + nx * fw[i]) * s;
    fy[i] = y + (uy * fd[i] + ny * fw[i]) * s;
  }
  uint16_t fc = c(flightCol()), fe = c(flightEdge());
  spr.fillTriangle((int)fx[0], (int)fy[0], (int)fx[1], (int)fy[1], (int)fx[4], (int)fy[4], fc);
  spr.fillTriangle((int)fx[1], (int)fy[1], (int)fx[2], (int)fy[2], (int)fx[4], (int)fy[4], fc);
  spr.fillTriangle((int)fx[2], (int)fy[2], (int)fx[3], (int)fy[3], (int)fx[4], (int)fy[4], fc);
  float ew = s > 1.3f ? 1.2f : 0.9f;
  for (int i = 0; i < 5; i++) {
    int j = (i + 1) % 5;
    spr.drawWideLine(fx[i], fy[i], fx[j], fy[j], ew, i == 2 ? fc : fe, BL);
  }
  spr.drawWedgeLine(DP(10.0f), DP(19.5f), 0.55f * s, 0.55f * s, c(0x3186), BL);    // shaft
  spr.drawWedgeLine(DP(4.0f), DP(10.5f), 1.25f * s, 1.5f * s, c(0x8C71), BL);      // barrel
  spr.drawWedgeLine(DP(4.6f), DP(10.0f), 0.7f * s, 0.85f * s, c(0xC618), BL);
  spr.drawWideLine(x + (ux * 5.0f - nx * 0.7f) * s, y + (uy * 5.0f - ny * 0.7f) * s,
                   x + (ux * 9.5f - nx * 0.8f) * s, y + (uy * 9.5f - ny * 0.8f) * s, 0.8f, c(TFT_WHITE), BL);
  for (int k = 0; k < 2; k++) {                    // grip rings
    float d = 6.6f + k * 2.0f;
    spr.drawWideLine(x + (ux * d + nx * 1.3f) * s, y + (uy * d + ny * 1.3f) * s,
                     x + (ux * d - nx * 1.3f) * s, y + (uy * d - ny * 1.3f) * s, 0.7f, c(0x52AA), BL);
  }
  spr.drawWedgeLine(x, y, DP(4.2f), 0.3f, 0.55f * s, c(0x9CF3), BL);              // point
  #undef DP
}

// saves the board under the dart, then draws it
static void dartOver(float x, float y, float s, float ang) {
  float tx = x + cosf(ang) * 20 * s, ty = y + sinf(ang) * 20 * s, m = 4.2f * s + 2;
  int x0 = (int)floorf((x < tx ? x : tx) - m), y0 = (int)floorf((y < ty ? y : ty) - m);
  int x1 = (int)ceilf((x > tx ? x : tx) + m), y1 = (int)ceilf((y > ty ? y : ty) + m);
  if (saveRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1)) drawDart(x, y, s, ang, false);
}

// ---------------- game flow ----------------
static float sweepSpeed() { return 1.25f + 0.15f * (roundNo - 1); }

static void startAim() {
  phase = AIM_X;
  phaseT = 0;
  aimX = rnd(2) ? -SWEEP : SWEEP;
  aimV = aimX < 0 ? sweepSpeed() : -sweepSpeed();
  driftX = 0;
}

static void startRound() {
  nThrown = 0;
  roundPts = 0;
  big180 = false;
  wob = 0.8f + 0.3f * (roundNo - 1);               // hand wobble amplitude, px
  snprintf(msg, sizeof msg, "Round %d", roundNo);
  msgCol = theme().neon ? theme().accent : TFT_WHITE;
  startAim();
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("darts_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  roundNo = 1;
  gameT = 0;
  newHigh = false;
  buildBoard();
  for (int i = 0; i < MAX_CONF; i++) conf[i].y = 999;
  startRound();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void throwDart() {
  // a last little twitch of the hand on release
  tgtX = CX + aimX + driftX + (frnd() - 0.5f) * wob * 0.8f;
  tgtY = CY + aimY + (frnd() - 0.5f) * wob * 0.8f;
  flyX0 = CX + 44;
  flyY0 = 236;
  phase = FLY;
  phaseT = 0;
}

static void land() {
  Dart &d = dart[nThrown];
  d.x = tgtX;
  d.y = tgtY;
  d.landT = gameT;
  Hit h = evalHit(tgtX - CX, tgtY - CY);
  hits[nThrown++] = h;
  int p = points(h);
  score += p;
  roundPts += p;
  const Theme &th = theme();
  uint16_t good = th.neon ? th.accent2 : TFT_YELLOW;
  if (!h.num) { snprintf(msg, sizeof msg, "MISS"); msgCol = themeSoft(); }
  else if (h.num == 25 && h.mult == 2) { snprintf(msg, sizeof msg, "BULL!"); msgCol = th.neon ? th.frameSel : TFT_RED; }
  else if (h.num == 25) { snprintf(msg, sizeof msg, "25"); msgCol = th.neon ? th.ok : TFT_GREEN; }
  else if (h.mult > 1) { snprintf(msg, sizeof msg, "%c%d = %d", h.mult == 3 ? 'T' : 'D', h.num, p); msgCol = good; }
  else { snprintf(msg, sizeof msg, "%d", h.num); msgCol = th.text; }

  if (nThrown < DARTS) { phase = HIT; phaseT = 0; return; }
  big180 = roundPts == 180;                        // only three treble 20s make 180
  showLen = big180 ? 220 : 110;
  if (big180) {
    snprintf(msg, sizeof msg, "180!");
    static const uint16_t CC[5] = {TFT_RED, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_MAGENTA};
    for (int i = 0; i < MAX_CONF; i++) {
      float a = frnd() * TWO_PI_F, v = 1.2f + frnd() * 2.2f;
      conf[i].x = CX;
      conf[i].y = CY - 30;
      conf[i].vx = cosf(a) * v;
      conf[i].vy = sinf(a) * v - 1.5f;
      conf[i].col = CC[i % 5];
    }
  }
  phase = SHOW;
  phaseT = 0;
}

static void tick(bool tap) {
  phaseT++;
  gameT++;
  for (int i = 0; i < MAX_CONF; i++) {
    if (conf[i].y > 240) continue;
    conf[i].vy += 0.07f;
    conf[i].vx *= 0.99f;
    conf[i].x += conf[i].vx;
    conf[i].y += conf[i].vy;
  }
  switch (phase) {
    case AIM_X:
    case AIM_Y: {
      float &p = phase == AIM_X ? aimX : aimY;
      p += aimV;
      if (p > SWEEP) { p = SWEEP; aimV = -aimV; }
      if (p < -SWEEP) { p = -SWEEP; aimV = -aimV; }
      if (phase == AIM_Y)                          // the stopped line wanders: the hand is not steady
        driftX = wob * (0.35f * (sinf(phaseT * 0.071f + ph1) - sinf(ph1)) +
                        0.25f * (sinf(phaseT * 0.113f + ph2) - sinf(ph2)));
      if (!tap) break;
      if (phase == AIM_Y) { throwDart(); break; }
      phase = AIM_Y;
      phaseT = 0;
      aimY = rnd(2) ? -SWEEP : SWEEP;
      aimV = aimY < 0 ? sweepSpeed() : -sweepSpeed();
      ph1 = frnd() * TWO_PI_F;                     // the wander starts at 0: no jump on the stop
      ph2 = frnd() * TWO_PI_F;
      break;
    }
    case FLY:
      if (phaseT >= FLY_T) land();
      break;
    case HIT:
      if (phaseT > 45 || (tap && phaseT > 8)) startAim();
      break;
    default:   // SHOW: the round's darts stay up for a moment
      if (phaseT > showLen || (tap && phaseT > 30)) {
        for (int i = 0; i < MAX_CONF; i++) conf[i].y = 999;
        if (roundNo >= ROUNDS) { gameOver(); return; }
        roundNo++;
        startRound();
      }
      break;
  }
}

// ---------------- rendering ----------------
static uint16_t aimCol() { return theme().neon ? theme().accent : TFT_YELLOW; }

static void drawAim() {
  uint16_t hi = aimCol(), lo = TFT_BLACK;
  int ax = (int)floorf(CX + aimX + driftX + 0.5f), ay = (int)floorf(CY + aimY + 0.5f);
  int top = ZONE_T + 1, h = ZONE_B - ZONE_T - 2;
  if (saveRect(ax - 1, top, 3, h)) {
    spr.drawFastVLine(ax - 1, top, h, lo);
    spr.drawFastVLine(ax + 1, top, h, lo);
    spr.drawFastVLine(ax, top, h, phase == AIM_X ? hi : tft.alphaBlend(170, hi, TFT_BLACK));
  }
  if (phase == AIM_X) {
    if (saveRect(ax - 4, ZONE_T, 9, 6)) spr.fillTriangle(ax - 4, ZONE_T, ax + 4, ZONE_T, ax, ZONE_T + 5, hi);
    if (saveRect(ax - 4, ZONE_B - 6, 9, 6)) spr.fillTriangle(ax - 4, ZONE_B - 1, ax + 4, ZONE_B - 1, ax, ZONE_B - 6, hi);
    return;
  }
  if (saveRect(0, ay - 1, 135, 3)) {
    spr.drawFastHLine(0, ay - 1, 135, lo);
    spr.drawFastHLine(0, ay + 1, 135, lo);
    spr.drawFastHLine(0, ay, 135, hi);
  }
  if (saveRect(0, ay - 4, 6, 9)) spr.fillTriangle(0, ay - 4, 0, ay + 4, 5, ay, hi);
  if (saveRect(129, ay - 4, 6, 9)) spr.fillTriangle(134, ay - 4, 134, ay + 4, 129, ay, hi);
  if (saveRect(ax - 7, ay - 7, 15, 15)) {
    spr.drawCircle(ax, ay, 6, lo);
    spr.drawCircle(ax, ay, 5, hi);
    spr.drawCircle(ax, ay, 4, lo);
  }
}

static void drawPanel() {
  const Theme &th = theme();
  char buf[32];
  uint16_t bg = wallCol();
  spr.fillRect(0, HEADER_H, 135, ZONE_T - HEADER_H, bg);
  spr.fillRect(0, ZONE_B, 135, 240 - ZONE_B, bg);

  // header: score on the left, round and darts in hand on the right
  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(roundNo == ROUNDS ? (th.neon ? th.frameSel : TFT_ORANGE) : (th.neon ? th.accent2 : TFT_YELLOW));
  snprintf(buf, sizeof buf, "%d/%d", roundNo, ROUNDS);
  int w = spr.drawString(buf, 132, 3, 2);
  spr.setTextColor(th.neon ? th.textDim : TFT_DARKGREY);
  spr.drawString("ROUND", 129 - w, 9, 1);
  int inHand = DARTS - nThrown - (phase == FLY ? 1 : 0);
  if (state != PLAY) inHand = 0;
  for (int i = 0; i < DARTS; i++) {
    int x = 128 - i * 9;
    bool on = i < inHand;
    uint16_t steel = on ? TFT_LIGHTGREY : themeFade(70, TFT_LIGHTGREY), fl = on ? flightCol() : themeFade(70, flightCol());
    spr.drawFastVLine(x, 21, 9, steel);
    spr.drawFastVLine(x + 1, 24, 4, steel);
    spr.fillTriangle(x - 2, 35, x + 3, 35, x, 29, fl);
    spr.fillTriangle(x - 2, 35, x + 3, 35, x + 1, 29, fl);
  }

  // message of the last dart (or the round), the round's darts, and a hint
  spr.setTextDatum(TC_DATUM);
  uint16_t mc = msgCol;
  if (phase == SHOW && big180) {
    static const uint16_t FL[4] = {TFT_RED, TFT_YELLOW, TFT_GREEN, TFT_WHITE};
    mc = FL[(phaseT / 6) % 4];
  }
  if (phase != FLY) {
    spr.setTextColor(mc);
    spr.drawString(msg, 67, 178, 4);
  }
  size_t len = 0;
  buf[0] = 0;
  for (int i = 0; i < DARTS; i++) {
    char lab[8];
    if (i < nThrown) shortLabel(hits[i], lab, sizeof lab);
    else snprintf(lab, sizeof lab, "-");
    len += snprintf(buf + len, sizeof buf - len, i ? "  %s" : "%s", lab);
  }
  spr.setTextColor(themeSoft());
  spr.drawString(buf, 67, 207, 2);

  spr.setTextColor(th.neon ? th.textDim : TFT_DARKGREY);
  if (phase == SHOW && big180) {
    spr.setTextColor(mc);
    snprintf(buf, sizeof buf, "ONE HUNDRED AND EIGHTY");
  } else if (phase == AIM_X) snprintf(buf, sizeof buf, roundNo == ROUNDS && nThrown == 0 ? "Last round! Tap: stop" : "Tap: stop across");
  else if (phase == AIM_Y) snprintf(buf, sizeof buf, "Tap: stop and throw");
  else if (phase == SHOW && roundPts >= 100) {
    spr.setTextColor(th.neon ? th.accent2 : TFT_YELLOW);
    snprintf(buf, sizeof buf, "Round score %d TON!", roundPts);
  } else snprintf(buf, sizeof buf, "Round score %d", roundPts);
  spr.drawString(buf, 67, 229, 1);
}

static void drawGame() {
  if (!boardOK) buildBoard();
  restoreAll();
  drawPanel();

  // stuck darts quiver for a moment after landing
  int stuckN = nThrown;
  for (int i = 0; i < stuckN; i++) {
    int age = gameT - dart[i].landT;
    float q = age < 24 ? 0.16f * sinf(age * 1.3f) * (1.0f - age / 24.0f) : 0;
    dartOver(dart[i].x, dart[i].y, DART_S, DART_ANG + q);
  }
  if (state == PLAY && phase == FLY) {
    float t = phaseT / (float)FLY_T, e = 1 - (1 - t) * (1 - t);
    float x = flyX0 + (tgtX - flyX0) * e, y = flyY0 + (tgtY - flyY0) * e - 26 * sinf(t * 3.1415927f);
    dartOver(x, y, 2.0f - (2.0f - DART_S) * e, DART_ANG - 0.35f * (1 - e));
  }
  if (state == PLAY && (phase == AIM_X || phase == AIM_Y)) drawAim();
  for (int i = 0; i < MAX_CONF; i++) {
    int x = (int)conf[i].x, y = (int)conf[i].y;
    if (y < ZONE_T || y > 238 || x < 0 || x > 133) continue;
    if (saveRect(x, y, 2, 2)) spr.fillRect(x, y, 2, 2, conf[i].col);
  }

  if (state == GAMEOVER) {
    drawGameOverBox("FINAL SCORE", score, hiScore, newHigh);
    boardOK = false;                               // the box covers part of the board
    nSaves = poolUsed = 0;
  }
}

static void drawTitle() {
  char buf[32];
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.neon ? th.frameSel : TFT_RED);
  spr.drawString("DARTS", 67, 12, 4);

  const float s = 0.42f;
  renderBoard(67, 72, s, false, th.neon);
  drawDart(67.5f, 72 - 31 * s, 0.9f, DART_ANG, false);   // treble 20
  drawDart(66.0f, 73.0f, 0.9f, DART_ANG, false);         // bull

  // "Tap L/R  stop the aim", centred
  int wk = spr.textWidth("Tap L/R", 2), wa = spr.textWidth("stop the aim", 2), x0 = (135 - wk - 5 - wa) / 2;
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.neon ? th.keyHint : TFT_YELLOW);
  spr.drawString("Tap L/R", x0, 108, 2);
  spr.setTextColor(th.text);
  spr.drawString("stop the aim", x0 + wk + 5, 108, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(themeSoft());
  spr.drawString("Across, then down", 67, 134, 1);
  spr.drawString("8 rounds of 3 darts", 67, 148, 1);
  spr.drawString("T20 = 60, Bull = 50", 67, 162, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(th.neon ? th.ok : TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
  boardOK = false;
}

static void render() {
  if (state == TITLE) drawTitle();
  else drawGame();
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  renderBoard(x, y - 4, 11.5f / R_EDGE, dim, false);
  // a dart flies in, sticks near the treble 20, stays a moment
  float p = t ? (t % 1600) / 1600.0f : 1.0f;
  float tx = x + 0.5f, ty = y - 9.0f;
  if (p < 0.3f) {
    float e = p / 0.3f;
    e = 1 - (1 - e) * (1 - e);
    drawDart(x + 7 + (tx - x - 7) * e, y + 6 + (ty - y - 6) * e, 0.8f - 0.22f * e, DART_ANG - 0.3f * (1 - e), dim);
  } else {
    drawDart(tx, ty, 0.58f, DART_ANG, dim);
  }
}

void begin() {
  hiScore = prefs.getUInt("darts_hi", 0);
  state = TITLE;
  armed = false;
  boardOK = false;
  nSaves = poolUsed = 0;
  if (!pool) pool = (uint16_t *)malloc(POOL * sizeof(uint16_t));
  if (!pool) dbg("darts: no room for the overlay buffer\n");
  lastInput = millis();
  dirty = true;
}

static EvQ qL, qR;                 // button events waiting for the next tick

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    qL.push(eL);
    qR.push(eR);
    if ((int32_t)(now - lastTick) > 200) lastTick = now - TICK_MS;   // stall guard
    while (state == PLAY && (int32_t)(now - lastTick) >= (int32_t)TICK_MS) {
      lastTick += TICK_MS;
      int a = qL.pop(), b = qR.pop();
      tick(a == 1 || b == 1);
      dirty = true;
    }
    // left alone mid-game: hand over to the screensaver like everywhere else
    if (state == PLAY && idleTooLong(now, lastInput)) { freePool(); goToSleep(); return; }
  } else {
    qL.clear();
    qR.clear();
    EndChoice ch = startOrEnd(state == GAMEOVER, eL, eR, armed);
    if (ch == END_PLAY) newGame();
    else if (ch == END_MENU) { freePool(); return; }
    if (idleTooLong(now, lastInput)) { freePool(); goToSleep(); return; }
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace darts

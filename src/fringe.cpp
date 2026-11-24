// Fringe (Ripple Crossing)
//
// Controls (two buttons):
//   Tap L / R  : send a ring from the left / right bottom corner
//   Hold L + R : cancel every ring (300 ms)
// Rings are harmless, but wherever a left ring crosses a right ring a spark is
// born on the baseline and climbs the screen. You never steer: the time gap
// between your two taps is the aim (1 ms of gap = a fixed sideways offset,
// L first sends the spark to the right). Sparks catch the drifting motes, one
// spark can chain several; mines (level 3+) cost a life. A mote that fades out
// costs a life too. Three motes on one spark: +1 life.
//
// Original game by Jeremy Noverraz, 2026.

#include "common.h"

namespace fringe {

static const int EL_X = 3, ER_X = 131, BASE_Y = 226, D = 128, MID_X = 67;
static const float V = 2.0f;                       // ring growth, px per tick (never changes)
static const int RING_MAX_R = 232, MAX_RINGS = 4;
static const int MAX_MOTES = 6, MAX_MINES = 3, MAX_FX = 6;
static const int FIELD_TOP = HEADER_H;
static const int MOTE_X0 = 10, MOTE_X1 = 124, MOTE_Y0 = 66, MOTE_Y1 = 202;
static const float CATCH_R = 9.0f, MINE_R = 8.0f;
static const uint32_t TICK_MS = 16, DAMP_MS = 300;

struct Ring { float r; bool alive, useful; };
struct Pair { uint8_t state; uint8_t chain; float px, py; };   // state 0 unborn, 1 live, 2 gone
struct Mote { float x, y, vx, vy; int life, maxLife; bool active; };
struct Mine { float x, y, vx, vy; int age; bool active; };
struct Fx { float x, y; int t, val, chain; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Ring rl[MAX_RINGS], rr[MAX_RINGS];
static Pair pairs[MAX_RINGS][MAX_RINGS];
static Mote motes[MAX_MOTES];
static Mine mines[MAX_MINES];
static Fx fx[MAX_FX];
static int lives, level, catches, streak, spawnT, mineT, hurtT, dampT, flashL, flashR;
static bool damped;
static uint32_t score = 0, hiScore = 0, frame = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;
static int8_t grid[15][20];                        // background fringe dots: x for k, h step

static int rnd(int n) { return esp_random() % n; }
static float frand(float a, float b) { return a + (b - a) * (rnd(10001) / 10000.0f); }

static int mult() { return min(8, 1 + streak / 3); }
static int spawnInterval() { return max(56, 176 - 10 * (level - 1)); }
static int moteLife() { return max(288, 704 - 32 * (level - 1)); }
static float moteSpeed() { return min(0.40f, 0.05f + 0.03f * (level - 1)); }
static int mineCap() { return level < 3 ? 0 : level < 6 ? 1 : level < 10 ? 2 : 3; }

// x of the fringe whose foot is A px from the centre, at height h above the baseline
static float fringeX(float A, float h) {
  float den = 4096.0f - A * A;
  if (den <= 1) return MID_X + A;
  return MID_X + A * sqrtf(1 + h * h / den);
}

static void buildGrid() {
  for (int k = 0; k < 15; k++)
    for (int s = 0; s < 20; s++) {
      float x = fringeX((k - 7) * 8.0f, s * 10.0f);
      grid[k][s] = (x < 0 || x > 134) ? INT8_MIN : (int8_t)(x - MID_X);
    }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("fringe_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static bool farFromOthers(float x, float y, float d) {
  for (int i = 0; i < MAX_MOTES; i++)
    if (motes[i].active && hypotf(motes[i].x - x, motes[i].y - y) < d) return false;
  for (int i = 0; i < MAX_MINES; i++)
    if (mines[i].active && hypotf(mines[i].x - x, mines[i].y - y) < d) return false;
  return true;
}

static void spawnMote() {
  for (int i = 0; i < MAX_MOTES; i++) {
    if (motes[i].active) continue;
    for (int tries = 0; tries < 12; tries++) {
      float x = frand(MOTE_X0, MOTE_X1), y = frand(MOTE_Y0, MOTE_Y1);
      if (!farFromOthers(x, y, 18)) continue;
      float a = frand(0, 2 * PI), s = moteSpeed();
      motes[i] = {x, y, cosf(a) * s, sinf(a) * s, moteLife(), moteLife(), true};
      return;
    }
    return;
  }
}

static bool sparkNear(float x, float y, float d) {
  for (int i = 0; i < MAX_RINGS; i++)
    for (int j = 0; j < MAX_RINGS; j++)
      if (pairs[i][j].state == 1 && hypotf(pairs[i][j].px - x, pairs[i][j].py - y) < d) return true;
  return false;
}

static void spawnMine() {
  for (int i = 0; i < MAX_MINES; i++) {
    if (mines[i].active) continue;
    for (int tries = 0; tries < 12; tries++) {
      float x = frand(MOTE_X0, MOTE_X1), y = frand(MOTE_Y0, MOTE_Y1);
      if (!farFromOthers(x, y, 18) || sparkNear(x, y, 30)) continue;
      float a = frand(0, 2 * PI), s = moteSpeed() * 0.8f;
      mines[i] = {x, y, cosf(a) * s, sinf(a) * s, 0, true};
      return;
    }
    return;
  }
}

static void addFx(float x, float y, int val, int chain) {
  int best = 0;
  for (int i = 0; i < MAX_FX; i++) if (fx[i].t < fx[best].t) best = i;
  fx[best] = {x, y, 40, val, chain};
}

static void killRing(Ring &r) {
  if (!r.alive) return;
  r.alive = false;
  if (!r.useful) {
    streak = 0;                                    // a wasted ring breaks the multiplier
    if (level >= 5)                                // ...and from level 5 it also costs time
      for (int i = 0; i < MAX_MOTES; i++) if (motes[i].active) motes[i].life = max(1, motes[i].life - 32);
  }
}

static void clearRings() {
  for (int i = 0; i < MAX_RINGS; i++) { killRing(rl[i]); killRing(rr[i]); }
}

static void loseLife() {
  lives--;
  hurtT = 6;
  streak = 0;
  for (int i = 0; i < MAX_MOTES; i++) if (motes[i].active) motes[i].life += 120;
  if (lives <= 0) gameOver();
}

static void emit(Ring *rings, bool left, uint32_t pressedAt, uint32_t tickAt) {
  for (int i = 0; i < MAX_RINGS; i++) {
    if (rings[i].alive) continue;
    // start the ring as big as it would be had it left exactly at the press, so
    // the aim resolves to about a millisecond instead of a whole tick
    float r0 = (int32_t)(tickAt - pressedAt) > 0 ? V * (tickAt - pressedAt) / (float)TICK_MS : 0;
    rings[i] = {min(r0, 2 * V), true, false};
    for (int k = 0; k < MAX_RINGS; k++) {
      Pair &p = left ? pairs[i][k] : pairs[k][i];
      p.state = 0;
      p.chain = 0;
    }
    if (left) flashL = 4; else flashR = 4;
    return;
  }
}

static void newGame() {
  score = 0;
  frame = 0;
  lives = 3;
  level = 1;
  catches = streak = 0;
  hurtT = dampT = flashL = flashR = 0;
  damped = false;
  newHigh = false;
  for (int i = 0; i < MAX_RINGS; i++) { rl[i].alive = rr[i].alive = false; }
  memset(pairs, 0, sizeof pairs);
  for (int i = 0; i < MAX_MOTES; i++) motes[i].active = false;
  for (int i = 0; i < MAX_MINES; i++) mines[i].active = false;
  for (int i = 0; i < MAX_FX; i++) fx[i].t = 0;
  spawnMote();
  spawnMote();
  spawnT = spawnInterval();
  mineT = 384;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

// distance from point (x, y) to the segment a-b
static float segDist(float ax, float ay, float bx, float by, float x, float y) {
  float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
  t = constrain(t, 0.0f, 1.0f);
  return hypotf(ax + dx * t - x, ay + dy * t - y);
}

static void tick(int eL, int eR, uint32_t tickAt) {
  frame++;
  if (hurtT > 0) hurtT--;
  if (dampT > 0) dampT--;
  if (flashL > 0) flashL--;
  if (flashR > 0) flashR--;
  for (int i = 0; i < MAX_FX; i++) if (fx[i].t > 0) { fx[i].t--; fx[i].y -= 0.5f; }

  if (eL == 1) emit(rl, true, bL.t0, tickAt);
  if (eR == 1) emit(rr, false, bR.t0, tickAt);

  // hold both: cancel the rings in flight (once per hold)
  if (bL.stable && bR.stable) {
    uint32_t since = max(bL.t0, bR.t0);
    if (!damped && (int32_t)(tickAt - since) >= (int32_t)DAMP_MS) { clearRings(); damped = true; dampT = 4; }
  } else damped = false;

  for (int i = 0; i < MAX_RINGS; i++) {
    if (rl[i].alive && (rl[i].r += V) > RING_MAX_R) killRing(rl[i]);
    if (rr[i].alive && (rr[i].r += V) > RING_MAX_R) killRing(rr[i]);
  }

  // sparks: every live L ring against every live R ring
  for (int i = 0; i < MAX_RINGS && state == PLAY; i++) {
    for (int j = 0; j < MAX_RINGS && state == PLAY; j++) {
      Pair &p = pairs[i][j];
      if (!rl[i].alive || !rr[j].alive) { if (p.state == 1) p.state = 2; continue; }
      if (p.state == 2) continue;
      float a = rl[i].r, b = rr[j].r;
      if (a + b <= D || fabsf(a - b) >= D) continue;
      float px = (a * a - b * b + D * D) / (2.0f * D);
      float h = sqrtf(max(0.0f, a * a - px * px));
      float x = EL_X + px, y = BASE_Y - h;
      if (p.state == 0) { p.state = 1; p.px = MID_X + (a - b) / 2; p.py = BASE_Y; }
      // check the path since the last tick: a fresh spark moves very fast
      for (int m = 0; m < MAX_MOTES; m++) {
        Mote &mo = motes[m];
        if (!mo.active || segDist(p.px, p.py, x, y, mo.x, mo.y) >= CATCH_R) continue;
        mo.active = false;
        p.chain++;
        int hgt = BASE_Y - (int)mo.y;
        int val = (10 + hgt / 10) * p.chain * mult();
        score += val;
        streak++;
        catches++;
        level = 1 + catches / 8;
        rl[i].useful = rr[j].useful = true;
        addFx(mo.x, mo.y, val, p.chain);
        if (p.chain == 3 && lives < 3) lives++;
      }
      for (int m = 0; m < MAX_MINES; m++) {
        Mine &mi = mines[m];
        if (!mi.active || mi.age < 60 || segDist(p.px, p.py, x, y, mi.x, mi.y) >= MINE_R) continue;
        mi.active = false;
        clearRings();
        loseLife();
        break;
      }
      if (state != PLAY || !rl[i].alive) break;
      p.px = x;
      p.py = y;
      if (y < FIELD_TOP) p.state = 2;
    }
  }
  if (state != PLAY) return;

  // motes drift, bounce and fade
  for (int m = 0; m < MAX_MOTES; m++) {
    Mote &mo = motes[m];
    if (!mo.active) continue;
    mo.x += mo.vx;
    mo.y += mo.vy;
    if (mo.x < MOTE_X0 || mo.x > MOTE_X1) mo.vx = -mo.vx;
    if (mo.y < MOTE_Y0 || mo.y > MOTE_Y1) mo.vy = -mo.vy;
    if (--mo.life <= 0) {
      mo.active = false;
      loseLife();
      if (state != PLAY) return;
    }
  }
  for (int m = 0; m < MAX_MINES; m++) {
    Mine &mi = mines[m];
    if (!mi.active) continue;
    mi.x += mi.vx;
    mi.y += mi.vy;
    if (mi.x < MOTE_X0 || mi.x > MOTE_X1) mi.vx = -mi.vx;
    if (mi.y < MOTE_Y0 || mi.y > MOTE_Y1) mi.vy = -mi.vy;
    if (++mi.age > 640) mi.active = false;
  }

  int nm = 0;
  for (int m = 0; m < MAX_MOTES; m++) nm += motes[m].active;
  if (--spawnT <= 0 || nm == 0) { spawnMote(); spawnT = spawnInterval(); }
  int nmine = 0;
  for (int m = 0; m < MAX_MINES; m++) nmine += mines[m].active;
  if (nmine < mineCap() && --mineT <= 0) { spawnMine(); mineT = 384; }
}

// ---------------- rendering ----------------
static const uint16_t COL_L = TFT_CYAN, COL_R = TFT_MAGENTA, BG = 0x0006;

static void drawGrid() {
  for (int k = 0; k < 15; k++) {
    uint16_t c = (k - 7) % 2 == 0 ? 0x1949 : 0x10A6;
    for (int s = 0; s < 19; s++) {
      if (grid[k][s] == INT8_MIN) continue;
      int y = BASE_Y - s * 10;
      if (y < FIELD_TOP) break;
      spr.drawPixel(MID_X + grid[k][s], y, c);
    }
  }
}

static void drawSpark(int i, int j) {
  // a short trail: where this pair crossed in the last few ticks
  float a = rl[i].r, b = rr[j].r;
  for (int k = 6; k >= 1; k--) {
    float aa = a - k * V, bb = b - k * V;
    if (aa + bb <= D || fabsf(aa - bb) >= D) continue;
    float px = (aa * aa - bb * bb + D * D) / (2.0f * D);
    float h = sqrtf(max(0.0f, aa * aa - px * px));
    spr.drawPixel(EL_X + (int)px, BASE_Y - (int)h, tft.alphaBlend(255 - k * 35, TFT_YELLOW, BG));
  }
  const Pair &p = pairs[i][j];
  spr.drawCircle((int)p.px, (int)p.py, 5, TFT_YELLOW);
  spr.fillCircle((int)p.px, (int)p.py, 3, TFT_WHITE);
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(BG);
  drawGrid();

  // level 1: each mote shows its own fringe down to the baseline (a wordless tutorial)
  if (level == 1) {
    for (int m = 0; m < MAX_MOTES; m++) {
      const Mote &mo = motes[m];
      if (!mo.active) continue;
      float a = hypotf(mo.x - EL_X, mo.y - BASE_Y), b = hypotf(mo.x - ER_X, mo.y - BASE_Y);
      float A = (a - b) / 2;
      for (float h = 0; h < BASE_Y - mo.y - 6; h += 5)
        spr.drawPixel((int)fringeX(A, h), BASE_Y - (int)h, 0x52AA);
      spr.fillTriangle((int)(MID_X + A) - 2, 236, (int)(MID_X + A) + 2, 236, (int)(MID_X + A), 232, 0x52AA);
    }
  }

  // rings, fading as they grow
  for (int i = 0; i < MAX_RINGS; i++) {
    if (rl[i].alive) spr.drawCircle(EL_X, BASE_Y, (int)rl[i].r, dampT ? TFT_DARKGREY : tft.alphaBlend(255 - (int)rl[i].r, COL_L, BG));
    if (rr[i].alive) spr.drawCircle(ER_X, BASE_Y, (int)rr[i].r, dampT ? TFT_DARKGREY : tft.alphaBlend(255 - (int)rr[i].r, COL_R, BG));
  }

  for (int m = 0; m < MAX_MINES; m++) {
    const Mine &mi = mines[m];
    if (!mi.active) continue;
    int x = (int)mi.x, y = (int)mi.y;
    if (mi.age < 60) {
      spr.drawLine(x, y - 6, x + 6, y, TFT_RED); spr.drawLine(x + 6, y, x, y + 6, TFT_RED);
      spr.drawLine(x, y + 6, x - 6, y, TFT_RED); spr.drawLine(x - 6, y, x, y - 6, TFT_RED);
    } else {
      spr.fillTriangle(x - 6, y, x + 6, y, x, y - 6, TFT_RED);
      spr.fillTriangle(x - 6, y, x + 6, y, x, y + 6, TFT_RED);
      spr.fillCircle(x, y, 1, TFT_BLACK);
    }
  }

  for (int m = 0; m < MAX_MOTES; m++) {
    const Mote &mo = motes[m];
    if (!mo.active) continue;
    float f = (float)mo.life / mo.maxLife;
    uint16_t c = f > 0.5f ? TFT_GREEN : f > 0.25f ? TFT_YELLOW : TFT_ORANGE;
    if (mo.life < 96 && (frame / 6) & 1) c = TFT_RED;
    int x = (int)mo.x, y = (int)mo.y;
    spr.drawCircle(x, y, 6 + (int)(7 * min(1.0f, f)), tft.alphaBlend(110, c, BG));
    spr.fillCircle(x, y, 5, c);
  }

  for (int i = 0; i < MAX_RINGS; i++)
    for (int j = 0; j < MAX_RINGS; j++)
      if (pairs[i][j].state == 1 && rl[i].alive && rr[j].alive) drawSpark(i, j);

  for (int i = 0; i < MAX_FX; i++) {
    if (fx[i].t <= 0) continue;
    spr.drawCircle((int)fx[i].x, (int)fx[i].y + (40 - fx[i].t) / 2, 6 + (40 - fx[i].t) / 2, tft.alphaBlend(fx[i].t * 6, TFT_WHITE, BG));
    spr.setTextDatum(BC_DATUM);
    spr.setTextColor(fx[i].chain >= 3 ? TFT_GOLD : TFT_WHITE);
    if (fx[i].chain >= 3) snprintf(buf, sizeof buf, "TRIPLE");
    else if (fx[i].chain == 2) snprintf(buf, sizeof buf, "+%d x2", fx[i].val);
    else snprintf(buf, sizeof buf, "+%d", fx[i].val);
    spr.drawString(buf, (int)fx[i].x, (int)fx[i].y - 6, 1);
  }

  // baseline strip: emitters and the aim carets
  spr.fillRect(0, BASE_Y + 1, 135, 240 - BASE_Y - 1, 0x0003);
  spr.drawFastHLine(0, BASE_Y, 135, 0x2104);
  int nl = 0, nr = 0;
  for (int i = 0; i < MAX_RINGS; i++) {
    nl += rl[i].alive;
    nr += rr[i].alive;
    // where a spark would start if the other button were pressed right now
    if (rl[i].alive && rl[i].r < D) { int x = MID_X + (int)(rl[i].r / 2); spr.fillTriangle(x - 3, 238, x + 3, 238, x, 230, COL_L); }
    if (rr[i].alive && rr[i].r < D) { int x = MID_X - (int)(rr[i].r / 2); spr.fillTriangle(x - 3, 238, x + 3, 238, x, 230, COL_R); }
  }
  spr.fillCircle(EL_X, BASE_Y, 5, flashL ? TFT_WHITE : nl >= MAX_RINGS ? TFT_DARKGREY : COL_L);
  spr.fillCircle(ER_X, BASE_Y, 5, flashR ? TFT_WHITE : nr >= MAX_RINGS ? TFT_DARKGREY : COL_R);

  if (hurtT > 0) {
    spr.drawRect(0, FIELD_TOP, 135, 240 - FIELD_TOP, TFT_RED);
    spr.drawRect(1, FIELD_TOP + 1, 133, 238 - FIELD_TOP, TFT_RED);
  }

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < 3; i++) {
    if (i < lives) spr.fillCircle(104 + i * 11, 10, 4, TFT_GREEN);
    else spr.drawCircle(104 + i * 11, 10, 4, TFT_DARKGREY);
  }
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(mult() > 1 ? TFT_YELLOW : TFT_DARKGREY);
  snprintf(buf, sizeof buf, "x%d  L%d", mult(), level);
  spr.drawString(buf, 132, 22, 2);

  if (state == GAMEOVER) drawGameOverBox("FADED", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(BG);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("FRINGE", 67, 12, 4);

  // two half rings from a baseline and the spark where they cross
  spr.drawFastHLine(20, 90, 95, 0x2104);
  spr.drawCircleHelper(35, 90, 38, 1 | 2, COL_L);
  spr.drawCircleHelper(99, 90, 42, 1 | 2, COL_R);
  spr.fillCircle(35, 90, 3, COL_L);
  spr.fillCircle(99, 90, 3, COL_R);
  float d = 64, a = 38, b = 42, px = (a * a - b * b + d * d) / (2 * d), h = sqrtf(a * a - px * px);
  spr.drawCircle(35 + (int)px, 90 - (int)h, 5, TFT_YELLOW);
  spr.fillCircle(35 + (int)px, 90 - (int)h, 3, TFT_WHITE);
  spr.fillCircle(56, 48, 4, TFT_GREEN);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Tap L / R", 8, 98, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("ring", 80, 98, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  static const char *LINES[] = {
    "Sparks rise where an", "L ring meets an R ring", "The gap between taps",
    "aims: L first = right", "Catch dots, miss red", "Hold both = cancel",
  };
  for (int i = 0; i < 6; i++) spr.drawString(LINES[i], 67, 122 + i * 10, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void render() {
  if (state == TITLE) drawTitle();
  else drawGame();
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // two rings swelling from the lower corners, a spark climbing where they cross
  float k = t ? (t % 1600) / 1600.0f : 0.6f;
  int r = 14 + (int)(12 * k);
  int lx = x - 12, rx = x + 12, by = y + 14;
  spr.drawCircle(lx, by, r, c(COL_L));
  spr.drawCircle(rx, by, r, c(COL_R));
  float h = sqrtf(max(0.0f, (float)(r * r - 144)));
  spr.fillCircle(x, by - (int)h, 2, c(TFT_WHITE));
  spr.fillCircle(lx, by, 2, c(COL_L));
  spr.fillCircle(rx, by, 2, c(COL_R));
}

void begin() {
  hiScore = prefs.getUInt("fringe_hi", 0);
  buildGrid();
  state = TITLE;
  armed = false;
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(qL.pop(), qR.pop(), lastTick);
      dirty = true;
    }
  } else {
    qL.clear();
    qR.clear();
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace fringe

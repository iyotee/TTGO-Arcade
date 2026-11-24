// Pinball (Flipper)
//
// Controls (two buttons):
//   Hold L : left flipper
//   Hold R : right flipper (and pull/release plunger to launch)
// Hit bumpers, drop targets and top lanes to build your multiplier (up to 5x).
// 3 balls per game.

#include "common.h"

namespace pinball {

// ---------------- layout ----------------
static const int PLAY_L = 2, PLAY_R = 120, LANE_R = 132;
static const int LANE_X = 126, PLUNGER_Y = 227;
static const int BALL_R = 3;
static const int START_BALLS = 3;
static const uint32_t TICK_MS = 16;

static const float GRAVITY = 0.085f;
static const float MAX_SPEED = 6.5f;

static const int FLIP_LEN = 22;
static const float FLIP_R = 2.5f;
static const float FLIP_L_X = 34.0f, FLIP_R_X = 88.0f, FLIP_Y = 216.0f;
static const float REST_ANG = 28.0f * DEG_TO_RAD;
static const float UP_ANG = -32.0f * DEG_TO_RAD;
static const float FLIP_SPEED = 0.34f;                    // rad per tick

static const uint16_t TABLE_BG = 0x0842;                  // very dark navy
static const uint16_t WALL_COL = 0x5AEB;
static const uint16_t FLIP_COL = TFT_ORANGE;
static const uint16_t FLIP_HI = TFT_YELLOW;

// ---------------- geometry ----------------
struct Seg { float x1, y1, x2, y2; float bounce; uint8_t kind; };
// kind: 0 = wall, 1 = left slingshot, 2 = right slingshot
static const Seg SEGS[] = {
  // top arch (elliptical arc from (2, 64) through (67, 40) to (132, 64))
  {  2, 64,   9, 53, 0.72f, 0},
  {  9, 53,  24, 45, 0.72f, 0},
  { 24, 45,  45, 41, 0.72f, 0},
  { 45, 41,  67, 40, 0.72f, 0},
  { 67, 40,  89, 41, 0.72f, 0},
  { 89, 41, 110, 45, 0.72f, 0},
  {110, 45, 125, 53, 0.72f, 0},
  {125, 53, 132, 64, 0.72f, 0},
  // upper lane dividers
  { 45, 52,  45, 68, 0.65f, 0},
  { 77, 52,  77, 68, 0.65f, 0},
  // left & right lower guides to flippers
  {  2, 198,  34, 216, 0.60f, 0},
  {120, 198,  88, 216, 0.60f, 0},
  // left slingshot
  { 12, 168,  28, 198, 1.35f, 1},
  { 12, 168,  12, 194, 0.65f, 0},
  { 12, 194,  28, 198, 0.65f, 0},
  // right slingshot
  {110, 168,  94, 198, 1.35f, 2},
  {110, 168, 110, 194, 0.65f, 0},
  {110, 194,  94, 198, 0.65f, 0},
};
static const int NUM_SEGS = sizeof SEGS / sizeof SEGS[0];

struct Bumper { float x, y, r; uint16_t col; uint8_t hitT; };
static Bumper bumpers[3] = {
  {42,  92, 8, TFT_MAGENTA, 0},
  {80,  92, 8, TFT_CYAN,    0},
  {61, 118, 8, TFT_YELLOW,  0},
};

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float bx, by, vx, vy;
static bool inLane;
static float plungerPull;
static bool pulling;

static float flipLAng, flipRAng;                          // relative to horizontal (L: 0=right, R: mirrored)
static float flipLOm, flipROm;                            // angular velocity this tick

static bool topLanes[3];
static bool dropL[3], dropR[3];
static uint8_t slingLT, slingRT;
static int ballsLeft, mult, stuckT;

static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void addScore(uint32_t pts) {
  score += pts * mult;
}

static void serveBall() {
  inLane = true;
  pulling = false;
  plungerPull = 0.0f;
  bx = LANE_X;
  by = PLUNGER_Y - BALL_R;
  vx = vy = 0.0f;
  stuckT = 0;
}

static void resetTargets() {
  for (int i = 0; i < 3; i++) {
    topLanes[i] = false;
    dropL[i] = true;
    dropR[i] = true;
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("pinball_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  ballsLeft = START_BALLS;
  mult = 1;
  newHigh = false;
  flipLAng = REST_ANG;
  flipRAng = REST_ANG;
  flipLOm = flipROm = 0.0f;
  slingLT = slingRT = 0;
  for (int i = 0; i < 3; i++) bumpers[i].hitT = 0;
  resetTargets();
  serveBall();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

// ---------------- physics ----------------
static void clampSpeed() {
  float s = sqrtf(vx * vx + vy * vy);
  if (s > MAX_SPEED) {
    vx *= MAX_SPEED / s;
    vy *= MAX_SPEED / s;
  }
}

static void collideSegment(float x1, float y1, float x2, float y2, float bounce, uint8_t kind) {
  float dx = x2 - x1, dy = y2 - y1;
  float len2 = dx * dx + dy * dy;
  if (len2 < 1e-4f) return;
  float t = constrain(((bx - x1) * dx + (by - y1) * dy) / len2, 0.0f, 1.0f);
  float cx = x1 + t * dx, cy = y1 + t * dy;
  float nx = bx - cx, ny = by - cy;
  float dist = sqrtf(nx * nx + ny * ny);
  if (dist >= BALL_R || dist < 1e-5f) return;

  nx /= dist;
  ny /= dist;
  bx = cx + nx * BALL_R;
  by = cy + ny * BALL_R;

  float vn = vx * nx + vy * ny;
  if (vn < 0) {
    vx -= (1.0f + bounce) * vn * nx;
    vy -= (1.0f + bounce) * vn * ny;
    if (kind == 1) {
      vx = max(vx, 2.6f);
      vy = min(vy, -1.8f);
      slingLT = 6;
      addScore(30);
    } else if (kind == 2) {
      vx = min(vx, -2.6f);
      vy = min(vy, -1.8f);
      slingRT = 6;
      addScore(30);
    }
    clampSpeed();
  }
}

static void collideFlipper(float px, float py, float ang, float om, bool isRight) {
  // For left flipper: angle 0 = +x, positive = down.
  // For right flipper: mirrored horizontally.
  float dir = isRight ? -1.0f : 1.0f;
  float tx = px + dir * FLIP_LEN * cosf(ang);
  float ty = py + FLIP_LEN * sinf(ang);

  float dx = tx - px, dy = ty - py;
  float len2 = dx * dx + dy * dy;
  float t = constrain(((bx - px) * dx + (by - py) * dy) / len2, 0.0f, 1.0f);
  float cx = px + t * dx, cy = py + t * dy;
  float nx = bx - cx, ny = by - cy;
  float dist = sqrtf(nx * nx + ny * ny);
  float minD = BALL_R + FLIP_R;
  if (dist >= minD || dist < 1e-5f) return;

  nx /= dist;
  ny /= dist;
  bx = cx + nx * minD;
  by = cy + ny * minD;

  // Surface velocity at contact point due to rotation
  // Left flipper: tip moves by (-L*sin(ang)*om, L*cos(ang)*om)
  // Right flipper: tip moves by (+L*sin(ang)*om, L*cos(ang)*om)
  float r = t * FLIP_LEN;
  float vfx = -dir * r * sinf(ang) * om;
  float vfy = r * cosf(ang) * om;

  float rvx = vx - vfx;
  float rvy = vy - vfy;
  float vn = rvx * nx + rvy * ny;
  if (vn < 0) {
    float bounce = (om < -0.05f) ? 0.85f : 0.45f;
    rvx -= (1.0f + bounce) * vn * nx;
    rvy -= (1.0f + bounce) * vn * ny;
    vx = rvx + vfx;
    vy = rvy + vfy;
    // Extra upward snap when actively flipping
    if (om < -0.05f && ny < 0) {
      vy -= 0.9f * (0.4f + 0.6f * t);
    }
    clampSpeed();
  }
}

static void checkTopLanes(float prevY) {
  if (inLane) return;
  if (prevY > 60 && by <= 60 && by >= 50) {
    int lane = -1;
    if (bx > 18 && bx < 44) lane = 0;
    else if (bx > 46 && bx < 76) lane = 1;
    else if (bx > 78 && bx < 104) lane = 2;
    if (lane >= 0 && !topLanes[lane]) {
      topLanes[lane] = true;
      addScore(50);
      if (topLanes[0] && topLanes[1] && topLanes[2]) {
        mult = min(5, mult + 1);
        addScore(150);
        for (int i = 0; i < 3; i++) topLanes[i] = false;
      }
    }
  }
}

static void checkDropTargets() {
  if (inLane) return;
  for (int i = 0; i < 3; i++) {
    float ty = 122.0f + i * 14.0f;
    if (dropL[i] && bx < PLAY_L + 6 + BALL_R && fabsf(by - ty) < 6 + BALL_R) {
      dropL[i] = false;
      vx = fabsf(vx) + 0.6f;
      addScore(75);
      if (!dropL[0] && !dropL[1] && !dropL[2]) {
        addScore(300);
        for (int k = 0; k < 3; k++) dropL[k] = true;
      }
    }
    if (dropR[i] && bx > PLAY_R - 6 - BALL_R && fabsf(by - ty) < 6 + BALL_R) {
      dropR[i] = false;
      vx = -fabsf(vx) - 0.6f;
      addScore(75);
      if (!dropR[0] && !dropR[1] && !dropR[2]) {
        addScore(300);
        for (int k = 0; k < 3; k++) dropR[k] = true;
      }
    }
  }
}

static void tick() {
  for (int i = 0; i < 3; i++) if (bumpers[i].hitT > 0) bumpers[i].hitT--;
  if (slingLT > 0) slingLT--;
  if (slingRT > 0) slingRT--;

  // Flippers
  float targetL = bL.stable ? UP_ANG : REST_ANG;
  float targetR = bR.stable ? UP_ANG : REST_ANG;
  float prevL = flipLAng, prevR = flipRAng;
  if (flipLAng < targetL) flipLAng = min(targetL, flipLAng + FLIP_SPEED);
  else if (flipLAng > targetL) flipLAng = max(targetL, flipLAng - FLIP_SPEED);
  if (flipRAng < targetR) flipRAng = min(targetR, flipRAng + FLIP_SPEED);
  else if (flipRAng > targetR) flipRAng = max(targetR, flipRAng - FLIP_SPEED);
  flipLOm = flipLAng - prevL;
  flipROm = flipRAng - prevR;

  // Plunger logic when ball is in the right lane
  if (inLane && by >= PLUNGER_Y - BALL_R - 2 && vy >= 0) {
    by = PLUNGER_Y - BALL_R;
    vy = 0;
    vx = 0;
    if (bR.stable) {
      pulling = true;
      plungerPull = min(1.0f, plungerPull + 0.045f);
    } else if (pulling) {
      pulling = false;
      if (plungerPull > 0.08f) {
        vy = -(5.0f + plungerPull * 4.2f);
        vx = -0.05f * rnd(4);
      }
      plungerPull = 0.0f;
    }
    return;
  }

  // Gravity & sub-stepped physics
  vy += GRAVITY;
  clampSpeed();

  const int steps = 4;
  for (int s = 0; s < steps; s++) {
    float prevY = by;
    bx += vx / steps;
    by += vy / steps;

    // Outer walls
    if (bx < PLAY_L + BALL_R) { bx = PLAY_L + BALL_R; vx = fabsf(vx) * 0.72f; }
    if (bx > LANE_R - BALL_R) { bx = LANE_R - BALL_R; vx = -fabsf(vx) * 0.72f; }
    if (by < 40 + BALL_R) { by = 40 + BALL_R; vy = fabsf(vy) * 0.7f; }

    // Plunger lane divider at x = PLAY_R (120)
    if (by >= 72) {
      // Solid divider below y = 72
      if (inLane) {
        if (bx < PLAY_R + BALL_R) { bx = PLAY_R + BALL_R; vx = fabsf(vx) * 0.7f; }
      } else {
        if (bx > PLAY_R - BALL_R) { bx = PLAY_R - BALL_R; vx = -fabsf(vx) * 0.7f; }
      }
    } else {
      // One-way gate above y = 72: ball exiting lane (vx < 0) enters main playfield
      if (inLane && bx < PLAY_R - BALL_R) {
        inLane = false;
      } else if (!inLane && bx > PLAY_R - BALL_R && vx > 0 && by >= 54) {
        bx = PLAY_R - BALL_R;
        vx = -fabsf(vx) * 0.7f;
      }
    }

    // Plunger floor
    if (inLane && by > PLUNGER_Y - BALL_R) {
      by = PLUNGER_Y - BALL_R;
      vy = 0;
    }

    // Static segments
    for (int i = 0; i < NUM_SEGS; i++) {
      collideSegment(SEGS[i].x1, SEGS[i].y1, SEGS[i].x2, SEGS[i].y2, SEGS[i].bounce, SEGS[i].kind);
    }

    // Bumpers
    for (int i = 0; i < 3; i++) {
      float dx = bx - bumpers[i].x, dy = by - bumpers[i].y;
      float d = sqrtf(dx * dx + dy * dy);
      float minD = bumpers[i].r + BALL_R;
      if (d < minD && d > 1e-4f) {
        float nx = dx / d, ny = dy / d;
        bx = bumpers[i].x + nx * minD;
        by = bumpers[i].y + ny * minD;
        float spd = max(3.6f, sqrtf(vx * vx + vy * vy));
        vx = nx * spd;
        vy = ny * spd;
        clampSpeed();
        bumpers[i].hitT = 8;
        addScore(50);
      }
    }

    // Flippers
    collideFlipper(FLIP_L_X, FLIP_Y, flipLAng, flipLOm, false);
    collideFlipper(FLIP_R_X, FLIP_Y, flipRAng, flipROm, true);

    checkTopLanes(prevY);
    checkDropTargets();

    // Drain at bottom
    if (!inLane && by > 240 + BALL_R) {
      if (--ballsLeft <= 0) {
        gameOver();
        return;
      }
      mult = 1;
      serveBall();
      return;
    }
  }

  // Anti-stuck safeguard
  if (!inLane && fabsf(vx) < 0.08f && fabsf(vy) < 0.08f) {
    if (++stuckT > 40) {
      vx += (rnd(2) ? 0.8f : -0.8f);
      vy -= 1.2f;
      stuckT = 0;
    }
  } else {
    stuckT = 0;
  }
}

// ---------------- rendering ----------------
static void drawFlipper(float px, float py, float ang, bool isRight, bool active) {
  float dir = isRight ? -1.0f : 1.0f;
  float tx = px + dir * FLIP_LEN * cosf(ang);
  float ty = py + FLIP_LEN * sinf(ang);
  uint16_t col = active ? FLIP_HI : FLIP_COL;
  // Thick capsule via 3 parallel lines + end circles
  for (int dy = -2; dy <= 2; dy++) {
    spr.drawLine((int)px, (int)py + dy, (int)tx, (int)ty + dy, col);
  }
  spr.fillCircle((int)px, (int)py, 3, TFT_WHITE);
  spr.fillCircle((int)tx, (int)ty, 2, col);
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(TABLE_BG);

  // Top lanes indicators
  const int laneX[3] = {31, 61, 91};
  for (int i = 0; i < 3; i++) {
    uint16_t c = topLanes[i] ? TFT_YELLOW : 0x2945;
    spr.fillCircle(laneX[i], 58, 3, c);
  }

  // Drop targets
  for (int i = 0; i < 3; i++) {
    int ty = 117 + i * 14;
    spr.fillRect(PLAY_L + 1, ty, 3, 10, dropL[i] ? TFT_GREEN : 0x18E3);
    spr.fillRect(PLAY_R - 4, ty, 3, 10, dropR[i] ? TFT_GREEN : 0x18E3);
  }

  // Static walls & slingshots
  for (int i = 0; i < NUM_SEGS; i++) {
    uint16_t c = WALL_COL;
    if (SEGS[i].kind == 1) c = slingLT ? TFT_WHITE : TFT_MAGENTA;
    if (SEGS[i].kind == 2) c = slingRT ? TFT_WHITE : TFT_MAGENTA;
    spr.drawLine((int)SEGS[i].x1, (int)SEGS[i].y1, (int)SEGS[i].x2, (int)SEGS[i].y2, c);
  }
  spr.drawFastVLine(PLAY_L, 64, 134, WALL_COL);
  spr.drawFastVLine(LANE_R, 64, 174, WALL_COL);
  spr.drawFastVLine(PLAY_R, 72, 166, WALL_COL);
  // One-way gate hint
  spr.drawFastVLine(PLAY_R, 54, 18, 0x39E7);

  // Slingshot fill accent
  spr.drawLine(12, 168, 28, 198, slingLT ? TFT_WHITE : TFT_RED);
  spr.drawLine(110, 168, 94, 198, slingRT ? TFT_WHITE : TFT_RED);

  // Bumpers
  for (int i = 0; i < 3; i++) {
    uint16_t c = bumpers[i].hitT ? TFT_WHITE : bumpers[i].col;
    spr.fillCircle((int)bumpers[i].x, (int)bumpers[i].y, (int)bumpers[i].r, c);
    spr.drawCircle((int)bumpers[i].x, (int)bumpers[i].y, (int)bumpers[i].r - 2, TABLE_BG);
  }

  // Plunger
  int py = PLUNGER_Y + (int)(plungerPull * 10);
  spr.fillRect(LANE_X - 3, py, 7, 3, TFT_RED);
  for (int sy = py + 3; sy < 239; sy += 3) {
    spr.drawFastHLine(LANE_X - 2, sy, 5, TFT_LIGHTGREY);
  }
  if (inLane && by >= PLUNGER_Y - BALL_R - 2 && !pulling) {
    // Subtle launch prompt arrow
    if ((millis() / 350) & 1) {
      spr.fillTriangle(LANE_X, 200, LANE_X - 3, 206, LANE_X + 3, 206, TFT_GREEN);
    }
  }

  // Flippers
  drawFlipper(FLIP_L_X, FLIP_Y, flipLAng, false, bL.stable);
  drawFlipper(FLIP_R_X, FLIP_Y, flipRAng, true, bR.stable);

  // Ball
  if (state == PLAY) {
    spr.fillCircle((int)roundf(bx), (int)roundf(by), BALL_R, TFT_WHITE);
    spr.drawPixel((int)roundf(bx) - 1, (int)roundf(by) - 1, TFT_LIGHTGREY);
  }

  // Header
  drawScoreHeader(score, hiScore);
  for (int i = 0; i < ballsLeft; i++) {
    spr.fillCircle(128 - i * 9, 9, 3, TFT_WHITE);
  }
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(mult > 1 ? TFT_YELLOW : TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "%dx", mult);
  spr.drawString(buf, 133, 22, 2);

  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("PINBALL", 67, 18, 4);

  // Decorative mini table preview
  spr.fillCircle(47, 66, 6, TFT_MAGENTA);
  spr.fillCircle(87, 66, 6, TFT_CYAN);
  spr.fillCircle(67, 82, 6, TFT_YELLOW);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R", "Pull R"};
  const char *acts[] = {"left flipper", "right flipper", "launch ball"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 106 + i * 19, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 58, 106 + i * 19, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Light top lanes for 5x", 67, 168, 1);

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

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // Bumper at top
  uint16_t bCol = (!dim && (t % 900) < 150) ? TFT_WHITE : c(TFT_MAGENTA);
  spr.fillCircle(x, y - 9, 4, bCol);
  // Flippers at bottom
  int lift = (!dim && (t % 900) > 720) ? 4 : 0;
  spr.drawLine(x - 10, y + 8, x - 2, y + 12 - lift, c(TFT_ORANGE));
  spr.drawLine(x + 10, y + 8, x + 2, y + 12 - lift, c(TFT_ORANGE));
  // Bouncing ball between bumper and flippers
  int by = y - 3 + (int)roundf(11 * triWave(t, 900));
  spr.fillCircle(x + (int)roundf(3 * (triWave(t + 200, 1400) - 0.5f)), by, 2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("pinball_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
      dirty = true;
    }
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace pinball

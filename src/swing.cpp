// Swing (Grapple Rope)
//
// Controls (two buttons):
//   Hold L or R : grapple the nearest anchor ahead; the rope reels in while held
// Release on the upward arc to fly forward. The screen keeps scrolling: don't
// fall off the bottom or get left behind.

#include "common.h"

namespace swing {

static const int NUM_ANCHORS = 5;
static const uint32_t TICK_MS = 16;

struct Anchor { float x, y; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Anchor anchors[NUM_ANCHORS];
static float px, py, vx, vy, camX, scrollSpd;
static int hookedIdx;
static float ropeLen;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false, fellOff = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("swing_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  camX = 0;
  px = 30;
  py = 110;
  vx = 2.2f;
  vy = -1.0f;
  scrollSpd = 0.3f;
  hookedIdx = -1;
  newHigh = fellOff = false;
  for (int i = 0; i < NUM_ANCHORS; i++) {
    anchors[i] = {60.0f + i * 55.0f, (float)(64 + rnd(26))};
  }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  bool wantHook = bL.stable || bR.stable;

  if (wantHook) {
    if (hookedIdx < 0) {
      // nearest anchor that is ahead of and above the player
      int best = -1;
      float bestD = 9999.0f;
      for (int i = 0; i < NUM_ANCHORS; i++) {
        float dx = anchors[i].x - px, dy = anchors[i].y - py;
        if (dx > 0 && dy < 0) {
          float d = hypotf(dx, dy);
          if (d < bestD) { bestD = d; best = i; }
        }
      }
      if (best >= 0 && bestD < 110.0f) {
        hookedIdx = best;
        ropeLen = bestD;
      }
    }
  } else {
    hookedIdx = -1;
  }

  vy += 0.12f;                                            // gravity
  px += vx;
  py += vy;

  if (hookedIdx >= 0) {
    // reeling in pumps energy into the swing (taut-rope jerks lose some)
    ropeLen = max(26.0f, ropeLen - 0.45f);
    const Anchor &a = anchors[hookedIdx];
    float dx = px - a.x, dy = py - a.y;
    float d = hypotf(dx, dy);
    if (d > ropeLen && d > 0.01f) {
      float nx = dx / d, ny = dy / d;
      px = a.x + nx * ropeLen;
      py = a.y + ny * ropeLen;
      float vn = vx * nx + vy * ny;
      if (vn > 0) {
        vx -= vn * nx;
        vy -= vn * ny;
      }
    }
  }

  float spd = hypotf(vx, vy);
  if (spd > 4.5f) { vx *= 4.5f / spd; vy *= 4.5f / spd; }

  // the view creeps forward on its own (faster over time) and follows the player
  scrollSpd = min(1.0f, scrollSpd + 0.00014f);
  camX += scrollSpd;
  float screenX = px - camX;
  if (screenX > 50.0f) camX += screenX - 50.0f;
  score = (uint32_t)(camX / 8.0f);

  // Recycle anchors behind the camera; gaps widen with distance
  float maxX = anchors[0].x;
  for (int i = 1; i < NUM_ANCHORS; i++) if (anchors[i].x > maxX) maxX = anchors[i].x;
  for (int i = 0; i < NUM_ANCHORS; i++) {
    if (anchors[i].x - camX < -20.0f) {
      if (hookedIdx == i) hookedIdx = -1;
      float gap = 46.0f + min(20.0f, score * 0.03f) + rnd(20);
      anchors[i] = {maxX + gap, (float)(58 + rnd(34))};
      maxX = anchors[i].x;
    }
  }

  if (py < HEADER_H + 4) { py = HEADER_H + 4; if (vy < 0) vy = 0; }
  if (py > 244) { fellOff = true; gameOver(); }
  else if (px - camX < -6.0f) gameOver();
}

static void drawGame() {
  spr.fillSprite(0x0842);

  for (int i = 0; i < NUM_ANCHORS; i++) {
    int ax = (int)(anchors[i].x - camX), ay = (int)anchors[i].y;
    if (ax < -10 || ax > 145) continue;
    spr.fillCircle(ax, ay, 4, (i == hookedIdx) ? TFT_YELLOW : TFT_CYAN);
  }

  int sx = (int)(px - camX), sy = (int)py;
  if (hookedIdx >= 0) {
    spr.drawLine(sx, sy, (int)(anchors[hookedIdx].x - camX), (int)anchors[hookedIdx].y, TFT_WHITE);
  }
  spr.fillCircle(sx, sy, 5, TFT_ORANGE);
  spr.drawCircle(sx, sy, 5, TFT_YELLOW);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox(fellOff ? "FELL!" : "LEFT BEHIND", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("SWING", 67, 18, 4);

  spr.fillCircle(67, 50, 4, TFT_YELLOW);
  spr.drawLine(67, 50, 56, 72, TFT_WHITE);
  spr.fillCircle(56, 72, 5, TFT_ORANGE);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Hold L/R", 8, 96, 2);
  spr.drawString("Release", 8, 116, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("grapple", 68, 96, 2);
  spr.drawString("fly", 68, 116, 2);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Rope reels in on hold", 67, 142, 1);
  spr.drawString("Let go on the upswing!", 67, 154, 1);

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
  spr.fillCircle(x, y - 10, 3, c(TFT_CYAN));
  float a = PI / 2 + 0.7f * (triWave(t, 1000) - 0.5f) * 2.0f;
  int bx = x + (int)roundf(cosf(a) * 14), by = y - 10 + (int)roundf(sinf(a) * 14);
  spr.drawLine(x, y - 10, bx, by, c(TFT_WHITE));
  spr.fillCircle(bx, by, 3, c(TFT_ORANGE));
}

void begin() {
  hiScore = prefs.getUInt("swing_hi", 0);
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

}  // namespace swing

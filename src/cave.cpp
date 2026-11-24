// Cave (Jetpack Flyer)
//
// Controls (two buttons):
//   Hold L or R : thrust upward (release to descend)
// Fly through a narrowing procedural cave and dodge stalactite pillars.

#include "common.h"

namespace cave {

static const int COLS = 46;                               // 46 x 3 px = 138 px
static const int SLICE_W = 3;
static const int SHIP_X = 28, SHIP_R = 4;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int16_t ceilY[COLS], floorY[COLS];
static float shipY, vy, camScroll, dist;
static int curCeil, curFloor, pillarTimer;
static int pilX, pilTop, pilBot;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void genSlice(int idx) {
  int minGap = max(48, 108 - (int)(score / 25));
  int dC = rnd(7) - 3;
  int dF = rnd(7) - 3;
  curCeil = constrain(curCeil + dC, HEADER_H + 6, 140);
  curFloor = constrain(curFloor + dF, curCeil + minGap, 234);
  if (curFloor - curCeil < minGap) curFloor = min(234, curCeil + minGap);
  ceilY[idx] = curCeil;
  floorY[idx] = curFloor;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("cave_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  dist = 0;
  camScroll = 0;
  shipY = 135;
  vy = 0;
  curCeil = HEADER_H + 22;
  curFloor = 216;
  pillarTimer = 45;
  pilX = -20;
  newHigh = false;
  for (int i = 0; i < COLS; i++) {
    ceilY[i] = curCeil;
    floorY[i] = curFloor;
  }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  bool thrust = bL.stable || bR.stable;
  vy += thrust ? -0.24f : 0.20f;
  vy = constrain(vy, -3.6f, 3.6f);
  shipY += vy;

  float spd = min(3.4f, 1.8f + score * 0.0012f);
  camScroll += spd;
  dist += spd;
  score = (uint32_t)(dist / 5);

  while (camScroll >= SLICE_W) {
    camScroll -= SLICE_W;
    for (int i = 0; i < COLS - 1; i++) {
      ceilY[i] = ceilY[i + 1];
      floorY[i] = floorY[i + 1];
    }
    genSlice(COLS - 1);
  }

  // Occasional floating rock pillar
  if (pilX > -12) {
    pilX -= (int)roundf(spd);
  } else if (--pillarTimer <= 0) {
    pillarTimer = 35 + rnd(35);
    pilX = 136;
    int mid = (ceilY[COLS - 2] + floorY[COLS - 2]) / 2 + (rnd(25) - 12);
    pilTop = mid - 12;
    pilBot = mid + 12;
  }

  int sIdx = constrain((SHIP_X + (int)camScroll) / SLICE_W, 0, COLS - 1);
  if (shipY - SHIP_R <= ceilY[sIdx] || shipY + SHIP_R >= floorY[sIdx]) {
    gameOver();
    return;
  }
  if (pilX > -12 && SHIP_X + SHIP_R > pilX && SHIP_X - SHIP_R < pilX + 8 &&
      shipY + SHIP_R > pilTop && shipY - SHIP_R < pilBot) {
    gameOver();
  }
}

static void drawGame() {
  spr.fillSprite(0x0821);

  for (int i = 0; i < COLS; i++) {
    int x = i * SLICE_W - (int)camScroll;
    spr.fillRect(x, HEADER_H, SLICE_W, ceilY[i] - HEADER_H, 0x5A02);
    spr.fillRect(x, floorY[i], SLICE_W, 240 - floorY[i], 0x5A02);
  }

  if (pilX > -12) {
    spr.fillRoundRect(pilX, pilTop, 8, pilBot - pilTop, 2, 0x7AE3);
  }

  // Ship + jetpack flame
  bool thrust = bL.stable || bR.stable;
  if (thrust && state == PLAY) {
    spr.fillTriangle(SHIP_X - 6, (int)shipY + 2, SHIP_X - 2, (int)shipY + 4,
                     SHIP_X - 10, (int)shipY + 7 + rnd(4), TFT_ORANGE);
  }
  spr.fillRoundRect(SHIP_X - 5, (int)shipY - 4, 11, 8, 3, TFT_YELLOW);
  spr.fillRect(SHIP_X + 2, (int)shipY - 2, 3, 3, TFT_CYAN);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("CRASHED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("CAVE", 67, 18, 4);

  spr.fillRoundRect(58, 60, 14, 9, 3, TFT_YELLOW);
  spr.fillTriangle(58, 66, 52, 72, 60, 68, TFT_ORANGE);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Hold L/R", 8, 102, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("thrust up", 70, 102, 2);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Release to descend", 67, 138, 1);

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
  spr.fillRect(x - 11, y - 13, 22, 5, c(0x5A02));
  spr.fillRect(x - 11, y + 9, 22, 5, c(0x5A02));
  int sy = y - 2 + (int)roundf(5 * (triWave(t, 1100) - 0.5f));
  spr.fillRoundRect(x - 4, sy - 3, 9, 6, 2, c(TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("cave_hi", 0);
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

}  // namespace cave

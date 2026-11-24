// Frog (Frogger)
//
// Controls (two buttons):
//   Tap L or R : hop forward
//   Hold L / R : hop left / right
// Cross traffic and ride logs to fill all 5 lilypad homes! 3 lives.

#include "common.h"

namespace frog {

static const int CELL = 15, COLS = 9, ROWS = 13, BOARD_Y = 44;
static const uint32_t HOLD_MS = 190, TICK_MS = 16;

// Rows 0..12 (top to bottom):
// 0: homes (cols 0,2,4,6,8), 1..4: river logs, 5: median, 6..11: road cars, 12: start
struct LaneObj { float x; int w; uint16_t col; };
struct Lane { float vx; int n; LaneObj obj[3]; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Lane lanes[ROWS];
static bool homes[5];
static int row, lives, level;
static float fx;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static void initLanes() {
  float mul = 1.0f + (level - 1) * 0.18f;
  const float speeds[ROWS] = {0, 0.55f, -0.7f, 0.6f, -0.48f, 0, -0.75f, 0.6f, -0.9f, 0.5f, -0.65f, 0.7f, 0};
  const uint16_t cols[4] = {TFT_RED, TFT_YELLOW, TFT_CYAN, TFT_ORANGE};
  for (int r = 0; r < ROWS; r++) {
    lanes[r].vx = speeds[r] * mul;
    lanes[r].n = 0;
    if (r >= 1 && r <= 4) {
      lanes[r].n = 2;
      int lw = (r == 2) ? 30 : 38;
      lanes[r].obj[0] = {10, lw, 0x8A22};
      lanes[r].obj[1] = {85, lw, 0x8A22};
    } else if (r >= 6 && r <= 11) {
      lanes[r].n = 2;
      int cw = (r == 8) ? 24 : 18;
      lanes[r].obj[0] = {(float)((r * 23) % 80), cw, cols[r % 4]};
      lanes[r].obj[1] = {(float)(((r * 23) % 80) + 76), cw, cols[(r + 1) % 4]};
    }
  }
}

static void resetFrog() {
  row = 12;
  fx = 4 * CELL + CELL / 2.0f;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("frog_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  level = 1;
  newHigh = false;
  for (int i = 0; i < 5; i++) homes[i] = false;
  initLanes();
  resetFrog();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void loseLife() {
  if (--lives <= 0) gameOver();
  else resetFrog();
}

static void hopForward() {
  if (row > 0) {
    row--;
    score += 10;
  }
  if (row == 0) {
    int slot = -1;
    int col = (int)(fx / CELL);
    if ((col & 1) == 0) slot = col / 2;
    if (slot >= 0 && slot < 5 && !homes[slot]) {
      homes[slot] = true;
      score += 100;
      bool all = true;
      for (int i = 0; i < 5; i++) if (!homes[i]) all = false;
      if (all) {
        score += 300;
        level++;
        for (int i = 0; i < 5; i++) homes[i] = false;
        initLanes();
      }
      resetFrog();
    } else {
      loseLife();
    }
  }
}

static void tick(uint32_t now, int eL, int eR) {
  if (bL.stable && !bL.consumed && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) {
    bL.consumed = true;
    fx = max(CELL / 2.0f, fx - CELL);
  }
  if (bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) {
    bR.consumed = true;
    fx = min(135.0f - CELL / 2.0f, fx + CELL);
  }
  if ((eL == 2 && !bL.consumed) || (eR == 2 && !bR.consumed)) {
    hopForward();
    if (state != PLAY) return;
  }

  for (int r = 0; r < ROWS; r++) {
    for (int i = 0; i < lanes[r].n; i++) {
      lanes[r].obj[i].x += lanes[r].vx;
      if (lanes[r].vx > 0 && lanes[r].obj[i].x > 145) lanes[r].obj[i].x = -lanes[r].obj[i].w - 10;
      if (lanes[r].vx < 0 && lanes[r].obj[i].x + lanes[r].obj[i].w < -10) lanes[r].obj[i].x = 145;
    }
  }

  if (row >= 1 && row <= 4) {
    bool onLog = false;
    for (int i = 0; i < lanes[row].n; i++) {
      if (fx >= lanes[row].obj[i].x - 2 && fx <= lanes[row].obj[i].x + lanes[row].obj[i].w + 2) {
        onLog = true;
        break;
      }
    }
    if (!onLog) { loseLife(); return; }
    fx += lanes[row].vx;
    if (fx < 2 || fx > 133) { loseLife(); return; }
  } else if (row >= 6 && row <= 11) {
    for (int i = 0; i < lanes[row].n; i++) {
      if (fx + 4 >= lanes[row].obj[i].x && fx - 4 <= lanes[row].obj[i].x + lanes[row].obj[i].w) {
        loseLife();
        return;
      }
    }
  }
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);

  for (int r = 0; r < ROWS; r++) {
    int y = BOARD_Y + r * CELL;
    uint16_t bg = (r == 0) ? 0x1B42 : (r <= 4) ? 0x0015 : (r == 5 || r == 12) ? 0x39E7 : 0x2104;
    spr.fillRect(0, y, 135, CELL, bg);
    if (r == 0) {
      for (int i = 0; i < 5; i++) {
        int hx = i * 2 * CELL;
        spr.fillRect(hx + 1, y + 1, CELL - 2, CELL - 2, 0x0015);
        if (homes[i]) spr.fillCircle(hx + CELL / 2, y + CELL / 2, 4, TFT_GREEN);
      }
    }
    for (int i = 0; i < lanes[r].n; i++) {
      spr.fillRoundRect((int)lanes[r].obj[i].x, y + 2, lanes[r].obj[i].w, CELL - 4, 3, lanes[r].obj[i].col);
    }
  }

  int fy = BOARD_Y + row * CELL + CELL / 2;
  spr.fillCircle((int)fx, fy, 5, TFT_GREEN);
  spr.fillCircle((int)fx - 2, fy - 3, 1, TFT_WHITE);
  spr.fillCircle((int)fx + 2, fy - 3, 1, TFT_WHITE);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_GREEN);
  if (state == GAMEOVER) drawGameOverBox("SPLASH!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("FROG", 67, 18, 4);

  spr.fillCircle(67, 62, 8, TFT_GREEN);
  spr.fillCircle(63, 57, 2, TFT_WHITE);
  spr.fillCircle(71, 57, 2, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L/R"};
  const char *acts[] = {"hop forward", "hop sideways"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Fill all 5 lilypads!", 67, 148, 1);

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
  spr.fillRect(x - 11, y + 6, 22, 4, c(0x0015));
  int jy = y - (int)roundf(5 * triWave(t, 700));
  spr.fillCircle(x, jy, 5, c(TFT_GREEN));
}

void begin() {
  hiScore = prefs.getUInt("frog_hi", 0);
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    qL.push(eL);
    qR.push(eR);
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(now, qL.pop(), qR.pop());
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

}  // namespace frog

// Tron (Light Cycles)
//
// Controls (two buttons):
//   Tap L : turn 90 deg left
//   Tap R : turn 90 deg right
// Leave glowing light walls and trap the AI cycle! Clearing rounds speeds up.

#include "common.h"

namespace tron {

static const int CELL = 3, COLS = 45, ROWS = 66, BOARD_Y = 40;
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t trail[ROWS][COLS];                         // 0 = free, 1 = P1, 2 = AI
static int p1x, p1y, p1d, aix, aiy, aid;
static int stepT, roundNum;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void startRound() {
  memset(trail, 0, sizeof trail);
  p1x = COLS / 2; p1y = ROWS - 10; p1d = 0;
  aix = COLS / 2; aiy = 9; aid = 2;
  trail[p1y][p1x] = 1;
  trail[aiy][aix] = 2;
  stepT = 0;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("tron_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  roundNum = 1;
  newHigh = false;
  startRound();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool blocked(int x, int y) {
  return x < 0 || x >= COLS || y < 0 || y >= ROWS || trail[y][x] != 0;
}

static void tick(int eL, int eR) {
  if (eL == 1) p1d = (p1d + 3) & 3;
  if (eR == 1) p1d = (p1d + 1) & 3;

  int delayTicks = max(2, 5 - roundNum / 3);
  if (++stepT < delayTicks) return;
  stepT = 0;

  // AI picks open direction with most lookahead space
  int bestD = aid, bestScore = -1;
  for (int k = 0; k < 3; k++) {
    int d = (aid + (k == 0 ? 0 : k == 1 ? 1 : 3)) & 3;
    int nx = aix + DX[d], ny = aiy + DY[d];
    if (blocked(nx, ny)) continue;
    int look = 0;
    int tx = nx, ty = ny;
    while (look < 12 && !blocked(tx, ty)) { look++; tx += DX[d]; ty += DY[d]; }
    int sc = look * 10 + (d == aid ? 3 : 0) + rnd(4);
    if (sc > bestScore) { bestScore = sc; bestD = d; }
  }
  aid = bestD;

  p1x += DX[p1d]; p1y += DY[p1d];
  aix += DX[aid]; aiy += DY[aid];

  bool p1Dead = blocked(p1x, p1y);
  bool aiDead = blocked(aix, aiy) || (p1x == aix && p1y == aiy);

  if (p1Dead) { gameOver(); return; }
  trail[p1y][p1x] = 1;

  if (aiDead) {
    score += 100 * roundNum;
    roundNum++;
    startRound();
    return;
  }
  trail[aiy][aix] = 2;
  score++;
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(TFT_BLACK);
  spr.drawRect(0, BOARD_Y - 1, COLS * CELL, ROWS * CELL + 2, 0x2945);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (trail[r][c] == 1) spr.fillRect(c * CELL, BOARD_Y + r * CELL, CELL, CELL, TFT_CYAN);
      else if (trail[r][c] == 2) spr.fillRect(c * CELL, BOARD_Y + r * CELL, CELL, CELL, TFT_ORANGE);
    }
  }
  spr.fillRect(p1x * CELL, BOARD_Y + p1y * CELL, CELL, CELL, TFT_WHITE);
  spr.fillRect(aix * CELL, BOARD_Y + aiy * CELL, CELL, CELL, TFT_RED);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_CYAN);
  snprintf(buf, sizeof buf, "Rnd %d", roundNum);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("DEREZZED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("TRON", 67, 18, 4);

  spr.drawFastHLine(28, 60, 32, TFT_CYAN);
  spr.drawFastVLine(60, 60, 14, TFT_CYAN);
  spr.drawFastHLine(75, 74, 32, TFT_ORANGE);
  spr.drawFastVLine(75, 60, 14, TFT_ORANGE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"turn left", "turn right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Trap the orange cycle!", 67, 148, 1);

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
  int l = 6 + ((t / 80) % 8);
  spr.drawFastHLine(x - 10, y + 6, l, c(TFT_CYAN));
  spr.drawFastVLine(x - 10 + l, y - 4, 10, c(TFT_CYAN));
  spr.drawFastHLine(x - 2, y - 8, 10, c(TFT_ORANGE));
}

void begin() {
  hiScore = prefs.getUInt("tron_hi", 0);
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
      tick(qL.pop(), qR.pop());
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

}  // namespace tron

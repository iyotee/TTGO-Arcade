// Lights (Lights Out 5x5)
//
// Controls (two buttons):
//   Tap L  : next cell (Hold L: next row)
//   Tap R  : toggle cell + neighbors
// Turn off all 25 lights before running out of moves!

#include "common.h"

namespace lights {

static const int N = 5, CELL = 23, PITCH = 25;
static const int GRID_X = 6, GRID_Y = 56;
static const uint32_t HOLD_MS = 240;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static bool on[N][N];
static int curR, curC, movesLeft, level;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void toggleAt(int r, int c) {
  const int dr[5] = {0, -1, 1, 0, 0}, dc[5] = {0, 0, 0, -1, 1};
  for (int d = 0; d < 5; d++) {
    int nr = r + dr[d], nc = c + dc[d];
    if (nr >= 0 && nr < N && nc >= 0 && nc < N) on[nr][nc] = !on[nr][nc];
  }
}

static void initPuzzle() {
  memset(on, 0, sizeof on);
  int scrambles = min(14, 3 + level);
  for (int i = 0; i < scrambles; i++) toggleAt(rnd(N), rnd(N));
  bool any = false;
  for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) if (on[r][c]) any = true;
  if (!any) toggleAt(2, 2);
  movesLeft = scrambles * 2 + 8;
  curR = curC = 2;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("lights_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  level = 1;
  newHigh = false;
  initPuzzle();
  state = PLAY;
  dirty = true;
}

static void drawGame() {
  char buf[20];
  spr.fillSprite(TFT_BLACK);

  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      int x = GRID_X + c * PITCH, y = GRID_Y + r * PITCH;
      spr.fillRoundRect(x, y, CELL, CELL, 3, on[r][c] ? TFT_YELLOW : 0x2104);
      if (r == curR && c == curC && state == PLAY) {
        spr.drawRoundRect(x - 1, y - 1, CELL + 2, CELL + 2, 4, TFT_CYAN);
      }
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 12, 214, 2);
  spr.drawString("R", 74, 214, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("move", 24, 214, 2);
  spr.drawString("toggle", 86, 214, 2);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(movesLeft <= 4 ? TFT_RED : TFT_CYAN);
  snprintf(buf, sizeof buf, "Moves %d", movesLeft);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("OUT OF MOVES", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("LIGHTS", 67, 18, 4);

  spr.fillRoundRect(42, 52, 22, 22, 3, TFT_YELLOW);
  spr.fillRoundRect(70, 52, 22, 22, 3, 0x2104);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"move cursor", "toggle +"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Turn all lights OFF!", 67, 148, 1);

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
  bool alt = !dim && ((t / 500) & 1);
  spr.fillRoundRect(x - 9, y - 9, 8, 8, 2, c(alt ? TFT_YELLOW : 0x2104));
  spr.fillRoundRect(x + 1, y - 9, 8, 8, 2, c(TFT_YELLOW));
  spr.fillRoundRect(x - 9, y + 1, 8, 8, 2, c(TFT_YELLOW));
  spr.fillRoundRect(x + 1, y + 1, 8, 8, 2, c(alt ? 0x2104 : TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("lights_hi", 0);
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
    if (eL == 1) bL.nextRepeat = now + HOLD_MS;
    if (bL.stable && (int32_t)(now - bL.nextRepeat) >= 0) {
      bL.nextRepeat = now + 160;
      bL.consumed = true;
      curR = (curR + 1) % N;
      dirty = true;
    }
    if (eL == 2 && !bL.consumed) {
      int idx = (curR * N + curC + 1) % (N * N);
      curR = idx / N;
      curC = idx % N;
      dirty = true;
    }
    if (eR == 2) {
      toggleAt(curR, curC);
      movesLeft--;
      bool allOff = true;
      for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) if (on[r][c]) allOff = false;
      if (allOff) {
        score += 100 + movesLeft * 15;
        level++;
        initPuzzle();
      } else if (movesLeft <= 0) {
        gameOver();
      }
      dirty = true;
    }
    if (idleTooLong(now, lastInput)) goToSleep();
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace lights

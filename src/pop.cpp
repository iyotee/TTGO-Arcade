// Pop (SameGame)
//
// Controls (two buttons):
//   Tap L  : next tile (Hold L: next row)
//   Tap R  : pop connected group of 2+ matching tiles
// Clear big groups for huge scores (n*(n-1)*5). New board when no moves remain!

#include "common.h"

namespace pop {

static const int COLS = 9, ROWS = 11, CELL = 14;
static const int GRID_X = 5, GRID_Y = 46;
static const uint32_t HOLD_MS = 240;

static const uint16_t TILE_COL[4] = {TFT_RED, TFT_CYAN, TFT_YELLOW, TFT_GREEN};

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t grid[ROWS][COLS];                          // 0 = empty, 1..4 = color
static int curR, curC, movesLeft;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static bool hasGroupAt(int r, int c) {
  uint8_t v = grid[r][c];
  if (!v) return false;
  if (r > 0 && grid[r - 1][c] == v) return true;
  if (r + 1 < ROWS && grid[r + 1][c] == v) return true;
  if (c > 0 && grid[r][c - 1] == v) return true;
  if (c + 1 < COLS && grid[r][c + 1] == v) return true;
  return false;
}

static bool anyGroup() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (hasGroupAt(r, c)) return true;
  return false;
}

static void moveCursor(int step) {
  int idx = curR * COLS + curC;
  for (int k = 1; k <= ROWS * COLS; k++) {
    int ni = (idx + k * step) % (ROWS * COLS);
    int r = ni / COLS, c = ni % COLS;
    if (grid[r][c]) { curR = r; curC = c; return; }
  }
}

static void initBoard() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) grid[r][c] = 1 + rnd(4);
  curR = ROWS / 2;
  curC = COLS / 2;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("pop_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  movesLeft = 24;
  newHigh = false;
  initBoard();
  state = PLAY;
  dirty = true;
}

static void popGroup() {
  if (!hasGroupAt(curR, curC)) return;
  uint8_t col = grid[curR][curC];
  uint8_t qR[ROWS * COLS], qC[ROWS * COLS];
  int head = 0, tail = 0;
  grid[curR][curC] = 0;
  qR[tail] = curR; qC[tail++] = curC;

  while (head < tail) {
    int r = qR[head], c = qC[head++];
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    for (int d = 0; d < 4; d++) {
      int nr = r + dr[d], nc = c + dc[d];
      if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && grid[nr][nc] == col) {
        grid[nr][nc] = 0;
        qR[tail] = nr; qC[tail++] = nc;
      }
    }
  }

  score += (uint32_t)tail * (tail - 1) * 5;
  movesLeft--;

  // Gravity down
  for (int c = 0; c < COLS; c++) {
    int dst = ROWS - 1;
    for (int r = ROWS - 1; r >= 0; r--) {
      if (grid[r][c]) {
        uint8_t v = grid[r][c];
        grid[r][c] = 0;
        grid[dst--][c] = v;
      }
    }
  }
  // Shift columns left
  int dstC = 0;
  for (int c = 0; c < COLS; c++) {
    bool empty = true;
    for (int r = 0; r < ROWS; r++) if (grid[r][c]) empty = false;
    if (!empty) {
      if (dstC != c) {
        for (int r = 0; r < ROWS; r++) { grid[r][dstC] = grid[r][c]; grid[r][c] = 0; }
      }
      dstC++;
    }
  }

  if (dstC == 0) {
    score += 500;
    movesLeft += 10;
    initBoard();
  } else if (!anyGroup()) {
    movesLeft += 4;
    initBoard();
  }
  if (movesLeft <= 0) { gameOver(); return; }
  if (!grid[curR][curC]) moveCursor(1);
}

static void drawGame() {
  char buf[20];
  spr.fillSprite(TFT_BLACK);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (!grid[r][c]) continue;
      int x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
      drawCell(x, y, CELL, TILE_COL[grid[r][c] - 1]);
    }
  }
  if (state == PLAY && grid[curR][curC]) {
    spr.drawRect(GRID_X + curC * CELL - 1, GRID_Y + curR * CELL - 1, CELL + 1, CELL + 1, TFT_WHITE);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 12, 214, 2);
  spr.drawString("R", 74, 214, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 24, 214, 2);
  spr.drawString("pop!", 86, 214, 2);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(movesLeft <= 5 ? TFT_RED : TFT_GREEN);
  snprintf(buf, sizeof buf, "Moves %d", movesLeft);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("OUT OF MOVES", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("POP", 67, 18, 4);

  for (int i = 0; i < 4; i++) drawCell(37 + i * 16, 56, 14, TILE_COL[i]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"next tile", "pop group"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Bigger groups score more!", 67, 148, 1);

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
  spr.fillRect(x - 9, y - 9, 8, 8, c(TFT_RED));
  spr.fillRect(x + 1, y - 9, 8, 8, c(TFT_CYAN));
  bool pop = !dim && ((t / 450) & 1);
  if (!pop) spr.fillRect(x - 9, y + 1, 8, 8, c(TFT_YELLOW));
  spr.fillRect(x + 1, y + 1, 8, 8, c(TFT_GREEN));
}

void begin() {
  hiScore = prefs.getUInt("pop_hi", 0);
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
      bL.nextRepeat = now + 150;
      bL.consumed = true;
      moveCursor(COLS);
      dirty = true;
    }
    if (eL == 2 && !bL.consumed) {
      moveCursor(1);
      dirty = true;
    }
    if (eR == 2) {
      popGroup();
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

}  // namespace pop

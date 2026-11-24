// Columns
//
// Controls (two buttons):
//   Tap L / R : move left / right
//   Hold L    : cycle gem colors in the column
//   Hold R    : fast drop
// Match 3+ matching gems horizontally, vertically, or diagonally for chains!

#include "common.h"

namespace columns {

static const int COLS = 6, ROWS = 13, CELL = 15;
static const int BOARD_X = 22, BOARD_Y = 42;
static const int NUM_COLORS = 5;
static const uint32_t HOLD_MS = 220, TICK_MS = 16;

static const uint16_t GEM_COL[NUM_COLORS] = {
  TFT_RED, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_MAGENTA
};

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t grid[ROWS][COLS];                          // 0 = empty, 1..5 = gem color
static int curX, curY;
static uint8_t cur[3];
static int fallTimer, clearedTotal;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void spawnCol() {
  curX = COLS / 2;
  curY = 0;
  for (int i = 0; i < 3; i++) cur[i] = 1 + rnd(NUM_COLORS);
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("columns_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  clearedTotal = 0;
  fallTimer = 0;
  newHigh = false;
  memset(grid, 0, sizeof grid);
  spawnCol();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool canPlace(int x, int y) {
  if (x < 0 || x >= COLS || y >= ROWS) return false;
  return grid[y][x] == 0;
}

static void resolveMatches() {
  int chain = 1;
  while (true) {
    bool mark[ROWS][COLS] = {};
    int hits = 0;
    const int dr[4] = {0, 1, 1, 1};
    const int dc[4] = {1, 0, 1, -1};

    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        uint8_t col = grid[r][c];
        if (!col) continue;
        for (int d = 0; d < 4; d++) {
          int len = 1;
          int nr = r + dr[d], nc = c + dc[d];
          while (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && grid[nr][nc] == col) {
            len++;
            nr += dr[d];
            nc += dc[d];
          }
          if (len >= 3) {
            for (int k = 0; k < len; k++) mark[r + k * dr[d]][c + k * dc[d]] = true;
          }
        }
      }
    }

    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++)
        if (mark[r][c]) { grid[r][c] = 0; hits++; }

    if (hits == 0) break;
    clearedTotal += hits;
    score += hits * 10 * chain;
    chain++;

    // Gravity
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
  }
}

static void lockColumn() {
  for (int i = 0; i < 3; i++) {
    int r = curY - (2 - i);
    if (r < 0) { gameOver(); return; }
    grid[r][curX] = cur[2 - i];
  }
  resolveMatches();
  spawnCol();
  if (!canPlace(curX, curY)) gameOver();
}

static void tick(uint32_t now, int eL, int eR) {
  if (bL.stable && !bL.consumed && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) {
    bL.consumed = true;
    uint8_t tmp = cur[0];
    cur[0] = cur[1];
    cur[1] = cur[2];
    cur[2] = tmp;
  }
  if (eL == 2 && !bL.consumed && canPlace(curX - 1, curY)) curX--;
  if (eR == 2 && !bR.consumed && canPlace(curX + 1, curY)) curX++;

  int dropInterval = max(6, 26 - clearedTotal / 5);
  if (bR.stable && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) {
    bR.consumed = true;
    dropInterval = 2;
  }

  if (++fallTimer >= dropInterval) {
    fallTimer = 0;
    if (canPlace(curX, curY + 1)) curY++;
    else lockColumn();
  }
}

static void drawGem(int x, int y, uint8_t colIdx) {
  drawCell(x, y, CELL, GEM_COL[colIdx - 1]);
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);
  spr.drawRect(BOARD_X - 1, BOARD_Y - 1, COLS * CELL + 2, ROWS * CELL + 2, TFT_DARKGREY);

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (grid[r][c]) drawGem(BOARD_X + c * CELL, BOARD_Y + r * CELL, grid[r][c]);

  if (state == PLAY) {
    for (int i = 0; i < 3; i++) {
      int r = curY - (2 - i);
      if (r >= 0 && r < ROWS) drawGem(BOARD_X + curX * CELL, BOARD_Y + r * CELL, cur[2 - i]);
    }
  }

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("TOP OUT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("COLUMNS", 67, 18, 4);

  for (int i = 0; i < 3; i++) drawCell(60, 50 + i * 11, 11, GEM_COL[i]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L", "Hold R"};
  const char *acts[] = {"move", "cycle gems", "fast drop"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 19, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 19, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Match 3+ in any direction", 67, 162, 1);

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
  int shift = (t / 400) % 3;
  for (int i = 0; i < 3; i++) {
    spr.fillRect(x - 4, y - 12 + i * 8, 8, 7, c(GEM_COL[(i + shift) % 3]));
  }
}

void begin() {
  hiScore = prefs.getUInt("columns_hi", 0);
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

}  // namespace columns

// Drop 4 (Connect Four vs CPU)
//
// Controls (two buttons):
//   Tap L : next open column
//   Tap R : drop disc
// Connect 4 yellow discs to win the round and keep your streak going!

#include "common.h"

namespace drop4 {

static const int COLS = 7, ROWS = 6, CELL = 18;
static const int BOARD_X = 4, BOARD_Y = 82;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t grid[ROWS][COLS];                          // 0=empty, 1=player(yellow), 2=cpu(red)
static int selCol, streak;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static int dropRow(int c) {
  for (int r = ROWS - 1; r >= 0; r--) if (grid[r][c] == 0) return r;
  return -1;
}

static bool checkWin(uint8_t who) {
  const int dr[4] = {0, 1, 1, 1}, dc[4] = {1, 0, 1, -1};
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      if (grid[r][c] != who) continue;
      for (int d = 0; d < 4; d++) {
        int k = 1;
        while (k < 4) {
          int nr = r + k * dr[d], nc = c + k * dc[d];
          if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS || grid[nr][nc] != who) break;
          k++;
        }
        if (k == 4) return true;
      }
    }
  return false;
}

static void nextOpenCol() {
  for (int k = 1; k <= COLS; k++) {
    int c = (selCol + k) % COLS;
    if (dropRow(c) >= 0) { selCol = c; return; }
  }
}

static void startRound() {
  memset(grid, 0, sizeof grid);
  selCol = 3;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("drop4_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  streak = 0;
  newHigh = false;
  startRound();
  state = PLAY;
  dirty = true;
}

static int cpuPick() {
  // Win in 1
  for (int c = 0; c < COLS; c++) {
    int r = dropRow(c);
    if (r < 0) continue;
    grid[r][c] = 2;
    bool w = checkWin(2);
    grid[r][c] = 0;
    if (w) return c;
  }
  // Block player win in 1
  for (int c = 0; c < COLS; c++) {
    int r = dropRow(c);
    if (r < 0) continue;
    grid[r][c] = 1;
    bool w = checkWin(1);
    grid[r][c] = 0;
    if (w) return c;
  }
  const int order[7] = {3, 2, 4, 1, 5, 0, 6};
  for (int tries = 0; tries < 5; tries++) {
    int c = order[rnd(5)];
    if (dropRow(c) >= 0) return c;
  }
  for (int i = 0; i < COLS; i++) if (dropRow(order[i]) >= 0) return order[i];
  return -1;
}

static void playTurn() {
  int r = dropRow(selCol);
  if (r < 0) return;
  grid[r][selCol] = 1;
  if (checkWin(1)) {
    streak++;
    score += 100 * streak;
    startRound();
    return;
  }

  int cc = cpuPick();
  if (cc < 0) { startRound(); return; }                    // draw -> fresh board
  int cr = dropRow(cc);
  grid[cr][cc] = 2;
  if (checkWin(2)) {
    gameOver();
    return;
  }

  // Check full draw
  bool full = true;
  for (int c = 0; c < COLS; c++) if (dropRow(c) >= 0) full = false;
  if (full) startRound();
  else if (dropRow(selCol) < 0) nextOpenCol();
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);

  // Selector disc at top
  if (state == PLAY) {
    spr.fillCircle(BOARD_X + selCol * CELL + CELL / 2, 66, 7, TFT_YELLOW);
  }

  spr.fillRoundRect(BOARD_X - 2, BOARD_Y - 2, COLS * CELL + 4, ROWS * CELL + 4, 4, 0x0215);
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int cx = BOARD_X + c * CELL + CELL / 2, cy = BOARD_Y + r * CELL + CELL / 2;
      uint16_t col = grid[r][c] == 1 ? TFT_YELLOW : grid[r][c] == 2 ? TFT_RED : TFT_BLACK;
      spr.fillCircle(cx, cy, 7, col);
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 12, 212, 2);
  spr.drawString("R", 74, 212, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("col", 24, 212, 2);
  spr.drawString("drop", 86, 212, 2);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("CPU WINS!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("DROP 4", 67, 18, 4);

  spr.fillCircle(54, 62, 7, TFT_YELLOW);
  spr.fillCircle(80, 62, 7, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"next column", "drop disc"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Connect 4 before the CPU!", 67, 148, 1);

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
  spr.fillRoundRect(x - 10, y - 8, 20, 18, 3, c(0x0215));
  for (int i = 0; i < 3; i++) {
    spr.fillCircle(x - 6 + i * 6, y + 4, 2, c(TFT_YELLOW));
  }
  int dy = !dim ? (int)roundf(4 * triWave(t, 800)) : 0;
  spr.fillCircle(x, y - 4 + dy, 2, c(TFT_RED));
}

void begin() {
  hiScore = prefs.getUInt("drop4_hi", 0);
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
    if (eL == 2) { nextOpenCol(); dirty = true; }
    if (eR == 2) { playTurn(); dirty = true; }
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

}  // namespace drop4

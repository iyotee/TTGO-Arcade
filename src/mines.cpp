// Mines (Minesweeper)
//
// Controls (two buttons):
//   Tap L  : next unrevealed cell (Hold L: next row)
//   Tap R  : reveal cell
//   Hold R : flag / unflag cell
// First reveal is always safe. Clear the board to advance with more mines!

#include "common.h"

namespace mines {

static const int COLS = 8, ROWS = 10, CELL = 16;
static const int GRID_X = 3, GRID_Y = 44;
static const uint32_t HOLD_MS = 240;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static bool hasMine[ROWS][COLS], revealed[ROWS][COLS], flagged[ROWS][COLS];
static uint8_t adj[ROWS][COLS];
static int curR, curC, numMines, safeLeft, boardNum;
static bool minesPlaced;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static const uint16_t NUM_COL[9] = {
  0, TFT_CYAN, TFT_GREEN, TFT_RED, 0x435F, TFT_ORANGE, TFT_MAGENTA, TFT_WHITE, TFT_LIGHTGREY
};

static int rnd(int n) { return esp_random() % n; }

static void initBoard() {
  memset(hasMine, 0, sizeof hasMine);
  memset(revealed, 0, sizeof revealed);
  memset(flagged, 0, sizeof flagged);
  memset(adj, 0, sizeof adj);
  minesPlaced = false;
  numMines = min(22, 9 + boardNum);
  safeLeft = ROWS * COLS - numMines;
  curR = ROWS / 2;
  curC = COLS / 2;
}

static void placeMines(int safeR, int safeC) {
  int placed = 0;
  while (placed < numMines) {
    int r = rnd(ROWS), c = rnd(COLS);
    if (hasMine[r][c]) continue;
    if (abs(r - safeR) <= 1 && abs(c - safeC) <= 1) continue;
    hasMine[r][c] = true;
    placed++;
  }
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      uint8_t cnt = 0;
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
          int nr = r + dr, nc = c + dc;
          if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && hasMine[nr][nc]) cnt++;
        }
      adj[r][c] = cnt;
    }
  minesPlaced = true;
}

static void gameOver() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (hasMine[r][c]) revealed[r][c] = true;
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("mines_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  boardNum = 1;
  newHigh = false;
  initBoard();
  state = PLAY;
  dirty = true;
}

static void revealCell(int sr, int sc) {
  if (flagged[sr][sc] || revealed[sr][sc]) return;
  if (!minesPlaced) placeMines(sr, sc);
  if (hasMine[sr][sc]) { gameOver(); return; }

  uint8_t qR[ROWS * COLS], qC[ROWS * COLS];
  int head = 0, tail = 0;
  revealed[sr][sc] = true;
  safeLeft--;
  score += 5;
  qR[tail] = sr;
  qC[tail++] = sc;

  while (head < tail) {
    int r = qR[head], c = qC[head++];
    if (adj[r][c] != 0) continue;
    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++) {
        int nr = r + dr, nc = c + dc;
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) continue;
        if (revealed[nr][nc] || flagged[nr][nc] || hasMine[nr][nc]) continue;
        revealed[nr][nc] = true;
        safeLeft--;
        score += 5;
        qR[tail] = nr;
        qC[tail++] = nc;
      }
  }

  if (safeLeft <= 0) {
    score += 100 * boardNum;
    boardNum++;
    initBoard();
  }
}

static void nextCell(int step) {
  int idx = curR * COLS + curC;
  for (int k = 1; k <= ROWS * COLS; k++) {
    int ni = (idx + k * step) % (ROWS * COLS);
    int r = ni / COLS, c = ni % COLS;
    if (!revealed[r][c]) { curR = r; curC = c; return; }
  }
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(TFT_BLACK);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
      if (revealed[r][c]) {
        if (hasMine[r][c]) {
          spr.fillRect(x, y, CELL - 1, CELL - 1, TFT_RED);
          spr.fillCircle(x + 7, y + 7, 4, TFT_BLACK);
        } else {
          spr.fillRect(x, y, CELL - 1, CELL - 1, 0x1082);
          if (adj[r][c] > 0) {
            snprintf(buf, sizeof buf, "%d", adj[r][c]);
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(NUM_COL[adj[r][c]]);
            spr.drawString(buf, x + 7, y + 7, 1);
          }
        }
      } else {
        drawCell(x, y, CELL, flagged[r][c] ? 0x39E7 : 0x5AEB);
        if (flagged[r][c]) {
          spr.fillTriangle(x + 5, y + 3, x + 12, y + 6, x + 5, y + 9, TFT_RED);
          spr.drawFastVLine(x + 5, y + 3, 9, TFT_WHITE);
        }
      }
      if (r == curR && c == curC && state == PLAY) {
        spr.drawRect(x, y, CELL - 1, CELL - 1, TFT_YELLOW);
      }
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 8, 218, 2);
  spr.drawString("R", 70, 218, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("move", 20, 218, 2);
  spr.drawString("dig/flag", 82, 218, 2);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_RED);
  snprintf(buf, sizeof buf, "Mines %d", numMines);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("BOOM!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_RED);
  spr.drawString("MINES", 67, 18, 4);

  spr.fillCircle(67, 62, 9, TFT_DARKGREY);
  spr.drawLine(55, 50, 79, 74, TFT_LIGHTGREY);
  spr.drawLine(79, 50, 55, 74, TFT_LIGHTGREY);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R", "Hold R"};
  const char *acts[] = {"next cell", "reveal", "flag"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 19, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 66, 96 + i * 19, 2);
  }

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
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
  spr.fillCircle(x, y, 6, c(TFT_DARKGREY));
  spr.drawLine(x - 8, y - 8, x + 8, y + 8, c(TFT_LIGHTGREY));
  spr.drawLine(x + 8, y - 8, x - 8, y + 8, c(TFT_LIGHTGREY));
  if (!dim && ((t / 400) & 1)) spr.fillCircle(x, y, 2, TFT_RED);
}

void begin() {
  hiScore = prefs.getUInt("mines_hi", 0);
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
      nextCell(COLS);
      dirty = true;
    }
    if (eL == 2 && !bL.consumed) {
      nextCell(1);
      dirty = true;
    }
    if (bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) {
      bR.consumed = true;
      if (!revealed[curR][curC]) flagged[curR][curC] = !flagged[curR][curC];
      dirty = true;
    }
    if (eR == 2 && !bR.consumed) {
      revealCell(curR, curC);
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

}  // namespace mines

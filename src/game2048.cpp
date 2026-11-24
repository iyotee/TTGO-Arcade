// 2048
//
// Controls (two buttons):
//   Tap L  : slide Up
//   Tap R  : slide Right
//   Hold L : slide Left
//   Hold R : slide Down
// Merge matching tiles to reach 2048 and beyond!

#include "common.h"

namespace game2048 {

static const int GRID_X = 4, GRID_Y = 56, CELL = 31, PITCH = 32;
static const uint32_t HOLD_MS = 220;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint16_t board[4][4];                              // stores exponent: 0 = empty, 1 = 2, 2 = 4, ...
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static uint16_t tileColor(uint16_t exp) {
  static const uint16_t COLS[13] = {
    0x2104, 0xCE59, 0xCE34, 0xFD2C, 0xFC86, 0xFBE4,
    0xFA82, 0xFF6B, 0xFF27, 0xFEE4, 0xFEA2, 0xFE60, 0xF81F
  };
  return COLS[min((int)exp, 12)];
}

static void spawnTile() {
  int empty[16], cnt = 0;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (board[r][c] == 0) empty[cnt++] = r * 4 + c;
  if (cnt == 0) return;
  int pick = empty[rnd(cnt)];
  board[pick / 4][pick % 4] = (rnd(10) == 0) ? 2 : 1;
}

static bool canMove() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (board[r][c] == 0) return true;
      if (r < 3 && board[r][c] == board[r + 1][c]) return true;
      if (c < 3 && board[r][c] == board[r][c + 1]) return true;
    }
  return false;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("g2048_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  newHigh = false;
  memset(board, 0, sizeof board);
  spawnTile();
  spawnTile();
  state = PLAY;
  dirty = true;
}

// dir: 0=up, 1=right, 2=down, 3=left
static void slide(int dir) {
  bool moved = false;
  for (int i = 0; i < 4; i++) {
    uint16_t line[4];
    for (int j = 0; j < 4; j++) {
      int r = dir == 0 ? j : dir == 2 ? 3 - j : i;
      int c = dir == 3 ? j : dir == 1 ? 3 - j : i;
      line[j] = board[r][c];
    }

    uint16_t out[4] = {0, 0, 0, 0};
    int k = 0;
    for (int j = 0; j < 4; j++) {
      if (!line[j]) continue;
      if (k > 0 && out[k - 1] == line[j] && (out[k - 1] & 0x8000) == 0) {
        out[k - 1] = (line[j] + 1) | 0x8000;
        score += (1UL << (line[j] + 1));
      } else {
        out[k++] = line[j];
      }
    }
    for (int j = 0; j < 4; j++) out[j] &= 0x7FFF;

    for (int j = 0; j < 4; j++) {
      int r = dir == 0 ? j : dir == 2 ? 3 - j : i;
      int c = dir == 3 ? j : dir == 1 ? 3 - j : i;
      if (board[r][c] != out[j]) moved = true;
      board[r][c] = out[j];
    }
  }

  if (moved) {
    spawnTile();
    if (!canMove()) gameOver();
    dirty = true;
  }
}

static void drawGame() {
  char buf[12];
  spr.fillSprite(0x1082);
  spr.fillRoundRect(GRID_X - 2, GRID_Y - 2, PITCH * 4 + 3, PITCH * 4 + 3, 4, 0x39E7);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int x = GRID_X + c * PITCH, y = GRID_Y + r * PITCH;
      uint16_t exp = board[r][c];
      spr.fillRoundRect(x, y, CELL, CELL, 3, tileColor(exp));
      if (exp > 0) {
        uint32_t val = 1UL << exp;
        snprintf(buf, sizeof buf, "%lu", (unsigned long)val);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(exp <= 2 ? TFT_BLACK : TFT_WHITE);
        spr.drawString(buf, x + CELL / 2, y + CELL / 2, val >= 1000 ? 1 : 2);
      }
    }
  }

  // Control reminder below grid
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Tap: L=Up R=Right", 67, 198, 1);
  spr.drawString("Hold: L=Left R=Down", 67, 212, 1);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("NO MOVES", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("2048", 67, 18, 4);

  spr.fillRoundRect(49, 54, 36, 30, 4, 0xFD2C);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("2048", 67, 62, 2);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L/R"};
  const char *acts[] = {"up / right", "left / down"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 102 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 102 + i * 20, 2);
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
  spr.fillRoundRect(x - 10, y - 10, 9, 9, 2, c(0xCE59));
  spr.fillRoundRect(x + 1, y - 10, 9, 9, 2, c(0xFD2C));
  spr.fillRoundRect(x - 10, y + 1, 9, 9, 2, c(0xFBE4));
  bool pulse = !dim && ((t / 400) & 1);
  spr.fillRoundRect(x + 1, y + 1, 9, 9, 2, c(pulse ? TFT_YELLOW : 0xFF6B));
}

void begin() {
  hiScore = prefs.getUInt("g2048_hi", 0);
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
    if (bL.stable && !bL.consumed && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) {
      bL.consumed = true;
      slide(3);                                           // Hold L = Left
    }
    if (bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)HOLD_MS) {
      bR.consumed = true;
      slide(2);                                           // Hold R = Down
    }
    if (eL == 2 && !bL.consumed) slide(0);                // Tap L = Up
    if (eR == 2 && !bR.consumed) slide(1);                // Tap R = Right
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace game2048

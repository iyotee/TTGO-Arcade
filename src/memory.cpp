// Memory (Card Match)
//
// Controls (two buttons):
//   Tap L  : next card
//   Tap R  : flip card
// Find all 8 matching pairs before running out of tries (+4 tries per board).

#include "common.h"

namespace memory {

static const int GRID_X = 6, GRID_Y = 54, CELL = 29, PITCH = 31;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t cardSym[16];
static bool matched[16];
static int cursor, firstPick, secondPick, tries, boardNum;
static uint32_t revealUntil;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static const uint16_t SYM_COL[8] = {
  TFT_RED, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_MAGENTA, TFT_ORANGE, 0x435F, TFT_WHITE
};

static int rnd(int n) { return esp_random() % n; }

static void initBoard() {
  for (int i = 0; i < 16; i++) {
    cardSym[i] = i / 2;
    matched[i] = false;
  }
  for (int i = 15; i > 0; i--) {
    int j = rnd(i + 1);
    uint8_t tmp = cardSym[i];
    cardSym[i] = cardSym[j];
    cardSym[j] = tmp;
  }
  cursor = 0;
  firstPick = secondPick = -1;
  revealUntil = 0;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("memory_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  tries = 12;
  boardNum = 1;
  newHigh = false;
  initBoard();
  state = PLAY;
  dirty = true;
}

static void advanceCursor() {
  for (int k = 1; k <= 16; k++) {
    int idx = (cursor + k) % 16;
    if (!matched[idx] && idx != firstPick) {
      cursor = idx;
      return;
    }
  }
}

static void drawSym(int cx, int cy, uint8_t s) {
  uint16_t col = SYM_COL[s];
  switch (s) {
    case 0: spr.fillCircle(cx, cy, 7, col); break;
    case 1: spr.fillRect(cx - 6, cy - 6, 13, 13, col); break;
    case 2: spr.fillTriangle(cx, cy - 7, cx - 7, cy + 6, cx + 7, cy + 6, col); break;
    case 3:
      spr.fillTriangle(cx, cy - 7, cx - 7, cy, cx + 7, cy, col);
      spr.fillTriangle(cx, cy + 7, cx - 7, cy, cx + 7, cy, col);
      break;
    case 4:
      spr.fillRect(cx - 2, cy - 7, 5, 15, col);
      spr.fillRect(cx - 7, cy - 2, 15, 5, col);
      break;
    case 5:
      spr.drawCircle(cx, cy, 7, col);
      spr.drawCircle(cx, cy, 5, col);
      break;
    case 6:
      spr.fillRect(cx - 7, cy - 5, 14, 4, col);
      spr.fillRect(cx - 7, cy + 2, 14, 4, col);
      break;
    default:
      spr.fillCircle(cx - 4, cy, 4, col);
      spr.fillCircle(cx + 4, cy, 4, col);
      break;
  }
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(0x0842);

  for (int i = 0; i < 16; i++) {
    int r = i / 4, c = i % 4;
    int x = GRID_X + c * PITCH, y = GRID_Y + r * PITCH;
    bool open = matched[i] || i == firstPick || i == secondPick;
    if (matched[i]) {
      spr.drawRoundRect(x, y, CELL, CELL, 3, 0x2104);
    } else if (open) {
      spr.fillRoundRect(x, y, CELL, CELL, 3, 0x18E3);
      drawSym(x + CELL / 2, y + CELL / 2, cardSym[i]);
    } else {
      spr.fillRoundRect(x, y, CELL, CELL, 3, 0x2965);
      spr.drawRoundRect(x + 3, y + 3, CELL - 6, CELL - 6, 2, 0x4A69);
    }
    if (i == cursor && state == PLAY) {
      spr.drawRoundRect(x - 1, y - 1, CELL + 2, CELL + 2, 4, TFT_YELLOW);
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 14, 212, 2);
  spr.drawString("R", 76, 212, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 26, 212, 2);
  spr.drawString("flip", 88, 212, 2);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(tries <= 3 ? TFT_RED : TFT_GREEN);
  snprintf(buf, sizeof buf, "Tries %d", tries);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("NO TRIES!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_MAGENTA);
  spr.drawString("MEMORY", 67, 18, 4);

  spr.fillRoundRect(41, 52, 22, 22, 3, 0x2965);
  spr.fillRoundRect(71, 52, 22, 22, 3, 0x18E3);
  drawSym(82, 63, 2);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"next card", "flip card"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Match all 8 pairs!", 67, 148, 1);

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
  spr.fillRoundRect(x - 10, y - 10, 9, 20, 2, c(0x2965));
  bool flip = !dim && ((t / 600) & 1);
  spr.fillRoundRect(x + 1, y - 10, 9, 20, 2, c(flip ? TFT_CYAN : 0x2965));
}

void begin() {
  hiScore = prefs.getUInt("memory_hi", 0);
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
    if (secondPick >= 0 && (int32_t)(now - revealUntil) >= 0) {
      firstPick = secondPick = -1;
      if (tries <= 0) gameOver();
      dirty = true;
    }

    if (secondPick < 0) {
      if (eL == 1) { bL.nextRepeat = now + 350; }
      if (bL.stable && (int32_t)(now - bL.nextRepeat) >= 0) {
        bL.nextRepeat = now + 130;
        bL.consumed = true;
        advanceCursor();
        dirty = true;
      }
      if (eL == 2 && !bL.consumed) {
        advanceCursor();
        dirty = true;
      }
      if (eR == 2 && !matched[cursor] && cursor != firstPick) {
        if (firstPick < 0) {
          firstPick = cursor;
          advanceCursor();
        } else {
          secondPick = cursor;
          if (cardSym[firstPick] == cardSym[secondPick]) {
            matched[firstPick] = matched[secondPick] = true;
            firstPick = secondPick = -1;
            score += 50 * boardNum;
            bool allDone = true;
            for (int i = 0; i < 16; i++) if (!matched[i]) allDone = false;
            if (allDone) {
              score += 200;
              tries += 4;
              boardNum++;
              initBoard();
            } else {
              advanceCursor();
            }
          } else {
            tries--;
            revealUntil = now + 650;
          }
        }
        dirty = true;
      }
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

}  // namespace memory

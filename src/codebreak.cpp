// Code (Mastermind 4-Peg Codebreaker)
//
// Controls (two buttons):
//   Tap L  : cycle color in active peg (Hold L: previous peg)
//   Tap R  : next peg / submit guess on 4th peg
// White clue = right color & spot; orange clue = right color, wrong spot.

#include "common.h"

namespace codebreak {

static const int MAX_ROWS = 8, PEGS = 4, COLORS = 6;
static const uint32_t HOLD_MS = 240;

static const uint16_t PEG_COL[COLORS] = {
  TFT_RED, TFT_GREEN, TFT_CYAN, TFT_YELLOW, TFT_MAGENTA, TFT_ORANGE
};

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t secret[PEGS];
static uint8_t hist[MAX_ROWS][PEGS];
static uint8_t exact[MAX_ROWS], near[MAX_ROWS];
static uint8_t cur[PEGS];
static int curRow, curSlot, roundNum;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void startRound() {
  for (int i = 0; i < PEGS; i++) secret[i] = rnd(COLORS);
  for (int i = 0; i < PEGS; i++) cur[i] = 0;
  curRow = 0;
  curSlot = 0;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("code_hi", hiScore);
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
  dirty = true;
}

static void submitGuess() {
  bool usedS[PEGS] = {}, usedG[PEGS] = {};
  uint8_t ex = 0, nr = 0;
  for (int i = 0; i < PEGS; i++) {
    hist[curRow][i] = cur[i];
    if (cur[i] == secret[i]) { ex++; usedS[i] = usedG[i] = true; }
  }
  for (int i = 0; i < PEGS; i++) {
    if (usedG[i]) continue;
    for (int j = 0; j < PEGS; j++) {
      if (!usedS[j] && cur[i] == secret[j]) {
        nr++;
        usedS[j] = true;
        break;
      }
    }
  }
  exact[curRow] = ex;
  near[curRow] = nr;

  if (ex == PEGS) {
    score += 100 + (MAX_ROWS - curRow) * 25;
    roundNum++;
    startRound();
    return;
  }
  if (++curRow >= MAX_ROWS) {
    gameOver();
    return;
  }
  curSlot = 0;
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(0x0842);

  for (int r = 0; r < MAX_ROWS; r++) {
    int y = 44 + r * 20;
    bool active = (r == curRow && state == PLAY);
    for (int p = 0; p < PEGS; p++) {
      int x = 14 + p * 20;
      if (r < curRow || state == GAMEOVER) {
        spr.fillCircle(x, y + 8, 7, PEG_COL[hist[r][p]]);
      } else if (active) {
        spr.fillCircle(x, y + 8, 7, PEG_COL[cur[p]]);
        if (p == curSlot) spr.drawCircle(x, y + 8, 9, TFT_WHITE);
      } else {
        spr.drawCircle(x, y + 8, 6, 0x39E7);
      }
    }

    // Clue dots
    if (r < curRow) {
      int idx = 0;
      for (int k = 0; k < exact[r]; k++, idx++) {
        spr.fillCircle(104 + (idx & 1) * 8, y + 4 + (idx / 2) * 8, 3, TFT_WHITE);
      }
      for (int k = 0; k < near[r]; k++, idx++) {
        spr.fillCircle(104 + (idx & 1) * 8, y + 4 + (idx / 2) * 8, 3, TFT_ORANGE);
      }
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 8, 214, 2);
  spr.drawString("R", 68, 214, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("color", 20, 214, 2);
  spr.drawString(curSlot == PEGS - 1 ? "guess!" : "next", 80, 214, 2);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_CYAN);
  snprintf(buf, sizeof buf, "Lvl %d", roundNum);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("LOCKED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("CODE", 67, 18, 4);

  for (int i = 0; i < 4; i++) spr.fillCircle(37 + i * 20, 62, 7, PEG_COL[i]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"cycle color", "next / guess"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("White=exact  Orange=near", 67, 148, 1);

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
  int shift = !dim ? ((t / 400) % 4) : 0;
  for (int i = 0; i < 4; i++) {
    spr.fillCircle(x - 9 + i * 6, y, 3, c(PEG_COL[(i + shift) % COLORS]));
  }
}

void begin() {
  hiScore = prefs.getUInt("code_hi", 0);
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
      curSlot = (curSlot + PEGS - 1) % PEGS;
      dirty = true;
    }
    if (eL == 2 && !bL.consumed) {
      cur[curSlot] = (cur[curSlot] + 1) % COLORS;
      dirty = true;
    }
    if (eR == 2) {
      if (curSlot < PEGS - 1) curSlot++;
      else submitGuess();
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

}  // namespace codebreak

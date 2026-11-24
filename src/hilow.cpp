// Hi-Lo (Card Prediction)
//
// Controls (two buttons):
//   Tap L : next card is LOWER (or equal)
//   Tap R : next card is HIGHER (or equal)
// Build streaks for up to 5x multiplier! 3 lives.

#include "common.h"

namespace hilow {

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int curRank, curSuit, prevRank, lives, streak;
static char statusMsg[20];
static uint16_t statusCol;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void drawCard() {
  prevRank = curRank;
  curRank = 1 + rnd(13);
  curSuit = rnd(4);
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("hilow_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  streak = 0;
  newHigh = false;
  curRank = 7;
  curSuit = rnd(4);
  snprintf(statusMsg, sizeof statusMsg, "LOW or HIGH?");
  statusCol = TFT_LIGHTGREY;
  state = PLAY;
  dirty = true;
}

static void guess(bool high) {
  drawCard();
  bool ok = high ? (curRank >= prevRank) : (curRank <= prevRank);
  if (ok) {
    streak++;
    int mult = min(5, 1 + streak / 3);
    score += 20 * mult;
    snprintf(statusMsg, sizeof statusMsg, "NICE! %dx", mult);
    statusCol = TFT_GREEN;
  } else {
    streak = 0;
    snprintf(statusMsg, sizeof statusMsg, "WRONG!");
    statusCol = TFT_RED;
    if (--lives <= 0) gameOver();
  }
}

static void drawGame() {
  spr.fillSprite(0x0B23);

  const char *ranks[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  const char *suits[] = {"SPADE", "HEART", "DIAM", "CLUB"};
  uint16_t col = (curSuit == 1 || curSuit == 2) ? TFT_RED : TFT_BLACK;

  spr.fillRoundRect(39, 54, 57, 82, 6, TFT_WHITE);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(col);
  spr.drawString(ranks[curRank], 67, 86, 4);
  spr.drawString(suits[curSuit], 67, 118, 1);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(statusCol);
  spr.drawString(statusMsg, 67, 146, 2);

  spr.fillRoundRect(6, 176, 58, 44, 6, 0x1148);
  spr.drawRoundRect(6, 176, 58, 44, 6, TFT_CYAN);
  spr.fillRoundRect(71, 176, 58, 44, 6, 0x4200);
  spr.drawRoundRect(71, 176, 58, 44, 6, TFT_ORANGE);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("LOW", 35, 198, 2);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("HIGH", 100, 198, 2);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_RED);
  if (state == GAMEOVER) drawGameOverBox("NO LIVES!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("HI-LO", 67, 18, 4);

  spr.fillRoundRect(53, 50, 28, 36, 3, TFT_WHITE);
  spr.setTextColor(TFT_RED);
  spr.drawString("K", 67, 56, 4);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"next is lower", "next is higher"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Ties win! Streak up to 5x", 67, 148, 1);

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
  bool up = !dim && ((t / 500) & 1);
  spr.fillTriangle(x - 5, y - 10, x - 10, y - 2, x, y - 2, c(up ? TFT_ORANGE : TFT_DARKGREY));
  spr.fillTriangle(x + 5, y + 10, x, y + 2, x + 10, y + 2, c(!up ? TFT_CYAN : TFT_DARKGREY));
}

void begin() {
  hiScore = prefs.getUInt("hilow_hi", 0);
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
    if (eL == 2) { guess(false); dirty = true; }
    if (eR == 2) { guess(true); dirty = true; }
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

}  // namespace hilow

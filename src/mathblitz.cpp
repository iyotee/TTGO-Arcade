// Math (Quick True/False Arithmetic)
//
// Controls (two buttons):
//   Tap L : FALSE (wrong equation)
//   Tap R : TRUE (correct equation)
// Answer fast before the timer bar empties! 3 lives.

#include "common.h"

namespace mathblitz {

static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static char eqStr[24];
static bool eqTrue;
static float timerBar;
static int lives, streak, flashT;
static uint16_t flashCol;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void nextQuestion() {
  int maxN = min(30, 9 + (int)(score / 80));
  int op = rnd(score > 100 ? 3 : 2);
  int a = 1 + rnd(maxN), b = 1 + rnd(maxN), ans = 0;
  char opCh = '+';
  if (op == 0) { ans = a + b; opCh = '+'; }
  else if (op == 1) { if (a < b) { int t = a; a = b; b = t; } ans = a - b; opCh = '-'; }
  else { a = 2 + rnd(10); b = 2 + rnd(10); ans = a * b; opCh = 'x'; }

  eqTrue = (rnd(2) == 0);
  int shown = ans;
  if (!eqTrue) {
    int delta = (rnd(2) ? 1 : -1) * (1 + rnd(3));
    shown = max(0, ans + delta);
    if (shown == ans) shown = ans + 1;
  }
  snprintf(eqStr, sizeof eqStr, "%d %c %d = %d", a, opCh, b, shown);
  timerBar = 100.0f;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("math_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  streak = 0;
  flashT = 0;
  newHigh = false;
  nextQuestion();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void answer(bool guess) {
  if (guess == eqTrue) {
    streak++;
    score += 10 * min(5, 1 + streak / 5);
    flashCol = TFT_GREEN;
  } else {
    streak = 0;
    flashCol = TFT_RED;
    if (--lives <= 0) { gameOver(); return; }
  }
  flashT = 8;
  nextQuestion();
}

static void tick(int eL, int eR) {
  if (flashT > 0) flashT--;
  float drain = 0.35f + min(0.65f, score * 0.0012f);
  timerBar -= drain;
  if (timerBar <= 0) {
    streak = 0;
    flashCol = TFT_RED;
    flashT = 8;
    if (--lives <= 0) { gameOver(); return; }
    nextQuestion();
    return;
  }
  if (eL == 1) answer(false);
  else if (eR == 1) answer(true);
}

static void drawGame() {
  spr.fillSprite(flashT > 0 ? tft.alphaBlend(40, flashCol, TFT_BLACK) : 0x0842);

  spr.drawRect(14, 52, 107, 8, TFT_WHITE);
  uint16_t bcol = timerBar > 40 ? TFT_GREEN : timerBar > 20 ? TFT_YELLOW : TFT_RED;
  spr.fillRect(16, 54, max(0, (int)(timerBar * 1.03f)), 4, bcol);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(eqStr, 67, 118, 4);

  spr.fillRoundRect(6, 176, 58, 44, 6, 0x4000);
  spr.drawRoundRect(6, 176, 58, 44, 6, TFT_RED);
  spr.fillRoundRect(71, 176, 58, 44, 6, 0x0340);
  spr.drawRoundRect(71, 176, 58, 44, 6, TFT_GREEN);

  spr.setTextColor(TFT_RED);
  spr.drawString("NO", 35, 198, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("YES", 100, 198, 2);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_RED);
  if (state == GAMEOVER) drawGameOverBox("TIME UP!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("MATH", 67, 18, 4);

  spr.setTextColor(TFT_WHITE);
  spr.drawString("7 + 5 = 12", 67, 56, 2);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"FALSE (NO)", "TRUE (YES)"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Answer fast for streaks!", 67, 148, 1);

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
  spr.drawFastHLine(x - 7, y - 5, 8, c(TFT_CYAN));
  spr.drawFastVLine(x - 3, y - 9, 8, c(TFT_CYAN));
  spr.drawFastHLine(x, y + 4, 8, c(TFT_YELLOW));
  if (!dim && ((t / 400) & 1)) spr.drawFastHLine(x, y + 7, 8, c(TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("math_hi", 0);
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    bool first = true;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(first ? eL : 0, first ? eR : 0);
      first = false;
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

}  // namespace mathblitz

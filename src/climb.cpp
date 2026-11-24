// Climb (Timberman)
//
// Controls (two buttons):
//   Tap L : chop / climb on left side
//   Tap R : chop / climb on right side
// Avoid branches above you and keep the timer bar from running dry!

#include "common.h"

namespace climb {

static const int SEGMENTS = 8;
static const int SEG_H = 22;
static const int BASE_Y = 228;
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int8_t branch[SEGMENTS];                           // 0 = none, -1 = left, 1 = right
static int side;                                          // -1 = left, 1 = right
static float timerBar;                                    // 0 .. 100
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static int8_t nextBranch(int8_t prev) {
  if (prev != 0) return 0;                                // never two branches back-to-back
  int r = rnd(100);
  if (r < 38) return -1;
  if (r < 76) return 1;
  return 0;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("climb_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  side = -1;
  timerBar = 75.0f;
  newHigh = false;
  branch[0] = 0;
  for (int i = 1; i < SEGMENTS; i++) branch[i] = nextBranch(branch[i - 1]);
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void chop(int s) {
  side = s;
  if (branch[0] == side) { gameOver(); return; }
  for (int i = 0; i < SEGMENTS - 1; i++) branch[i] = branch[i + 1];
  branch[SEGMENTS - 1] = nextBranch(branch[SEGMENTS - 2]);
  if (branch[0] == side) { gameOver(); return; }
  score++;
  timerBar = min(100.0f, timerBar + 11.0f);
}

static void tick(int eL, int eR) {
  float drain = 0.24f + min(0.45f, score * 0.0025f);
  timerBar -= drain;
  if (timerBar <= 0) { timerBar = 0; gameOver(); return; }

  if (eL == 1) chop(-1);
  else if (eR == 1) chop(1);
}

static void drawGame() {
  spr.fillSprite(0x0842);
  spr.fillRect(0, BASE_Y, 135, 12, 0x1B42);

  // Trunk & branches
  for (int i = 0; i < SEGMENTS; i++) {
    int y = BASE_Y - (i + 1) * SEG_H;
    spr.fillRect(55, y, 25, SEG_H, (i & 1) ? 0x8A22 : 0x79C0);
    if (branch[i] == -1) {
      spr.fillRect(18, y + 7, 37, 7, 0x8A22);
      spr.fillCircle(20, y + 7, 6, 0x1B42);
    } else if (branch[i] == 1) {
      spr.fillRect(80, y + 7, 37, 7, 0x8A22);
      spr.fillCircle(115, y + 7, 6, 0x1B42);
    }
  }

  // Climber
  int px = (side < 0) ? 32 : 90;
  int py = BASE_Y - 18;
  spr.fillRoundRect(px, py, 13, 18, 3, TFT_YELLOW);
  spr.fillRect(px + 2, py + 3, 9, 5, TFT_RED);

  // Timer bar
  spr.drawRect(14, 44, 107, 8, TFT_WHITE);
  uint16_t bcol = timerBar > 40 ? TFT_GREEN : timerBar > 20 ? TFT_YELLOW : TFT_RED;
  spr.fillRect(16, 46, (int)(timerBar * 1.03f), 4, bcol);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("BONK!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("CLIMB", 67, 18, 4);

  spr.fillRect(57, 48, 20, 34, 0x8A22);
  spr.fillRect(36, 56, 21, 6, 0x8A22);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"climb left", "climb right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Chop fast, dodge branches!", 67, 148, 1);

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
  spr.fillRect(x - 4, y - 13, 8, 26, c(0x8A22));
  spr.fillRect(x - 12, y - 4, 8, 4, c(0x1B42));
  int sideX = !dim && ((t / 400) & 1) ? x + 6 : x - 11;
  spr.fillRect(sideX, y + 4, 5, 7, c(TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("climb_hi", 0);
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

}  // namespace climb

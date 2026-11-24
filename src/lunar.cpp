// Lunar (Lunar Lander)
//
// Controls (two buttons):
//   Hold L : push left (+ slight lift)
//   Hold R : push right (+ slight lift)
//   L + R  : main vertical thruster
// Land softly (vy < 1.2) on the flat green pad!

#include "common.h"

namespace lunar {

static const int COLS = 28;                               // 28 x 5 px = 140
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static int16_t terrain[COLS];
static int padCol, padW, level, fuel, landedT;
static float lx, ly, vx, vy;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void genTerrain() {
  padW = max(3, 6 - level / 2);
  padCol = 2 + rnd(COLS - padW - 4);
  int padY = 185 + rnd(35);
  int h = 190;
  for (int i = 0; i < COLS; i++) {
    if (i >= padCol && i < padCol + padW) {
      terrain[i] = padY;
      h = padY;
    } else {
      h = constrain(h + (rnd(29) - 14), 130, 232);
      terrain[i] = h;
    }
  }
}

static void startLevel() {
  genTerrain();
  lx = 20 + rnd(95);
  ly = 50;
  vx = (rnd(2) ? 0.2f : -0.2f);
  vy = 0;
  fuel = max(180, 320 - level * 15);
  landedT = 0;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("lunar_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  level = 1;
  newHigh = false;
  startLevel();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (landedT > 0) {
    if (--landedT == 0) {
      level++;
      startLevel();
    }
    return;
  }

  vy += 0.025f;                                           // lunar gravity

  if (fuel > 0) {
    if (bL.stable && bR.stable) {
      vy -= 0.068f;
      fuel = max(0, fuel - 2);
    } else if (bL.stable) {
      vx -= 0.032f;
      vy -= 0.018f;
      fuel--;
    } else if (bR.stable) {
      vx += 0.032f;
      vy -= 0.018f;
      fuel--;
    }
  }

  vx = constrain(vx, -2.5f, 2.5f);
  vy = constrain(vy, -2.5f, 3.0f);
  lx += vx;
  ly += vy;

  if (lx < 6) { lx = 6; vx = fabsf(vx) * 0.5f; }
  if (lx > 129) { lx = 129; vx = -fabsf(vx) * 0.5f; }
  if (ly < HEADER_H + 6) { ly = HEADER_H + 6; vy = 0.1f; }

  int col = constrain((int)(lx / 5), 0, COLS - 1);
  if (ly + 6 >= terrain[col]) {
    bool onPad = (lx >= padCol * 5 + 2 && lx <= (padCol + padW) * 5 - 2);
    if (onPad && vy < 1.25f && fabsf(vx) < 0.85f) {
      ly = terrain[col] - 6;
      vx = vy = 0;
      score += 100 + fuel / 2;
      landedT = 45;
    } else {
      gameOver();
    }
  }
}

static void drawGame() {
  char buf[20];
  spr.fillSprite(0x0008);

  // Terrain
  for (int i = 0; i < COLS - 1; i++) {
    int x0 = i * 5, x1 = (i + 1) * 5;
    spr.drawLine(x0, terrain[i], x1, terrain[i + 1], TFT_LIGHTGREY);
    spr.fillRect(x0, terrain[i], 5, 240 - terrain[i], 0x2945);
  }
  int px = padCol * 5, pw = padW * 5, py = terrain[padCol];
  spr.fillRect(px, py - 1, pw, 3, TFT_GREEN);

  // Thruster flames
  int x = (int)lx, y = (int)ly;
  if (state == PLAY && landedT == 0 && fuel > 0) {
    if (bL.stable && bR.stable) spr.fillTriangle(x - 3, y + 5, x + 3, y + 5, x, y + 12 + rnd(4), TFT_ORANGE);
    else if (bL.stable) spr.fillTriangle(x + 4, y, x + 4, y + 3, x + 10, y + 2, TFT_YELLOW);
    else if (bR.stable) spr.fillTriangle(x - 4, y, x - 4, y + 3, x - 10, y + 2, TFT_YELLOW);
  }

  // Lander
  spr.drawLine(x - 5, y + 6, x - 2, y + 2, TFT_LIGHTGREY);
  spr.drawLine(x + 5, y + 6, x + 2, y + 2, TFT_LIGHTGREY);
  spr.fillRoundRect(x - 4, y - 5, 9, 8, 2, TFT_WHITE);
  spr.fillRect(x - 2, y - 3, 5, 3, TFT_CYAN);

  if (landedT > 0) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_GREEN);
    spr.drawString("LANDED!", 67, 100, 4);
  }

  drawScoreHeader(score, hiScore);
  // Fuel bar + vy indicator
  spr.drawRect(88, 6, 42, 6, TFT_LIGHTGREY);
  spr.fillRect(89, 7, max(0, fuel * 40 / 320), 4, fuel > 70 ? TFT_GREEN : TFT_RED);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(vy < 1.25f ? TFT_GREEN : TFT_RED);
  snprintf(buf, sizeof buf, "v:%.1f", vy);
  spr.drawString(buf, 132, 20, 1);

  if (state == GAMEOVER) drawGameOverBox("CRASHED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("LUNAR", 67, 18, 4);

  spr.fillRoundRect(61, 56, 12, 10, 2, TFT_WHITE);
  spr.fillTriangle(64, 66, 70, 66, 67, 74, TFT_ORANGE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"push left/right", "main thrust"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Land gently on green pad", 67, 148, 1);

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
  spr.fillRect(x - 10, y + 9, 20, 3, c(TFT_GREEN));
  int ly = y - 6 + (int)roundf(8 * triWave(t, 1600));
  spr.fillRoundRect(x - 4, ly, 8, 6, 2, c(TFT_WHITE));
  if (!dim && ((t / 120) & 1)) spr.fillTriangle(x - 2, ly + 6, x + 2, ly + 6, x, ly + 10, TFT_ORANGE);
}

void begin() {
  hiScore = prefs.getUInt("lunar_hi", 0);
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
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
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

}  // namespace lunar

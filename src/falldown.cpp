// Falldown
//
// Controls (two buttons):
//   Hold L : roll left
//   Hold R : roll right
// Floors with gaps rise from the bottom; fall through before reaching the top.

#include "common.h"

namespace falldown {

static const int NUM_FLOORS = 7;
static const int FLOOR_GAP_Y = 34;
static const int BALL_R = 5;
static const uint32_t TICK_MS = 16;

struct Floor { float y; int gap1, gap2, gapW; bool passed; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Floor floors[NUM_FLOORS];
static float bx, by, vy, riseSpeed;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void initFloor(Floor &f, float y) {
  f.y = y;
  f.gapW = 24;
  f.gap1 = 4 + rnd(135 - f.gapW - 8);
  f.gap2 = (rnd(100) < 40) ? (4 + rnd(135 - f.gapW - 8)) : -100;
  f.passed = false;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("falldown_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  newHigh = false;
  bx = 67;
  by = 70;
  vy = 0;
  riseSpeed = 0.75f;
  for (int i = 0; i < NUM_FLOORS; i++) {
    initFloor(floors[i], 110.0f + i * FLOOR_GAP_Y);
  }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool inGap(const Floor &f, float x) {
  if (x - BALL_R >= f.gap1 && x + BALL_R <= f.gap1 + f.gapW) return true;
  if (f.gap2 >= 0 && x - BALL_R >= f.gap2 && x + BALL_R <= f.gap2 + f.gapW) return true;
  return false;
}

static void tick() {
  if (bL.stable && !bR.stable) bx -= 3.1f;
  if (bR.stable && !bL.stable) bx += 3.1f;
  bx = constrain(bx, (float)BALL_R + 1, 134.0f - BALL_R);

  riseSpeed = min(2.35f, 0.75f + score * 0.018f);

  float prevY = by;
  vy = min(5.5f, vy + 0.25f);
  by += vy;

  float lowestY = 0;
  for (int i = 0; i < NUM_FLOORS; i++) {
    floors[i].y -= riseSpeed;
    if (floors[i].y > lowestY) lowestY = floors[i].y;
  }

  for (int i = 0; i < NUM_FLOORS; i++) {
    Floor &f = floors[i];
    if (!f.passed && by > f.y + 4) {
      f.passed = true;
      score++;
    }
    if (f.y < HEADER_H) {
      initFloor(f, max(242.0f, lowestY + FLOOR_GAP_Y));
      lowestY = f.y;
    }
    // Rest on floor if not inside a gap
    if (!inGap(f, bx) && prevY + BALL_R <= f.y + riseSpeed + 2 && by + BALL_R >= f.y) {
      by = f.y - BALL_R;
      vy = 0;
    }
  }

  if (by + BALL_R > 239) { by = 239 - BALL_R; vy = 0; }
  if (by - BALL_R <= HEADER_H) gameOver();
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);
  spr.drawFastHLine(0, HEADER_H, 135, TFT_RED);

  for (int i = 0; i < NUM_FLOORS; i++) {
    const Floor &f = floors[i];
    int y = (int)f.y;
    if (y < HEADER_H || y > 240) continue;
    for (int x = 0; x < 135; x++) {
      bool gap = (x >= f.gap1 && x < f.gap1 + f.gapW) ||
                 (f.gap2 >= 0 && x >= f.gap2 && x < f.gap2 + f.gapW);
      if (!gap) spr.drawFastVLine(x, y, 4, 0x3C9F);
    }
  }

  spr.fillCircle((int)bx, (int)by, BALL_R, TFT_ORANGE);
  spr.drawPixel((int)bx - 2, (int)by - 2, TFT_YELLOW);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("CRUSHED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("FALLDOWN", 67, 18, 4);

  spr.fillRect(10, 72, 46, 4, 0x3C9F);
  spr.fillRect(82, 72, 43, 4, 0x3C9F);
  spr.fillCircle(67, 64, 5, TFT_ORANGE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"roll left", "roll right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 102 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 102 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Drop through gaps!", 67, 154, 1);

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
  int dy = (t / 40) % 14;
  spr.fillRect(x - 11, y - 4 - dy / 2, 8, 3, c(0x3C9F));
  spr.fillRect(x + 3, y - 4 - dy / 2, 8, 3, c(0x3C9F));
  spr.fillRect(x - 11, y + 10 - dy / 2, 14, 3, c(0x3C9F));
  spr.fillCircle(x - 1, y - 8 + dy / 2, 3, c(TFT_ORANGE));
}

void begin() {
  hiScore = prefs.getUInt("falldown_hi", 0);
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

}  // namespace falldown

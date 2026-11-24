// Catch (Coins & Bombs)
//
// Controls (two buttons):
//   Hold L : move basket left
//   Hold R : move basket right
// Catch gold coins (+10) and cyan gems (+30). Avoid red bombs! 3 lives.

#include "common.h"

namespace catchgame {

static const int MAX_ITEMS = 12;
static const int BASKET_W = 28, BASKET_Y = 222;
static const uint32_t TICK_MS = 16;

enum ItemKind : uint8_t { I_COIN, I_GEM, I_BOMB };
struct Item { float x, y, vy; ItemKind kind; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float bx, dropperX, dropperVx;
static int lives, spawnT;
static Item items[MAX_ITEMS];
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("catch_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  bx = 67;
  dropperX = 67;
  dropperVx = 1.6f;
  spawnT = 20;
  newHigh = false;
  for (int i = 0; i < MAX_ITEMS; i++) items[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (bL.stable && !bR.stable) bx -= 3.5f;
  if (bR.stable && !bL.stable) bx += 3.5f;
  bx = constrain(bx, BASKET_W / 2.0f, 135.0f - BASKET_W / 2.0f);

  dropperX += dropperVx;
  if (dropperX < 14 || dropperX > 121) {
    dropperVx = -dropperVx;
    dropperX = constrain(dropperX, 14.0f, 121.0f);
  } else if (rnd(100) < 3) {
    dropperVx = -dropperVx;
  }

  if (--spawnT <= 0) {
    spawnT = max(14, 34 - (int)(score / 80));
    for (int i = 0; i < MAX_ITEMS; i++) {
      if (items[i].active) continue;
      int r = rnd(100);
      ItemKind k = (r < 28) ? I_BOMB : (r < 45) ? I_GEM : I_COIN;
      float spd = min(3.8f, 1.7f + score * 0.0018f + (k == I_GEM ? 0.4f : 0.0f));
      items[i] = {dropperX, (float)HEADER_H + 14, spd, k, true};
      break;
    }
  }

  for (int i = 0; i < MAX_ITEMS; i++) {
    if (!items[i].active) continue;
    items[i].y += items[i].vy;
    if (items[i].y >= BASKET_Y - 3 && items[i].y <= BASKET_Y + 8 &&
        fabsf(items[i].x - bx) <= BASKET_W / 2.0f + 3) {
      items[i].active = false;
      if (items[i].kind == I_BOMB) {
        if (--lives <= 0) { gameOver(); return; }
      } else {
        score += (items[i].kind == I_GEM) ? 30 : 10;
      }
      continue;
    }
    if (items[i].y > 242) {
      if (items[i].kind != I_BOMB) {
        if (--lives <= 0) { gameOver(); return; }
      }
      items[i].active = false;
    }
  }
}

static void drawGame() {
  spr.fillSprite(0x0842);

  // Dropper cloud/bot
  spr.fillRoundRect((int)dropperX - 8, HEADER_H + 3, 16, 8, 3, TFT_LIGHTGREY);

  for (int i = 0; i < MAX_ITEMS; i++) {
    if (!items[i].active) continue;
    int ix = (int)items[i].x, iy = (int)items[i].y;
    if (items[i].kind == I_COIN) {
      spr.fillCircle(ix, iy, 5, TFT_YELLOW);
      spr.drawCircle(ix, iy, 3, TFT_ORANGE);
    } else if (items[i].kind == I_GEM) {
      spr.fillTriangle(ix, iy - 5, ix - 5, iy, ix + 5, iy, TFT_CYAN);
      spr.fillTriangle(ix, iy + 5, ix - 5, iy, ix + 5, iy, TFT_BLUE);
    } else {
      spr.fillCircle(ix, iy, 5, TFT_RED);
      spr.drawLine(ix, iy - 5, ix + 3, iy - 8, TFT_YELLOW);
    }
  }

  // Basket
  int x0 = (int)bx - BASKET_W / 2;
  spr.fillRoundRect(x0, BASKET_Y, BASKET_W, 8, 2, TFT_ORANGE);
  spr.drawFastHLine(x0, BASKET_Y, BASKET_W, TFT_YELLOW);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_RED);
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("CATCH", 67, 18, 4);

  spr.fillCircle(52, 62, 5, TFT_YELLOW);
  spr.fillCircle(82, 62, 5, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"move left", "move right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Catch coins, dodge bombs!", 67, 148, 1);

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
  int cy = y - 10 + ((t / 50) % 16);
  spr.fillCircle(x, cy, 3, c(TFT_YELLOW));
  spr.fillRoundRect(x - 8, y + 7, 16, 5, 2, c(TFT_ORANGE));
}

void begin() {
  hiScore = prefs.getUInt("catch_hi", 0);
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

}  // namespace catchgame

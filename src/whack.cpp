// Whack (Whac-A-Mole)
//
// Controls (two buttons):
//   Tap L : whack left burrow
//   Tap R : whack right burrow
// Whack brown moles (+10) and gold moles (+30). Don't hit red bombs! 3 lives.

#include "common.h"

namespace whack {

static const uint32_t TICK_MS = 16;

enum PopKind : uint8_t { PK_NONE, PK_MOLE, PK_GOLD, PK_BOMB };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static PopKind hole[2];
static int holeT[2], flashT[2], lives, spawnT;
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
    prefs.putUInt("whack_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  spawnT = 20;
  hole[0] = hole[1] = PK_NONE;
  holeT[0] = holeT[1] = flashT[0] = flashT[1] = 0;
  newHigh = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void hitHole(int h) {
  flashT[h] = 7;
  if (hole[h] == PK_NONE) return;
  if (hole[h] == PK_BOMB) {
    hole[h] = PK_NONE;
    if (--lives <= 0) gameOver();
    return;
  }
  score += (hole[h] == PK_GOLD) ? 30 : 10;
  hole[h] = PK_NONE;
  holeT[h] = 0;
}

static void tick(int eL, int eR) {
  if (flashT[0] > 0) flashT[0]--;
  if (flashT[1] > 0) flashT[1]--;

  if (eL == 1) hitHole(0);
  if (state == PLAY && eR == 1) hitHole(1);
  if (state != PLAY) return;

  for (int i = 0; i < 2; i++) {
    if (hole[i] != PK_NONE && --holeT[i] <= 0) {
      if (hole[i] == PK_MOLE || hole[i] == PK_GOLD) {
        if (--lives <= 0) { gameOver(); return; }
      }
      hole[i] = PK_NONE;
    }
  }

  if (--spawnT <= 0) {
    spawnT = max(14, 40 - (int)(score / 30));
    int h = rnd(2);
    if (hole[h] == PK_NONE) {
      int r = rnd(100);
      hole[h] = (r < 22) ? PK_BOMB : (r < 42) ? PK_GOLD : PK_MOLE;
      holeT[h] = max(24, 62 - (int)(score / 25));
    }
  }
}

static void drawGame() {
  spr.fillSprite(0x2B43);                               // grassy green

  const int hx[2] = {36, 99};
  const int hy = 152;
  for (int i = 0; i < 2; i++) {
    spr.fillEllipse(hx[i], hy + 18, 26, 12, 0x4180);
    spr.drawEllipse(hx[i], hy + 18, 26, 12, flashT[i] ? TFT_WHITE : 0x7AE3);

    if (hole[i] == PK_MOLE || hole[i] == PK_GOLD) {
      uint16_t col = (hole[i] == PK_GOLD) ? TFT_YELLOW : 0x9A60;
      spr.fillRoundRect(hx[i] - 14, hy - 14, 28, 32, 12, col);
      spr.fillCircle(hx[i] - 5, hy - 3, 2, TFT_BLACK);
      spr.fillCircle(hx[i] + 5, hy - 3, 2, TFT_BLACK);
      spr.fillCircle(hx[i], hy + 3, 3, TFT_RED);
    } else if (hole[i] == PK_BOMB) {
      spr.fillCircle(hx[i], hy, 14, TFT_RED);
      spr.fillCircle(hx[i], hy, 10, 0x18E3);
      spr.drawLine(hx[i], hy - 14, hx[i] + 4, hy - 20, TFT_YELLOW);
    }

    if (flashT[i] > 0) {
      spr.fillRoundRect(hx[i] - 12, hy - 26, 24, 10, 3, TFT_ORANGE);
    }
  }

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("L", 36, 204, 4);
  spr.drawString("R", 99, 204, 4);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_RED);
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("WHACK", 67, 18, 4);

  spr.fillRoundRect(53, 50, 28, 26, 10, 0x9A60);
  spr.fillCircle(62, 58, 2, TFT_BLACK);
  spr.fillCircle(72, 58, 2, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"whack left", "whack right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Whack moles, skip red bombs!", 67, 148, 1);

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
  spr.fillEllipse(x, y + 8, 10, 4, c(0x4180));
  int pop = (int)roundf(8 * triWave(t, 900));
  spr.fillRoundRect(x - 6, y + 2 - pop, 12, 10, 4, c(0x9A60));
}

void begin() {
  hiScore = prefs.getUInt("whack_hi", 0);
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

}  // namespace whack

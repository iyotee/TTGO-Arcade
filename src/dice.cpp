// Dice (Dice, Coin & Random Picks)
//
// Controls (two buttons):
//   Tap L  : next mode (1-5 dice, coin, D20, 1-100, yes/no)
//   Tap R  : roll
//   Hold L : back to menu
// The mode is remembered.

#include "common.h"

namespace dice {

enum Mode { D1, D2, D3, D4, D5, COIN, D20, PCT, YESNO, NUM_MODES };
static const char *MODE_NAME[NUM_MODES] = {"1 die", "2 dice", "3 dice", "4 dice", "5 dice", "Coin", "D20", "1 - 100", "Yes / No"};
static const uint32_t ROLL_MS = 750;

static int mode = D2;
static int vals[5];
static bool rolled = false, rolling = false;
static uint32_t rollEnd = 0, lastShuffle = 0, lastInput = 0;
static bool dirty = true, pressedL = false, pressedR = false;

static int rnd(int n) { return esp_random() % n; }

static int range() {
  if (mode <= D5) return 6;
  if (mode == COIN || mode == YESNO) return 2;
  return mode == D20 ? 20 : 100;
}
static int numVals() { return mode <= D5 ? mode + 1 : 1; }

static void shuffle() {
  for (int i = 0; i < numVals(); i++) vals[i] = 1 + rnd(range());
}

// ---------------- rendering ----------------
static void drawDie(int cx, int cy, int s, int v, uint16_t face) {
  spr.fillRoundRect(cx - s / 2, cy - s / 2, s, s, s / 6, face);
  spr.drawRoundRect(cx - s / 2, cy - s / 2, s, s, s / 6, TFT_DARKGREY);
  int o = s * 27 / 100, r = max(2, s / 11);
  auto pip = [&](int dx, int dy) { spr.fillCircle(cx + dx * o, cy + dy * o, r, TFT_BLACK); };
  if (v & 1) pip(0, 0);
  if (v >= 2) { pip(-1, -1); pip(1, 1); }
  if (v >= 4) { pip(1, -1); pip(-1, 1); }
  if (v == 6) { pip(-1, 0); pip(1, 0); }
}

static void bigText(const char *s, int y, uint16_t col) {
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(col);
  spr.setTextSize(2);
  spr.drawString(s, 67, y, 4);
  spr.setTextSize(1);
}

static void render(uint32_t now) {
  char buf[24];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("DICE", 67, 6, 4);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString(MODE_NAME[mode], 67, 34, 2);

  uint16_t face = rolling ? TFT_LIGHTGREY : TFT_WHITE;
  int cy = 118;
  if (!rolled && !rolling) {
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("Tap R to roll", 67, cy - 8, 2);
  } else if (mode <= D5) {
    int n = numVals();
    static const int8_t POS[5][5][2] = {
      {{0, 0}},
      {{-30, 0}, {30, 0}},
      {{-30, -28}, {30, -28}, {0, 28}},
      {{-30, -30}, {30, -30}, {-30, 30}, {30, 30}},
      {{-32, -42}, {32, -42}, {0, 0}, {-32, 42}, {32, 42}},
    };
    int s = n == 1 ? 64 : n == 5 ? 40 : 46;
    for (int i = 0; i < n; i++) {
      int jx = rolling ? rnd(5) - 2 : 0, jy = rolling ? rnd(5) - 2 : 0;
      drawDie(67 + POS[n - 1][i][0] + jx, cy - 6 + POS[n - 1][i][1] + jy, s, vals[i], face);
    }
    if (n > 1 && !rolling) {
      int sum = 0;
      for (int i = 0; i < n; i++) sum += vals[i];
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(TFT_CYAN);
      snprintf(buf, sizeof buf, "Total %d", sum);
      spr.drawString(buf, 67, 180, 2);
    }
  } else if (mode == COIN) {
    // the coin spins as a narrowing ellipse while rolling
    int rx = rolling ? 4 + (int)(32 * fabsf(sinf(now * 0.02f))) : 36;
    spr.fillEllipse(67, cy, rx, 36, TFT_GOLD);
    spr.drawEllipse(67, cy, max(1, rx - 4), 32, 0xC460);
    if (!rolling) {
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(0x8200);
      spr.drawString(vals[0] == 1 ? "H" : "T", 67, cy + 1, 4);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(TFT_WHITE);
      spr.drawString(vals[0] == 1 ? "HEADS" : "TAILS", 67, 166, 4);
    }
  } else if (mode == D20) {
    int r = 46;
    // a hexagon with a triangle face, like a d20 seen from above
    for (int k = 0; k < 6; k++) {
      float a0 = k * PI / 3 - PI / 2, a1 = (k + 1) * PI / 3 - PI / 2;
      spr.fillTriangle(67, cy, 67 + (int)(cosf(a0) * r), cy + (int)(sinf(a0) * r),
                       67 + (int)(cosf(a1) * r), cy + (int)(sinf(a1) * r), k & 1 ? 0x4A69 : 0x632C);
    }
    snprintf(buf, sizeof buf, "%d", vals[0]);
    bigText(buf, cy + 2, rolling ? TFT_LIGHTGREY : (vals[0] == 20 ? TFT_GREEN : vals[0] == 1 ? TFT_RED : TFT_WHITE));
  } else if (mode == PCT) {
    snprintf(buf, sizeof buf, "%d", vals[0]);
    bigText(buf, cy, rolling ? TFT_DARKGREY : TFT_CYAN);
  } else {
    bigText(vals[0] == 1 ? "YES" : "NO", cy, rolling ? TFT_DARKGREY : (vals[0] == 1 ? TFT_GREEN : TFT_RED));
  }

  drawToolFooter("mode", "roll");
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  uint16_t face = dim ? TFT_DARKGREY : TFT_WHITE;
  int v = t ? 1 + (t / 250) % 6 : 5;               // the die keeps landing on new faces
  int wob = t ? (int)roundf(2 * (triWave(t, 500) - 0.5f)) : 0;
  spr.fillRoundRect(x - 10 + wob, y - 10, 20, 20, 4, face);
  int o = 5;
  auto pip = [&](int dx, int dy) { spr.fillCircle(x + wob + dx * o, y + dy * o, 1, TFT_BLACK); };
  if (v & 1) pip(0, 0);
  if (v >= 2) { pip(-1, -1); pip(1, 1); }
  if (v >= 4) { pip(1, -1); pip(-1, 1); }
  if (v == 6) { pip(-1, 0); pip(1, 0); }
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Dice, coin, D20");
}

void begin() {
  mode = prefs.getUChar("dicemode", D2) % NUM_MODES;
  rolled = rolling = false;
  pressedL = pressedR = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    if (!rolling) {
      mode = (mode + 1) % NUM_MODES;
      prefs.putUChar("dicemode", mode);
      rolled = false;
      dirty = true;
    }
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    if (!rolling) {
      rolling = true;
      rollEnd = now + ROLL_MS;
      shuffle();
      dirty = true;
    }
  }

  if (rolling) {
    if ((int32_t)(now - rollEnd) >= 0) {
      rolling = false;
      rolled = true;
      shuffle();                                   // the real result
      dirty = true;
    } else if ((int32_t)(now - lastShuffle) >= 70) {
      lastShuffle = now;
      shuffle();
      dirty = true;
    }
  }

  if (!rolling && idleTooLong(now, lastInput)) goToSleep();
  if (dirty) {
    render(now);
    dirty = false;
  }
}

}  // namespace dice

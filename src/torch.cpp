// Torch (Pocket Flashlight)
//
// Controls (two buttons):
//   Tap L : cycle mode (White, Warm, Red, SOS, Strobe)
//   Tap R : back to menu (restores saved brightness)

#include "common.h"

namespace torch {

enum Mode { M_WHITE, M_WARM, M_RED, M_SOS, M_STROBE, NUM_MODES };
static const char *MODE_NAMES[NUM_MODES] = {"WHITE", "WARM", "RED", "SOS", "STROBE"};

static Mode mode = M_WHITE;
static int savedBright = 2;
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t openedAt = 0, lastFrame = 0;

static bool sosOn(uint32_t ms) {
  // ... --- ... pattern in 200 ms units
  static const uint8_t PAT[] = {
    1,0,1,0,1,0,0, 1,1,1,0,1,1,1,0,1,1,1,0,0, 1,0,1,0,1,0,0,0,0,0
  };
  uint32_t idx = (ms / 180) % (sizeof PAT / sizeof PAT[0]);
  return PAT[idx] != 0;
}

static void render(uint32_t now) {
  uint16_t bg = TFT_WHITE;
  uint16_t fg = TFT_BLACK;
  if (mode == M_WARM) bg = 0xFE94;
  else if (mode == M_RED) { bg = TFT_RED; fg = TFT_WHITE; }
  else if (mode == M_SOS) bg = sosOn(now - openedAt) ? TFT_WHITE : TFT_BLACK;
  else if (mode == M_STROBE) bg = ((now / 70) & 1) ? TFT_WHITE : TFT_BLACK;

  if (bg == TFT_BLACK) fg = TFT_DARKGREY;
  spr.fillSprite(bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(fg);
  spr.drawString(MODE_NAMES[mode], 67, 108, 2);

  spr.setTextDatum(TL_DATUM);
  spr.drawString("L mode", 10, 220, 2);
  spr.setTextDatum(TR_DATUM);
  spr.drawString("R exit", 125, 220, 2);
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  spr.fillRoundRect(x - 4, y - 2, 8, 12, 2, c(TFT_LIGHTGREY));
  spr.fillTriangle(x - 6, y - 2, x + 6, y - 2, x - 4, y - 8, c(TFT_YELLOW));
  spr.fillTriangle(x + 6, y - 2, x + 4, y - 8, x - 4, y - 8, c(TFT_YELLOW));
  if (!dim && ((t / 400) & 1)) {
    spr.drawLine(x - 6, y - 11, x - 9, y - 14, TFT_WHITE);
    spr.drawLine(x, y - 11, x, y - 15, TFT_WHITE);
    spr.drawLine(x + 6, y - 11, x + 9, y - 14, TFT_WHITE);
  }
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Screen flashlight");
}

void begin() {
  mode = M_WHITE;
  savedBright = brightnessLevel();
  setBrightness(NUM_BRIGHTNESS - 1, false);
  pressedL = pressedR = false;
  openedAt = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (eL == 2 && pressedL) {
    pressedL = false;
    mode = (Mode)((mode + 1) % NUM_MODES);
    dirty = true;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    setBrightness(savedBright, false);
    returnToMenu();
    return;
  }

  if ((mode == M_SOS || mode == M_STROBE) && (int32_t)(now - lastFrame) >= 35) dirty = true;
  if (dirty) {
    lastFrame = now;
    render(now);
    dirty = false;
  }
}

}  // namespace torch

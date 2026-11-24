// UI themes: Original (default) and Neon. Saved under "theme".

#include "common.h"

static const Theme THEMES[NUM_THEMES] = {
  // neon,  bg,         panel,  frame,        frameSel,    text,       textDim,       accent,      accent2,     ok,         keyHint
  {false, TFT_BLACK, 0x10A2, TFT_DARKGREY, TFT_YELLOW,  TFT_WHITE,  TFT_DARKGREY,  TFT_YELLOW,  TFT_CYAN,    TFT_GREEN,  TFT_YELLOW},
  {true,  0x0804,    0x1828, 0x500F,       0xF81F,      TFT_WHITE,  0x8C5F,        0x07FF,      0xAFE5,      0x07F0,     0xFFE0},
};
static int current = -1;

static void load() {
  if (current < 0) current = constrain((int)prefs.getUChar("theme", 0), 0, NUM_THEMES - 1);
}

const Theme &theme() { load(); return THEMES[current]; }
int themeIndex() { load(); return current; }
void setThemeIndex(int i) {
  current = constrain(i, 0, NUM_THEMES - 1);
  prefs.putUChar("theme", current);
}
const char *themeName(int i) { return i == 1 ? "Neon" : "Original"; }

void drawThemedBox(int x, int y, int w, int h, int r, uint16_t col, bool glow) {
  if (glow && theme().neon) {
    // two faint halos outside the bright outline
    spr.drawRoundRect(x - 2, y - 2, w + 4, h + 4, r + 2, tft.alphaBlend(50, col, theme().bg));
    spr.drawRoundRect(x - 1, y - 1, w + 2, h + 2, r + 1, tft.alphaBlend(110, col, theme().bg));
  }
  spr.drawRoundRect(x, y, w, h, r, col);
}

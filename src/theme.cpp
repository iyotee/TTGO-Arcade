// UI themes: Original (default) and Neon. Saved under "theme".
// Original is the classic look (black, grey frames, yellow selection); Neon is a
// deep purple-black with glowing pink / cyan / lime accents, to go with neon Tigo.

#include "common.h"
#include "themeui.h"

static const Theme THEMES[NUM_THEMES] = {
  // neon,  bg,         panel,  frame,        frameSel,    text,       textDim,       accent,      accent2,     ok,         keyHint
  {false, TFT_BLACK, 0x10A2, TFT_DARKGREY, TFT_YELLOW,  TFT_WHITE,  TFT_DARKGREY,  TFT_YELLOW,  TFT_CYAN,    TFT_GREEN,  TFT_YELLOW},
  // Neon: bg (8,0,32), panel (24,8,64), frame (96,16,144) purple, frameSel hot pink,
  // textDim (120,72,208) muted violet, accent cyan, accent2 lime, ok spring green
  {true,  0x0804,    0x1848, 0x6092,       0xF81F,      TFT_WHITE,  0x7A5A,        0x07FF,      0xAFE5,      0x07F0,     0xFFE0},
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

// ---------------- extra helpers (themeui.h) ----------------
uint16_t themeSoft() { return theme().neon ? 0xBD5F : TFT_LIGHTGREY; }   // (184,168,248)

uint16_t themeFade(uint8_t a, uint16_t col) { return tft.alphaBlend(a, col, theme().bg); }

void drawSelBox(int x, int y, int w, int h, int r, bool sel, uint16_t selCol) {
  const Theme &th = theme();
  uint16_t col = sel ? (selCol ? selCol : th.frameSel) : th.frame;
  if (th.neon) {
    // the halo sits outside the box; the inner line makes the selection read
    // as a bright tube rather than a hairline
    drawThemedBox(x, y, w, h, r, col, sel);
    if (sel) spr.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, tft.alphaBlend(120, col, th.bg));
    return;
  }
  spr.drawRoundRect(x, y, w, h, r, col);
  if (sel) spr.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, col);
}

void drawThemedFooter(const char *l, const char *r, int y, int rx) {
  const Theme &th = theme();
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("L", 10, y, 2);
  spr.drawString("R", rx, y, 2);
  spr.setTextColor(th.text);
  spr.drawString(l, 22, y, 2);
  spr.drawString(r, rx + 12, y, 2);
}

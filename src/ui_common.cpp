// Shared helpers compiled into both firmwares (the launcher and the "Big
// Games" cartridge): globals, button debouncing, the themed score / game-over
// / tool screens, the hardware start-up and the Sleep entry.
//
// No controls of its own: the helpers act on the buttons the caller reads.

#include "common.h"
#include "ui_common.h"
#include "themeui.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
Preferences prefs;
Btn bL{PIN_LEFT}, bR{PIN_RIGHT};

int updateBtn(Btn &b, uint32_t now) {
  bool r = digitalRead(b.pin) == LOW;
  if (r != b.raw) { b.raw = r; b.rawT = now; }
  if (r != b.stable && now - b.rawT >= DEBOUNCE_MS) {
    b.stable = r;
    if (r) { b.t0 = now; b.held = false; b.consumed = false; return 1; }
    return 2;
  }
  return 0;
}

void drawCell(int px, int py, int sz, uint16_t col) {
  spr.fillRect(px, py, sz - 1, sz - 1, col);
  uint16_t lt = tft.alphaBlend(120, TFT_WHITE, col);
  uint16_t dk = tft.alphaBlend(110, TFT_BLACK, col);
  spr.drawFastHLine(px, py, sz - 1, lt);
  spr.drawFastVLine(px, py, sz - 1, lt);
  spr.drawFastHLine(px, py + sz - 2, sz - 1, dk);
  spr.drawFastVLine(px + sz - 2, py, sz - 1, dk);
}

float triWave(uint32_t t, uint32_t period) {
  float f = (float)(t % period) / period;
  return f < 0.5f ? 2 * f : 2 - 2 * f;
}

// ---------------- shared screens (themed) ----------------
void drawScoreHeader(uint32_t score, uint32_t best) {
  char buf[32];
  const Theme &th = theme();
  spr.fillRect(0, 0, 135, HEADER_H, th.bg);
  if (th.neon) spr.drawFastHLine(0, HEADER_H - 2, 135, themeFade(110, th.frameSel));   // neon underline
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.text);
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(th.neon ? th.accent : TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  spr.drawString(buf, 2, 24, 1);
}

void drawGameOverBox(const char *title, uint32_t score, uint32_t best, bool newHigh) {
  char buf[32];
  const Theme &th = theme();
  uint16_t edge = th.neon ? th.frameSel : TFT_RED;
  if (th.neon) {
    spr.fillRoundRect(6, 92, 123, 84, 6, th.panel);
    drawThemedBox(6, 92, 123, 84, 6, edge, true);
  } else {
    spr.fillRect(6, 92, 123, 84, TFT_BLACK);
    spr.drawRect(6, 92, 123, 84, TFT_RED);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(edge);
  spr.drawString(title, 67, 98, 2);
  spr.setTextColor(th.text);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
  spr.drawString(buf, 67, 116, 2);
  if (newHigh) {
    spr.setTextColor(th.neon ? th.accent2 : TFT_YELLOW);
    spr.drawString("NEW BEST!", 67, 134, 2);
  } else {
    spr.setTextColor(themeSoft());
    snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
    spr.drawString(buf, 67, 134, 2);
  }
  // button hints: "L menu   R again"
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("L", 16, 154, 2);
  spr.drawString("R", 68, 154, 2);
  spr.setTextColor(th.neon ? th.accent : TFT_GREEN);
  spr.drawString("menu", 28, 154, 2);
  spr.drawString("again", 80, 154, 2);
}

EndChoice startOrEnd(bool gameOver, int eL, int eR, bool &armed) {
  if (eL == 1 || eR == 1) armed = true;              // need a fresh press...
  if (!armed || (eL != 2 && eR != 2)) return END_NONE;   // ...and its release
  armed = false;
  if (gameOver && eL == 2) {
    returnToMenu();
    return END_MENU;
  }
  return END_PLAY;
}

void drawToolFooter(const char *l, const char *r) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(theme().textDim);
  spr.drawString("hold L: menu", 67, 206, 1);
  drawThemedFooter(l, r, 220, 70);
}

// ---------------- sleep entry ----------------
#if PIN_RIGHT == 35
  #define WAKE_BTN "R"
#else
  #define WAKE_BTN "L"
#endif

void drawMoon(int x, int y, int r, uint16_t col, uint16_t bg) {
  spr.fillCircle(x, y, r, col);
  spr.fillCircle(x + r / 2, y - r / 3, r - 1, bg);    // bite out a crescent
}

void sleepIcon(int x, int y, bool dim, uint32_t t) {
  const Theme &th = theme();
  drawMoon(x - 2, y, 11, dim ? themeFade(80, TFT_YELLOW) : TFT_YELLOW, th.bg);
  // the two stars twinkle in turn
  auto star = [&](uint32_t phase) -> uint16_t {
    if (dim) return th.textDim;
    if (!t) return TFT_WHITE;
    return themeFade(60 + (int)(195 * triWave(t + phase, 1600)), TFT_WHITE);
  };
  spr.fillCircle(x + 9, y + 6, 1, star(800));
  spr.drawPixel(x + 5, y - 10, star(0));
}

void sleepInfo(char *buf, size_t n) { snprintf(buf, n, WAKE_BTN " wakes it up"); }

void sleepNow() {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  drawMoon(67, 80, 24, TFT_YELLOW, th.bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.text);
  spr.drawString("Good night", 67, 130, 4);
  spr.setTextColor(themeSoft());
  spr.drawString("Press " WAKE_BTN " to wake", 67, 164, 2);
  spr.pushSprite(0, 0);
  delay(1500);
  deepSleepNow();
}

// ---------------- hardware start-up ----------------
void bootHardware() {
  initPower();

  pinMode(0, INPUT_PULLUP);
  pinMode(35, INPUT);              // input-only pin, pulled up on the board

  // a button still held from the wake-up press must not count as a press
  bL.raw = bL.stable = digitalRead(bL.pin) == LOW;
  bR.raw = bR.stable = digitalRead(bR.pin) == LOW;

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  spr.setColorDepth(16);
  if (!spr.createSprite(tft.width(), tft.height())) {
    spr.setColorDepth(8);          // fallback if heap is tight
    spr.createSprite(tft.width(), tft.height());
  }

  prefs.begin("tetris", false);
}

int bothHoldStep(BothHold &h, uint32_t now) {
  if (!(bL.stable && bR.stable)) { h.on = false; return 0; }
  bL.consumed = bR.consumed = true;
  if (!h.on) {                                   // a short wait: a sloppy double tap is not a step
    h.on = true;
    h.next = now + 250;
    return 0;
  }
  if ((int32_t)(now - h.next) < 0) return 0;
  h.next = now + 160;
  return 1;
}

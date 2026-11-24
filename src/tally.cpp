// Tally (Two-Player Counter)
//
// Controls (two buttons):
//   Tap L    : +1 for A (top)
//   Tap R    : +1 for B (bottom)
//   Hold R   : undo the last +1
//   Hold L+R : reset both to 0
//   Hold L   : back to menu
// Both counts are saved, so they survive sleep and power-off.

#include "common.h"

namespace tally {

static const uint32_t UNDO_MS = 600, RESET_MS = 1200;
static const int MAX_UNDO = 16;

static uint32_t a = 0, b = 0;
static int8_t hist[MAX_UNDO];                      // 1 = A, 2 = B, newest last
static int nHist = 0;
static uint32_t lastInput = 0, flashAt = 0, bothAt = 0;
static int flashWho = 0;
static bool dirty = true, pressedL = false, pressedR = false, both = false;

static void save() {
  prefs.putUInt("tallyA", a);
  prefs.putUInt("tallyB", b);
}

static void bump(int who, uint32_t now) {
  if (who == 1) { if (a < 9999) a++; }
  else if (b < 9999) b++;
  if (nHist == MAX_UNDO) { memmove(hist, hist + 1, MAX_UNDO - 1); nHist--; }
  hist[nHist++] = who;
  flashWho = who;
  flashAt = now;
  save();
}

static void bigNum(uint32_t v, int y, uint16_t col) {
  char buf[8];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(col);
  spr.setTextSize(2);
  spr.drawString(buf, 67, y, 4);
  spr.setTextSize(1);
}

static void render(uint32_t now) {
  spr.fillSprite(TFT_BLACK);
  bool fl = (int32_t)(now - flashAt) < 150;
  // A on top, B below
  spr.fillRoundRect(4, 4, 127, 96, 8, fl && flashWho == 1 ? 0x0228 : 0x0124);
  spr.fillRoundRect(4, 104, 127, 96, 8, fl && flashWho == 2 ? 0x4008 : 0x2004);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("A", 12, 10, 2);
  spr.setTextColor(TFT_MAGENTA);
  spr.drawString("B", 12, 110, 2);
  bigNum(a, 56, TFT_CYAN);
  bigNum(b, 156, TFT_MAGENTA);

  if (both) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_RED);
    spr.drawString("keep holding: reset", 67, 94, 1);
  }
  drawToolFooter("A +1", "B +1");
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  int n = t ? 1 + (t / 400) % 5 : 4;               // tally marks being added
  for (int i = 0; i < min(n, 4); i++) spr.drawFastVLine(x - 9 + i * 5, y - 9, 18, c(TFT_WHITE));
  spr.drawFastVLine(x - 9 + 1, y - 9, 18, c(TFT_WHITE));
  if (n == 5) spr.drawLine(x - 12, y + 6, x + 9, y - 6, c(TFT_YELLOW));
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "%lu : %lu", (unsigned long)prefs.getUInt("tallyA", 0), (unsigned long)prefs.getUInt("tallyB", 0));
}

void begin() {
  a = prefs.getUInt("tallyA", 0);
  b = prefs.getUInt("tallyB", 0);
  nHist = 0;
  pressedL = pressedR = both = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  // both held: reset after a long hold; nothing else fires for this press
  if (bL.stable && bR.stable && (pressedL || pressedR)) {
    if (!both) { both = true; bothAt = now; dirty = true; }
    if ((int32_t)(now - bothAt) >= (int32_t)RESET_MS) {
      a = b = 0;
      nHist = 0;
      save();
      both = false;
      pressedL = pressedR = false;
      bL.consumed = bR.consumed = true;
      dirty = true;
    }
  } else if (both && !bL.stable && !bR.stable) {
    both = false;
    pressedL = pressedR = false;
    dirty = true;
  }

  if (!both) {
    if (pressedL && bL.stable && !bR.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
      pressedL = false;
      returnToMenu();
      return;
    }
    if (pressedR && bR.stable && !bL.stable && !bR.consumed && (int32_t)(now - bR.t0) >= (int32_t)UNDO_MS) {
      bR.consumed = true;
      if (nHist) {
        int who = hist[--nHist];
        if (who == 1 && a) a--;
        if (who == 2 && b) b--;
        save();
      }
      dirty = true;
    }
    if (eL == 2 && pressedL) { pressedL = false; bump(1, now); dirty = true; }
    if (eR == 2 && pressedR) { pressedR = false; if (!bR.consumed) { bump(2, now); dirty = true; } }
  }

  static bool wasFlash = false;
  bool fl = (int32_t)(now - flashAt) < 150;
  if (fl != wasFlash) { wasFlash = fl; dirty = true; }

  if (idleTooLong(now, lastInput)) goToSleep();
  if (dirty) {
    render(now);
    dirty = false;
  }
}

}  // namespace tally

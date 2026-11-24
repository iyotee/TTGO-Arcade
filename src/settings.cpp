// Settings screen: battery level (read only), display brightness, USB logging,
// reset high scores submenu and the last recorded crash.
//
//   Tap L : next row (hold to auto-repeat in reset list)
//   Tap R : change brightness / toggle USB log / open reset / back

#include "common.h"

namespace settings {

enum Row { ROW_BRIGHTNESS, ROW_USBLOG, ROW_SLEEP, ROW_RESET, ROW_BACK, NUM_ROWS };
enum Screen { SCR_MAIN, SCR_RESET, SCR_CONFIRM };

static Screen screen = SCR_MAIN;
static int row = ROW_BRIGHTNESS;
static int resetIdx = 0, resetScroll = 0;
static int mv = 0;                                      // last battery reading
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastRead = 0, lastInput = 0;

static const int RESET_LIST_Y = 34, RESET_LIST_H = 180;
static const int RESET_BOX_H = 24, RESET_PITCH = 27;

static int resetTotal() { return numGames() + 2; }      // "All games" + games + "Back"

static int resetScrollTarget() {
  int maxScroll = max(0, resetTotal() * RESET_PITCH - 3 - RESET_LIST_H);
  int top = resetIdx * RESET_PITCH;
  int bottom = top + RESET_BOX_H + 2;
  int t = resetScroll;
  if (bottom > t + RESET_LIST_H) t = bottom - RESET_LIST_H;
  if (top < t) t = top;
  return constrain(t, 0, maxScroll);
}

static void drawBattery(int x, int y, int w, int h, int pct, uint16_t col) {
  spr.drawRoundRect(x, y, w, h, 3, TFT_LIGHTGREY);
  spr.fillRect(x + w, y + h / 3, 3, h / 3, TFT_LIGHTGREY);   // terminal
  int fill = (w - 4) * pct / 100;
  if (fill > 0) spr.fillRect(x + 2, y + 2, fill, h - 4, col);
}

static void drawBolt(int x, int y, uint16_t col) {
  spr.fillTriangle(x + 4, y, x, y + 8, x + 5, y + 8, col);
  spr.fillTriangle(x + 3, y + 6, x + 8, y + 6, x + 2, y + 14, col);
}

static void drawRowBox(int y, int h, bool sel) {
  uint16_t frame = sel ? TFT_YELLOW : TFT_DARKGREY;
  spr.drawRoundRect(6, y, 123, h, 6, frame);
  if (sel) spr.drawRoundRect(7, y + 1, 121, h - 2, 5, frame);
}

static void renderMain() {
  char buf[40];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("SETTINGS", 67, 4, 4);

  // battery (read only)
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Battery", 12, 28, 1);
  if (onUsbPower(mv)) {
    drawBattery(12, 40, 42, 18, 100, 0x2D6A);
    drawBolt(29, 42, TFT_YELLOW);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("USB", 64, 38, 4);
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("power", 64, 60, 1);
  } else {
    int pct = batteryPercent(mv);
    uint16_t col = pct > 40 ? TFT_GREEN : pct > 15 ? TFT_YELLOW : TFT_RED;
    drawBattery(12, 40, 42, 18, pct, col);
    spr.setTextColor(col);
    snprintf(buf, sizeof buf, "%d%%", pct);
    spr.drawString(buf, 64, 38, 4);
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "%d.%02d V", mv / 1000, mv % 1000 / 10);
    spr.drawString(buf, 64, 60, 1);
  }

  // brightness
  bool sel = row == ROW_BRIGHTNESS;
  drawRowBox(72, 36, sel);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Brightness", 14, 74, 2);
  int lvl = brightnessLevel();
  for (int i = 0; i < NUM_BRIGHTNESS; i++) {
    int x = 15 + i * 22, h = 4 + i * 2, y = 104 - h;
    if (i <= lvl) spr.fillRect(x, y, 18, h, sel ? TFT_YELLOW : TFT_DARKGREY);
    else spr.drawRect(x, y, 18, h, TFT_DARKGREY);
  }

  // USB log
  sel = row == ROW_USBLOG;
  drawRowBox(112, 20, sel);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("USB log", 14, 114, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(!sel ? TFT_DARKGREY : usbLogOn() ? TFT_GREEN : TFT_LIGHTGREY);
  spr.drawString(usbLogOn() ? "On" : "Off", 121, 114, 2);

  // Auto-sleep
  sel = row == ROW_SLEEP;
  drawRowBox(136, 20, sel);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Auto-sleep", 14, 138, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(sel ? TFT_CYAN : TFT_DARKGREY);
  spr.drawString(sleepLabel(), 121, 138, 2);

  // Reset scores
  sel = row == ROW_RESET;
  drawRowBox(160, 20, sel);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Reset scores", 14, 162, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(sel ? TFT_YELLOW : TFT_DARKGREY);
  spr.drawString(">", 121, 162, 2);

  // back
  sel = row == ROW_BACK;
  drawRowBox(184, 20, sel);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Back to menu", 67, 186, 2);

  // last crash, if any (small, so it doesn't crowd the screen)
  if (lastCrash(buf, sizeof buf)) {
    char line[48];
    snprintf(line, sizeof line, "! %s", buf);   // e.g. "! watchdog in Flappy"
    spr.setTextColor(TFT_ORANGE);
    spr.drawString(line, 67, 208, 1);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 222, 2);
  spr.drawString("R", 70, 222, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 22, 222, 2);
  const char *act = (row == ROW_BRIGHTNESS || row == ROW_SLEEP) ? "change"
                  : row == ROW_USBLOG                            ? "toggle"
                  : row == ROW_RESET                             ? "open"
                                                                 : "back";
  spr.drawString(act, 82, 222, 2);
}

static void renderResetList() {
  char buf[24];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("RESET SCORES", 67, 8, 2);

  int total = resetTotal();
  int ng = numGames();
  int maxScroll = max(0, total * RESET_PITCH - 3 - RESET_LIST_H);

  spr.setViewport(0, RESET_LIST_Y, 135, RESET_LIST_H);
  for (int i = 0; i < total; i++) {
    int y = 2 + i * RESET_PITCH - resetScroll;
    if (y + RESET_BOX_H < 0 || y >= RESET_LIST_H) continue;
    bool sel = (i == resetIdx);
    drawRowBox(y, RESET_BOX_H, sel);

    if (i == 0) {
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(sel ? TFT_RED : TFT_DARKGREY);
      spr.drawString("All games", 14, y + 4, 2);
    } else if (i <= ng) {
      const char *hiKey = nullptr;
      const char *name = gameInfo(i - 1, &hiKey);
      uint32_t hi = hiKey ? prefs.getUInt(hiKey, 0) : 0;
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
      spr.drawString(name, 14, y + 4, 2);
      spr.setTextDatum(TR_DATUM);
      spr.setTextColor(sel ? TFT_LIGHTGREY : TFT_DARKGREY);
      snprintf(buf, sizeof buf, "%lu", (unsigned long)hi);
      spr.drawString(buf, 121, y + 8, 1);
    } else {
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
      spr.drawString("Back", 67, y + 4, 2);
    }
  }
  spr.resetViewport();

  if (resetScroll > 0) spr.fillTriangle(124, 15, 132, 15, 128, 9, TFT_LIGHTGREY);
  if (resetScroll < maxScroll) spr.fillTriangle(124, 224, 132, 224, 128, 230, TFT_LIGHTGREY);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 222, 2);
  spr.drawString("R", 70, 222, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 22, 222, 2);
  spr.drawString(resetIdx == total - 1 ? "back" : "reset", 82, 222, 2);
}

static void renderConfirm() {
  char buf[32];
  renderResetList();
  spr.fillRect(6, 74, 123, 92, TFT_BLACK);
  spr.drawRect(6, 74, 123, 92, TFT_RED);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_RED);
  spr.drawString("DELETE BEST?", 67, 82, 2);

  spr.setTextColor(TFT_WHITE);
  if (resetIdx == 0) {
    spr.drawString("All games", 67, 104, 2);
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("Clears every score", 67, 124, 1);
  } else {
    const char *hiKey = nullptr;
    const char *name = gameInfo(resetIdx - 1, &hiKey);
    uint32_t hi = hiKey ? prefs.getUInt(hiKey, 0) : 0;
    spr.drawString(name, 67, 104, 2);
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "Best: %lu", (unsigned long)hi);
    spr.drawString(buf, 67, 124, 1);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 14, 144, 2);
  spr.drawString("R", 72, 144, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("no", 26, 144, 2);
  spr.setTextColor(TFT_RED);
  spr.drawString("yes", 84, 144, 2);
}

static void render() {
  if (screen == SCR_MAIN) renderMain();
  else if (screen == SCR_RESET) renderResetList();
  else renderConfirm();
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  uint16_t col = dim ? tft.alphaBlend(80, TFT_LIGHTGREY, TFT_BLACK) : TFT_LIGHTGREY;
  for (int i = 0; i < 8; i++) {                         // gear teeth, turning slowly
    float a = i * PI / 4 + t * 0.0015f;
    spr.fillCircle(x + (int)roundf(cosf(a) * 8), y + (int)roundf(sinf(a) * 8), 2, col);
  }
  spr.fillCircle(x, y, 7, col);
  spr.fillCircle(x, y, 3, TFT_BLACK);
}

void menuInfo(char *buf, size_t n) {
  // the menu redraws every frame while scrolling; don't measure that often
  static int v = 0;
  static uint32_t readAt = 0;
  if (!readAt || millis() - readAt > 5000) { v = batteryMilliVolts(); readAt = millis(); }
  if (onUsbPower(v)) snprintf(buf, n, "USB power");
  else snprintf(buf, n, "Battery %d%%", batteryPercent(v));
}

void begin() {
  screen = SCR_MAIN;
  row = ROW_BRIGHTNESS;
  resetIdx = resetScroll = 0;
  pressedL = pressedR = false;
  mv = batteryMilliVolts();
  lastRead = lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  // act on release, only for presses that started on the current screen
  if (eL == 1) { pressedL = true; bL.nextRepeat = now + 380; }
  if (eR == 1) pressedR = true;

  if (screen == SCR_MAIN) {
    if (eL == 2 && pressedL) {
      pressedL = false;
      row = (row + 1) % NUM_ROWS;
      dirty = true;
    }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (row == ROW_BACK) { returnToMenu(); return; }
      if (row == ROW_USBLOG) setUsbLog(!usbLogOn());
      else if (row == ROW_BRIGHTNESS) setBrightness((brightnessLevel() + 1) % NUM_BRIGHTNESS, true);
      else if (row == ROW_SLEEP) setSleepOption((sleepOption() + 1) % NUM_SLEEP_OPTS, true);
      else {
        screen = SCR_RESET;
        resetIdx = resetScroll = 0;
        pressedL = pressedR = false;
      }
      dirty = true;
    }
  } else if (screen == SCR_RESET) {
    int total = resetTotal();
    if (bL.stable && pressedL && (int32_t)(now - bL.nextRepeat) >= 0) {
      bL.nextRepeat = now + 150;
      bL.consumed = true;
      resetIdx = (resetIdx + 1) % total;
      resetScroll = resetScrollTarget();
      dirty = true;
    }
    if (eL == 2 && pressedL) {
      pressedL = false;
      if (!bL.consumed) {
        resetIdx = (resetIdx + 1) % total;
        resetScroll = resetScrollTarget();
        dirty = true;
      }
    }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (resetIdx == total - 1) {
        screen = SCR_MAIN;
      } else {
        screen = SCR_CONFIRM;
      }
      pressedL = pressedR = false;
      dirty = true;
    }
  } else {                                               // SCR_CONFIRM
    if (eL == 2 && pressedL) {
      pressedL = false;
      screen = SCR_RESET;
      dirty = true;
    }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (resetIdx == 0) {
        for (int i = 0; i < numGames(); i++) {
          const char *hiKey = nullptr;
          gameInfo(i, &hiKey);
          if (hiKey) prefs.remove(hiKey);
        }
      } else {
        const char *hiKey = nullptr;
        gameInfo(resetIdx - 1, &hiKey);
        if (hiKey) prefs.remove(hiKey);
      }
      screen = SCR_RESET;
      dirty = true;
    }
  }

  if (screen == SCR_MAIN && now - lastRead >= 1000) {    // live battery reading
    lastRead = now;
    mv = batteryMilliVolts();
    dirty = true;
  }
  if (idleTooLong(now, lastInput)) goToSleep();

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace settings

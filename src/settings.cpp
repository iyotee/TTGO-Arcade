// Settings screen: battery level (read only) above a scrolling list of
// brightness, screensaver delay, sleep delay, PIN lock (lockscr.cpp), WiFi
// (wifiui.cpp), Bluetooth, USB logging and the reset high scores submenu; the
// last recorded crash shows under the list.
//
//   Tap L : next row (hold to auto-repeat)
//   Tap R : change / toggle / open / back

#include "common.h"
#include "power.h"
#include "lockscr.h"

namespace wifiui { void open(); bool update(uint32_t now); }

namespace settings {

enum Row { ROW_BRIGHTNESS, ROW_SAVER, ROW_SLEEP, ROW_PIN, ROW_CHANGEPIN, ROW_WIFI, ROW_BLE, ROW_USBLOG,
           ROW_RESET, ROW_BACK, NUM_ROWS };
enum Screen { SCR_MAIN, SCR_RESET, SCR_CONFIRM, SCR_WIFI };

static Screen screen = SCR_MAIN;
static int row = ROW_BRIGHTNESS;
static int resetIdx = 0, resetScroll = 0, mainScroll = 0;
static int mv = 0;                                      // last battery reading
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastRead = 0, lastInput = 0;

static const int RESET_LIST_Y = 34, RESET_LIST_H = 180;
static const int RESET_BOX_H = 24, RESET_PITCH = 27;
static const int MAIN_LIST_Y = 70, MAIN_LIST_H = 142;   // screen rows 70..211, under the battery
static const int ROW_GAP = 3;

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

// ---------------- main list geometry ----------------
static int rowH(int r) { return r == ROW_BRIGHTNESS ? 32 : 22; }
static int rowTop(int r) {                              // in list coordinates
  int y = 0;
  for (int i = 0; i < r; i++) y += rowH(i) + ROW_GAP;
  return y;
}
static int mainMaxScroll() { return max(0, rowTop(NUM_ROWS - 1) + rowH(NUM_ROWS - 1) + 4 - MAIN_LIST_H); }

// smallest scroll change that shows the selected row completely
static int mainScrollTarget() {
  int top = rowTop(row), bottom = top + rowH(row) + 4;
  int t = mainScroll;
  if (bottom > t + MAIN_LIST_H) t = bottom - MAIN_LIST_H;
  if (top < t) t = top;
  return constrain(t, 0, mainMaxScroll());
}

// ---------------- rendering ----------------
static void drawValueRow(int y, bool sel, const char *label, const char *value, uint16_t valueCol, bool usable = true) {
  drawRowBox(y, 22, sel);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(sel && usable ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString(label, 14, y + 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(sel && usable ? valueCol : TFT_DARKGREY);
  spr.drawString(value, 121, y + 3, 2);
}

static void renderRow(int r, int y) {
  bool sel = r == row;
  switch (r) {
    case ROW_BRIGHTNESS: {
      drawRowBox(y, 32, sel);
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
      spr.drawString("Brightness", 14, y + 1, 2);
      int lvl = brightnessLevel();
      for (int i = 0; i < NUM_BRIGHTNESS; i++) {
        int x = 15 + i * 22, h = 3 + i * 2, by = y + 29 - h;
        if (i <= lvl) spr.fillRect(x, by, 18, h, sel ? TFT_YELLOW : TFT_DARKGREY);
        else spr.drawRect(x, by, 18, h, TFT_DARKGREY);
      }
      break;
    }
    case ROW_SAVER:
      drawValueRow(y, sel, "Screensaver", saverLabel(), saverOption() ? TFT_CYAN : TFT_LIGHTGREY);
      break;
    case ROW_SLEEP:
      drawValueRow(y, sel, "Sleep", sleepDelayLabel(), TFT_CYAN);
      break;
    case ROW_PIN:
      drawValueRow(y, sel, "PIN lock", lockscr::enabled() ? "On" : "Off", lockscr::enabled() ? TFT_GREEN : TFT_LIGHTGREY);
      break;
    case ROW_CHANGEPIN:
      drawValueRow(y, sel, "Change PIN", ">", TFT_YELLOW, lockscr::enabled());
      break;
    case ROW_WIFI:
      drawValueRow(y, sel, "WiFi", wifiConnected() ? "On  >" : wifiEnabled() ? "...  >" : "Off  >",
                   wifiConnected() ? TFT_GREEN : wifiEnabled() ? TFT_ORANGE : TFT_LIGHTGREY);
      break;
    case ROW_BLE:
      drawValueRow(y, sel, "Bluetooth", bleConnected() ? "Linked" : bleEnabled() ? "On" : "Off",
                   bleConnected() ? TFT_CYAN : bleEnabled() ? TFT_GREEN : TFT_LIGHTGREY);
      break;
    case ROW_USBLOG:
      drawValueRow(y, sel, "USB log", usbLogOn() ? "On" : "Off", usbLogOn() ? TFT_GREEN : TFT_LIGHTGREY);
      break;
    case ROW_RESET:
      drawValueRow(y, sel, "Reset scores", ">", TFT_YELLOW);
      break;
    default:                                            // ROW_BACK
      drawRowBox(y, 22, sel);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
      spr.drawString("Back to menu", 67, y + 3, 2);
      break;
  }
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

  // the rows, clipped to the list area (viewport coordinates start at its top)
  spr.setViewport(0, MAIN_LIST_Y, 135, MAIN_LIST_H);
  for (int r = 0; r < NUM_ROWS; r++) {
    int y = 2 + rowTop(r) - mainScroll;
    if (y + rowH(r) < 0 || y >= MAIN_LIST_H) continue;
    renderRow(r, y);
  }
  spr.resetViewport();

  // scroll bar at the right edge
  int maxScroll = mainMaxScroll();
  if (maxScroll > 0) {
    int total = MAIN_LIST_H + maxScroll;
    int barH = max(12, MAIN_LIST_H * MAIN_LIST_H / total);
    int barY = MAIN_LIST_Y + (MAIN_LIST_H - barH) * mainScroll / maxScroll;
    spr.drawFastVLine(132, MAIN_LIST_Y, MAIN_LIST_H, 0x2104);
    spr.fillRect(131, barY, 3, barH, TFT_DARKGREY);
  }

  // last crash, if any (small, so it doesn't crowd the screen)
  if (lastCrash(buf, sizeof buf)) {
    char line[48];
    snprintf(line, sizeof line, "! %s", buf);   // e.g. "! watchdog in Flappy"
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_ORANGE);
    spr.drawString(line, 67, 213, 1);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 222, 2);
  spr.drawString("R", 70, 222, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 22, 222, 2);
  const char *act = (row == ROW_BRIGHTNESS || row == ROW_SAVER || row == ROW_SLEEP) ? "change"
                  : row == ROW_PIN                    ? (lockscr::enabled() ? "turn off" : "turn on")
                  : row == ROW_CHANGEPIN              ? (lockscr::enabled() ? "change" : "-")
                  : row == ROW_USBLOG || row == ROW_BLE ? "toggle"
                  : row == ROW_RESET || row == ROW_WIFI ? "open"
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

static void nextRow() {
  row = (row + 1) % NUM_ROWS;
  mainScroll = mainScrollTarget();
  dirty = true;
}

// the PIN screens block and read the buttons themselves: start fresh here
static void afterFlow() {
  pressedL = pressedR = false;
  lastInput = lastRead = millis();
  mv = batteryMilliVolts();
}

void begin() {
  screen = SCR_MAIN;
  row = ROW_BRIGHTNESS;
  resetIdx = resetScroll = mainScroll = 0;
  pressedL = pressedR = false;
  mv = batteryMilliVolts();
  lastRead = lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  if (screen == SCR_WIFI) {                              // the WiFi screens run themselves
    if (!wifiui::update(now)) {
      screen = SCR_MAIN;
      pressedL = pressedR = false;
      lastInput = now;
      render();
    }
    return;
  }
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  // act on release, only for presses that started on the current screen
  if (eL == 1) { pressedL = true; bL.nextRepeat = now + 380; }
  if (eR == 1) pressedR = true;

  if (screen == SCR_MAIN) {
    if (bL.stable && pressedL && (int32_t)(now - bL.nextRepeat) >= 0) {
      bL.nextRepeat = now + 150;
      bL.consumed = true;
      nextRow();
    }
    if (eL == 2 && pressedL) {
      pressedL = false;
      if (!bL.consumed) nextRow();
    }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (row == ROW_BACK) { returnToMenu(); return; }
      if (row == ROW_USBLOG) setUsbLog(!usbLogOn());
      else if (row == ROW_BRIGHTNESS) setBrightness((brightnessLevel() + 1) % NUM_BRIGHTNESS, true);
      else if (row == ROW_SAVER) setSaverOption((saverOption() + 1) % NUM_SAVER_OPTS, true);
      else if (row == ROW_SLEEP) setSleepDelayOption((sleepDelayOption() + 1) % NUM_SLEEPDLY_OPTS, true);
      else if (row == ROW_BLE) setBleEnabled(!bleEnabled());
      else if (row == ROW_PIN) {
        // on: choose a code; off: only with the current code
        if (!lockscr::enabled()) lockscr::setPin();
        else if (lockscr::checkPin()) lockscr::disable();
        afterFlow();
      } else if (row == ROW_CHANGEPIN) {
        if (lockscr::enabled() && lockscr::checkPin()) lockscr::setPin();
        afterFlow();
      } else if (row == ROW_WIFI) {
        screen = SCR_WIFI;
        wifiui::open();
        return;
      } else {
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

  if (screen == SCR_MAIN && (int32_t)(now - lastRead) >= 1000) {   // signed: a PIN screen may have run since    // live battery reading
    lastRead = now;
    mv = batteryMilliVolts();
    dirty = true;
  }
  if (idleTooLong(now, lastInput)) {
    goToSleep();                                         // back here only via the menu
    return;
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace settings

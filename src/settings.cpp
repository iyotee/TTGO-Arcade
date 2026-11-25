// Settings screen: battery level (read only) above a scrolling list of
// brightness, theme (Original / Neon, theme.cpp), screensaver delay, sleep
// delay, PIN lock (lockscr.cpp), WiFi (wifiui.cpp), Updates (updates.cpp:
// check / install from GitHub, "New" when a newer launcher is out), OTA
// update (otaui.cpp, upload from the PC), Bluetooth, USB logging and the reset
// high scores submenu; the last recorded crash shows under the list.
//
//   Tap L : next row (hold to auto-repeat)
//   Tap R : change / toggle / open / back

#include "common.h"
#include "power.h"
#include "lockscr.h"
#include "themeui.h"
#include "updates.h"

namespace wifiui { void open(); bool update(uint32_t now); }

namespace settings {

enum Row { ROW_BRIGHTNESS, ROW_THEME, ROW_SAVER, ROW_SLEEP, ROW_PIN, ROW_CHANGEPIN, ROW_WIFI, ROW_UPDATES, ROW_OTA,
           ROW_BLE, ROW_USBLOG, ROW_RESET, ROW_BACK, NUM_ROWS };
enum Screen { SCR_MAIN, SCR_RESET, SCR_CONFIRM, SCR_WIFI, SCR_OTA, SCR_UPDATES };

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
  spr.drawRoundRect(x, y, w, h, 3, themeSoft());
  spr.fillRect(x + w, y + h / 3, 3, h / 3, themeSoft());     // terminal
  int fill = (w - 4) * pct / 100;
  if (fill > 0) spr.fillRect(x + 2, y + 2, fill, h - 4, col);
}

static void drawBolt(int x, int y, uint16_t col) {
  spr.fillTriangle(x + 4, y, x, y + 8, x + 5, y + 8, col);
  spr.fillTriangle(x + 3, y + 6, x + 8, y + 6, x + 2, y + 14, col);
}

static void drawRowBox(int y, int h, bool sel) { drawSelBox(6, y, 123, h, 6, sel); }

// Original colours read from the theme: grey text on the classic theme
static uint16_t dimCol() { return theme().textDim; }
static uint16_t softCol() { return themeSoft(); }

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
  spr.setTextColor(sel && usable ? theme().text : dimCol());
  spr.drawString(label, 14, y + 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(sel && usable ? valueCol : dimCol());
  spr.drawString(value, 121, y + 3, 2);
}

static void renderRow(int r, int y) {
  bool sel = r == row;
  switch (r) {
    case ROW_BRIGHTNESS: {
      drawRowBox(y, 32, sel);
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(sel ? theme().text : dimCol());
      spr.drawString("Brightness", 14, y + 1, 2);
      int lvl = brightnessLevel();
      for (int i = 0; i < NUM_BRIGHTNESS; i++) {
        int x = 15 + i * 22, h = 3 + i * 2, by = y + 29 - h;
        if (i <= lvl) spr.fillRect(x, by, 18, h, sel ? theme().accent : dimCol());
        else spr.drawRect(x, by, 18, h, dimCol());
      }
      break;
    }
    case ROW_THEME:
      drawValueRow(y, sel, "Theme", themeName(themeIndex()), theme().neon ? theme().frameSel : TFT_CYAN);
      break;
    case ROW_SAVER:
      drawValueRow(y, sel, "Screensaver", saverLabel(), saverOption() ? TFT_CYAN : softCol());
      break;
    case ROW_SLEEP:
      drawValueRow(y, sel, "Sleep", sleepDelayLabel(), TFT_CYAN);
      break;
    case ROW_PIN:
      drawValueRow(y, sel, "PIN lock", lockscr::enabled() ? "On" : "Off", lockscr::enabled() ? theme().ok : softCol());
      break;
    case ROW_CHANGEPIN:
      drawValueRow(y, sel, "Change PIN", ">", theme().keyHint, lockscr::enabled());
      break;
    case ROW_WIFI:
      drawValueRow(y, sel, "WiFi", wifiConnected() ? "On  >" : wifiEnabled() ? "...  >" : "Off  >",
                   wifiConnected() ? theme().ok : wifiEnabled() ? TFT_ORANGE : softCol());
      break;
    case ROW_UPDATES:                                   // "New" once a newer launcher is known
      drawValueRow(y, sel, "Updates", updates::updateAvailable() ? "New  >" : ">",
                   updates::updateAvailable() ? theme().ok : wifiConnected() ? theme().keyHint : softCol());
      break;
    case ROW_OTA:                                       // needs WiFi: brighter once connected
      drawValueRow(y, sel, "OTA update", ">", wifiConnected() ? theme().keyHint : softCol());
      break;
    case ROW_BLE:
      drawValueRow(y, sel, "Bluetooth", bleConnected() ? "Linked" : bleEnabled() ? "On" : "Off",
                   bleConnected() ? TFT_CYAN : bleEnabled() ? theme().ok : softCol());
      break;
    case ROW_USBLOG:
      drawValueRow(y, sel, "USB log", usbLogOn() ? "On" : "Off", usbLogOn() ? theme().ok : softCol());
      break;
    case ROW_RESET:
      drawValueRow(y, sel, "Reset scores", ">", theme().keyHint);
      break;
    default:                                            // ROW_BACK
      drawRowBox(y, 22, sel);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(sel ? theme().text : dimCol());
      spr.drawString("Back to menu", 67, y + 3, 2);
      break;
  }
}

static void renderMain() {
  char buf[40];
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  spr.setTextDatum(TC_DATUM);
  if (th.neon) {                                        // cyan shadow under a pink title
    spr.setTextColor(th.accent);
    spr.drawString("SETTINGS", 68, 5, 4);
    spr.setTextColor(th.frameSel);
  } else {
    spr.setTextColor(TFT_WHITE);
  }
  spr.drawString("SETTINGS", 67, 4, 4);

  // battery (read only)
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(softCol());
  spr.drawString("Battery", 12, 28, 1);
  if (onUsbPower(mv)) {
    drawBattery(12, 40, 42, 18, 100, 0x2D6A);
    drawBolt(29, 42, TFT_YELLOW);
    spr.setTextColor(th.text);
    spr.drawString("USB", 64, 38, 4);
    spr.setTextColor(softCol());
    spr.drawString("power", 64, 60, 1);
  } else {
    int pct = batteryPercent(mv);
    uint16_t col = pct > 40 ? TFT_GREEN : pct > 15 ? TFT_YELLOW : TFT_RED;
    drawBattery(12, 40, 42, 18, pct, col);
    spr.setTextColor(col);
    snprintf(buf, sizeof buf, "%d%%", pct);
    spr.drawString(buf, 64, 38, 4);
    spr.setTextColor(softCol());
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
    spr.drawFastVLine(132, MAIN_LIST_Y, MAIN_LIST_H, th.neon ? th.panel : 0x2104);
    spr.fillRect(131, barY, 3, barH, th.neon ? th.frame : TFT_DARKGREY);
  }

  // last crash, if any (small, so it doesn't crowd the screen)
  if (lastCrash(buf, sizeof buf)) {
    char line[48];
    snprintf(line, sizeof line, "! %s", buf);   // e.g. "! watchdog in Flappy"
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_ORANGE);
    spr.drawString(line, 67, 213, 1);
  }

  const char *act = (row == ROW_BRIGHTNESS || row == ROW_THEME || row == ROW_SAVER || row == ROW_SLEEP) ? "change"
                  : row == ROW_PIN                    ? (lockscr::enabled() ? "turn off" : "turn on")
                  : row == ROW_CHANGEPIN              ? (lockscr::enabled() ? "change" : "-")
                  : row == ROW_USBLOG || row == ROW_BLE ? "toggle"
                  : row == ROW_RESET || row == ROW_WIFI || row == ROW_OTA || row == ROW_UPDATES ? "open"
                                                        : "back";
  drawThemedFooter("next", act);
}

static void renderResetList() {
  char buf[24];
  const Theme &th = theme();
  spr.fillSprite(th.bg);
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
      spr.setTextColor(sel ? TFT_RED : dimCol());
      spr.drawString("All games", 14, y + 4, 2);
    } else if (i <= ng) {
      const char *hiKey = nullptr;
      const char *name = gameInfo(i - 1, &hiKey);
      uint32_t hi = hiKey ? prefs.getUInt(hiKey, 0) : 0;
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(sel ? th.text : dimCol());
      spr.drawString(name, 14, y + 4, 2);
      spr.setTextDatum(TR_DATUM);
      spr.setTextColor(sel ? softCol() : dimCol());
      snprintf(buf, sizeof buf, "%lu", (unsigned long)hi);
      spr.drawString(buf, 121, y + 8, 1);
    } else {
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(sel ? th.text : dimCol());
      spr.drawString("Back", 67, y + 4, 2);
    }
  }
  spr.resetViewport();

  uint16_t arrow = th.neon ? th.accent : TFT_LIGHTGREY;
  if (resetScroll > 0) spr.fillTriangle(124, 15, 132, 15, 128, 9, arrow);
  if (resetScroll < maxScroll) spr.fillTriangle(124, 224, 132, 224, 128, 230, arrow);

  drawThemedFooter("next", resetIdx == total - 1 ? "back" : "reset");
}

static void renderConfirm() {
  char buf[32];
  const Theme &th = theme();
  renderResetList();
  if (th.neon) {
    spr.fillRoundRect(6, 74, 123, 92, 6, th.panel);
    drawThemedBox(6, 74, 123, 92, 6, TFT_RED, true);
  } else {
    spr.fillRect(6, 74, 123, 92, TFT_BLACK);
    spr.drawRect(6, 74, 123, 92, TFT_RED);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_RED);
  spr.drawString("DELETE BEST?", 67, 82, 2);

  spr.setTextColor(th.text);
  if (resetIdx == 0) {
    spr.drawString("All games", 67, 104, 2);
    spr.setTextColor(softCol());
    spr.drawString("Clears every score", 67, 124, 1);
  } else {
    const char *hiKey = nullptr;
    const char *name = gameInfo(resetIdx - 1, &hiKey);
    uint32_t hi = hiKey ? prefs.getUInt(hiKey, 0) : 0;
    spr.drawString(name, 67, 104, 2);
    spr.setTextColor(softCol());
    snprintf(buf, sizeof buf, "Best: %lu", (unsigned long)hi);
    spr.drawString(buf, 67, 124, 1);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("L", 14, 144, 2);
  spr.drawString("R", 72, 144, 2);
  spr.setTextColor(th.ok);
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
  const Theme &th = theme();
  uint16_t base = th.neon ? th.accent2 : TFT_LIGHTGREY;    // lime gear on Neon
  uint16_t col = dim ? themeFade(80, base) : base;
  for (int i = 0; i < 8; i++) {                         // gear teeth, turning slowly
    float a = i * PI / 4 + t * 0.0015f;
    spr.fillCircle(x + (int)roundf(cosf(a) * 8), y + (int)roundf(sinf(a) * 8), 2, col);
  }
  spr.fillCircle(x, y, 7, col);
  spr.fillCircle(x, y, 3, th.bg);
}

void menuInfo(char *buf, size_t n) {
  // the menu redraws every frame while scrolling; don't measure that often
  static int v = 0;
  static uint32_t readAt = 0;
  if (!readAt || millis() - readAt > 5000) { v = batteryMilliVolts(); readAt = millis(); }
  if (updates::updateAvailable()) snprintf(buf, n, "Update available");
  else if (onUsbPower(v)) snprintf(buf, n, "USB power");
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
  if (screen == SCR_WIFI || screen == SCR_OTA || screen == SCR_UPDATES) {   // these screens run themselves
    bool stay = screen == SCR_WIFI ? wifiui::update(now) : screen == SCR_OTA ? otaui::update(now) : updui::update(now);
    if (!stay) {
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
      else if (row == ROW_THEME) setThemeIndex((themeIndex() + 1) % NUM_THEMES);   // redrawn below, at once
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
      } else if (row == ROW_OTA) {
        screen = SCR_OTA;
        otaui::open();
        return;
      } else if (row == ROW_UPDATES) {
        screen = SCR_UPDATES;
        updui::open();
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

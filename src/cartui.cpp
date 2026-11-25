// Cartridge screen: opened from the Games list for a game that lives in the
// "Big Games" cartridge (cart.h). Installed: starts it at once (the board
// reboots into the cartridge). Otherwise it shows what the download holds, its
// size, the free space and the battery, and offers:
//   WiFi : streams cartridge.bin from GitHub Pages into the other app slot
//          (updates.cpp job), with progress, cancel and the SHA-256 check;
//          without WiFi it explains and opens Settings > WiFi. WiFi runs on
//          demand: held for a quiet size check when the screen opens, and from
//          choosing WiFi until the download ends or is left.
//   Phone: QR code to the companion page; its Install section sends the file
//          over Bluetooth (ble.cpp, same cartInstall* API), progress shown here.
//
//   Tap L : next row / cancel a download      Tap R : choose
//   Hold L: back to the Games list

#include "common.h"
#include "cart.h"
#include "updates.h"
#include "ble.h"
#include "themeui.h"
#include <qrcode.h>

namespace wifiui { void open(); bool update(uint32_t now); }
// ble.cpp: ends a phone install on both sides (page told, Bluetooth state reset).
// Weak stand-in until ble.cpp has it: the real one replaces it at link time.
void bleInstallCancel();

namespace cartui {

using namespace updates;

static const char PAGE_URL[] = "https://iyotee.github.io/TTGO-Arcade/companion.html#install";
static const uint8_t QR_VERSION = 4;                  // 33 modules, up to 78 bytes at ECC_LOW
static const int QR_SCALE = 3;
static const uint32_t FRAME_MS = 50;
static const uint32_t PHONE_IDLE_MS = 180000;         // the phone side takes a while: no early sleep
static const uint32_t CHECK_WAIT_MS = 45000;          // the size check gives WiFi this long to connect
static const char HOLD_DL[] = "cart", HOLD_CHECK[] = "cartchk";   // WiFi holders

enum State { C_INFO, C_NOWIFI, C_WIFIUI, C_RUN, C_PHONE, C_DONE, C_ERROR };
enum Option { O_PLAY, O_WIFI, O_PHONE, O_BACK };

static State state = C_INFO;
static int game = 0;                                  // CART_GAMES index
static int opts[4], numOpts = 0, sel = 0;
static bool pressedL = false, pressedR = false;
static bool phoneWasActive = false;
static bool checkWanted = false;                      // the size check waits for WiFi
static bool dlWanted = false;                         // "Get via WiFi" waits for a check to end
static uint32_t lastFrame = 0, lastInput = 0, lastRead = 0;
static int mv = 0;
static char err[40] = "";

// ---------------- helpers ----------------
static uint16_t errCol() { return theme().neon ? 0xF8B2 : TFT_RED; }

static void setState(State s) {
  state = s;
  lastFrame = 0;
  pressedL = pressedR = false;
  // off the WiFi path (the WiFi screen holds it itself: it may also go to sleep from there)
  if (s != C_NOWIFI && s != C_RUN) wifiRelease(HOLD_DL);
}

static void dropCheck() {
  checkWanted = false;
  wifiRelease(HOLD_CHECK);
}

static void fail(const char *msg) {
  snprintf(err, sizeof err, "%s", msg);
  setState(C_ERROR);
}

static void buildOptions() {
  numOpts = 0;
  if (cartState() != CART_NONE) opts[numOpts++] = O_PLAY;
  opts[numOpts++] = O_WIFI;
  opts[numOpts++] = O_PHONE;
  opts[numOpts++] = O_BACK;
  sel = 0;
}

static const char *optLabel(int o) {
  bool have = cartState() != CART_NONE;
  switch (o) {
    case O_PLAY:  return "Play";
    case O_WIFI:  return have ? "Update (WiFi)" : "Get via WiFi";
    case O_PHONE: return have ? "Update (phone)" : "Get via phone";
    default:      return "Back";
  }
}

// the download does not fit (only known once the manifest was read)
static bool tooBig() {
  const Manifest &m = manifest();
  return m.valid && m.cart.size && m.cart.size > cartSlotSize();
}

static void centred(const char *s, int y, int font, uint16_t col) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(col);
  spr.drawString(s, 67, y, font);
}

static void infoLine(int y, const char *label, const char *value, uint16_t col) {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(themeSoft());
  spr.drawString(label, 10, y, 1);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(col);
  spr.drawString(value, 125, y, 1);
}

static void startWifi() {
  wifiAcquire(HOLD_DL, true);                         // the user chose WiFi: Bluetooth may give way
  dlWanted = false;
  if (busy() && job() == JOB_CHECK) {                 // a check runs (ours or the hourly one): then go on
    dlWanted = true;
    setState(C_RUN);
    return;
  }
  if (!wifiConnected()) { setState(C_NOWIFI); return; }
  if (tooBig()) { fail("not enough space"); return; }
  char e[32];
  if (start(JOB_CART, e, sizeof e)) setState(C_RUN);
  else fail(e);
}

// ---------------- drawing ----------------
static void renderInfo(uint32_t now) {
  const Theme &th = theme();
  char buf[40], v[24];
  // the games it holds, e.g. "Abyx, Crypt"
  buf[0] = 0;
  for (int i = 0; i < CART_NUM_GAMES; i++) {
    size_t l = strlen(buf);
    snprintf(buf + l, sizeof buf - l, "%s%s", i ? ", " : "", CART_GAMES[i].name);
  }
  centred(buf, 32, 2, th.text);

  const Manifest &m = manifest();
  if (m.valid && m.cart.size) fmtSize(v, sizeof v, m.cart.size);
  else if (busy() || checkWanted) snprintf(v, sizeof v, "checking...");
  else snprintf(v, sizeof v, wifiEnabled() && wifiSavedSsid()[0] ? "unknown" : "? (no WiFi)");
  infoLine(54, "Download", v, tooBig() ? errCol() : th.text);
  fmtSize(v, sizeof v, cartSlotSize());
  infoLine(66, "Free space", v, th.text);
  if (onUsbPower(mv)) snprintf(v, sizeof v, "USB");
  else snprintf(v, sizeof v, "%d%%", batteryPercent(mv));
  infoLine(78, "Battery", v, onUsbPower(mv) || batteryPercent(mv) > 30 ? th.text : TFT_ORANGE);
  uint32_t have = cartInstalledBuild();
  if (have) {
    snprintf(v, sizeof v, "b%u%s", (unsigned)have, cartState() == CART_OUTDATED ? " (old)" : "");
    infoLine(90, "Installed", v, cartState() == CART_OUTDATED ? th.ok : th.text);
  } else {
    infoLine(90, "Installed", "no", themeSoft());
  }

  for (int i = 0; i < numOpts; i++) {
    int y = 106 + i * 26;
    bool s = i == sel;
    bool usable = opts[i] != O_WIFI || !tooBig();
    drawSelBox(6, y, 123, 22, 6, s);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(s && usable ? th.text : th.textDim);
    spr.drawString(optLabel(opts[i]), 14, y + 3, 2);
  }
  drawThemedFooter("next", opts[sel] == O_BACK ? "back" : opts[sel] == O_PLAY ? "play" : "choose");
}

static void renderNoWifi(uint32_t now) {
  const Theme &th = theme();
  bool ready = wifiEnabled() && wifiSavedSsid()[0];   // just connecting: not a sad "No WiFi"
  drawTigo(67, 100, 48, ready ? TIGO_WALK : TIGO_SAD, now, false);
  centred(ready ? "WiFi" : "No WiFi", 116, 4, th.text);
  if (!wifiEnabled()) {
    centred("WiFi is off.", 148, 1, th.textDim);
    centred("R opens the WiFi setup", 160, 1, th.textDim);
  } else if (!wifiSavedSsid()[0]) {
    centred("No network saved yet:", 148, 1, th.textDim);
    centred("pick one: R WiFi setup", 160, 1, th.textDim);
  } else {
    char dots[4] = "...";
    dots[(now / 400) % 4] = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "Connecting%s", dots);
    centred(buf, 148, 2, th.text);
    snprintf(buf, sizeof buf, "%.22s", wifiSavedSsid());
    centred(buf, 168, 1, th.accent2);
    const char *why = wifiFailReason();
    if (why[0]) centred(why, 180, 1, errCol());
  }
  centred("Or: get it via phone", 196, 1, themeSoft());
  drawThemedFooter("back", "WiFi");
}

static void drawQr(int top) {
  static QRCode qr;
  static uint8_t data[160];                           // qrcode_getBufferSize(4) = 137
  static bool made = false;
  if (!made) {
    qrcode_initText(&qr, data, QR_VERSION, ECC_LOW, PAGE_URL);
    made = true;
  }
  int px = qr.size * QR_SCALE, quiet = 2 * QR_SCALE;
  int x0 = (135 - px) / 2;
  spr.fillRect(x0 - quiet, top - quiet, px + 2 * quiet, px + 2 * quiet, TFT_WHITE);   // light border for scanners
  for (int y = 0; y < qr.size; y++)
    for (int x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) spr.fillRect(x0 + x * QR_SCALE, top + y * QR_SCALE, QR_SCALE, QR_SCALE, TFT_BLACK);
}

static void renderPhone(uint32_t now) {
  const Theme &th = theme();
  if (cartInstallActive()) {
    centred("Receiving", 36, 2, th.text);
    uint32_t done, total;
    cartInstallBytes(&done, &total);
    drawProgressBar(118, done, total, now);
    centred("Keep the page open", 186, 1, th.keyHint);
    drawThemedFooter("cancel", nullptr);
    return;
  }
  drawQr(36);                                         // 99 px square + border: rows 30..141
  centred("Scan, tap Connect,", 148, 1, th.text);
  centred("then Install there", 159, 1, th.text);
  centred("iyotee.github.io", 174, 1, themeSoft());
  centred("/TTGO-Arcade/companion", 184, 1, themeSoft());
  const char *st = bleConnected() ? "Phone linked" : bleRunning() ? "Bluetooth on: waiting" : "Bluetooth starting...";
  centred(st, 200, 1, bleConnected() ? th.ok : th.accent);
  drawThemedFooter("back", nullptr);
}

static void renderDone(uint32_t now) {
  const Theme &th = theme();
  char buf[32];
  drawTigo(67, 112, 56, TIGO_HAPPY, now, false);
  centred("Installed!", 128, 4, th.ok);
  snprintf(buf, sizeof buf, "%s is ready", CART_GAMES[game].name);
  centred(buf, 160, 2, th.text);
  drawThemedFooter("back", "play");
}

static void renderError(uint32_t now) {
  const Theme &th = theme();
  drawTigo(67, 112, 50, TIGO_SAD, now, false);
  centred("Not installed", 128, 2, errCol());
  drawWrapped(err, 150, th.text);
  drawThemedFooter("back", "retry");
}

static void render(uint32_t now) {
  spr.fillSprite(theme().bg);
  drawTitle(state == C_PHONE ? "Phone install" : CART_TITLE);
  switch (state) {
    case C_INFO:   renderInfo(now); break;
    case C_NOWIFI: renderNoWifi(now); break;
    case C_RUN:    drawJob("Downloading", now); break;
    case C_PHONE:  renderPhone(now); break;
    case C_DONE:   renderDone(now); break;
    case C_ERROR:  renderError(now); break;
    case C_WIFIUI: break;                               // wifiui draws itself
  }
  spr.pushSprite(0, 0);
  lastFrame = now ? now : 1;
}

// ---------------- entry points ----------------
static void play() {
  if (!cartLaunch(CART_GAMES[game].hiKey)) {          // returns only if it could not start
    buildOptions();
    fail("cartridge damaged: get it again");
  }
}

void open(const char *hiKey) {
  game = cartGameIndex(hiKey);
  if (game < 0) game = 0;
  err[0] = 0;
  mv = batteryMilliVolts();
  lastInput = lastRead = millis();
  buildOptions();
  setState(C_INFO);
  if (cartState() == CART_READY) {                    // installed and current: straight in
    play();
    if (state != C_ERROR) return;
  }
  // learn the size before the user decides: WiFi comes up for a quiet check
  // (never pauses Bluetooth or shrinks the screen; nothing happens if it can't run)
  checkWanted = !manifest().valid && wifiEnabled() && wifiSavedSsid()[0];
  dlWanted = false;
  if (checkWanted) wifiAcquireFor(HOLD_CHECK, CHECK_WAIT_MS);
  render(lastInput);
}

static void dropHolds() {
  dropCheck();
  dlWanted = false;
  wifiRelease(HOLD_DL);
}

static bool leave() {
  if (state == C_PHONE) {
    if (cartInstallActive()) bleInstallCancel();      // no half-written cartridge left claiming the slot
    bleRelease();
  }
  if (busy() && (job() == JOB_CART)) cancel();
  dropHolds();
  pressedL = pressedR = false;
  return false;
}

// the quiet size check: once WiFi is up (gives up when the hold runs out)
static void serviceCheck() {
  if (!checkWanted) return;
  if (busy() || !wifiHeldBy(HOLD_CHECK)) { dropCheck(); return; }   // another job runs, or no WiFi came
  if (!wifiConnected()) return;
  char e[24];
  start(JOB_CHECK, e, sizeof e, false);               // the job holds WiFi itself from here
  dropCheck();
}

bool update(uint32_t now) {
  serviceCheck();
  if (state == C_WIFIUI) {                            // Settings > WiFi, nested
    if (!wifiui::update(now)) {
      lastInput = now;
      startWifi();                                    // holds WiFi again: downloads, or waits for it
    }
    return true;
  }

  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  bool hold = pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS;
  if (hold && state != C_RUN) return leave();
  bool tapL = eL == 2 && pressedL, tapR = eR == 2 && pressedR;
  if (eL == 2) pressedL = false;
  if (eR == 2) pressedR = false;

  switch (state) {
    case C_INFO:
      if (tapL) sel = (sel + 1) % numOpts;
      if (tapR) {
        int o = opts[sel];
        if (o == O_BACK) return leave();
        if (o == O_PLAY) play();
        else if (o == O_WIFI) startWifi();
        else {
          dropCheck();                                // Bluetooth needs the memory more
          phoneWasActive = false;
          bleUse(now);
          setState(C_PHONE);
        }
      }
      if ((int32_t)(now - lastRead) >= 5000) { lastRead = now; mv = batteryMilliVolts(); }
      break;
    case C_NOWIFI:
      if (tapL) { buildOptions(); setState(C_INFO); }
      else if (tapR) { setState(C_WIFIUI); wifiui::open(); return true; }
      else if (wifiConnected()) startWifi();          // joined meanwhile: go on by itself
      break;
    case C_RUN:
      if (dlWanted) {                                 // waiting for a check to end first
        if (tapL || hold) { dlWanted = false; buildOptions(); setState(C_INFO); }
        else if (!busy()) startWifi();
        break;
      }
      if ((tapL || hold) && (phase() == PH_CONNECT || phase() == PH_DOWNLOAD)) cancel();
      if (!busy() && phase() == PH_DONE) setState(C_DONE);
      else if (!busy() && phase() == PH_FAILED) {
        if (cancelled()) { buildOptions(); setState(C_INFO); }
        else fail(error());
      }
      break;
    case C_PHONE: {
      bleUse(now);                                    // keep Bluetooth up while this is open
      bool active = cartInstallActive();
      if (active) lastInput = now;
      if (phoneWasActive && !active) {
        // the transfer ended: Begin dropped the old record, so a record now means it went in
        if (cartInstalledBuild()) setState(C_DONE);
        else fail("transfer failed");
      }
      phoneWasActive = active;
      if (tapL) {
        if (active) bleInstallCancel();               // tells the page and resets the Bluetooth side too
        else return leave();
      }
      break;
    }
    case C_DONE:
      if (tapL) return leave();
      if (tapR) play();
      break;
    case C_ERROR:
      if (tapL) return leave();
      if (tapR) { buildOptions(); setState(C_INFO); }
      break;
    case C_WIFIUI:
      break;
  }

  // idle: never while something is being written
  bool writing = state == C_RUN || cartInstallActive();
  uint32_t idleLim = state == C_PHONE ? PHONE_IDLE_MS : idleSleepMs();
  if (!writing && idleLim && (int32_t)(now - lastInput) > (int32_t)idleLim) {
    if (state == C_PHONE) bleRelease();
    dropHolds();
    goToSleep();                                      // back via the launcher
    return true;
  }
  if (lastFrame == 0 || (int32_t)(now - lastFrame) >= (int32_t)FRAME_MS) render(now);
  return true;
}

}  // namespace cartui

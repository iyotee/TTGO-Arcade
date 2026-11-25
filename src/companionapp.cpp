// Companion: a QR code to the companion web page (docs/companion.html on GitHub
// Pages) and the Bluetooth link state. Bluetooth stays on while the app is open
// (keep-alive, the saved setting is not changed); the page can also install the
// Big Games cartridge over it, with the progress shown here.
//
// Controls:
//   Tap R  : QR code / the page address as text
//   Hold L : back to menu (not while a game install is running: cancel it on the phone)

#include "common.h"
#include "themeui.h"
#include "ble.h"
#include <qrcode.h>

namespace companionapp {

static const char PAGE_URL[] = "https://iyotee.github.io/TTGO-Arcade/companion.html";
// version 3 = 29 x 29 modules, 53 bytes at ECC_LOW: the smallest that holds the
// address, so the modules can be 4 px (132 px with the quiet zone, on a 135 px screen)
static const uint8_t QR_VERSION = 3;
static const int QR_SCALE = 4, QR_QUIET = 2;          // px per module, white modules around it
static const int QR_Y = 22, QR_SIDE = (QR_VERSION * 4 + 17 + 2 * QR_QUIET) * QR_SCALE;
// the library doesn't check the capacity itself (it would overrun its buffers)
static_assert(sizeof PAGE_URL - 1 <= 53, "the page address no longer fits a version 3 QR code");
static_assert(QR_SIDE <= 135, "the QR code is wider than the screen");
static const int LINK_Y = 158, STEPS_Y = 176, HINT_Y = 208;
static const uint32_t FRAME_MS = 50;
static const uint32_t LINKED_IDLE_MS = 300000UL;      // a linked page may sit untouched a while

static QRCode qr;
static uint8_t qrBuf[128];                            // qrcode_getBufferSize(3) = 106
static bool qrOk = false, qrTried = false;
static bool showText = false, pressedL = false, pressedR = false;
static uint32_t lastInput = 0, lastDraw = 0, blockedAt = 0;
static uint32_t rateAt = 0, rateFrom = 0;             // install speed, for the time left

// ---------------- QR code ----------------
static void makeQr() {
  if (qrTried) return;                                // the text never changes: encode once
  qrTried = true;
  qrOk = qrcode_getBufferSize(QR_VERSION) <= sizeof qrBuf &&
         qrcode_initText(&qr, qrBuf, QR_VERSION, ECC_LOW, PAGE_URL) == 0;
  dbg("companion: qr %s\n", qrOk ? "ok" : "failed");
}

// crisp square modules on a white quiet zone (phones need the light border)
static void drawQr(int x, int y) {
  int side = (qr.size + 2 * QR_QUIET) * QR_SCALE;
  spr.fillRoundRect(x, y, side, side, 3, TFT_WHITE);
  for (int my = 0; my < qr.size; my++)
    for (int mx = 0; mx < qr.size; mx++)
      if (qrcode_getModule(&qr, mx, my))
        spr.fillRect(x + (QR_QUIET + mx) * QR_SCALE, y + (QR_QUIET + my) * QR_SCALE, QR_SCALE, QR_SCALE, TFT_BLACK);
}

// ---------------- drawing ----------------
static void centred(const char *s, int y, int font, uint16_t col) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(col);
  spr.drawString(s, 67, y, font);
}

static uint16_t errCol() { return theme().neon ? 0xF8B2 : TFT_RED; }

// the address as text, for when the camera won't read the screen
static void drawAddress(uint32_t now) {
  const Theme &th = theme();
  drawThemedBox(6, QR_Y + 2, 123, QR_SIDE - 4, 6, th.frame, true);
  centred("Open in Chrome:", QR_Y + 12, 1, th.textDim);
  centred("iyotee.github.io", QR_Y + 30, 2, th.accent);
  centred("/TTGO-Arcade/", QR_Y + 48, 2, th.accent);
  centred("companion.html", QR_Y + 66, 2, th.accent);
  centred("then Connect to", QR_Y + 92, 1, th.textDim);
  centred("TTGO-Arcade", QR_Y + 104, 2, th.text);
}

static void drawLink(uint32_t now) {
  const Theme &th = theme();
  const char *err = bleError();
  const char *state;
  uint16_t col, dot;
  bool blink = (now / 500) % 2;
  if (!bleRunning()) {
    state = err[0] ? err : "Starting...";
    col = dot = err[0] ? errCol() : th.textDim;
  } else if (bleCompanionLinked()) {
    state = "Connected";
    col = dot = th.ok;
  } else {
    state = "Bluetooth on";
    col = th.text;
    dot = blink ? th.accent2 : th.panel;
  }
  spr.fillSmoothCircle(16, LINK_Y + 7, 4, dot, th.bg);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(col);
  spr.drawString(state, 26, LINK_Y, 2);
}

static void drawSteps() {
  const Theme &th = theme();
  // font 1 is 6 px a character: at most 22 per line
  if (bleCompanionLinked()) {
    centred("Linked to the page:", STEPS_Y, 1, themeSoft());
    centred("send messages, treats,", STEPS_Y + 10, 1, th.textDim);
    centred("WiFi, clock and games", STEPS_Y + 20, 1, th.textDim);
  } else {
    centred("Scan with your phone,", STEPS_Y, 1, themeSoft());
    centred("open it in Chrome,", STEPS_Y + 10, 1, themeSoft());
    centred("tap Connect.", STEPS_Y + 20, 1, themeSoft());
  }
}

// replaces the steps while the page installs games (and a little after)
static void drawInstall(uint32_t now) {
  const Theme &th = theme();
  int st = bleInstallState();
  uint32_t done = 0, total = 0;
  bleInstallBytes(&done, &total);
  int pct = bleInstallPercent();
  char buf[28];
  if (st == BLE_INST_DONE) centred("Games installed!", STEPS_Y, 1, th.ok);
  else if (st == BLE_INST_FAILED) centred("Install failed", STEPS_Y, 1, errCol());
  else centred("Installing games", STEPS_Y, 1, th.text);

  // average speed since the data started flowing, for a rough time left
  if (st != BLE_INST_RECEIVING || done == 0) rateAt = 0;
  else if (!rateAt) { rateAt = now ? now : 1; rateFrom = done; }
  int left = -1;
  int32_t dt = rateAt ? (int32_t)(now - rateAt) : 0;
  if (dt > 3000 && done > rateFrom) left = (int)((uint64_t)(total - done) * dt / (done - rateFrom) / 1000);

  int x = 10, w = 115, y = STEPS_Y + 11;
  spr.drawRoundRect(x, y, w, 8, 3, th.frame);
  int fill = (w - 4) * pct / 100;
  uint16_t barCol = st == BLE_INST_FAILED ? errCol() : st == BLE_INST_DONE ? th.ok : th.accent;
  if (fill > 0) spr.fillRect(x + 2, y + 2, fill, 4, barCol);
  if (st == BLE_INST_PREPARING || st == BLE_INST_CHECKING || st == BLE_INST_FAILED || st == BLE_INST_DONE)
    snprintf(buf, sizeof buf, "%.22s", bleInstallMessage());
  else if (left >= 0)
    snprintf(buf, sizeof buf, "%d%% %lu/%luK ~%ds", pct, (unsigned long)(done / 1024), (unsigned long)(total / 1024),
             left > 999 ? 999 : left);
  else
    snprintf(buf, sizeof buf, "%d%%  %lu/%lu KB", pct, (unsigned long)(done / 1024), (unsigned long)(total / 1024));
  centred(buf, STEPS_Y + 22, 1, st == BLE_INST_FAILED ? errCol() : themeSoft());
}

static bool installShown() {
  int st = bleInstallState();
  return bleInstallActive() || st == BLE_INST_DONE || st == BLE_INST_FAILED;
}

static void render(uint32_t now) {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  centred("Companion", 3, 2, th.neon ? th.frameSel : th.accent);
  if (th.neon) spr.drawFastHLine(0, 20, 135, themeFade(90, th.frameSel));

  if (showText || !qrOk) drawAddress(now);
  else drawQr((135 - QR_SIDE + 1) / 2, QR_Y);

  drawLink(now);
  if (installShown()) drawInstall(now);
  else drawSteps();

  // only R has a job: "hold L: menu" above it (or why Hold L is refused right now)
  if (blockedAt && (int32_t)(now - blockedAt) < 1500) centred("installing: wait", HINT_Y, 1, errCol());
  else centred("hold L: menu", HINT_Y, 1, th.textDim);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("R", 70, 220, 2);
  spr.setTextColor(th.text);
  spr.drawString(showText ? "QR code" : "address", 82, 220, 2);
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim, uint32_t t) {
  const Theme &th = theme();
  auto c = [dim](uint16_t col) { return dim ? themeFade(80, col) : col; };
  // a phone showing a tiny QR code, radio waves going to the board
  uint16_t body = th.neon ? th.frameSel : 0xFB49, ink = th.neon ? th.panel : 0x38E7;
  spr.fillRoundRect(cx - 13, cy - 13, 15, 26, 3, c(body));
  spr.fillRect(cx - 11, cy - 8, 11, 14, c(TFT_WHITE));
  static const uint8_t PAT[5] = {0x17, 0x15, 0x0B, 0x1D, 0x13};   // 5 x 5 pseudo-QR
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 5; x++)
      if ((PAT[y] >> x) & 1) spr.fillRect(cx - 11 + x * 2, cy - 6 + y * 2, 2, 2, c(ink));
  int lit = t ? (int)((t / 300) % 4) : 3;
  for (int i = 0; i < 3; i++) {
    uint16_t col = i < lit ? c(th.neon ? th.accent : 0x2D95) : c(th.textDim);
    int r = 3 + i * 3;
    spr.drawSmoothArc(cx + 2, cy, r + 1, r, 225, 315, col, th.bg, true);
  }
}

void menuInfo(char *buf, size_t n) {
  if (bleInstallActive()) snprintf(buf, n, "Installing %d%%", bleInstallPercent());
  else snprintf(buf, n, "%s", bleCompanionLinked() ? "Linked" : "Phone page");
}

void begin() {
  pressedL = pressedR = false;
  showText = false;
  blockedAt = 0;
  lastInput = millis();
  makeQr();
  if (!bleRunning()) {                                // starting the stack takes a moment: say so
    const Theme &th = theme();
    spr.fillSprite(th.bg);
    centred("Starting", 104, 4, th.accent);
    centred("Bluetooth...", 130, 4, th.accent);
    spr.pushSprite(0, 0);
  }
  bleUse(millis());
  lastDraw = 0;
}

void update(uint32_t now) {
  bleUse(now);                                        // keep-alive while the app is open

  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    if (bleInstallActive()) {
      blockedAt = now ? now : 1;                      // leaving would let the board sleep mid-install
    } else {
      bleRelease();                                   // stops BLE again only if the setting is off
      returnToMenu();
      return;
    }
  }
  if (eL == 2) pressedL = false;
  if (eR == 2 && pressedR) {
    pressedR = false;
    showText = !showText;
  }

  // never sleep during an install; a linked page gets a longer idle time
  if (bleInstallActive()) {
    lastInput = now;
  } else if (bleCompanionLinked()) {
    uint32_t lim = idleSleepMs();
    if (lim > 0 && (int32_t)(now - lastInput) > (int32_t)(lim > LINKED_IDLE_MS ? lim : LINKED_IDLE_MS)) goToSleep();
  } else if (idleTooLong(now, lastInput)) {
    goToSleep();
  }

  if ((int32_t)(now - lastDraw) >= (int32_t)FRAME_MS) {
    lastDraw = now;
    render(now);
  }
}

}  // namespace companionapp

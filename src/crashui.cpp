// Settings > Crash report: the last crash as a QR code (scan it with a phone
// and send the text to whoever decodes it) or as plain text.
//
// Controls:
//   Tap R  : QR code / text
//   Tap L  : forget the report
//   Hold L : back to Settings

#include "crashrec.h"
#include "themeui.h"
#include <qrcode.h>

namespace crashui {

// version 10 at ECC_LOW holds 271 bytes; 57 modules at 2 px = 114 px (+ quiet zone)
static const uint8_t QR_VERSION = 10;
static const int QR_SCALE = 2, QR_QUIET = 3;

static QRCode qr;
static uint8_t qrBuf[420];                           // qrcode_getBufferSize(10) = 407
static bool qrOk = false, showText = false, have = false;
static bool pressedL = false, pressedR = false, dirty = true;
static char text[240];

static void build() {
  CrashInfo c;
  have = crashInfo(&c);
  crashText(text, sizeof text);
  qrOk = have && qrcode_getBufferSize(QR_VERSION) <= sizeof qrBuf &&
         qrcode_initText(&qr, qrBuf, QR_VERSION, ECC_LOW, text) == 0;
}

static void render() {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.neon ? th.frameSel : TFT_ORANGE);
  spr.drawString("Crash report", 67, 4, 2);

  if (!have) {
    drawTigo(67, 120, 50, TIGO_HAPPY, millis(), false);
    spr.setTextColor(th.text);
    spr.drawString("No crash recorded", 67, 136, 2);
    drawThemedFooter("-", "-");
    spr.pushSprite(0, 0);
    return;
  }
  CrashInfo c;
  crashInfo(&c);
  char buf[40];
  snprintf(buf, sizeof buf, "%s, build %u", c.fw ? "cartridge" : "launcher", (unsigned)c.build);
  spr.setTextColor(themeSoft());
  spr.drawString(buf, 67, 22, 1);
  snprintf(buf, sizeof buf, "in %s, cause %u", c.where, (unsigned)c.cause);
  spr.drawString(buf, 67, 32, 1);

  if (!showText && qrOk) {
    int side = (qr.size + 2 * QR_QUIET) * QR_SCALE, x = (135 - side) / 2, y = 44;
    spr.fillRect(x, y, side, side, TFT_WHITE);
    for (int my = 0; my < qr.size; my++)
      for (int mx = 0; mx < qr.size; mx++)
        if (qrcode_getModule(&qr, mx, my))
          spr.fillRect(x + (QR_QUIET + mx) * QR_SCALE, y + (QR_QUIET + my) * QR_SCALE, QR_SCALE, QR_SCALE, TFT_BLACK);
    spr.setTextColor(th.textDim);
    spr.drawString("scan it, send the text", 67, y + side + 4, 1);
  } else {
    // PC first, then the callers: two addresses per line
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(th.text);
    for (uint32_t i = 0; i < c.n; i += 2) {
      if (i + 1 < c.n) snprintf(buf, sizeof buf, "%08x %08x", (unsigned)c.bt[i], (unsigned)c.bt[i + 1]);
      else snprintf(buf, sizeof buf, "%08x", (unsigned)c.bt[i]);
      spr.drawString(buf, 14, 46 + (i / 2) * 12, 1);
    }
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.textDim);
  spr.drawString("hold L: back", 67, 206, 1);
  drawThemedFooter("forget", showText ? "QR" : "text");
  spr.pushSprite(0, 0);
}

void open() {
  pressedL = pressedR = false;
  showText = false;
  build();
  dirty = true;
}

bool update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    return false;
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    if (have) { crashClear(); build(); dirty = true; }
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    showText = !showText;
    dirty = true;
  }
  if (dirty) {
    render();
    dirty = false;
  }
  return true;
}

}  // namespace crashui

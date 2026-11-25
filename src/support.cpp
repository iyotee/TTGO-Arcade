// Support: Tigo asks for a coffee. A QR code to the Buy Me a Coffee page
// (https://buymeacoffee.com/zcnxsnw3vw): donations keep the project going.
//
// Controls:
//   Tap L  : Tigo cheers
//   Tap R  : big QR code (easier to scan) / back to Tigo
//   Hold L : back to menu

#include "common.h"
#include "themeui.h"
#include "tigo.h"
#include <qrcode.h>

namespace support {

static const char COFFEE_URL[] = "https://buymeacoffee.com/zcnxsnw3vw";
// version 3 = 29 x 29 modules; ECC_MEDIUM holds 42 bytes and survives a smudge or glare
static const uint8_t QR_VERSION = 3;
// the library doesn't check the capacity itself (it would overrun its buffers)
static_assert(sizeof COFFEE_URL - 1 <= 42, "the address no longer fits a version 3-M QR code");
static const uint32_t FRAME_MS = 40, CHEER_MS = 2200;
static const int TIGO_X = 78, TIGO_Y = 92, TIGO_SIZE = 42;
static const uint16_t C_COFFEE = 0x6A02, C_FOAM = 0xEED6;

static QRCode qr;
static uint8_t qrBuf[128];                            // qrcode_getBufferSize(3) = 106
static bool qrOk = false, qrTried = false;
static bool big = false, pressedL = false, pressedR = false;
static uint32_t lastInput = 0, lastDraw = 0, bubbleAt = 0, cheerAt = 0;
static bool cheering = false;

// ---------------- QR code ----------------
static void makeQr() {
  if (qrTried) return;                                // the text never changes: encode once
  qrTried = true;
  qrOk = qrcode_getBufferSize(QR_VERSION) <= sizeof qrBuf &&
         qrcode_initText(&qr, qrBuf, QR_VERSION, ECC_MEDIUM, COFFEE_URL) == 0;
  dbg("support: qr %s\n", qrOk ? "ok" : "failed");
}

// crisp square modules on a white quiet zone, centred on x = 67
static void drawQr(int y, int scale, int quiet) {
  int side = (qr.size + 2 * quiet) * scale, x = (135 - side) / 2;
  spr.fillRoundRect(x, y, side, side, 4, TFT_WHITE);
  for (int my = 0; my < qr.size; my++)
    for (int mx = 0; mx < qr.size; mx++)
      if (qrcode_getModule(&qr, mx, my))
        spr.fillRect(x + (quiet + mx) * scale, y + (quiet + my) * scale, scale, scale, TFT_BLACK);
}

// ---------------- drawing ----------------
static void centred(const char *s, int y, int font, uint16_t col) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(col);
  spr.drawString(s, 67, y, font);
}

// a steaming paper cup; (x, y) = centre of its bottom
static void drawCup(int x, int y, uint32_t t) {
  const Theme &th = theme();
  uint16_t cup = th.neon ? th.accent : TFT_WHITE, band = th.neon ? th.frameSel : tigo::COL_CORAL;
  spr.fillTriangle(x - 7, y - 13, x + 6, y - 13, x - 5, y, cup);          // tapered body
  spr.fillTriangle(x + 6, y - 13, x + 4, y, x - 5, y, cup);
  spr.fillRect(x - 6, y - 9, 11, 4, band);                                // sleeve
  spr.fillRoundRect(x - 8, y - 16, 15, 4, 2, th.neon ? th.panel : C_COFFEE);   // lid
  spr.drawRoundRect(x - 8, y - 16, 15, 4, 2, cup);
  // two wisps of steam drifting up
  for (int i = 0; i < 2; i++) {
    for (int k = 0; k < 9; k++) {
      float ph = t * 0.006f + k * 0.8f + i * 2.1f;
      int px = x - 3 + i * 5 + (int)(sinf(ph) * 1.6f);
      uint8_t a = 200 - k * 20;
      spr.drawPixel(px, y - 19 - k, themeFade(a, th.neon ? th.accent2 : C_FOAM));
    }
  }
}

static void drawHeart(int x, int y, int r, uint16_t col) {
  spr.fillCircle(x - r, y, r, col);
  spr.fillCircle(x + r, y, r, col);
  spr.fillTriangle(x - 2 * r, y + 1, x + 2 * r, y + 1, x, y + 3 * r, col);
}

static void renderTigo(uint32_t now) {
  const Theme &th = theme();
  uint32_t ct = now - cheerAt;
  TigoPose pose = cheering ? TIGO_HAPPY : ((now / 1000) % 6 == 0 ? TIGO_HAPPY : TIGO_IDLE);
  int hop = cheering ? (int)(4 * triWave(ct, 400)) : 0;
  drawTigo(TIGO_X, TIGO_Y - hop, TIGO_SIZE, pose, now, true);
  // the cup sits in his left claw (he faces left)
  int cupX = TIGO_X - (int)tigo::clawReach(TIGO_SIZE) + 2;
  drawCup(cupX, TIGO_Y - 4 - hop, now);
  if (cheering) {                                     // hearts float up and fade
    for (int i = 0; i < 3; i++) {
      int32_t a = (int32_t)ct - i * 350;
      if (a < 0 || a > 1400) continue;
      int y = TIGO_Y - TIGO_SIZE - a / 30, x = TIGO_X + 14 + i * 9 + (int)(sinf(a * 0.01f) * 3);
      drawHeart(x, y, 2, themeFade(255 - a / 6, th.neon ? th.frameSel : TFT_RED));
    }
  }
  tigo::drawBubble(cheering ? "Thank you!!" : "Buy Tigo a coffee?", TIGO_X - 10, TIGO_Y - TIGO_SIZE, 2,
                   now - bubbleAt);
}

static void render(uint32_t now) {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  if (big) {
    centred("Thank you!", 6, 2, th.neon ? th.frameSel : th.accent);
    if (qrOk) drawQr(28, 4, 2);
    centred("buymeacoffee.com/", 168, 1, themeSoft());
    centred("zcnxsnw3vw", 180, 1, themeSoft());
    drawToolFooter("cheer", "Tigo");
  } else {
    renderTigo(now);
    if (qrOk) drawQr(98, 3, 3);
    else centred("buymeacoffee.com", 140, 2, th.accent);
    drawToolFooter("cheer", "big");
  }
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim, uint32_t t) {
  const Theme &th = theme();
  auto c = [dim](uint16_t col) { return dim ? themeFade(80, col) : col; };
  // a coffee cup with a heart and a wisp of steam
  uint16_t cup = th.neon ? th.accent : TFT_WHITE, heart = th.neon ? th.frameSel : TFT_RED;
  spr.fillRoundRect(cx - 10, cy - 4, 16, 15, 3, c(cup));
  spr.drawCircle(cx + 8, cy + 2, 4, c(cup));
  spr.drawCircle(cx + 8, cy + 2, 3, c(cup));
  spr.fillRect(cx - 9, cy - 3, 14, 3, c(th.neon ? th.panel : C_COFFEE));
  drawHeart(cx - 2, cy + 3, 2, c(heart));
  for (int i = 0; i < 2; i++)
    for (int k = 0; k < 8; k++) {
      int px = cx - 5 + i * 6 + (int)(sinf(t * 0.006f + k * 0.8f + i * 2) * (t ? 1.5f : 1.0f));
      spr.drawPixel(px, cy - 7 - k, c(themeFade(220 - k * 22, th.neon ? th.accent2 : C_FOAM)));
    }
}

void menuInfo(char *buf, size_t n) { snprintf(buf, n, "Buy Tigo a coffee"); }

void begin() {
  pressedL = pressedR = false;
  big = cheering = false;
  lastInput = bubbleAt = millis();
  makeQr();
  lastDraw = 0;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    cheering = true;
    cheerAt = bubbleAt = now;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    big = !big;
  }
  if (cheering && (int32_t)(now - cheerAt) >= (int32_t)CHEER_MS) {
    cheering = false;
    bubbleAt = now;
  }

  if (idleTooLong(now, lastInput)) goToSleep();
  if ((int32_t)(now - lastDraw) >= (int32_t)FRAME_MS) {
    lastDraw = now;
    render(now);
  }
}

}  // namespace support

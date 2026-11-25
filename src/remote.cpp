// Remote: the board as a Bluetooth remote for a phone or computer (HID keyboard,
// no app needed). Pair once: on the phone, Bluetooth > TTGO-Arcade.
//
// Controls:
//   Hold L+R : switch mode (Camera / Music / Slides)
//   Camera   : Tap L or Tap R = shutter (volume up)
//   Music    : Tap L previous · Tap R next · Hold R play/pause
//   Slides   : Tap L previous (left arrow) · Tap R next (right arrow)
//   Hold L   : back to menu

#include "common.h"
#include "ble.h"

namespace remote {

enum Mode { CAMERA, MUSIC, SLIDES, NUM_MODES };
enum Sent { S_NONE, S_LEFT, S_RIGHT, S_HOLD, S_FAIL };
static const char *MODE_NAME[NUM_MODES] = {"Camera", "Music", "Slides"};
static const uint32_t HOLD_R_MS = 500, FLASH_MS = 380, FRAME_MS = 40;
static const uint32_t CONNECTED_IDLE_MS = 300000UL;   // a paired remote waits longer before sleeping

// Tigo's palette (see the README style guide)
static const uint16_t C_CORAL = 0xFB49, C_CLAW = 0xE245, C_PEACH = 0xFD91, C_SAND = 0xF6B4;
static const uint16_t C_TEAL = 0x2D95, C_PLUM = 0x38E7, C_CARD = 0x18A3;

static int mode = CAMERA;
static Sent sent = S_NONE;
static uint32_t sentAt = 0, lastInput = 0, lastDraw = 0, openedAt = 0;
static int slide = 1;                                  // slide counter, just for show
static bool pressedL = false, pressedR = false, combo = false;

// ---------------- icons ----------------
static float flashLevel(uint32_t now) {               // 1 right after a send, fading to 0
  if (sent == S_NONE) return 0;
  int32_t dt = (int32_t)(now - sentAt);
  if (dt < 0 || dt >= (int32_t)FLASH_MS) return 0;
  return 1.0f - (float)dt / FLASH_MS;
}

static void cameraIcon(int cx, int cy, uint32_t now) {
  float f = sent == S_FAIL ? 0 : flashLevel(now);
  spr.fillSmoothRoundRect(cx - 12, cy - 27, 24, 12, 4, C_CLAW, TFT_BLACK);       // viewfinder bump
  spr.fillSmoothRoundRect(cx - 32, cy - 19, 64, 44, 9, C_CORAL, TFT_BLACK);
  spr.fillRect(cx - 32, cy - 6, 64, 3, C_CLAW);                                    // grip band
  spr.fillSmoothRoundRect(cx + 17, cy - 15, 10, 6, 2, C_SAND, C_CORAL);           // flash
  spr.fillSmoothCircle(cx, cy + 3, 17, C_PLUM, C_CORAL);
  spr.fillSmoothCircle(cx, cy + 3, 13, C_TEAL, C_PLUM);
  // the aperture narrows on the shot
  int ap = 6 - (int)(4 * f);
  spr.fillSmoothCircle(cx, cy + 3, ap, C_PLUM, C_TEAL);
  spr.fillSmoothCircle(cx - 5, cy - 2, 3, TFT_WHITE, C_TEAL);
  if (f > 0) {                                                                    // flash burst
    uint16_t w = tft.alphaBlend((uint8_t)(255 * f), TFT_WHITE, TFT_BLACK);
    for (int k = 0; k < 8; k++) {
      float a = k * PI / 4;
      int r0 = 33, r1 = 33 + (int)(7 * f);
      spr.drawWideLine(cx + cosf(a) * r0, cy + 3 + sinf(a) * r0, cx + cosf(a) * r1, cy + 3 + sinf(a) * r1, 3, w, TFT_BLACK);
    }
  }
}

static void noteIcon(int cx, int cy, uint32_t now) {
  float bob = sinf(now * 0.006f) * 3;
  int y1 = cy + 14 + (int)bob, y2 = cy + 8 - (int)bob;
  spr.drawWedgeLine(cx - 8, y1 - 34, cx + 20, y2 - 34, 5, 5, C_CLAW, TFT_BLACK);   // beam
  spr.drawWideLine(cx - 8, y1, cx - 8, y1 - 34, 4, C_CORAL, TFT_BLACK);            // stems
  spr.drawWideLine(cx + 20, y2, cx + 20, y2 - 34, 4, C_CORAL, TFT_BLACK);
  spr.fillEllipse(cx - 15, y1 + 1, 9, 7, C_TEAL);                                  // heads
  spr.fillEllipse(cx + 13, y2 + 1, 9, 7, C_TEAL);
  spr.fillSmoothCircle(cx - 18, y1 - 2, 2, TFT_WHITE, C_TEAL);
  spr.fillSmoothCircle(cx + 10, y2 - 2, 2, TFT_WHITE, C_TEAL);

  float f = flashLevel(now);
  if (f <= 0 || sent == S_FAIL) return;
  uint16_t col = tft.alphaBlend((uint8_t)(255 * f), C_SAND, TFT_BLACK);
  int sx = (int)(10 * (1 - f));
  if (sent == S_LEFT) {                                                           // |<<
    spr.fillRect(cx - 46 - sx, cy - 8, 3, 16, col);
    spr.fillTriangle(cx - 43 - sx, cy, cx - 35 - sx, cy - 8, cx - 35 - sx, cy + 8, col);
    spr.fillTriangle(cx - 35 - sx, cy, cx - 27 - sx, cy - 8, cx - 27 - sx, cy + 8, col);
  } else if (sent == S_RIGHT) {                                                   // >>|
    spr.fillTriangle(cx + 35 + sx, cy, cx + 27 + sx, cy - 8, cx + 27 + sx, cy + 8, col);
    spr.fillTriangle(cx + 43 + sx, cy, cx + 35 + sx, cy - 8, cx + 35 + sx, cy + 8, col);
    spr.fillRect(cx + 43 + sx, cy - 8, 3, 16, col);
  } else if (sent == S_HOLD) {                                                    // play | pause on both sides
    spr.fillTriangle(cx - 46, cy - 8, cx - 46, cy + 8, cx - 34, cy, col);
    spr.fillRect(cx + 34, cy - 8, 4, 16, col);
    spr.fillRect(cx + 41, cy - 8, 4, 16, col);
  }
}

static void slidesIcon(int cx, int cy, uint32_t now) {
  spr.drawWideLine(cx, cy + 18, cx, cy + 30, 3, C_PLUM, TFT_BLACK);              // stand
  spr.drawWideLine(cx, cy + 26, cx - 12, cy + 34, 3, C_PLUM, TFT_BLACK);
  spr.drawWideLine(cx, cy + 26, cx + 12, cy + 34, 3, C_PLUM, TFT_BLACK);
  spr.fillSmoothRoundRect(cx - 36, cy - 28, 72, 48, 5, C_SAND, TFT_BLACK);       // screen
  spr.drawRoundRect(cx - 36, cy - 28, 72, 48, 5, C_CLAW);

  // the slide content slides in from the side it came from
  float f = sent == S_FAIL ? 0 : flashLevel(now);
  int dx = sent == S_RIGHT ? (int)(30 * f) : sent == S_LEFT ? -(int)(30 * f) : 0;
  spr.setViewport(cx - 33, cy - 25, 66, 42, false);   // clip, keep screen coordinates
  int x = cx - 28 + dx;
  spr.fillRect(x, cy - 21, 40, 6, slide % 2 ? C_CORAL : C_TEAL);                  // title
  for (int i = 0; i < 3; i++) {
    spr.fillSmoothCircle(x + 2, cy - 7 + i * 8, 2, C_PLUM, C_SAND);
    spr.fillRect(x + 7, cy - 8 + i * 8, 22 + ((slide + i) * 7) % 18, 3, C_PLUM);
  }
  char buf[6];
  snprintf(buf, sizeof buf, "%d", slide);
  spr.setTextDatum(BR_DATUM);
  spr.setTextColor(C_CLAW);
  spr.drawString(buf, cx + 32 + dx, cy + 17, 1);
  spr.resetViewport();
}

static void modeDots(int cy) {
  for (int i = 0; i < NUM_MODES; i++) {
    int x = 67 + (i - 1) * 14;
    if (i == mode) spr.fillSmoothCircle(x, cy, 4, C_CORAL, TFT_BLACK);
    else spr.drawCircle(x, cy, 3, TFT_DARKGREY);
  }
}

// ---------------- rendering ----------------
static void render(uint32_t now) {
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_CORAL);
  spr.drawString("REMOTE", 67, 2, 4);

  // status card
  bool ready = bleHidReady();
  bool fail = sent == S_FAIL && flashLevel(now) > 0;
  spr.fillSmoothRoundRect(4, 30, 127, 52, 7, fail ? 0x5000 : C_CARD, TFT_BLACK);
  int dotX = 14;
  uint16_t dotCol = !bleRunning() ? TFT_DARKGREY : ready ? TFT_GREEN : (now / 500) % 2 ? C_TEAL : C_CARD;
  spr.fillSmoothCircle(dotX, 42, 4, dotCol, fail ? 0x5000 : C_CARD);
  spr.setTextDatum(TL_DATUM);
  // not running while open = still starting, or refused (low memory): it retries by itself
  const char *err = bleError();
  spr.setTextColor(ready ? TFT_GREEN : !bleRunning() ? (err[0] ? TFT_RED : TFT_DARKGREY) : TFT_ORANGE);
  spr.drawString(!bleRunning() ? (err[0] ? err : "Starting...") : ready ? "Connected" : fail ? "Not connected" : "Waiting...",
                 24, 34, 2);
  spr.setTextDatum(TC_DATUM);
  if (!bleRunning() && bleSuspended()) {        // an update holds the radio's memory
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("Back after the", 67, 54, 1);
    spr.setTextColor(C_SAND);
    spr.drawString("update / download", 67, 66, 1);
  } else if (!bleRunning() && err[0]) {
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("Retrying... turning", 67, 54, 1);
    spr.setTextColor(C_SAND);
    spr.drawString("WiFi off may help", 67, 66, 1);
  } else if (!ready) {
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("On your phone:", 67, 54, 1);
    spr.setTextColor(C_SAND);
    spr.drawString("Bluetooth > TTGO-Arcade", 67, 66, 1);
  } else {
    static const char *HINT[NUM_MODES] = {"L or R: take a photo", "hold R: play/pause", "L/R: arrow keys"};
    spr.setTextColor(C_SAND);
    spr.drawString(HINT[mode], 67, 54, 1);
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("hold L+R: change mode", 67, 66, 1);
  }

  // big mode icon, name and dots
  int cx = 67, cy = 124;
  if (mode == CAMERA) cameraIcon(cx, cy, now);
  else if (mode == MUSIC) noteIcon(cx, cy, now);
  else slidesIcon(cx, cy, now);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(MODE_NAME[mode], 67, 164, 4);
  modeDots(196);

  if (mode == CAMERA) drawToolFooter("shot", "shot");
  else drawToolFooter("prev", "next");

  // the L or R label lights up on each send
  float f = flashLevel(now);
  if (f > 0 && (sent == S_LEFT || sent == S_RIGHT || sent == S_HOLD)) {
    int x = sent == S_LEFT ? 6 : 66;
    spr.drawRoundRect(x, 217, 60, 21, 5, tft.alphaBlend((uint8_t)(255 * f), C_TEAL, TFT_BLACK));
  }
  spr.pushSprite(0, 0);
}

// ---------------- actions ----------------
static void send(Sent kind, uint32_t now) {
  bool ok;
  if (mode == CAMERA) ok = bleTapMedia(BLE_MEDIA_VOL_UP);
  else if (mode == MUSIC) ok = bleTapMedia(kind == S_LEFT ? BLE_MEDIA_PREV : kind == S_RIGHT ? BLE_MEDIA_NEXT : BLE_MEDIA_PLAY_PAUSE);
  else ok = bleTapKey(kind == S_LEFT ? BLE_KEY_LEFT : BLE_KEY_RIGHT);
  if (ok && mode == SLIDES) slide = max(1, slide + (kind == S_LEFT ? -1 : 1));
  sent = ok ? kind : S_FAIL;
  sentAt = now;
  dbg("remote: %s %d %s\n", MODE_NAME[mode], (int)kind, ok ? "sent" : "not connected");
}

static void leave() {
  bleRelease();                       // stops BLE again only if the setting is off
  returnToMenu();
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // a phone with radio waves pulsing out of it
  spr.fillRoundRect(cx - 13, cy - 13, 13, 26, 3, c(C_CORAL));
  spr.fillRect(cx - 11, cy - 9, 9, 16, c(C_PLUM));
  spr.fillRect(cx - 9, cy - 4, 5, 5, c(C_TEAL));                       // the shutter button on screen
  spr.drawFastHLine(cx - 9, cy + 10, 5, c(C_SAND));
  int lit = t ? (int)((t / 300) % 4) : 3;
  for (int i = 0; i < 3; i++) {
    uint16_t col = (i < lit) ? c(C_TEAL) : c(TFT_DARKGREY);
    int r = 4 + i * 3;
    spr.drawSmoothArc(cx, cy, r + 1, r, 225, 315, col, TFT_BLACK, true);
  }
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "%s", bleHidReady() ? "Connected" : "Phone remote");
}

void begin() {
  mode = prefs.getUChar("remote_mode", CAMERA) % NUM_MODES;
  pressedL = pressedR = combo = false;
  sent = S_NONE;
  lastInput = openedAt = millis();
  if (!bleRunning()) {                // starting the stack takes a moment: say so
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_TEAL);
    spr.drawString("Starting", 67, 110, 4);
    spr.drawString("Bluetooth...", 67, 136, 4);
    spr.pushSprite(0, 0);
  }
  bleUse(millis());
  lastDraw = 0;
}

void update(uint32_t now) {
  bleUse(now);                        // keep-alive while the tool is open

  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  // Hold L+R: next mode; both releases are swallowed
  if (!combo && pressedL && pressedR && bL.stable && bR.stable) {
    combo = true;
    mode = (mode + 1) % NUM_MODES;
    prefs.putUChar("remote_mode", mode);
    sent = S_NONE;
  }
  if (combo) {
    if (!bL.stable && !bR.stable) combo = pressedL = pressedR = false;
  } else {
    if (pressedL && bL.stable && !bR.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
      pressedL = false;
      leave();
      return;
    }
    if (mode == MUSIC && pressedR && bR.stable && !bL.stable && !bR.consumed &&
        (int32_t)(now - bR.t0) >= (int32_t)HOLD_R_MS) {
      bR.consumed = true;
      send(S_HOLD, now);
    }
    if (eL == 2 && pressedL) { pressedL = false; send(S_LEFT, now); }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (!bR.consumed) send(S_RIGHT, now);
    }
  }

  // idle: a paired remote may sit untouched for a while (music, a tripod shot)
  if (bleHidReady()) {
    uint32_t lim = idleSleepMs();
    if (lim > 0 && (int32_t)(now - lastInput) > (int32_t)max(lim, CONNECTED_IDLE_MS)) goToSleep();
  } else if (idleTooLong(now, lastInput)) {
    goToSleep();
  }

  if ((int32_t)(now - lastDraw) >= (int32_t)FRAME_MS) {
    lastDraw = now;
    render(now);
  }
}

}  // namespace remote

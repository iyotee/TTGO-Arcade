// Minute (Countdown Timer)
//
// Controls (two buttons):
//   Set     : Tap L next preset · Tap R start
//   Running : Tap L +1 min · Tap R pause
//   Paused  : Tap L reset · Tap R resume
//   Ringing : any button stops the alarm
//   Hold L  : back to menu
// The board has no buzzer: when time is up the screen flashes at full brightness.
// The board stays awake while counting down. The last preset is remembered.

#include "common.h"

namespace minute {

static const uint8_t PRESETS[] = {1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60};
static const int NUM_PRESETS = sizeof PRESETS / sizeof PRESETS[0];
static const uint32_t RING_MAX_MS = 60000;

enum Mode { SET, RUN, PAUSE, RING };
static Mode mode = SET;
static int preset = 3;
static uint32_t totalMs = 0, leftMs = 0, endAt = 0, ringAt = 0;
static int savedBright = 2;
static uint32_t lastInput = 0, lastDraw = 0;
static bool dirty = true, pressedL = false, pressedR = false;

static uint32_t remaining(uint32_t now) {
  if (mode != RUN) return leftMs;
  int32_t d = (int32_t)(endAt - now);
  return d > 0 ? d : 0;
}

static void toSet() {
  mode = SET;
  totalMs = leftMs = PRESETS[preset] * 60000UL;
}

static void render(uint32_t now) {
  char buf[16];
  if (mode == RING) {
    bool on = ((now - ringAt) / 250) & 1;
    spr.fillSprite(on ? TFT_RED : TFT_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(on ? TFT_WHITE : TFT_RED);
    spr.setTextSize(2);
    spr.drawString("TIME!", 67, 100, 4);
    spr.setTextSize(1);
    spr.drawString("press a button", 67, 160, 2);
    spr.pushSprite(0, 0);
    return;
  }

  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("MINUTE", 67, 6, 4);

  uint32_t ms = remaining(now);
  // ring of 60 dots: lit ones show the time left
  int lit = totalMs ? (int)((uint64_t)ms * 60 / totalMs + (ms ? 1 : 0)) : 0;
  for (int i = 0; i < 60; i++) {
    float a = -PI / 2 + i * 2 * PI / 60;
    int x = 67 + (int)roundf(cosf(a) * 52), y = 112 + (int)roundf(sinf(a) * 52);
    spr.fillCircle(x, y, i % 5 ? 1 : 2, i < lit ? (mode == PAUSE ? TFT_YELLOW : TFT_ORANGE) : 0x2945);
  }
  uint32_t secs = (ms + 999) / 1000;
  snprintf(buf, sizeof buf, "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(mode == PAUSE && ((now / 400) & 1) ? TFT_DARKGREY : TFT_WHITE);
  spr.setTextSize(secs >= 600 ? 1 : 2);             // big digits once they fit inside the ring
  spr.drawString(buf, 67, 112, 4);
  spr.setTextSize(1);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  const char *st = mode == SET ? "set" : mode == RUN ? "running" : "paused";
  spr.drawString(st, 67, 176, 2);

  if (mode == SET) drawToolFooter("time", "start");
  else if (mode == RUN) drawToolFooter("+1 min", "pause");
  else drawToolFooter("reset", "resume");
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  // an hourglass whose sand runs down
  float f = t ? (t % 3000) / 3000.0f : 0.4f;
  spr.drawTriangle(x - 8, y - 12, x + 8, y - 12, x, y, c(TFT_LIGHTGREY));
  spr.drawTriangle(x - 8, y + 12, x + 8, y + 12, x, y, c(TFT_LIGHTGREY));
  int top = (int)(10 * (1 - f)), bot = (int)(10 * f);
  if (top > 0) spr.fillTriangle(x - top * 7 / 10, y - top, x + top * 7 / 10, y - top, x, y, c(TFT_ORANGE));
  if (bot > 0) spr.fillRect(x - bot * 7 / 10, y + 12 - bot, bot * 14 / 10 + 1, bot, c(TFT_ORANGE));
  spr.drawFastHLine(x - 10, y - 13, 21, c(TFT_ORANGE));
  spr.drawFastHLine(x - 10, y + 13, 21, c(TFT_ORANGE));
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Countdown %d min", PRESETS[prefs.getUChar("cdpreset", 3) % NUM_PRESETS]);
}

void begin() {
  preset = prefs.getUChar("cdpreset", 3) % NUM_PRESETS;
  toSet();
  pressedL = pressedR = false;
  lastInput = millis();
  dirty = true;
}

static void leave() {
  if (mode == RING) setBrightness(savedBright, false);
  mode = SET;
  returnToMenu();
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (mode == RING) {
    // any press stops the alarm; it also gives up after a minute
    if (eL == 1 || eR == 1 || (int32_t)(now - ringAt) >= (int32_t)RING_MAX_MS) {
      setBrightness(savedBright, false);
      pressedL = pressedR = false;
      toSet();
      dirty = true;
    }
  } else {
    if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
      pressedL = false;
      leave();
      return;
    }
    if (eL == 2 && pressedL) {
      pressedL = false;
      if (mode == SET) {
        preset = (preset + 1) % NUM_PRESETS;
        prefs.putUChar("cdpreset", preset);
        toSet();
      } else if (mode == RUN) {
        endAt += 60000;
        totalMs += 60000;
      } else toSet();
      dirty = true;
    }
    if (eR == 2 && pressedR) {
      pressedR = false;
      if (mode == RUN) { leftMs = remaining(now); mode = PAUSE; }
      else { endAt = now + leftMs; mode = RUN; }
      dirty = true;
    }
    if (mode == RUN && remaining(now) == 0) {
      mode = RING;
      ringAt = now;
      savedBright = brightnessLevel();
      setBrightness(NUM_BRIGHTNESS - 1, false);
      dirty = true;
    }
  }

  if ((mode == SET || mode == PAUSE) && idleTooLong(now, lastInput)) goToSleep();
  int frameMs = mode == RING ? 50 : 200;
  if ((mode != SET) && (int32_t)(now - lastDraw) >= frameMs) dirty = true;
  if (dirty) {
    lastDraw = now;
    render(now);
    dirty = false;
  }
}

}  // namespace minute

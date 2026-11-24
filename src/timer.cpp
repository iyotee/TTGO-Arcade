// Timer (Stopwatch + Laps)
//
// Controls (two buttons):
//   Tap R  : start / stop
//   Tap L  : record lap (while running) / reset (when stopped)
//   Hold L : back to menu

#include "common.h"

namespace timer {

static const int MAX_LAPS = 5;
static const uint32_t HOLD_MS = 450;

static bool running = false;
static uint32_t elapsed = 0, startedAt = 0;
static uint32_t laps[MAX_LAPS];
static int numLaps = 0;
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastInput = 0, lastDraw = 0;

static uint32_t currentMs(uint32_t now) {
  return running ? elapsed + (now - startedAt) : elapsed;
}

static void fmtTime(char *buf, size_t n, uint32_t ms) {
  uint32_t tenths = (ms / 100) % 10;
  uint32_t secs = (ms / 1000) % 60;
  uint32_t mins = (ms / 60000) % 100;
  snprintf(buf, n, "%02lu:%02lu.%lu", (unsigned long)mins, (unsigned long)secs, (unsigned long)tenths);
}

static void render(uint32_t now) {
  char buf[24];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("TIMER", 67, 8, 4);

  uint32_t ms = currentMs(now);
  fmtTime(buf, sizeof buf, ms);
  spr.setTextColor(running ? TFT_GREEN : TFT_WHITE);
  spr.drawString(buf, 67, 46, 4);

  spr.drawFastHLine(10, 82, 115, TFT_DARKGREY);
  spr.setTextDatum(TL_DATUM);
  for (int i = 0; i < numLaps; i++) {
    int y = 90 + i * 20;
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "L%d", numLaps - i);
    spr.drawString(buf, 14, y, 2);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(TFT_YELLOW);
    fmtTime(buf, sizeof buf, laps[i]);
    spr.drawString(buf, 121, y, 2);
    spr.setTextDatum(TL_DATUM);
  }

  if (numLaps == 0) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("Hold L to exit", 67, 130, 2);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 220, 2);
  spr.drawString("R", 70, 220, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(running ? "lap" : "reset", 22, 220, 2);
  spr.drawString(running ? "stop" : "start", 82, 220, 2);
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  spr.drawCircle(x, y + 1, 10, c(TFT_CYAN));
  spr.fillRect(x - 2, y - 12, 4, 3, c(TFT_LIGHTGREY));
  float a = -PI / 2 + (dim ? 0.0f : (t % 2000) * (2 * PI / 2000.0f));
  spr.drawLine(x, y + 1, x + (int)roundf(cosf(a) * 7), y + 1 + (int)roundf(sinf(a) * 7), c(TFT_YELLOW));
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Stopwatch & laps");
}

void begin() {
  pressedL = pressedR = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (bL.stable && pressedL && (int32_t)(now - bL.t0) >= (int32_t)HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }

  if (eL == 2 && pressedL) {
    pressedL = false;
    if (running) {
      for (int i = MAX_LAPS - 1; i > 0; i--) laps[i] = laps[i - 1];
      laps[0] = currentMs(now);
      if (numLaps < MAX_LAPS) numLaps++;
    } else {
      elapsed = 0;
      numLaps = 0;
    }
    dirty = true;
  }

  if (eR == 2 && pressedR) {
    pressedR = false;
    if (running) {
      elapsed += now - startedAt;
      running = false;
    } else {
      startedAt = now;
      running = true;
    }
    dirty = true;
  }

  if (!running && idleTooLong(now, lastInput)) goToSleep();

  if (running && (int32_t)(now - lastDraw) >= 80) dirty = true;
  if (dirty) {
    lastDraw = now;
    render(now);
    dirty = false;
  }
}

}  // namespace timer

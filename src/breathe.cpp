// Breathe (Guided Breathing)
//
// Controls (two buttons):
//   Tap L  : next pattern (Box 4-4-4-4, Relax 4-7-8, Calm 5-5)
//   Tap R  : start / pause
//   Hold L : back to menu
// Follow the circle: it grows while you breathe in and shrinks while you
// breathe out. The board stays awake during a session.

#include "common.h"

namespace breathe {

struct Pattern { const char *name; uint8_t sec[4]; };   // in, hold, out, hold
static const Pattern PATTERNS[] = {
  {"Box 4-4-4-4", {4, 4, 4, 4}},
  {"Relax 4-7-8", {4, 7, 8, 0}},
  {"Calm 5-5", {5, 0, 5, 0}},
};
static const int NUM_PATTERNS = sizeof PATTERNS / sizeof PATTERNS[0];
static const char *PHASE_NAME[4] = {"Breathe in", "Hold", "Breathe out", "Hold"};
static const uint16_t PHASE_COL[4] = {TFT_CYAN, TFT_SKYBLUE, TFT_GREEN, TFT_DARKGREEN};
static const int R_MIN = 16, R_MAX = 54, CY = 112;

static int pat = 0;
static bool running = false;
static uint32_t elapsed = 0, startedAt = 0;        // session time, ms (pauses excluded)
static uint32_t lastInput = 0, lastDraw = 0;
static bool dirty = true, pressedL = false, pressedR = false;

static uint32_t sessionMs(uint32_t now) { return running ? elapsed + (now - startedAt) : elapsed; }

// where in the cycle we are: phase 0..3, progress 0..1 in it, seconds left, cycles done
static void where(uint32_t ms, int &phase, float &prog, int &secLeft, uint32_t &cycles) {
  const uint8_t *s = PATTERNS[pat].sec;
  uint32_t cycle = (s[0] + s[1] + s[2] + s[3]) * 1000UL;
  cycles = ms / cycle;
  uint32_t t = ms % cycle;
  for (phase = 0; phase < 4; phase++) {
    uint32_t len = s[phase] * 1000UL;
    if (t < len) { prog = (float)t / len; secLeft = (len - t + 999) / 1000; return; }
    t -= len;
  }
  phase = 0; prog = 0; secLeft = s[0];
}

static void render(uint32_t now) {
  char buf[24];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_SKYBLUE);
  spr.drawString("BREATHE", 67, 6, 4);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString(PATTERNS[pat].name, 67, 34, 2);

  int phase;
  float prog;
  int secLeft;
  uint32_t cycles;
  uint32_t ms = sessionMs(now);
  where(ms, phase, prog, secLeft, cycles);
  // eased size: grows on the in-breath, stays, shrinks on the out-breath
  float e = 0.5f - 0.5f * cosf(prog * PI);
  float k = phase == 0 ? e : phase == 1 ? 1 : phase == 2 ? 1 - e : 0;
  if (!running && ms == 0) k = 0.35f;
  int r = R_MIN + (int)((R_MAX - R_MIN) * k);
  uint16_t col = PHASE_COL[phase];
  spr.drawCircle(67, CY, R_MAX + 2, 0x18E3);
  spr.fillCircle(67, CY, r, tft.alphaBlend(90, col, TFT_BLACK));
  spr.drawCircle(67, CY, r, col);

  spr.setTextDatum(MC_DATUM);
  if (running || ms) {
    spr.setTextColor(TFT_WHITE);
    snprintf(buf, sizeof buf, "%d", secLeft);
    spr.drawString(buf, 67, CY, 4);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(running ? col : TFT_DARKGREY);
    spr.drawString(running ? PHASE_NAME[phase] : "paused", 67, 172, 2);
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "%lu:%02lu  x%lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60), (unsigned long)cycles);
    spr.drawString(buf, 67, 190, 1);
  } else {
    spr.setTextColor(TFT_WHITE);
    spr.drawString("ready", 67, CY, 2);
  }

  drawToolFooter("pattern", running ? "pause" : "start");
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  int r = 5 + (int)roundf(7 * (t ? triWave(t, 3000) : 0.5f));
  spr.drawCircle(x, y, 13, c(0x18E3));
  spr.fillCircle(x, y, r, c(0x0451));
  spr.drawCircle(x, y, r, c(TFT_CYAN));
}

void menuInfo(char *buf, size_t n) {
  snprintf(buf, n, "Guided breathing");
}

void begin() {
  pat = prefs.getUChar("breathe", 0) % NUM_PATTERNS;
  running = false;
  elapsed = 0;
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
  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    // a new pattern restarts the session
    pat = (pat + 1) % NUM_PATTERNS;
    prefs.putUChar("breathe", pat);
    running = false;
    elapsed = 0;
    dirty = true;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    if (running) { elapsed += now - startedAt; running = false; }
    else { startedAt = now; running = true; }
    dirty = true;
  }

  if (!running && idleTooLong(now, lastInput)) goToSleep();
  if (running && (int32_t)(now - lastDraw) >= 33) dirty = true;
  if (dirty) {
    lastDraw = now;
    render(now);
    dirty = false;
  }
}

}  // namespace breathe

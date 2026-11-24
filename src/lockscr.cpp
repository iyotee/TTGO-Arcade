// PIN lock: a code of 4 to 8 presses of L / R (e.g. LLRL), asked at boot,
// on wake from deep sleep and when leaving the screensaver; off by default.
//
// Lock screen:  Tap L / Tap R : enter the code (checked at its length)
//               5 wrong tries -> 30 s wait; 30 s idle -> deep sleep
// Set PIN:      Tap L / Tap R 4-8 times, Hold L+R : OK, then type it again
//               Hold L (alone, 1 s) : cancel
// Check PIN:    Tap L / Tap R, Hold L (1 s) : cancel

#include "common.h"
#include "lockscr.h"

namespace lockscr {

// ---------------- configuration ----------------
static const int MAX_TRIES = 5;
static const uint32_t LOCKOUT_MS = 30000;
static const uint32_t IDLE_MS = 30000;       // lock screen: deep sleep; flows: cancel
static const uint32_t SHAKE_MS = 450;
static const uint32_t OK_MS = 550;           // unlock animation before leaving
static const uint32_t BOTH_MS = 150;         // L+R overlap shorter than this = two taps
static const uint32_t CHORD_MS = 500;        // hold L+R this long to confirm
static const uint32_t CANCEL_MS = 1000;      // hold L alone this long to cancel
static const uint32_t FRAME_MS = 33;

// Tigo's palette
static const uint16_t C_CORAL = 0xFB49, C_CLAW = 0xE245, C_PEACH = 0xFD91;
static const uint16_t C_SHELL = 0xF6B4, C_TEAL = 0x2D95, C_PLUM = 0x38E7;

// ---------------- stored state ----------------
static char pin[PIN_MAX_LEN + 1] = "";
static bool on = false, loaded = false;
static int fails = 0;                        // wrong tries in a row, saved (a reset doesn't skip the wait)
static bool lockedOut = false;
static uint32_t lockoutEnd = 0;
static uint32_t lastInput = 0;               // last button activity on these screens

static bool validPin(const char *s) {
  int n = strlen(s);
  if (n < PIN_MIN_LEN || n > PIN_MAX_LEN) return false;
  for (int i = 0; i < n; i++) if (s[i] != 'L' && s[i] != 'R') return false;
  return true;
}

static void startLockout(uint32_t now) {
  lockedOut = true;
  lockoutEnd = now + LOCKOUT_MS;
}

static void saveFails() { prefs.putUChar("pinfail", (uint8_t)fails); }

static void load() {
  if (loaded) return;
  loaded = true;
  pin[0] = 0;
  if (prefs.isKey("pin")) prefs.getString("pin", pin, sizeof pin);
  if (!validPin(pin)) pin[0] = 0;
  on = prefs.getBool("pinon", false) && pin[0];
  fails = prefs.getUChar("pinfail", 0);
  if (fails >= MAX_TRIES) startLockout(millis());
}

static int32_t lockoutLeft(uint32_t now) {
  if (!lockedOut) return 0;
  int32_t left = (int32_t)(lockoutEnd - now);
  if (left > 0) return left;
  lockedOut = false;                           // served: five more tries
  if ((int32_t)(lockoutEnd - lastInput) > 0) lastInput = lockoutEnd;   // idle counts from here
  fails = 0;
  saveFails();
  return 0;
}

// ---------------- input ----------------
// Taps act on release of a press that started here. Holding L and R together
// is a chord (OK in the set flow) and neither press counts as a tap; a short
// overlap from fast typing still gives two taps.
enum Input { IN_NONE, IN_L, IN_R, IN_CONFIRM, IN_CANCEL };

static bool pressedL = false, pressedR = false, both = false, chord = false, holdFired = false;
static uint32_t bothT0 = 0;

static void resetInput(uint32_t now) {
  pressedL = pressedR = both = chord = holdFired = false;
  lastInput = now;
}

static Input readInput(uint32_t now, bool chordOk, bool cancelOk) {
  int eL = updateBtn(bL, now), eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (pressedL && pressedR && bL.stable && bR.stable) {
    if (!both) { both = true; bothT0 = now; }
    int32_t held = (int32_t)(now - bothT0);
    if (held >= (int32_t)BOTH_MS) chord = true;
    if (chordOk && !holdFired && held >= (int32_t)CHORD_MS) { holdFired = true; return IN_CONFIRM; }
  } else {
    both = false;
  }
  if (cancelOk && !chord && !holdFired && pressedL && bL.stable && !bR.stable &&
      (int32_t)(now - bL.t0) >= (int32_t)CANCEL_MS) {
    holdFired = true;
    return IN_CANCEL;
  }

  Input in = IN_NONE;
  if (eL == 2 && pressedL) { pressedL = false; if (!chord && !holdFired) in = IN_L; }
  if (eR == 2 && pressedR) { pressedR = false; if (!chord && !holdFired && in == IN_NONE) in = IN_R; }
  if (!pressedL && !pressedR) chord = holdFired = false;
  return in;
}

// ---------------- entry state ----------------
enum Mode { M_LOCK, M_CHECK, M_SET1, M_SET2 };
// RES_ prefix: unistd.h defines R_OK
enum Result { RES_BUSY, RES_OK, RES_CANCEL };

static Mode mode = M_LOCK;
static char seq[PIN_MAX_LEN + 1];
static char first[PIN_MAX_LEN + 1];          // set flow: the first entry
static int n = 0;
static int shakeN = 0;                       // dots shown in red while shaking
static uint32_t shakeAt = 0, okAt = 0, msgAt = 0, lastFrame = 0;
static bool shaking = false, okAnim = false;
static const char *msg = nullptr;            // overrides the status line for a while
static uint16_t msgCol = TFT_WHITE;

static void start(Mode m) {
  load();
  mode = m;
  n = 0;
  seq[0] = 0;
  shaking = okAnim = false;
  msg = nullptr;
  lastFrame = 0;
  resetInput(millis());
}

static void say(uint32_t now, const char *m, uint16_t col) { msg = m; msgCol = col; msgAt = now; }

static void shake(uint32_t now, const char *m) {
  shaking = true;
  shakeAt = now;
  shakeN = n;
  n = 0;
  seq[0] = 0;
  say(now, m, TFT_RED);
}

// ---------------- rendering ----------------
static uint16_t bgAt(int y) { return tft.alphaBlend(y * 150 / 239, C_PLUM, TFT_BLACK); }

static void drawBackground(uint32_t now) {
  for (int y = 0; y < 240; y++) spr.drawFastHLine(0, y, 135, bgAt(y));
  // a few stars twinkling over the night beach
  static const uint8_t SX[] = {9, 27, 52, 88, 110, 126, 18, 118};
  static const uint8_t SY[] = {8, 62, 4, 10, 58, 22, 40, 84};
  for (int i = 0; i < 8; i++) {
    int a = 50 + (int)(190 * triWave(now + i * 731, 2400 + i * 170));
    spr.drawPixel(SX[i], SY[i], tft.alphaBlend(a, TFT_WHITE, bgAt(SY[i])));
  }
}

// Padlock in Tigo's colours: cream shackle, coral body with the teal shell
// band, plum outlines. top = top of the shackle, lift = shackle raised (open).
static void drawPadlock(int cx, int top, int lift, bool dark) {
  const int R = 19, IR = 12, BW = 64, BH = 46;
  int sy = top + R - lift;                     // shackle arc centre
  int by = top + R + 2;                        // body top
  int bx = cx - BW / 2;
  uint16_t shellCol = dark ? tft.alphaBlend(140, C_SHELL, TFT_BLACK) : C_SHELL;
  uint16_t bodyCol = dark ? tft.alphaBlend(150, C_CLAW, TFT_BLACK) : C_CORAL;
  uint16_t bandCol = dark ? tft.alphaBlend(150, C_TEAL, TFT_BLACK) : C_TEAL;
  uint16_t bg = bgAt(sy);

  // shackle: outline then fill; the legs run down into the body
  int legH = by + 6 - sy;
  spr.drawSmoothArc(cx, sy, R + 1, IR - 1, 90, 270, C_PLUM, bg, false);
  spr.fillRect(cx - R, sy, R - IR + 2, legH, C_PLUM);
  spr.fillRect(cx + IR - 1, sy, R - IR + 2, legH, C_PLUM);
  spr.drawArc(cx, sy, R, IR, 90, 270, shellCol, C_PLUM, true);
  spr.fillRect(cx - R + 1, sy, R - IR, legH, shellCol);
  spr.fillRect(cx + IR, sy, R - IR, legH, shellCol);
  spr.drawFastVLine(cx - R + 3, sy, legH - 2, tft.alphaBlend(150, TFT_WHITE, shellCol));   // shine

  // body
  spr.fillSmoothRoundRect(bx - 1, by - 1, BW + 2, BH + 2, 10, C_PLUM, 0x00FFFFFF);
  spr.fillSmoothRoundRect(bx, by, BW, BH, 9, bodyCol, C_PLUM);
  spr.fillRect(bx + 1, by + BH - 17, BW - 2, 6, bandCol);                 // the shell's spiral band
  spr.drawFastHLine(bx + 1, by + BH - 18, BW - 2, C_PLUM);
  spr.drawFastHLine(bx + 1, by + BH - 11, BW - 2, C_PLUM);
  spr.fillSmoothRoundRect(bx + 5, by + 5, 4, 14, 2, tft.alphaBlend(130, TFT_WHITE, bodyCol), bodyCol);   // highlight

  // keyhole
  int kx = cx, ky = by + 14;
  spr.fillSmoothCircle(kx, ky, 5, C_PLUM, bodyCol);
  spr.fillTriangle(kx - 3, ky + 2, kx + 3, ky + 2, kx, ky + 11, C_PLUM);
  spr.fillRect(kx - 2, ky + 2, 5, 8, C_PLUM);
}

static void drawDots(int y, int count, int filled, uint16_t fillCol) {
  const int pitch = 14;
  int x0 = 67 - (count - 1) * pitch / 2;
  for (int i = 0; i < count; i++) {
    int x = x0 + i * pitch;
    if (i < filled) spr.fillSmoothCircle(x, y, 5, fillCol, 0x00FFFFFF);
    else {
      spr.fillSmoothCircle(x, y, 5, C_PEACH, 0x00FFFFFF);
      spr.fillSmoothCircle(x, y, 3, bgAt(y), C_PEACH);
    }
  }
}

// set flow: the entered presses as L / R chips, so the owner sees the code
static void drawChips(int y) {
  const int pitch = 15;
  int count = shaking ? shakeN : n;
  if (!count) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("- - - -", 67, y, 2);
    return;
  }
  int x0 = 67 - (count - 1) * pitch / 2;
  spr.setTextDatum(MC_DATUM);
  for (int i = 0; i < count; i++) {
    int x = x0 + i * pitch;
    bool isL = shaking ? true : seq[i] == 'L';
    uint16_t col = shaking ? TFT_RED : isL ? C_CORAL : C_TEAL;
    spr.fillSmoothRoundRect(x - 6, y - 8, 13, 16, 3, col, 0x00FFFFFF);
    spr.setTextColor(TFT_WHITE);
    if (!shaking) spr.drawString(isL ? "L" : "R", x + 1, y + 1, 2);
  }
}

static void render(uint32_t now) {
  char buf[24];
  int32_t left = (mode == M_LOCK || mode == M_CHECK) ? lockoutLeft(now) : 0;
  drawBackground(now);

  // header: the time on the lock screen when known, else a title
  spr.setTextDatum(TC_DATUM);
  struct tm tmv;
  if (mode == M_LOCK && localTimeNow(&tmv)) {
    strftime(buf, sizeof buf, "%H:%M", &tmv);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(buf, 67, 10, 4);
    strftime(buf, sizeof buf, "%a %d %b", &tmv);
    spr.setTextColor(C_PEACH);
    spr.drawString(buf, 67, 40, 2);
  } else {
    const char *title = mode == M_LOCK ? "TTGO-Arcade" : mode == M_CHECK ? "Current PIN"
                      : mode == M_SET1 ? "New PIN" : "Confirm PIN";
    spr.setTextColor(C_CORAL);
    spr.drawString(title, 67, 18, mode == M_LOCK ? 2 : 4);
  }

  // padlock, shaking after a wrong code, opening once right
  int dx = 0;
  if (shaking) {
    float k = (float)(int32_t)(now - shakeAt) / SHAKE_MS;
    if (k >= 1) shaking = false;
    else dx = (int)roundf(sinf(k * 6 * PI) * 7 * (1 - k));
  }
  int lift = 0;
  if (okAnim) lift = min(10, (int)((int32_t)(now - okAt) / 18));
  drawPadlock(67 + dx, 64, lift, left > 0);

  // presses entered
  if (mode == M_LOCK || mode == M_CHECK) {
    int len = strlen(pin);
    if (shaking) drawDots(150, len, shakeN, TFT_RED);
    else drawDots(150, len, okAnim ? len : n, okAnim ? TFT_GREEN : C_CORAL);
  } else {
    drawChips(150);
  }

  // status line
  spr.setTextDatum(TC_DATUM);
  if (okAnim) {
    spr.setTextColor(TFT_GREEN);
    spr.drawString(mode == M_LOCK || mode == M_CHECK ? "Unlocked!" : "PIN saved", 67, 166, 2);
  } else if (left > 0) {
    spr.setTextColor(TFT_RED);
    snprintf(buf, sizeof buf, "Wait %d s", (int)((left + 999) / 1000));
    spr.drawString(buf, 67, 166, 2);
  } else if (msg) {
    spr.setTextColor(msgCol);
    spr.drawString(msg, 67, 166, 2);
  } else {
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString(mode == M_SET1 ? "Tap L / R 4-8 times" : mode == M_SET2 ? "Type it again" : "Enter PIN", 67, 166, 2);
  }

  if (mode == M_LOCK) {
    // Tigo keeps watch: hides during the wait, sulks after a miss, cheers when open
    TigoPose pose = okAnim ? TIGO_HAPPY : left > 0 ? TIGO_HIDE : (msg && (int32_t)(now - msgAt) < 1500) ? TIGO_SAD : TIGO_IDLE;
    drawTigo(67, 234, 42, pose, now, false);
  } else {
    spr.setTextColor(TFT_LIGHTGREY);
    if (mode == M_SET1 || mode == M_SET2) spr.drawString("hold L+R: OK", 67, 200, 2);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("hold L: cancel", 67, 222, 1);
  }
  spr.pushSprite(0, 0);
}

// ---------------- logic ----------------
static void checkEntry(uint32_t now) {
  if (strcmp(seq, pin) == 0) {
    if (fails) { fails = 0; saveFails(); }
    okAnim = true;
    okAt = now;
    dbg("pin ok\n");
    return;
  }
  fails++;
  saveFails();
  dbg("pin wrong (%d)\n", fails);
  shake(now, "Wrong PIN");
  if (fails >= MAX_TRIES) startLockout(now);
}

static void savePin(uint32_t now) {
  strcpy(pin, first);
  on = true;
  fails = 0;
  prefs.putString("pin", pin);
  prefs.putBool("pinon", true);
  saveFails();
  okAnim = true;
  okAt = now;
  dbg("pin set (%d presses)\n", (int)strlen(pin));
}

static Result step(uint32_t now) {
  if (okAnim) {
    if ((int32_t)(now - okAt) >= (int32_t)OK_MS) return RES_OK;
    if ((int32_t)(now - lastFrame) >= (int32_t)FRAME_MS) { lastFrame = now; render(now); }
    return RES_BUSY;
  }
  bool setting = mode == M_SET1 || mode == M_SET2;
  Input in = readInput(now, setting, mode != M_LOCK);
  if (in == IN_CANCEL) return RES_CANCEL;
  bool wait = !setting && lockoutLeft(now) > 0;
  if (shaking || wait) {
    if (in != IN_NONE) lastInput = now;
    in = IN_NONE;                              // ignored until the shake / wait is over
  }

  if (in == IN_L || in == IN_R) {
    msg = nullptr;
    if (n >= PIN_MAX_LEN) {
      shake(now, "8 presses max");
    } else {
      seq[n++] = in == IN_L ? 'L' : 'R';
      seq[n] = 0;
      if (!setting && n == (int)strlen(pin)) checkEntry(now);
      else if (mode == M_SET2 && n == (int)strlen(first)) {
        if (strcmp(seq, first) == 0) savePin(now);
        else { shake(now, "No match, again"); mode = M_SET1; }
      }
    }
  } else if (in == IN_CONFIRM) {
    if (mode == M_SET1) {
      if (n < PIN_MIN_LEN) shake(now, "4 presses min");
      else {
        strcpy(first, seq);
        n = 0;
        seq[0] = 0;
        mode = M_SET2;
        say(now, "Type it again", C_PEACH);
      }
    } else if (mode == M_SET2) {
      shake(now, "No match, again");             // shorter than the first entry
      mode = M_SET1;
    }
  }

  // idle: the lock screen sleeps (counting from the end of a wait), the flows give up
  uint32_t idleFrom = lastInput;
  if (lockedOut && (int32_t)(lockoutEnd - idleFrom) > 0) idleFrom = lockoutEnd;
  if ((int32_t)(now - idleFrom) > (int32_t)IDLE_MS) {
    if (mode == M_LOCK) deepSleepNow();
    return RES_CANCEL;
  }

  if ((int32_t)(now - lastFrame) >= (int32_t)FRAME_MS) {
    lastFrame = now;
    render(now);
  }
  return RES_BUSY;
}

// runs a flow to its end, keeping the clock and Bluetooth serviced
static Result runFlow(Mode m) {
  start(m);
  for (;;) {
    uint32_t now = millis();
    Result r = step(now);
    if (r != RES_BUSY) return r;
    timeService(now);
    bleService(now);
    delay(2);
  }
}

// ---------------- API ----------------
bool enabled() { load(); return on; }

void begin() { start(M_LOCK); }

bool update(uint32_t now) { return step(now) == RES_BUSY; }

void run() {
  dbg("lock screen\n");
  runFlow(M_LOCK);
}

bool setPin() { return runFlow(M_SET1) == RES_OK; }

bool checkPin() {
  load();
  if (!pin[0]) return true;                    // nothing to check against
  return runFlow(M_CHECK) == RES_OK;
}

void disable() {
  load();
  on = false;
  pin[0] = 0;
  prefs.putBool("pinon", false);
  prefs.remove("pin");
  dbg("pin lock off\n");
}

}  // namespace lockscr

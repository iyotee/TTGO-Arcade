// Clock: big digital or analog clock in the time zone of your choice.
//
// Controls:
//   Tap L  : next time zone (saved; its name shows for a moment)
//   Tap R  : digital / analog (saved)
//   Hold L : back to menu
//
// The time comes from NTP over WiFi (clock.cpp) or from the phone via
// Bluetooth; until then a hint explains how to get it.

#include "common.h"
#include "clock.h"
#include <sys/time.h>

namespace clockapp {

// ---------------- palette (Tigo's colours) ----------------
static const uint16_t C_CORAL = 0xFB49, C_CLAW = 0xE245, C_PEACH = 0xFD91, C_SAND = 0xF6B4;
static const uint16_t C_TEAL = 0x2D95, C_PLUM = 0x38E7;
static const uint16_t C_NIGHT = 0x1863, C_DAY = 0x0947, C_FACE = 0x10A2, C_GREY = 0x8C51;
static const uint32_t BLEND = 0x00FFFFFF;      // smooth primitives: blend with the sprite

static const uint32_t TOAST_MS = 1600;
static const uint32_t FRAME_DIGITAL_MS = 100, FRAME_ANALOG_MS = 50;
static const char *PREF_ANALOG = "clk_analog";

static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *DAYS3[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};
static const char *MON3[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static bool analog = false, toastOn = false;
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastInput = 0, lastDraw = 0, toastAt = 0, openedAt = 0;

// ---------------- helpers ----------------
// local time with milliseconds; false while the time is unknown
static bool nowLocal(struct tm *tm, int *ms) {
  if (!localTimeNow(tm)) return false;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  localtime_r(&t, tm);               // same second as the milliseconds
  *ms = tv.tv_usec / 1000;
  return true;
}

// centred text; drops to the next smaller font when it would not fit
static void drawFit(const char *s, int x, int y, int font, int maxW = 131) {
  if (font == 4 && spr.textWidth(s, 4) > maxW) font = 2;
  if (font == 2 && spr.textWidth(s, 2) > maxW) font = 1;
  spr.drawString(s, x, y, font);
}

static void wrapCentred(const char *s, int y, int font, int lineH, int maxW = 127) {
  char line[40];
  int len = 0;
  const char *p = s;
  while (*p) {
    // next word
    const char *w = p;
    while (*w == ' ') w++;
    const char *e = w;
    while (*e && *e != ' ') e++;
    char trial[40];
    snprintf(trial, sizeof trial, "%.*s%s%.*s", len, line, len ? " " : "", (int)(e - w), w);
    if (len && spr.textWidth(trial, font) > maxW) {
      spr.drawString(line, 67, y, font);
      y += lineH;
      len = snprintf(line, sizeof line, "%.*s", (int)(e - w), w);
    } else {
      len = snprintf(line, sizeof line, "%s", trial);
    }
    p = e;
  }
  if (len) spr.drawString(line, 67, y, font);
}

static uint16_t dimmed(uint16_t col, uint8_t a = 34) { return tft.alphaBlend(a, col, C_FACE); }

static void background(int hour) {
  bool day = hour >= 7 && hour < 19;
  spr.fillRectVGradient(0, 0, 135, 240, day ? C_DAY : C_NIGHT, TFT_BLACK);
}

// "UTC+1  NTP 5m ago": the offset and how fresh the time is
static void statusLine(char *buf, size_t n) {
  char off[12];
  clockFmtOffset(off, sizeof off);
  time_t last = clockLastSync();
  ClockSource src = clockSyncSource();
  if (!last || src == CLOCK_SRC_NONE) {
    snprintf(buf, n, "%s%s", off, clockSyncing() ? "  syncing..." : "");
    return;
  }
  long age = (long)(time(nullptr) - last);
  if (age < 0) age = 0;
  char ago[12];
  if (age < 60) snprintf(ago, sizeof ago, "just now");
  else if (age < 3600) snprintf(ago, sizeof ago, "%ldm ago", age / 60);
  else if (age < 48 * 3600L) snprintf(ago, sizeof ago, "%ldh ago", age / 3600);
  else snprintf(ago, sizeof ago, "%ldd ago", age / 86400);
  snprintf(buf, n, "%s  %s %s", off, src == CLOCK_SRC_PHONE ? "phone" : "NTP", ago);
}

static void drawHeader() {
  char buf[32];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_PEACH);
  drawFit(clockZoneCity(clockZone()), 67, 4, 2);
  statusLine(buf, sizeof buf);
  spr.setTextColor(C_GREY);
  drawFit(buf, 67, 22, 1);
}

// ---------------- rounded 7-segment digits ----------------
// bits a..g = 1, 2, 4, ... 64; -1 = a dash
static const uint8_t SEGS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static void segment(int x, int y, int w, int h, int r, uint16_t col) {
  spr.fillSmoothRoundRect(x, y, w, h, r, col, BLEND);
}

// box (x, y, w, h), stroke t; unlit segments as faint "ghosts" first, then the lit ones
static void drawDigit(int x, int y, int w, int h, int t, int d, uint16_t on, uint16_t off) {
  uint8_t bits = d < 0 ? 0x40 : SEGS[d % 10];
  int g = t / 3 + 1;                   // gap that keeps the pill ends apart
  int r = t / 2;
  int hx = x + t / 2 + g, hw = w - t - 2 * g;
  int vh = h / 2 - t / 2 - 2 * g;
  int vTop = y + t / 2 + g, vBot = y + h / 2 + g;
  int xs[7] = {hx, x + w - t, x + w - t, hx, x, x, hx};
  int ys[7] = {y, vTop, vBot, y + h - t, vBot, vTop, y + h / 2 - t / 2};
  for (int pass = 0; pass < 2; pass++)
    for (int s = 0; s < 7; s++) {
      bool lit = bits & (1 << s);
      if (lit != (pass == 1)) continue;
      bool horiz = s == 0 || s == 3 || s == 6;
      segment(xs[s], ys[s], horiz ? hw : t, horiz ? t : vh, r, lit ? on : off);
    }
}

static void drawPair(int y, int value, uint16_t on) {
  const int W = 48, H = 56, T = 9, GAP = 12, X0 = (135 - 2 * W - GAP) / 2;
  uint16_t off = tft.alphaBlend(30, on, TFT_BLACK);
  drawDigit(X0, y, W, H, T, value / 10, on, off);
  drawDigit(X0 + W + GAP, y, W, H, T, value % 10, on, off);
}

// ---------------- digital ----------------
static void renderDigital(const struct tm &tm, int ms) {
  char buf[32];
  background(tm.tm_hour);
  drawHeader();

  drawPair(36, tm.tm_hour, C_CORAL);

  // seconds: a bar that fills over the minute, and the number next to it
  const int BX = 13, BY = 98, BW = 92, BH = 6;
  float frac = (tm.tm_sec + ms / 1000.0f) / 60.0f;
  segment(BX, BY, BW, BH, BH / 2, tft.alphaBlend(40, C_SAND, TFT_BLACK));
  int fw = BH + (int)((BW - BH) * frac);
  segment(BX, BY, fw, BH, BH / 2, C_SAND);
  spr.fillSmoothCircle(BX + fw - BH / 2, BY + BH / 2, 4, TFT_WHITE, BLEND);
  spr.setTextDatum(MR_DATUM);
  spr.setTextColor(C_SAND);
  snprintf(buf, sizeof buf, "%02d", tm.tm_sec);
  spr.drawString(buf, 124, BY + BH / 2 + 1, 2);

  drawPair(112, tm.tm_min, C_TEAL);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_PEACH);
  spr.drawString(DAYS[tm.tm_wday], 67, 174, 2);
  snprintf(buf, sizeof buf, "%d %s %d", tm.tm_mday, MONTHS[tm.tm_mon], tm.tm_year + 1900);
  spr.setTextColor(C_GREY);
  drawFit(buf, 67, 193, 1);
}

// ---------------- analog ----------------
static const int CX = 67, CY = 98, R = 56;

static void polar(float deg, float r, float &x, float &y) {
  float a = deg * (float)DEG_TO_RAD;
  x = CX + sinf(a) * r;
  y = CY - cosf(a) * r;
}

static void hand(float deg, float back, float len, float r0, float r1, uint16_t col, bool shadow) {
  float ax, ay, bx, by;
  polar(deg + 180, back, ax, ay);
  polar(deg, len, bx, by);
  if (shadow) spr.drawWedgeLine(ax + 2, ay + 2, bx + 2, by + 2, r0, r1, TFT_BLACK, BLEND);
  spr.drawWedgeLine(ax, ay, bx, by, r0, r1, col, BLEND);
}

static void renderAnalog(const struct tm &tm, int ms) {
  char buf[32];
  background(tm.tm_hour);
  drawHeader();

  // face: coral rim, dark dial
  spr.fillSmoothCircle(CX, CY, R + 2, C_CLAW, BLEND);
  spr.fillSmoothCircle(CX, CY, R, C_CORAL, BLEND);
  spr.fillSmoothCircle(CX, CY, R - 3, C_FACE, BLEND);

  for (int i = 0; i < 60; i++) {
    float x0, y0, x1, y1;
    if (i % 5 == 0) {
      bool quarter = i % 15 == 0;
      polar(i * 6, R - (quarter ? 15 : 12), x0, y0);
      polar(i * 6, R - 7, x1, y1);
      spr.drawWideLine(x0, y0, x1, y1, quarter ? 3.0f : 2.0f, quarter ? C_SAND : C_PEACH, BLEND);
    } else {
      polar(i * 6, R - 8, x0, y0);
      spr.fillSmoothCircle((int)roundf(x0), (int)roundf(y0), 1, dimmed(C_SAND, 110), BLEND);
    }
  }

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_CORAL);
  spr.drawString("TTGO", CX, CY - 22, 1);

  // date window at 6 o'clock
  snprintf(buf, sizeof buf, "%s %d", DAYS3[tm.tm_wday], tm.tm_mday);
  int ww = spr.textWidth(buf, 1) + 8;
  spr.fillSmoothRoundRect(CX - ww / 2, CY + 17, ww, 13, 3, C_PLUM, BLEND);
  spr.setTextColor(C_SAND);
  spr.drawString(buf, CX, CY + 24, 1);

  float sec = tm.tm_sec + ms / 1000.0f;
  float mn = tm.tm_min + sec / 60.0f;
  float hr = (tm.tm_hour % 12) + mn / 60.0f;
  hand(hr * 30, 6, R * 0.50f, 3.5f, 2.0f, C_PEACH, true);
  hand(mn * 6, 8, R * 0.80f, 2.6f, 1.2f, TFT_WHITE, true);
  hand(sec * 6, 14, R * 0.88f, 0.9f, 0.7f, C_CORAL, false);
  float tx, ty;
  polar(sec * 6 + 180, 12, tx, ty);
  spr.fillSmoothCircle((int)roundf(tx), (int)roundf(ty), 3, C_CORAL, BLEND);   // counterweight
  spr.fillSmoothCircle(CX, CY, 5, C_CORAL, BLEND);
  spr.fillSmoothCircle(CX, CY, 2, C_TEAL, BLEND);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_PEACH);
  snprintf(buf, sizeof buf, "%s %d %s", DAYS[tm.tm_wday], tm.tm_mday, MON3[tm.tm_mon]);
  drawFit(buf, 67, 164, 2);
  snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  spr.setTextColor(C_TEAL);
  spr.drawString(buf, 67, 184, 2);
}

// ---------------- time unknown ----------------
static void wifiState(char *buf, size_t n, uint16_t &col) {
  if (bleConnected()) { snprintf(buf, n, "Phone connected"); col = C_TEAL; return; }
  if (!wifiEnabled()) { snprintf(buf, n, "WiFi is off"); col = C_GREY; return; }
  if (wifiConnected()) { snprintf(buf, n, "WiFi OK, asking NTP..."); col = C_TEAL; return; }
  const char *ssid = wifiSavedSsid();
  if (!ssid[0]) { snprintf(buf, n, "WiFi on, no network"); col = C_SAND; return; }
  const char *why = wifiFailReason();
  if (why[0]) snprintf(buf, n, "%s", why);
  else snprintf(buf, n, "Joining %.13s...", ssid);
  col = C_SAND;
}

static void renderUnknown(uint32_t now) {
  char buf[40];
  background(0);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_PEACH);
  drawFit(clockZoneCity(clockZone()), 67, 4, 2);

  // ghost "--:--" with breathing dashes
  uint8_t a = 90 + (uint8_t)(150 * triWave(now - openedAt, 1800));
  uint16_t on = tft.alphaBlend(a, C_CORAL, TFT_BLACK), off = tft.alphaBlend(26, C_CORAL, TFT_BLACK);
  const int W = 22, H = 40, T = 5, Y = 28;
  drawDigit(8, Y, W, H, T, -1, on, off);
  drawDigit(35, Y, W, H, T, -1, on, off);
  spr.fillSmoothCircle(67, Y + 12, 3, on, BLEND);
  spr.fillSmoothCircle(67, Y + 28, 3, on, BLEND);
  drawDigit(78, Y, W, H, T, -1, on, off);
  drawDigit(105, Y, W, H, T, -1, on, off);

  bool waiting = wifiConnected() || bleConnected();
  drawTigo(67, 146, 56, waiting ? TIGO_IDLE : TIGO_SLEEP, now - openedAt, false);

  spr.setTextColor(TFT_WHITE);
  wrapCentred("Connect WiFi in Settings or sync from the companion page", 156, 1, 11);
  uint16_t col;
  wifiState(buf, sizeof buf, col);
  spr.setTextColor(col);
  drawFit(buf, 67, 192, 1);
}

// ---------------- zone toast ----------------
static void renderToast(const struct tm *tm) {
  char buf[32];
  int z = clockZone();
  spr.fillSmoothRoundRect(6, 80, 123, 74, 10, C_CORAL, BLEND);
  spr.fillSmoothRoundRect(8, 82, 119, 70, 9, C_FACE, BLEND);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_GREY);
  snprintf(buf, sizeof buf, "TIME ZONE %d/%d", z + 1, CLOCK_NUM_ZONES);
  spr.drawString(buf, 67, 88, 1);
  spr.setTextColor(TFT_WHITE);
  drawFit(clockZoneCity(z), 67, 100, 4, 115);
  spr.setTextColor(C_SAND);
  drawFit(clockZoneLabel(z), 67, 128, 1, 115);
  char off[12];
  clockFmtOffset(off, sizeof off);
  if (tm) snprintf(buf, sizeof buf, "%s  %02d:%02d", off, tm->tm_hour, tm->tm_min);
  else snprintf(buf, sizeof buf, "%s", off);
  spr.setTextColor(C_TEAL);
  spr.drawString(buf, 67, 139, 1);
}

static void render(uint32_t now) {
  struct tm tm;
  int ms = 0;
  bool known = nowLocal(&tm, &ms);
  if (!known) renderUnknown(now);
  else if (analog) renderAnalog(tm, ms);
  else renderDigital(tm, ms);
  if (toastOn) renderToast(known ? &tm : nullptr);
  drawToolFooter("zone", analog ? "digital" : "analog");
  spr.pushSprite(0, 0);
}

// ---------------- menu ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  int cy = y + 2;
  spr.fillRect(x - 2, cy - 15, 4, 3, c(C_TEAL));                  // crown
  spr.fillSmoothCircle(x, cy, 12, c(C_CORAL), TFT_BLACK);
  spr.fillSmoothCircle(x, cy, 10, c(C_FACE), c(C_CORAL));
  for (int i = 0; i < 12; i += 3) {
    float a = i * 30 * (float)DEG_TO_RAD;
    spr.drawPixel(x + (int)roundf(sinf(a) * 8), cy - (int)roundf(cosf(a) * 8), c(C_SAND));
  }
  // real time when known, else the classic 10:10; the seconds hand sweeps
  struct tm tm;
  int ms = 0;
  float hr = 10.17f, mn = 10, sec = (t % 6000) / 100.0f;
  if (nowLocal(&tm, &ms)) {
    hr = (tm.tm_hour % 12) + tm.tm_min / 60.0f;
    mn = tm.tm_min;
    if (t) sec = tm.tm_sec + ms / 1000.0f;
  }
  if (dim || !t) sec = 0;
  auto line = [&](float deg, float len, float w, uint16_t col) {
    float a = deg * (float)DEG_TO_RAD;
    spr.drawWideLine(x, cy, x + sinf(a) * len, cy - cosf(a) * len, w, c(col), BLEND);
  };
  line(hr * 30, 5, 2.0f, C_PEACH);
  line(mn * 6, 7.5f, 1.5f, TFT_WHITE);
  if (!dim) line(sec * 6, 8, 1.0f, C_CORAL);
  spr.fillSmoothCircle(x, cy, 1, c(C_TEAL), BLEND);
}

void menuInfo(char *buf, size_t n) {
  struct tm tm;
  if (localTimeNow(&tm)) snprintf(buf, n, "%02d:%02d %s", tm.tm_hour, tm.tm_min, clockZoneCity(clockZone()));
  else snprintf(buf, n, wifiConnected() ? "Syncing..." : "Needs WiFi");
}

// ---------------- loop ----------------
void begin() {
  analog = prefs.getBool(PREF_ANALOG, false);
  pressedL = pressedR = false;
  toastOn = false;
  lastInput = openedAt = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;

  if (bL.stable && pressedL && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) {
    pressedL = false;
    returnToMenu();
    return;
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    setClockZone((clockZone() + 1) % CLOCK_NUM_ZONES, true);
    toastOn = true;
    toastAt = now;
    dirty = true;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    analog = !analog;
    prefs.putBool(PREF_ANALOG, analog);
    dirty = true;
  }
  if (toastOn && (int32_t)(now - toastAt) >= (int32_t)TOAST_MS) {
    toastOn = false;
    dirty = true;
  }

  if (idleTooLong(now, lastInput)) {
    pressedL = pressedR = false;
    goToSleep();
    return;
  }

  uint32_t frame = analog && timeIsSet() ? FRAME_ANALOG_MS : FRAME_DIGITAL_MS;
  if ((int32_t)(now - lastDraw) >= (int32_t)frame) dirty = true;
  if (dirty) {
    lastDraw = now;
    render(now);
    dirty = false;
  }
}

}  // namespace clockapp

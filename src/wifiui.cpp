// WiFi screen (opened from Settings): status, on/off, scan & connect, WPS, forget.
//
//   Lists  : Tap L next (hold to repeat) · Tap R choose
//   Password entry (character wheel):
//            Tap L next char · Hold L previous (repeats) · Tap R type it
//            Hold R jump to the next group (abc / ABC / 123 / #+= / done: OK DEL ESC)
//   Connecting / WPS : Tap L cancel

#include "common.h"
#include "themeui.h"
#include <WiFi.h>

namespace wifiui {

enum Screen { MAIN, SCAN, PASS, CONNECT, WPS };
enum MainRow { M_TOGGLE, M_SCAN, M_WPS, M_FORGET, M_BACK, NUM_MAIN };
static const int MAX_NETS = 16;
static const uint32_t CONNECT_TIMEOUT_MS = 20000, WPS_TIMEOUT_MS = 125000;

static Screen screen = MAIN;
static int row = 0, scanIdx = 0, scanScroll = 0, nNets = 0;
static char netSsid[MAX_NETS][33];
static int8_t netRssi[MAX_NETS];
static bool netLocked[MAX_NETS];
static bool scanning = false;
static char ssid[33], pass[64];
static int passLen = 0, wheel = 0;
static uint32_t phaseAt = 0, lastInput = 0, lastAnim = 0;
static bool connectOk = false, connectFailed = false;
static bool dirty = true, pressedL = false, pressedR = false;

// ---------------- password wheel ----------------
static const char *GROUPS[] = {
  "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789",
  " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
};
static const char *GROUP_NAME[] = {"abc", "ABC", "123", "#+=", "done"};
static const int NUM_GROUPS = 4;
enum Action { A_OK, A_DEL, A_ESC, NUM_ACTIONS };
static const char *ACTION_NAME[NUM_ACTIONS] = {"OK", "DEL", "ESC"};

static int wheelSize() {
  int n = NUM_ACTIONS;
  for (int g = 0; g < NUM_GROUPS; g++) n += strlen(GROUPS[g]);
  return n;
}
// item i -> group (NUM_GROUPS = actions) and a char or action index
static int itemGroup(int i, int *k) {
  for (int g = 0; g < NUM_GROUPS; g++) {
    int len = strlen(GROUPS[g]);
    if (i < len) { *k = i; return g; }
    i -= len;
  }
  *k = i;
  return NUM_GROUPS;
}
static int groupStart(int g) {
  int i = 0;
  for (int k = 0; k < g && k < NUM_GROUPS; k++) i += strlen(GROUPS[k]);
  return i;
}
static void itemLabel(int i, char *buf, size_t n) {
  int k, g = itemGroup(i, &k);
  if (g == NUM_GROUPS) snprintf(buf, n, "%s", ACTION_NAME[k]);
  else if (GROUPS[g][k] == ' ') snprintf(buf, n, "spc");
  else snprintf(buf, n, "%c", GROUPS[g][k]);
}

// ---------------- helpers ----------------
static void footer(const char *l, const char *r) { drawThemedFooter(l, r); }

static void rowBox(int y, int h, bool sel) { drawSelBox(6, y, 123, h, 6, sel); }

// theme colours (Original: the classic greys / green / cyan)
static uint16_t dimCol() { return theme().textDim; }
static uint16_t softCol() { return themeSoft(); }
static uint16_t okCol() { return theme().ok; }
static uint16_t titleCol() { return theme().neon ? theme().accent : TFT_CYAN; }

static void bars(int x, int y, int rssi, uint16_t on) {   // 4 signal bars, bottom at y
  int lvl = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : rssi > -90 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    if (i < lvl) spr.fillRect(x + i * 4, y - h, 3, h, on);
    else spr.drawRect(x + i * 4, y - h, 3, h, dimCol());
  }
}

static void lockIcon(int x, int y, uint16_t col) {
  spr.drawRoundRect(x + 1, y, 5, 5, 2, col);
  spr.fillRect(x, y + 3, 7, 5, col);
}

static void fitText(const char *s, int x, int y, int maxW, int font) {
  // draws s, cutting it with ".." when wider than maxW
  char buf[40];
  strncpy(buf, s, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  if (spr.textWidth(buf, font) <= maxW) { spr.drawString(buf, x, y, font); return; }
  int n = strlen(buf);
  while (n > 0) {
    buf[--n] = 0;
    char t[44];
    snprintf(t, sizeof t, "%s..", buf);
    if (spr.textWidth(t, font) <= maxW) { spr.drawString(t, x, y, font); return; }
  }
}

static void spinner(int cx, int cy, uint32_t now, uint16_t col) {
  for (int k = 0; k < 8; k++) {
    float a = k * PI / 4 + (now / 90) * PI / 4;
    uint16_t c = themeFade(40 + k * 27, col);
    spr.fillCircle(cx + (int)(cosf(a) * 12), cy + (int)(sinf(a) * 12), 2, c);
  }
}

// ---------------- screens ----------------
static void renderMain() {
  char buf[40];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(titleCol());
  spr.drawString("WIFI", 67, 4, 4);

  // status card
  spr.fillRoundRect(6, 30, 123, 54, 6, theme().panel);
  if (theme().neon) drawThemedBox(6, 30, 123, 54, 6, theme().frame, false);
  spr.setTextDatum(TL_DATUM);
  if (!wifiEnabled()) {
    spr.setTextColor(dimCol());
    spr.drawString("WiFi is off", 12, 36, 2);
    if (wifiSavedSsid()[0]) { spr.drawString("saved:", 12, 56, 1); fitText(wifiSavedSsid(), 12, 66, 112, 1); }
  } else if (wifiConnected()) {
    spr.setTextColor(okCol());
    fitText(WiFi.SSID().c_str(), 12, 34, 90, 2);
    bars(108, 48, wifiRssi(), okCol());
    spr.setTextColor(theme().text);
    wifiIp(buf, sizeof buf);
    spr.drawString(buf, 12, 54, 2);
    spr.setTextColor(softCol());
    snprintf(buf, sizeof buf, "%d dBm", wifiRssi());
    spr.drawString(buf, 12, 72, 1);
  } else {
    spr.setTextColor(TFT_ORANGE);
    spr.drawString(wifiSavedSsid()[0] ? "Connecting..." : "Not connected", 12, 36, 2);
    spr.setTextColor(softCol());
    if (wifiSavedSsid()[0]) fitText(wifiSavedSsid(), 12, 56, 112, 2);
    else spr.drawString("Scan or use WPS", 12, 56, 2);
  }

  static const char *LABEL[NUM_MAIN] = {"WiFi", "Scan networks", "WPS connect", "Forget network", "Back"};
  for (int i = 0; i < NUM_MAIN; i++) {
    int y = 90 + i * 25;
    bool sel = i == row;
    rowBox(y, 21, sel);
    spr.setTextDatum(i == M_BACK ? TC_DATUM : TL_DATUM);
    spr.setTextColor(sel ? theme().text : dimCol());
    spr.drawString(LABEL[i], i == M_BACK ? 67 : 14, y + 3, 2);
    spr.setTextDatum(TR_DATUM);
    if (i == M_TOGGLE) {
      spr.setTextColor(!sel ? dimCol() : wifiEnabled() ? okCol() : softCol());
      spr.drawString(wifiEnabled() ? "On" : "Off", 121, y + 3, 2);
    } else if (i == M_SCAN || i == M_WPS) {
      spr.setTextColor(sel ? theme().keyHint : dimCol());
      spr.drawString(">", 121, y + 3, 2);
    }
  }
  footer("next", row == M_TOGGLE ? "toggle" : row == M_BACK ? "back" : row == M_FORGET ? "forget" : "open");
}

static const int NET_LIST_Y = 30, NET_LIST_H = 186, ITEM_PITCH = 24;

static void renderScan(uint32_t now) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(titleCol());
  spr.drawString("NETWORKS", 67, 6, 2);
  if (scanning) {
    spinner(67, 110, now, titleCol());
    spr.setTextColor(softCol());
    spr.drawString("Scanning...", 67, 140, 2);
    footer("back", "");
    return;
  }
  int total = nNets + 2;                           // + Rescan + Back
  spr.setViewport(0, NET_LIST_Y, 135, NET_LIST_H);
  for (int i = 0; i < total; i++) {
    int y = 2 + i * ITEM_PITCH - scanScroll;
    if (y + 20 < 0 || y >= NET_LIST_H) continue;
    bool sel = i == scanIdx;
    rowBox(y, 21, sel);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(sel ? theme().text : softCol());
    if (i < nNets) {
      fitText(netSsid[i], 12, y + 3, 82, 2);
      if (netLocked[i]) lockIcon(98, y + 6, sel ? theme().keyHint : dimCol());
      bars(108, y + 16, netRssi[i], sel ? okCol() : dimCol());
    } else {
      spr.setTextDatum(TC_DATUM);
      spr.drawString(i == nNets ? "Rescan" : "Back", 67, y + 3, 2);
    }
  }
  spr.resetViewport();
  if (!nNets) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(dimCol());
    spr.drawString("no network found", 67, 96, 2);
  }
  footer("next", scanIdx < nNets ? "join" : scanIdx == nNets ? "scan" : "back");
}

static void renderPass() {
  char buf[16];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(titleCol());
  spr.drawString("PASSWORD", 67, 4, 2);
  spr.setTextColor(softCol());
  spr.setTextDatum(TC_DATUM);
  char nm[40];
  snprintf(nm, sizeof nm, "%s", ssid);
  spr.setTextDatum(TL_DATUM);
  fitText(nm, 8, 22, 119, 1);

  // what has been typed so far (the end of it, if long), shown in clear
  spr.fillRoundRect(4, 34, 127, 26, 4, theme().panel);
  const char *shown = pass;
  while (*shown && spr.textWidth(shown, 2) > 112) shown++;
  spr.setTextColor(theme().text);
  spr.drawString(shown, 8, 39, 2);
  int cx = 8 + spr.textWidth(shown, 2);
  spr.fillRect(cx + 1, 52, 7, 2, okCol());
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(dimCol());
  snprintf(buf, sizeof buf, "%d", passLen);
  spr.drawString(buf, 129, 62, 1);

  // the wheel: current item in a big box, neighbours either side
  int k, g = itemGroup(wheel, &k);
  spr.setTextDatum(TC_DATUM);
  for (int gi = 0; gi <= NUM_GROUPS; gi++) {       // group tabs
    int x = 4 + gi * 26;
    bool on = gi == g;
    spr.fillRoundRect(x, 76, 24, 14, 3, on ? titleCol() : theme().neon ? theme().panel : 0x2104);
    spr.setTextColor(on ? TFT_BLACK : softCol());
    spr.drawString(GROUP_NAME[gi], x + 12, 79, 1);
  }
  int n = wheelSize();
  for (int d = -2; d <= 2; d++) {
    if (!d) continue;
    itemLabel((wheel + d + n) % n, buf, sizeof buf);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(abs(d) == 1 ? softCol() : dimCol());
    spr.drawString(buf, 67 + d * 26 + (d > 0 ? 8 : -8), 124, 2);
  }
  spr.fillRoundRect(46, 100, 43, 48, 6, g == NUM_GROUPS ? 0x8200 : theme().neon ? theme().panel : 0x2945);
  drawThemedBox(46, 100, 43, 48, 6, theme().frameSel, true);
  itemLabel(wheel, buf, sizeof buf);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(theme().text);
  spr.drawString(buf, 67, 125, strlen(buf) > 1 ? 2 : 4);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(dimCol());
  spr.drawString("hold L: previous", 67, 160, 1);
  spr.drawString("hold R: next group", 67, 170, 1);
  spr.setTextColor(softCol());
  spr.drawString("or use WPS: no typing", 67, 190, 1);
  footer("next", g == NUM_GROUPS ? ACTION_NAME[k] : "type");
}

static void renderConnect(uint32_t now) {
  char buf[40];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(titleCol());
  spr.drawString(screen == WPS ? "WPS" : "CONNECT", 67, 6, 4);
  spr.setTextColor(theme().text);
  if (screen == WPS && wifiWpsState() == WPS_RUNNING) {
    spinner(67, 80, now, titleCol());
    spr.drawString("Press the WPS button", 67, 110, 2);
    spr.drawString("on your router", 67, 128, 2);
    uint32_t left = (WPS_TIMEOUT_MS - min(WPS_TIMEOUT_MS, now - phaseAt)) / 1000;
    spr.setTextColor(softCol());
    snprintf(buf, sizeof buf, "%lus", (unsigned long)left);
    spr.drawString(buf, 67, 154, 2);
    footer("cancel", "");
    return;
  }
  if (connectOk) {
    spr.setTextColor(okCol());
    spr.drawString("Connected!", 67, 70, 4);
    spr.setTextColor(theme().text);
    spr.setTextDatum(TL_DATUM);
    fitText(WiFi.SSID().c_str(), 10, 110, 115, 2);
    wifiIp(buf, sizeof buf);
    spr.drawString(buf, 10, 132, 2);
    footer("ok", "ok");
    return;
  }
  if (connectFailed) {
    spr.setTextColor(TFT_RED);
    spr.drawString("Failed", 67, 70, 4);
    spr.setTextColor(softCol());
    const char *why = screen == WPS && wifiWpsState() == WPS_TIMEOUT ? "no WPS router answered"
                    : screen == WPS && wifiWpsState() == WPS_FAILED  ? "WPS was refused"
                    : wifiFailReason()[0] ? wifiFailReason() : "timed out";
    spr.drawString(why, 67, 110, 2);
    footer("back", "retry");
    return;
  }
  spinner(67, 80, now, titleCol());
  spr.drawString("Joining", 67, 110, 2);
  spr.setTextDatum(TL_DATUM);
  fitText(screen == WPS ? "router from WPS" : ssid, 10, 130, 115, 2);
  footer("cancel", "");
}

static void render(uint32_t now) {
  spr.fillSprite(theme().bg);
  switch (screen) {
    case MAIN: renderMain(); break;
    case SCAN: renderScan(now); break;
    case PASS: renderPass(); break;
    default: renderConnect(now); break;
  }
  spr.pushSprite(0, 0);
}

// ---------------- logic ----------------
static int scanScrollTarget() {
  int total = nNets + 2;
  int maxScroll = max(0, total * ITEM_PITCH - 2 - NET_LIST_H);
  int top = scanIdx * ITEM_PITCH, bottom = top + 24;
  int t = scanScroll;
  if (bottom > t + NET_LIST_H) t = bottom - NET_LIST_H;
  if (top < t) t = top;
  return constrain(t, 0, maxScroll);
}

static void startScan() {
  screen = SCAN;
  scanning = true;
  nNets = scanIdx = scanScroll = 0;
  wifiStartScan();
}

static void collectScan(int n) {
  // strongest first, one entry per name, hidden networks left out
  int order[40];
  int m = min(n, 40);
  for (int i = 0; i < m; i++) order[i] = i;
  for (int i = 1; i < m; i++)
    for (int j = i; j > 0 && WiFi.RSSI(order[j]) > WiFi.RSSI(order[j - 1]); j--) {
      int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
    }
  nNets = 0;
  for (int i = 0; i < m && nNets < MAX_NETS; i++) {
    String s = WiFi.SSID(order[i]);
    if (!s.length()) continue;
    bool dup = false;
    for (int k = 0; k < nNets; k++) if (!strcmp(netSsid[k], s.c_str())) dup = true;
    if (dup) continue;
    strncpy(netSsid[nNets], s.c_str(), 32);
    netSsid[nNets][32] = 0;
    netRssi[nNets] = WiFi.RSSI(order[i]);
    netLocked[nNets] = WiFi.encryptionType(order[i]) != WIFI_AUTH_OPEN;
    nNets++;
  }
  wifiScanFree();
}

static void startConnect() {
  screen = CONNECT;
  connectOk = connectFailed = false;
  phaseAt = millis();
  wifiConnect(ssid, pass);
}

static void startWps() {
  screen = WPS;
  connectOk = connectFailed = false;
  phaseAt = millis();
  wifiStartWps();
}

static void backToMain() {
  if (screen == WPS) wifiStopWps();
  screen = MAIN;
  pressedL = pressedR = false;
  dirty = true;
}

void open() {
  screen = MAIN;
  row = 0;
  pressedL = pressedR = false;
  lastInput = millis();
  dirty = true;
}

// one loop pass; returns false when the user leaves back to Settings
bool update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) { pressedL = true; bL.nextRepeat = now + 380; }
  if (eR == 1) pressedR = true;
  bool tapL = eL == 2 && pressedL && !bL.consumed;
  bool tapR = eR == 2 && pressedR && !bR.consumed;
  if (eL == 2) pressedL = false;
  if (eR == 2) pressedR = false;

  switch (screen) {
    case MAIN:
      if (tapL) { row = (row + 1) % NUM_MAIN; dirty = true; }
      if (tapR) {
        if (row == M_BACK) return false;
        if (row == M_TOGGLE) setWifiEnabled(!wifiEnabled());
        else if (row == M_SCAN) startScan();
        else if (row == M_WPS) startWps();
        else wifiForget();
        dirty = true;
      }
      break;

    case SCAN: {
      if (scanning) {
        int n = wifiScanResult();
        if (n >= 0 || n == -2) { scanning = false; if (n > 0) collectScan(n); dirty = true; }
        if (tapL) { wifiScanFree(); backToMain(); }
        break;
      }
      int total = nNets + 2;
      if (pressedL && bL.stable && (int32_t)(now - bL.nextRepeat) >= 0) {
        bL.nextRepeat = now + 150;
        bL.consumed = true;
        scanIdx = (scanIdx + 1) % total;
        dirty = true;
      }
      if (tapL) { scanIdx = (scanIdx + 1) % total; dirty = true; }
      if (tapR) {
        if (scanIdx == nNets) startScan();
        else if (scanIdx == nNets + 1) backToMain();
        else {
          strcpy(ssid, netSsid[scanIdx]);
          pass[0] = 0;
          passLen = 0;
          if (netLocked[scanIdx]) { screen = PASS; wheel = 0; }
          else startConnect();
        }
        dirty = true;
      }
      scanScroll = scanScrollTarget();
      break;
    }

    case PASS: {
      int n = wheelSize();
      // hold L: step backwards, faster the longer it is held
      if (pressedL && bL.stable && (int32_t)(now - bL.nextRepeat) >= 0) {
        uint32_t held = now - bL.t0;
        bL.nextRepeat = now + (held > 1500 ? 60 : 140);
        bL.consumed = true;
        wheel = (wheel + n - 1) % n;
        dirty = true;
      }
      if (pressedR && bR.stable && !bR.consumed && (int32_t)(now - bR.t0) >= 450) {
        bR.consumed = true;                         // jump to the next group
        int k, g = itemGroup(wheel, &k);
        wheel = g >= NUM_GROUPS ? 0 : groupStart(g + 1);
        dirty = true;
      }
      if (tapL) { wheel = (wheel + 1) % n; dirty = true; }
      if (tapR) {
        int k, g = itemGroup(wheel, &k);
        if (g < NUM_GROUPS) {
          if (passLen < (int)sizeof pass - 1) { pass[passLen++] = GROUPS[g][k]; pass[passLen] = 0; }
        } else if (k == A_DEL) {
          if (passLen) pass[--passLen] = 0;
        } else if (k == A_ESC) {
          screen = SCAN;
        } else startConnect();
        dirty = true;
      }
      break;
    }

    default: {                                     // CONNECT / WPS
      if (screen == WPS && wifiWpsState() == WPS_RUNNING) {
        if (tapL) { backToMain(); break; }
        if (now - phaseAt > WPS_TIMEOUT_MS) { wifiStopWps(); connectFailed = true; dirty = true; }
        break;
      }
      if (screen == WPS && (wifiWpsState() == WPS_FAILED || wifiWpsState() == WPS_TIMEOUT)) connectFailed = true;
      if (!connectOk && !connectFailed) {
        if (wifiConnected()) { connectOk = true; wifiNoteConnected(); dirty = true; }
        else if ((int32_t)(now - phaseAt) > (int32_t)(screen == WPS ? WPS_TIMEOUT_MS : CONNECT_TIMEOUT_MS)) { connectFailed = true; dirty = true; }
        if (tapL) { backToMain(); break; }
      } else if (connectOk) {
        if (tapL || tapR) backToMain();
      } else {
        if (tapL) backToMain();
        if (tapR) { if (screen == WPS) startWps(); else startConnect(); dirty = true; }
      }
      break;
    }
  }

  // nothing should put the board to sleep while it is talking to a router
  bool busy = scanning || ((screen == CONNECT || screen == WPS) && !connectOk && !connectFailed);
  if (!busy && idleTooLong(now, lastInput)) goToSleep();

  bool animating = busy || screen == MAIN;          // spinner, live status
  if (animating && (int32_t)(now - lastAnim) >= (screen == MAIN ? 500 : 60)) { lastAnim = now; dirty = true; }
  if (dirty) {
    render(now);
    dirty = false;
  }
  return true;
}

}  // namespace wifiui

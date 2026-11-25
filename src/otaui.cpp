// OTA screen (opened from Settings): wireless firmware updates with ArduinoOTA.
// Uploads are accepted only while this screen is open (no password: leaving
// the screen stops the OTA server). From the PC:
//   pio run -e ota -t upload        (hostname ttgo-arcade.local, or the IP shown)
//
//   No WiFi   : Tap R turn WiFi on · Tap L / Hold L back
//   Waiting   : Tap L / Hold L back
//   Uploading : buttons ignored (the upload runs inside ArduinoOTA.handle())
//   Error     : Tap R try again · Tap L / Hold L back
// The board never goes to sleep while this screen is open. An upload goes into
// the other app slot, where the Big Games cartridge lives: it is forgotten
// (cart.h) and can be downloaded again afterwards.

#include "common.h"
#include "tigo.h"
#include "cart.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "ble.h"

namespace otaui {

enum State { S_NOWIFI, S_WAIT, S_UPLOAD, S_DONE, S_ERROR };
static const char *OTA_HOST = "ttgo-arcade";
static const uint16_t OTA_PORT = 3232;
static const uint32_t FRAME_MS = 50, PROGRESS_FRAME_MS = 120;

static State state = S_NOWIFI;
static bool otaRunning = false, configured = false, sleepOff = false;
static bool pressedL = false, pressedR = false;
static uint32_t stateAt = 0, lastFrame = 0, lastProgressFrame = 0;
static uint32_t upDone = 0, upTotal = 0;
static char errText[32] = "";
static char ipText[20] = "-";

// ---------------- helpers ----------------
static uint16_t errCol() { return theme().neon ? 0xF8B2 : TFT_RED; }

static void title() {
  const Theme &th = theme();
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.accent);
  spr.drawString("OTA update", 67, 6, 2);
  spr.drawFastHLine(8, 26, 119, th.frame);
}

static void footer(const char *l, const char *r) {
  const Theme &th = theme();
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  if (l) spr.drawString("L", 10, 222, 2);
  if (r) spr.drawString("R", 70, 222, 2);
  spr.setTextColor(th.text);
  if (l) spr.drawString(l, 22, 222, 2);
  if (r) spr.drawString(r, 82, 222, 2);
}

static void centred(const char *s, int y, int font, uint16_t col) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(col);
  spr.drawString(s, 67, y, font);
}

// shortens to fit 135 px in font 1 (22 chars)
static void centredFit(const char *s, int y, uint16_t col) {
  char buf[23];
  snprintf(buf, sizeof buf, "%s", s);
  centred(buf, y, 1, col);
}

static void setState(State s, uint32_t now) {
  state = s;
  stateAt = now;
  lastFrame = 0;                                       // redraw right away
}

// ---------------- OTA server ----------------
static void render(uint32_t now);

static const char *otaErrorName(ota_error_t e) {
  switch (e) {
    case OTA_AUTH_ERROR:    return "auth failed";
    case OTA_BEGIN_ERROR:   return "no room / bad image";
    case OTA_CONNECT_ERROR: return "connect failed";
    case OTA_RECEIVE_ERROR: return "receive failed";
    case OTA_END_ERROR:     return "check failed";
    default:                return "unknown error";
  }
}

static void configure() {
  if (configured) return;                              // setters only work before the first begin()
  configured = true;
  ArduinoOTA.setHostname(OTA_HOST);
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.onStart([]() {
    dbg("ota: upload started\n");
    cartForget();                                      // it is written over the cartridge's slot
    upDone = 0;
    lastProgressFrame = 0;
    setState(S_UPLOAD, millis());
  });
  // handle() blocks for the whole upload, so the screen is drawn from here
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    upDone = done;
    upTotal = total;
    uint32_t now = millis();
    if (lastProgressFrame == 0 || done >= total || (int32_t)(now - lastProgressFrame) >= (int32_t)PROGRESS_FRAME_MS) {
      lastProgressFrame = now ? now : 1;
      render(now);
    }
  });
  ArduinoOTA.onEnd([]() {
    dbg("ota: done, rebooting\n");
    cartForget();                                      // the cartridge's slot now holds this launcher
    tigo::save(millis(), true);                        // keep Tigo's needs across the reboot
    setState(S_DONE, millis());
    render(millis());                                  // ArduinoOTA restarts the board right after
  });
  ArduinoOTA.onError([](ota_error_t e) {
    snprintf(errText, sizeof errText, "%s", otaErrorName(e));
    dbg("ota: error %d (%s) %s\n", (int)e, errText, Update.errorString());
    setState(S_ERROR, millis());
  });
}

static void startOta(uint32_t now) {
  configure();
  wifiIp(ipText, sizeof ipText);
  // Bluetooth pauses (it holds heap and needs modem sleep on), then modem sleep
  // can go off: uploads crawl with it
  bleSuspend(true);
  sleepOff = true;
  WiFi.setSleep(false);
  ArduinoOTA.begin();
  otaRunning = true;
  dbg("ota: listening on %s.local (%s):%u\n", OTA_HOST, ipText, OTA_PORT);
  setState(S_WAIT, now);
}

static void stopOta() {
  if (!otaRunning) return;
  ArduinoOTA.end();
  otaRunning = false;
  if (sleepOff && wifiConnected()) WiFi.setSleep(true);
  sleepOff = false;
  bleSuspend(false);
  dbg("ota: stopped\n");
}

// ---------------- drawing ----------------
static void drawNoWifi(uint32_t now) {
  const Theme &th = theme();
  drawTigo(67, 128, 52, TIGO_SAD, now, false);
  if (!wifiEnabled()) {
    centred("WiFi is off", 146, 2, th.text);
    centred("Turn it on to update", 168, 1, th.textDim);
    centred("over the air.", 180, 1, th.textDim);
    footer("back", "WiFi on");
  } else if (!wifiSavedSsid()[0]) {
    centred("No network saved", 146, 2, th.text);
    centred("Set it up first in", 168, 1, th.textDim);
    centred("Settings > WiFi", 180, 1, th.accent2);
    footer("back", nullptr);
  } else {
    char dots[4] = "...";
    dots[(now / 400) % 4] = 0;
    char buf[24];
    snprintf(buf, sizeof buf, "Connecting%s", dots);
    centred(buf, 146, 2, th.text);
    centredFit(wifiSavedSsid(), 168, th.accent2);
    const char *why = wifiFailReason();
    if (why[0]) centredFit(why, 182, errCol());
    footer("back", nullptr);
  }
}

static void drawWait(uint32_t now) {
  const Theme &th = theme();
  // address panel
  drawThemedBox(6, 34, 123, 44, 6, th.frame, true);
  centred(ipText, 40, 2, th.accent);
  char host[24];
  snprintf(host, sizeof host, "%s.local", OTA_HOST);
  centred(host, 60, 1, th.accent2);

  // Tigo strolls back and forth while he waits
  float f = triWave(now - stateAt, 8000);
  int x = 30 + (int)(f * 75);
  bool left = ((now - stateAt) % 8000) >= 4000;
  tigo::drawShadow(x, 146, 40);
  drawTigo(x, 146, 40, TIGO_WALK, now, left);

  char dots[4] = "...";
  dots[(now / 400) % 4] = 0;
  char buf[28];
  snprintf(buf, sizeof buf, "Waiting for upload%s", dots);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.text);
  spr.drawString(buf, 6, 156, 1);                      // left-aligned: the dots don't shift it

  centred("on the PC run:", 174, 1, th.textDim);
  centred("pio run -e ota", 186, 1, th.ok);
  centred("-t upload", 196, 1, th.ok);
  footer("back", nullptr);
}

static void drawUpload(uint32_t now) {
  const Theme &th = theme();
  int pct = upTotal ? (int)((uint64_t)upDone * 100 / upTotal) : 0;
  centred("Uploading", 36, 2, th.text);

  // Tigo walks along with the progress
  const int bx = 8, by = 132, bw = 119, bh = 14;
  int tx = constrain(bx + bw * pct / 100, bx + 14, bx + bw - 14);
  drawTigo(tx, by - 6, 34, TIGO_WALK, now, false);

  drawThemedBox(bx, by, bw, bh, 5, th.frameSel, true);
  int fw = (bw - 4) * pct / 100;
  if (fw > 0) spr.fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, th.accent);

  char buf[24];
  snprintf(buf, sizeof buf, "%d%%", pct);
  centred(buf, 154, 4, th.accent);
  snprintf(buf, sizeof buf, "%u / %u KB", (unsigned)(upDone / 1024), (unsigned)(upTotal / 1024));
  centred(buf, 184, 1, th.textDim);
  centred("Don't turn it off", 204, 1, th.keyHint);
}

static void drawDone(uint32_t now) {
  const Theme &th = theme();
  drawTigo(67, 128, 56, TIGO_HAPPY, now, false);
  centred("Done!", 146, 4, th.ok);
  centred("Rebooting...", 176, 2, th.text);
}

static void drawError(uint32_t now) {
  const Theme &th = theme();
  drawTigo(67, 120, 50, TIGO_SAD, now, false);
  centred("Upload failed", 136, 2, errCol());
  centredFit(errText, 158, th.text);
  if (Update.hasError()) centredFit(Update.errorString(), 172, th.textDim);
  footer("back", "retry");
}

static void render(uint32_t now) {
  spr.fillSprite(theme().bg);
  title();
  switch (state) {
    case S_NOWIFI: drawNoWifi(now); break;
    case S_WAIT:   drawWait(now); break;
    case S_UPLOAD: drawUpload(now); break;
    case S_DONE:   drawDone(now); break;
    case S_ERROR:  drawError(now); break;
  }
  spr.pushSprite(0, 0);
  lastFrame = now ? now : 1;
}

// ---------------- entry points ----------------
void open() {
  uint32_t now = millis();
  pressedL = pressedR = false;
  errText[0] = 0;
  upDone = upTotal = 0;
  if (wifiConnected()) startOta(now);
  else setState(S_NOWIFI, now);
  render(now);
}

static bool leave() {
  stopOta();
  pressedL = pressedR = false;
  return false;
}

bool update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  // Hold L leaves at once, its release is then ignored by the caller
  if (pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS) return leave();
  bool tapL = eL == 2 && pressedL;
  bool tapR = eR == 2 && pressedR;
  if (eL == 2) pressedL = false;
  if (eR == 2) pressedR = false;

  switch (state) {
    case S_NOWIFI:
      if (tapL) return leave();
      if (tapR && !wifiEnabled()) setWifiEnabled(true);
      if (wifiConnected()) startOta(now);
      break;
    case S_WAIT:
      if (tapL) return leave();
      if (!wifiConnected()) {                          // lost the network: wait for it again
        stopOta();
        setState(S_NOWIFI, now);
        break;
      }
      ArduinoOTA.handle();                             // blocks during an upload (drawn by onProgress)
      break;
    case S_UPLOAD:                                     // only seen if an upload ended oddly
      setState(S_WAIT, now);
      break;
    case S_DONE:                                       // ArduinoOTA reboots; nothing to do
      break;
    case S_ERROR:
      if (tapL) return leave();
      if (tapR) setState(wifiConnected() ? S_WAIT : S_NOWIFI, now);
      if (wifiConnected()) ArduinoOTA.handle();        // still listening: a new upload may start
      else { stopOta(); setState(S_NOWIFI, now); }
      break;
  }

  if (lastFrame == 0 || (int32_t)(now - lastFrame) >= (int32_t)FRAME_MS) render(now);
  return true;
}

}  // namespace otaui

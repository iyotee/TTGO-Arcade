// WiFi on demand: saved-network reconnect, scanning, manual connect and WPS
// (push button).
//
// The radio and driver take ~50 KB of heap, which Bluetooth and HTTPS need too,
// so WiFi is no longer up all the time. The setting ("wifi_on") only says
// whether WiFi is allowed; the station runs while someone holds it
// (wifiAcquire / wifiRelease: the update check, the NTP sync, the WiFi, OTA,
// Updates and cartridge screens, a new network from the companion page). When
// the last holder lets go it stays up for a short grace (back-to-back users
// don't reconnect), then the driver is shut down (WiFi.mode(WIFI_OFF) frees
// its heap). A holder the user opened a screen for may pause Bluetooth
// (bleSuspend, the saved setting is untouched) when the heap is too short for
// WiFi to start; it comes back once no such holder is left.
//
// The ESP32 WiFi stack keeps the last network's credentials in its own NVS area;
// we only remember whether WiFi is allowed ("wifi_on") and the network name
// for display ("wifi_ssid"). Everything here runs on the main loop.

#include "common.h"
#include "ble.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wps.h>

namespace wifisvc {

static const uint32_t GRACE_MS = 20000;            // up this long after the last release
static const uint32_t RETRY_MS = 5000;             // a start that failed (memory) is retried this often
// the driver plus a connection take ~50 KB: leave the rest of the board some room
static const uint32_t NEED_FREE_HEAP = 65000, NEED_BLOCK = 30000;
static const uint32_t CONNECT_HOLD_MS = 60000;     // wifiConnect(): long enough to test the network
static const int MAX_HOLDERS = 8;
static const int REASON_NO_MEMORY = -1;            // lastReason: the driver could not start

struct Holder { const char *who; uint32_t until; bool timed, user; };
static Holder holders[MAX_HOLDERS];
static int numHolders = 0;

static bool enabled = false, started = false, eventsOn = false, bleRoom = false;
static uint32_t idleSince = 0, failAt = 0;         // idleSince: grace start, 0 = held
static volatile int wpsState = WPS_IDLE;
static volatile int lastReason = 0;                // last disconnect reason from the stack
static esp_wps_config_t wpsConfig;
static char savedSsid[33] = "";
// credentials given while the driver was down: used when it starts
static char pendSsid[33] = "", pendPass[65] = "";
static bool pending = false;

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      // the router handed over its credentials: stop WPS and join it
      esp_wifi_wps_disable();
      wpsState = WPS_GOT_CREDS;
      WiFi.begin();
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      esp_wifi_wps_disable();
      wpsState = WPS_FAILED;
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      esp_wifi_wps_disable();
      wpsState = WPS_TIMEOUT;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastReason = info.wifi_sta_disconnected.reason;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      lastReason = 0;
      break;
    default:
      break;
  }
}

static int find(const char *who) {
  for (int i = 0; i < numHolders; i++)
    if (!strcmp(holders[i].who, who)) return i;
  return -1;
}

static bool userHeld() {
  for (int i = 0; i < numHolders; i++)
    if (holders[i].user) return true;
  return false;
}

// driver up in station mode, joining the saved (or just given) network
static bool startStack(bool force) {
  if (started) return true;
  uint32_t heap = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  if (!force && (heap < NEED_FREE_HEAP || block < NEED_BLOCK)) {
    if (lastReason != REASON_NO_MEMORY) dbg("wifi: not started, heap %u (block %u) too low\n", heap, block);
    lastReason = REASON_NO_MEMORY;
    failAt = millis() | 1;
    return false;
  }
  if (!eventsOn) {                                 // the handler list outlives the driver: once only
    WiFi.onEvent(onWifiEvent);
    eventsOn = true;
  }
  WiFi.setHostname("ttgo-arcade");
  if (!WiFi.mode(WIFI_STA)) {
    dbg("wifi: driver did not start, heap %u\n", ESP.getFreeHeap());
    WiFi.mode(WIFI_OFF);
    lastReason = REASON_NO_MEMORY;
    failAt = millis() | 1;
    return false;
  }
  WiFi.setAutoReconnect(true);
  started = true;
  failAt = 0;
  if (lastReason == REASON_NO_MEMORY) lastReason = 0;
  dbg("wifi: on, heap %u -> %u\n", heap, ESP.getFreeHeap());
  return true;
}

static void join() {
  if (pending) {
    pending = false;
    if (pendPass[0]) WiFi.begin(pendSsid, pendPass);   // also stores them for next time
    else WiFi.begin(pendSsid);
    memset(pendPass, 0, sizeof pendPass);
  } else {
    WiFi.begin();                                  // the saved network, if any
  }
}

static void stopStack(const char *why) {
  if (!started) return;
  uint32_t heap = ESP.getFreeHeap();
  if (wpsState == WPS_RUNNING) { esp_wifi_wps_disable(); wpsState = WPS_IDLE; }
  WiFi.disconnect(true, false);                    // radio off, keep the credentials
  WiFi.mode(WIFI_OFF);                             // driver deinit: its heap comes back
  started = false;
  idleSince = 0;
  dbg("wifi: off (%s), heap %u -> %u\n", why, heap, ESP.getFreeHeap());
}

// start / stop as the setting and the holders want
static void apply(uint32_t now) {
  for (int i = 0; i < numHolders;) {               // timed holds run out
    if (holders[i].timed && (int32_t)(now - holders[i].until) >= 0) {
      dbg("wifi: %s hold ran out\n", holders[i].who);
      holders[i] = holders[--numHolders];
    } else {
      i++;
    }
  }
  bool want = enabled && numHolders > 0;
  if (want) {
    idleSince = 0;
    if (!started && (!failAt || (int32_t)(now - failAt) >= (int32_t)RETRY_MS)) {
      if (startStack(false)) join();
    }
  } else if (started) {
    if (!enabled) stopStack("switched off");
    else if (!idleSince) idleSince = now | 1;
    else if ((int32_t)(now - idleSince) >= (int32_t)GRACE_MS) stopStack("idle");
  }

  // a screen the user opened for WiFi: Bluetooth gives way while memory is short
  bool user = want && userHeld();
  if (user && !started && lastReason == REASON_NO_MEMORY && bleRunning() && !bleSuspended() &&
      !bleInstallActive()) {
    dbg("wifi: pausing Bluetooth to make room\n");
    bleSuspend(true);
    bleRoom = true;
    failAt = 0;                                    // retry at the next pass
  } else if (!user && bleRoom) {
    bleRoom = false;
    bleSuspend(false);
  }
}

static void hold(const char *who, bool timed, uint32_t ms, bool user) {
  uint32_t now = millis();
  int i = find(who);
  if (i < 0) {
    if (numHolders >= MAX_HOLDERS) { dbg("wifi: too many holders, %s ignored\n", who); return; }
    i = numHolders++;
    holders[i].who = who;
    holders[i].user = false;
    dbg("wifi: held by %s (%d)\n", who, numHolders);
  }
  holders[i].timed = timed;
  holders[i].until = now + ms;
  holders[i].user = holders[i].user || user;
  failAt = 0;                                      // someone asked: try right away
  apply(now);
}

}  // namespace wifisvc

using namespace wifisvc;

// ---------------- holders ----------------
void wifiAcquire(const char *who, bool user) { hold(who, false, 0, user); }
void wifiAcquireFor(const char *who, uint32_t ms) { hold(who, true, ms, false); }

void wifiRelease(const char *who) {
  int i = find(who);
  if (i < 0) return;
  holders[i] = holders[--numHolders];
  dbg("wifi: released by %s (%d)\n", who, numHolders);
  apply(millis());
}

bool wifiHeldBy(const char *who) { return find(who) >= 0; }

bool wifiIdleOff() {
  if (!started || (enabled && numHolders > 0)) return false;
  stopStack("memory wanted");
  return true;
}

void wifiService(uint32_t now) {
  static bool wasUp = false;
  apply(now);
  bool up = wifiConnected();
  if (up && !wasUp) {
    wifiNoteConnected();                           // e.g. a network sent from the phone
    dbg("wifi: connected, heap %u\n", ESP.getFreeHeap());
  }
  wasUp = up;
}

bool wifiBusy() { return started; }

int wifiStatus() {
  if (!enabled) return WIFI_ST_OFF;
  if (!started) return WIFI_ST_IDLE;
  return WiFi.status() == WL_CONNECTED ? WIFI_ST_CONNECTED : WIFI_ST_CONNECTING;
}

const char *wifiStatusText() {
  static const char *const TEXT[] = {"Off", "Idle", "Connecting", "Connected"};
  return TEXT[wifiStatus()];
}

bool wifiLowMemory() { return lastReason == REASON_NO_MEMORY; }

// ---------------- setting ----------------
void initWifi() {
  enabled = prefs.getBool("wifi_on", false);
  if (prefs.isKey("wifi_ssid")) prefs.getString("wifi_ssid", savedSsid, sizeof savedSsid);
  // nothing starts here: the first holder brings the radio up
}

bool wifiEnabled() { return enabled; }

void setWifiEnabled(bool on) {
  if (on != enabled) prefs.putBool("wifi_on", on);
  enabled = on;
  failAt = 0;
  apply(millis());
}

bool wifiConnected() { return started && WiFi.status() == WL_CONNECTED; }
const char *wifiSavedSsid() { return savedSsid; }
int wifiRssi() { return wifiConnected() ? WiFi.RSSI() : -127; }

void wifiIp(char *buf, size_t n) {
  if (!wifiConnected()) { snprintf(buf, n, "-"); return; }
  IPAddress ip = WiFi.localIP();
  snprintf(buf, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

// remember the name of the network we just joined (main loop only)
void wifiNoteConnected() {
  if (!started) return;
  String s = WiFi.SSID();
  if (s.length() && strcmp(s.c_str(), savedSsid)) {
    strncpy(savedSsid, s.c_str(), sizeof savedSsid - 1);
    savedSsid[sizeof savedSsid - 1] = 0;
    prefs.putString("wifi_ssid", savedSsid);
  }
}

const char *wifiFailReason() {
  switch (lastReason) {
    case 0: return "";
    case REASON_NO_MEMORY: return "not enough memory";
    case 2: case 15: case 202: case 204: return "wrong password?";
    case 201: return "network not found";
    default: return "connection lost";
  }
}

// ---------------- scan, connect, WPS (the WiFi screen holds the radio) ----------------
void wifiStartScan() {
  if (!enabled) setWifiEnabled(true);
  if (!started) return;                            // no memory: wifiScanResult() says failed
  // a pending reconnect attempt makes scans fail; scanning while connected is fine
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
}

int wifiScanResult() { return started ? WiFi.scanComplete() : WIFI_SCAN_FAILED; }   // -1 running, -2 failed, else count
void wifiScanFree() { if (started) WiFi.scanDelete(); }

void wifiConnect(const char *ssid, const char *pass) {
  if (!enabled) setWifiEnabled(true);
  lastReason = 0;
  snprintf(pendSsid, sizeof pendSsid, "%s", ssid);
  snprintf(pendPass, sizeof pendPass, "%s", pass ? pass : "");
  pending = true;
  if (started) {
    WiFi.disconnect(false, false);
    delay(50);
    join();
  }
  wifiAcquireFor("connect", CONNECT_HOLD_MS);      // up for a while, so the new network gets tested
}

void wifiStartWps() {
  if (!enabled) setWifiEnabled(true);
  lastReason = 0;
  if (!started) { wpsState = WPS_FAILED; return; }
  WiFi.disconnect(false, false);
  delay(50);
  memset(&wpsConfig, 0, sizeof wpsConfig);
  wpsConfig.wps_type = WPS_TYPE_PBC;
  strcpy(wpsConfig.factory_info.manufacturer, "LILYGO");
  strcpy(wpsConfig.factory_info.model_number, "T-Display");
  strcpy(wpsConfig.factory_info.model_name, "TTGO Arcade");
  strcpy(wpsConfig.factory_info.device_name, "ttgo-arcade");
  wpsState = WPS_RUNNING;
  if (esp_wifi_wps_enable(&wpsConfig) != ESP_OK || esp_wifi_wps_start(0) != ESP_OK) {
    esp_wifi_wps_disable();
    wpsState = WPS_FAILED;
  }
}

int wifiWpsState() { return wpsState; }

void wifiStopWps() {
  if (wpsState == WPS_RUNNING) esp_wifi_wps_disable();
  wpsState = WPS_IDLE;
}

void wifiForget() {
  bool was = started;
  if (!started && !startStack(true)) return;       // the driver erases its own copy
  WiFi.disconnect(false, true);                    // erase the stored credentials
  savedSsid[0] = 0;
  pending = false;
  prefs.remove("wifi_ssid");
  if (!was) stopStack("forgot");
}

void wifiShutdown() {
  if (started) WiFi.mode(WIFI_OFF);
  started = false;
}

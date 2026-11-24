// WiFi: saved-network reconnect, scanning, manual connect and WPS (push button).
//
// The ESP32 WiFi stack keeps the last network's credentials in its own NVS area;
// we only remember whether WiFi is switched on ("wifi_on") and the network name
// for display ("wifi_ssid").

#include "common.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wps.h>

static bool enabled = false, started = false;
static volatile int wpsState = WPS_IDLE;
static volatile int lastReason = 0;                // last disconnect reason from the stack
static esp_wps_config_t wpsConfig;
static char savedSsid[33] = "";

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

static void startStack() {
  if (started) return;
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname("ttgo-arcade");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  started = true;
}

void initWifi() {
  enabled = prefs.getBool("wifi_on", false);
  if (prefs.isKey("wifi_ssid")) prefs.getString("wifi_ssid", savedSsid, sizeof savedSsid);
  if (enabled) {
    startStack();
    WiFi.begin();                                  // the saved network, if any
  }
}

bool wifiEnabled() { return enabled; }

void setWifiEnabled(bool on) {
  enabled = on;
  prefs.putBool("wifi_on", on);
  if (on) {
    startStack();
    WiFi.begin();
  } else if (started) {
    WiFi.disconnect(true, false);                  // radio off, keep the credentials
    WiFi.mode(WIFI_OFF);
    started = false;
  }
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
    case 2: case 15: case 202: case 204: return "wrong password?";
    case 201: return "network not found";
    default: return "connection lost";
  }
}

void wifiStartScan() {
  if (!enabled) setWifiEnabled(true);
  // a pending reconnect attempt makes scans fail; scanning while connected is fine
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
}

int wifiScanResult() { return WiFi.scanComplete(); }   // -1 running, -2 failed, else count
void wifiScanFree() { WiFi.scanDelete(); }

void wifiConnect(const char *ssid, const char *pass) {
  if (!enabled) setWifiEnabled(true);
  lastReason = 0;
  WiFi.disconnect(false, false);
  delay(50);
  if (pass && pass[0]) WiFi.begin(ssid, pass);     // also stores them for next boot
  else WiFi.begin(ssid);
}

void wifiStartWps() {
  if (!enabled) setWifiEnabled(true);
  WiFi.disconnect(false, false);
  delay(50);
  memset(&wpsConfig, 0, sizeof wpsConfig);
  wpsConfig.wps_type = WPS_TYPE_PBC;
  strcpy(wpsConfig.factory_info.manufacturer, "LILYGO");
  strcpy(wpsConfig.factory_info.model_number, "T-Display");
  strcpy(wpsConfig.factory_info.model_name, "TTGO Arcade");
  strcpy(wpsConfig.factory_info.device_name, "ttgo-arcade");
  lastReason = 0;
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
  if (!started) startStack();
  WiFi.disconnect(false, true);                    // erase the stored credentials
  savedSsid[0] = 0;
  prefs.remove("wifi_ssid");
  if (!enabled) { WiFi.mode(WIFI_OFF); started = false; }
}

void wifiShutdown() {
  if (started) WiFi.mode(WIFI_OFF);
}

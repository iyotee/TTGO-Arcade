// Bluetooth LE (NimBLE): one GATT server named "TTGO-Arcade" with
//   - HID over GATT (keyboard + consumer control), bonded "just works", plus the
//     battery service: the Remote tool's camera shutter / music / slide keys;
//   - the companion service, used by docs/companion.html (Web Bluetooth).
// No controls: this is a service. Settings switches it ("ble_on"); the Remote
// tool keeps it running while open (bleUse) even when the setting is off.
//
// Companion service 7160c0de-5e11-4c0d-8a7c-7160a2cade00
//   ...cade01 message  write  UTF-8 text <= 60 chars -> Tigo's speech bubble (bleTakeMessage)
//   ...cade02 treat    write  1 byte n (1..255, empty = 1) treats -> bleTakeTreats
//   ...cade03 wifi     write  "ssid\npassword", encrypted link required (pairs) -> wifiConnect
//   ...cade04 time     write  4-byte little-endian Unix epoch (UTC) -> settimeofday
//   ...cade05 status   read + notify  JSON {"bat":87,"mv":3950,"usb":false,"wifi":true,
//                                     "ssid":"Home","ip":"192.168.1.20","ble":true}
// BLE callbacks run on the NimBLE host task: they only fill mailboxes, the main
// loop (bleService) does the work (WiFi, clock, logs).

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <WiFi.h>
#include <sys/time.h>
#include "common.h"
#include "clock.h"
#include "ble.h"

namespace {

const char *DEV_NAME = "TTGO-Arcade";
const char *UUID_SVC = "7160c0de-5e11-4c0d-8a7c-7160a2cade00";
const char *UUID_MSG = "7160c0de-5e11-4c0d-8a7c-7160a2cade01";
const char *UUID_TREAT = "7160c0de-5e11-4c0d-8a7c-7160a2cade02";
const char *UUID_WIFI = "7160c0de-5e11-4c0d-8a7c-7160a2cade03";
const char *UUID_TIME = "7160c0de-5e11-4c0d-8a7c-7160a2cade04";
const char *UUID_STATUS = "7160c0de-5e11-4c0d-8a7c-7160a2cade05";

const uint8_t RID_KEYBOARD = 1, RID_MEDIA = 2;
const uint16_t APPEARANCE_KEYBOARD = 0x03C1;
const int MSG_MAX = 60;
const uint32_t USE_TIMEOUT_MS = 3000, STATUS_MS = 5000, BATTERY_MS = 20000;

// keyboard (report 1: modifiers, reserved, 6 keys; LED output) + consumer bits (report 2)
const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,            // Generic Desktop, Keyboard, Collection
  0x85, RID_KEYBOARD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,            //   modifier bits
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,            //   reserved byte
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,   // LEDs
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,            //   LED padding
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, //   6 key codes
  0xC0,
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,            // Consumer Control, Collection
  0x85, RID_MEDIA,
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x07,
  0x09, 0xB5, 0x09, 0xB6, 0x09, 0xB7, 0x09, 0xCD, // next, previous, stop, play/pause
  0x09, 0xE2, 0x09, 0xE9, 0x09, 0xEA,            // mute, volume up (= camera shutter), volume down
  0x81, 0x02,
  0x95, 0x01, 0x81, 0x01,                        //   padding bit
  0xC0,
};

bool enabled = false, running = false;
uint32_t usedAt = 0;
bool used = false;

NimBLEServer *server = nullptr;
NimBLEHIDDevice *hid = nullptr;
NimBLECharacteristic *keyInput = nullptr, *mediaInput = nullptr, *statusChr = nullptr;
uint32_t lastStatus = 0, lastBattery = 0;
int batteryPct = -1, batteryMv = 0;

// ---------------- mailboxes (host task -> main loop) ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile int conns = 0;
volatile uint16_t hidConn = 0xFFFF;              // connection that subscribed to key reports
volatile bool hidReady = false;
volatile bool evConnect = false, evDisconnect = false, evAuth = false;
char msgBox[MSG_MAX + 1];
bool msgWaiting = false;
int treats = 0;
char wifiSsid[33], wifiPass[65];
bool wifiWaiting = false;
uint32_t epochBox = 0;
bool epochWaiting = false;

// UTF-8 -> printable ASCII for the device fonts: accents folded (e -> e), quotes
// straightened, anything else dropped
void foldAscii(const uint8_t *s, size_t len, char *out, size_t n) {
  static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < n;) {
    uint8_t c = s[i];
    if (c < 0x80) {
      out[o++] = (c >= 32 && c < 127) ? (char)c : ' ';
      i++;
      continue;
    }
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 1; k <= extra && i + k < len; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += 1 + extra;
    if (cp >= 0xC0 && cp <= 0xFF) out[o++] = LATIN1[cp - 0xC0];
    else if (cp == 0x2018 || cp == 0x2019) out[o++] = '\'';
    else if (cp == 0x201C || cp == 0x201D) out[o++] = '"';
    else if (cp == 0x2013 || cp == 0x2014) out[o++] = '-';
  }
  out[o] = 0;
}

// ---------------- callbacks (NimBLE host task) ----------------
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    conns = conns + 1;
    evConnect = true;
    // room for a second peer (e.g. the companion page while the phone holds HID)
    if (conns < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    if (conns > 0) conns = conns - 1;
    if (desc->conn_handle == hidConn || conns == 0) { hidReady = false; hidConn = 0xFFFF; }
    evDisconnect = true;
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override { evAuth = desc->sec_state.encrypted; }
};

class InputCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *c, ble_gap_conn_desc *desc, uint16_t subValue) override {
    if (subValue) { hidReady = true; hidConn = desc->conn_handle; }
    else if (desc->conn_handle == hidConn) { hidReady = false; hidConn = 0xFFFF; }
  }
};

class MsgCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    NimBLEAttValue v = c->getValue();
    char tmp[MSG_MAX + 1];
    foldAscii(v.data(), v.length(), tmp, sizeof tmp);
    if (!tmp[0]) return;
    portENTER_CRITICAL(&mux);
    memcpy(msgBox, tmp, sizeof msgBox);
    msgWaiting = true;
    portEXIT_CRITICAL(&mux);
  }
};

class TreatCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    NimBLEAttValue v = c->getValue();
    int n = (v.length() && v.data()[0]) ? v.data()[0] : 1;
    portENTER_CRITICAL(&mux);
    treats = min(treats + n, 999);
    portEXIT_CRITICAL(&mux);
  }
};

class WifiCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    NimBLEAttValue v = c->getValue();
    const char *p = (const char *)v.data();
    size_t len = v.length(), nl = 0;
    while (nl < len && p[nl] != '\n') nl++;
    if (nl == 0 || nl > 32) return;                // no or too long a network name
    size_t plen = nl < len ? len - nl - 1 : 0;
    if (plen > 64) return;
    portENTER_CRITICAL(&mux);
    memcpy(wifiSsid, p, nl);
    wifiSsid[nl] = 0;
    memcpy(wifiPass, p + nl + 1, plen);
    wifiPass[plen] = 0;
    wifiWaiting = true;
    portEXIT_CRITICAL(&mux);
    c->setValue((const uint8_t *)"", 0);           // don't keep the password readable in RAM
  }
};

class TimeCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    NimBLEAttValue v = c->getValue();
    if (v.length() < 4) return;
    const uint8_t *d = v.data();
    portENTER_CRITICAL(&mux);
    epochBox = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
    epochWaiting = true;
    portEXIT_CRITICAL(&mux);
  }
};

// static: NimBLE doesn't own characteristic callbacks, and deinit deletes the server
ServerCB serverCB;
InputCB inputCB;
MsgCB msgCB;
TreatCB treatCB;
WifiCB wifiCB;
TimeCB timeCB;

// ---------------- status ----------------
void jsonStr(char *&o, char *end, const char *s) {
  // a JSON string, escaping quotes, backslashes and control chars
  if (o < end) *o++ = '"';
  for (; *s && o + 7 < end; s++) {
    uint8_t c = *s;
    if (c == '"' || c == '\\') { *o++ = '\\'; *o++ = c; }
    else if (c < 32) o += snprintf(o, end - o, "\\u%04x", c);
    else *o++ = c;
  }
  if (o < end) *o++ = '"';
  *o = 0;
}

void updateStatus(uint32_t now, bool readBattery) {
  if (!statusChr) return;
  if (readBattery || batteryPct < 0) {
    lastBattery = now;
    batteryMv = batteryMilliVolts();
    int pct = batteryPercent(batteryMv);
    if (pct != batteryPct && hid) {
      hid->setBatteryLevel(pct);
      hid->batteryLevel()->notify();
    }
    batteryPct = pct;
  }
  char buf[200], ip[20];
  char *o = buf, *end = buf + sizeof buf - 1;
  bool wc = wifiConnected();
  wifiIp(ip, sizeof ip);
  o += snprintf(o, end - o, "{\"bat\":%d,\"mv\":%d,\"usb\":%s,\"wifi\":%s,\"ssid\":", batteryPct, batteryMv,
                onUsbPower(batteryMv) ? "true" : "false", wc ? "true" : "false");
  jsonStr(o, end - 40, wc ? WiFi.SSID().c_str() : wifiSavedSsid());
  snprintf(o, end - o, ",\"ip\":\"%s\",\"on\":%s,\"ble\":true}", ip, wifiEnabled() ? "true" : "false");
  statusChr->setValue((const uint8_t *)buf, strlen(buf));
  statusChr->notify();
  lastStatus = now;
}

// ---------------- stack ----------------
void startStack() {
  if (running) return;
  uint32_t heapBefore = ESP.getFreeHeap();
  NimBLEDevice::init(DEV_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);          // bonding, no MITM ("just works"), secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(185);                                 // room for the status JSON in one notification

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB, false);

  hid = new NimBLEHIDDevice(server);
  hid->manufacturer()->setValue("TTGO-Arcade");
  hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t *)REPORT_MAP, sizeof REPORT_MAP);
  keyInput = hid->inputReport(RID_KEYBOARD);
  mediaInput = hid->inputReport(RID_MEDIA);
  hid->outputReport(RID_KEYBOARD);                           // LEDs: hosts write it, we ignore it
  keyInput->setCallbacks(&inputCB);
  mediaInput->setCallbacks(&inputCB);
  hid->setBatteryLevel(batteryPct >= 0 ? batteryPct : 100);
  hid->startServices();

  NimBLEService *svc = server->createService(UUID_SVC);
  svc->createCharacteristic(UUID_MSG, NIMBLE_PROPERTY::WRITE)->setCallbacks(&msgCB);
  svc->createCharacteristic(UUID_TREAT, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(&treatCB);
  svc->createCharacteristic(UUID_WIFI, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC)->setCallbacks(&wifiCB);
  svc->createCharacteristic(UUID_TIME, NIMBLE_PROPERTY::WRITE)->setCallbacks(&timeCB);
  statusChr = svc->createCharacteristic(UUID_STATUS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusChr->setValue("{}");
  svc->start();

  // advertising: flags + name + appearance + HID (16-bit) fit the 31 bytes; the
  // 128-bit companion UUID goes in the scan response (Web Bluetooth filters on it)
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(APPEARANCE_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  NimBLEAdvertisementData scanData;
  scanData.setCompleteServices(NimBLEUUID(UUID_SVC));
  adv->setScanResponseData(scanData);
  adv->start();

  conns = 0;
  hidReady = false;
  hidConn = 0xFFFF;
  running = true;
  batteryPct = -1;                                           // fresh reading at the next service pass
  dbg("ble: started, heap %u -> %u (min %u)\n", heapBefore, ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

void stopStack() {
  if (!running) return;
  if (server) {
    std::vector<uint16_t> peers = server->getPeerDevices();
    for (uint16_t h : peers) server->disconnect(h);
    if (!peers.empty()) delay(60);                           // let the disconnects go out
  }
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deinit(true);                                // also deletes the server and its services
  delete hid;
  hid = nullptr;
  server = nullptr;
  keyInput = mediaInput = statusChr = nullptr;
  running = false;
  conns = 0;
  hidReady = false;
  hidConn = 0xFFFF;
  dbg("ble: stopped, heap %u\n", ESP.getFreeHeap());
}

void apply() {
  if (enabled || used) startStack();
  else stopStack();
}

bool sendReport(NimBLECharacteristic *c, const uint8_t *data, size_t len) {
  if (!running || !hidReady || !c) return false;
  c->setValue(data, len);
  c->notify();
  return true;
}

}  // namespace

// ---------------- API (common.h) ----------------
void initBle() {
  enabled = prefs.getBool("ble_on", false);
  apply();
}

bool bleEnabled() { return enabled; }

void setBleEnabled(bool on) {
  enabled = on;
  prefs.putBool("ble_on", on);
  apply();
}

bool bleConnected() { return running && conns > 0; }

bool bleTakeMessage(char *buf, size_t n) {
  bool got = false;
  portENTER_CRITICAL(&mux);
  if (msgWaiting && n) {
    strncpy(buf, msgBox, n - 1);
    buf[n - 1] = 0;
    msgWaiting = false;
    got = true;
  }
  portEXIT_CRITICAL(&mux);
  return got;
}

int bleTakeTreats() {
  portENTER_CRITICAL(&mux);
  int t = treats;
  treats = 0;
  portEXIT_CRITICAL(&mux);
  return t;
}

void bleService(uint32_t now) {
  if (used && (int32_t)(now - usedAt) > (int32_t)USE_TIMEOUT_MS) {
    used = false;                                            // the tool that held it is gone
    apply();
  }
  if (!running) return;

  if (evConnect) {
    evConnect = false;
    dbg("ble: connected (%d)\n", (int)conns);
    updateStatus(now, batteryPct < 0);                       // fresh status for the companion's first read
  }
  if (evDisconnect) { evDisconnect = false; dbg("ble: disconnected (%d)\n", (int)conns); }
  if (evAuth) { evAuth = false; dbg("ble: link encrypted\n"); }

  char ssid[33], pass[65];
  bool doWifi = false, doTime = false;
  uint32_t epoch = 0;
  portENTER_CRITICAL(&mux);
  if (wifiWaiting) {
    memcpy(ssid, wifiSsid, sizeof ssid);
    memcpy(pass, wifiPass, sizeof pass);
    memset(wifiPass, 0, sizeof wifiPass);
    wifiWaiting = false;
    doWifi = true;
  }
  if (epochWaiting) { epoch = epochBox; epochWaiting = false; doTime = true; }
  portEXIT_CRITICAL(&mux);

  if (doWifi) {
    dbg("ble: wifi credentials for '%s'\n", ssid);
    wifiConnect(ssid, pass);
    memset(pass, 0, sizeof pass);
    lastStatus = now - STATUS_MS + 1500;                     // report the outcome soon
  }
  if (doTime && epoch > 1600000000UL) {
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    clockNoteSynced(CLOCK_SRC_PHONE);       // the clock app shows "phone 5m ago"
    dbg("ble: clock set from phone, epoch %lu\n", (unsigned long)epoch);
  }

  if (conns > 0 && (int32_t)(now - lastStatus) >= (int32_t)STATUS_MS)
    updateStatus(now, batteryPct < 0 || (int32_t)(now - lastBattery) >= (int32_t)BATTERY_MS);
}

// ---------------- API (ble.h) ----------------
void bleUse(uint32_t now) {
  usedAt = now;
  if (!used) {
    used = true;
    apply();
  }
}

void bleRelease() {
  if (!used) return;
  used = false;
  apply();
}

bool bleRunning() { return running; }
bool bleHidReady() { return running && hidReady; }

bool bleTapMedia(BleMedia key) {
  uint8_t down = 1 << key, up = 0;
  if (!sendReport(mediaInput, &down, 1)) return false;
  sendReport(mediaInput, &up, 1);
  return true;
}

bool bleTapKey(uint8_t usage, uint8_t modifiers) {
  uint8_t r[8] = {modifiers, 0, usage, 0, 0, 0, 0, 0};
  if (!sendReport(keyInput, r, sizeof r)) return false;
  memset(r, 0, sizeof r);
  sendReport(keyInput, r, sizeof r);
  return true;
}

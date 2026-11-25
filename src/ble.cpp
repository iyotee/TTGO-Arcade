// Bluetooth LE (NimBLE): one GATT server named "TTGO-Arcade" with
//   - HID over GATT (keyboard + consumer control), bonded "just works", plus the
//     battery service: the Remote tool's camera shutter / music / slide keys;
//   - the companion service, used by docs/companion.html (Web Bluetooth).
// No controls: this is a service. Settings switches it ("ble_on"); tools keep it
// running while open (bleUse) even when the setting is off; bleSuspend() stops it
// for heavy work (HTTPS downloads) without touching the setting.
//
// Companion service 7160c0de-5e11-4c0d-8a7c-7160a2cade00
//   ...cade01 message  write  UTF-8 text <= 60 chars -> Tigo's speech bubble (bleTakeMessage)
//   ...cade02 treat    write  1 byte n (1..255, empty = 1) treats -> bleTakeTreats
//   ...cade03 wifi     write  "ssid\npassword", encrypted link required (pairs) -> wifiConnect
//   ...cade04 time     write  4-byte little-endian Unix epoch (UTC) -> settimeofday
//   ...cade05 status   read + notify  JSON {"bat":87,"mv":3950,"usb":false,"wifi":true,
//                                     "ssid":"Home","ip":"192.168.1.20","on":true,"ble":true}
//   ...cade06 install  write + write without response + notify, encrypted link required:
//                      streams a cartridge image into the other app slot (cart.cpp).
//
// Install protocol (little-endian). Page -> board, first byte = opcode:
//   01 BEGIN   size u32, build u32, sha256[32]   (with response)  -> 81 READY or 8F ERROR
//   02 DATA    offset u32, bytes                 (without response), offset = bytes before it
//   03 END                                       (with response)  -> 83 DONE or 8F ERROR
//   04 ABORT                                                      -> 8F ERROR code 10
//   05 STATUS                                                     -> 85 STATUS
// Board -> page, notifications on the same characteristic:
//   81 READY   resume u32, window u32, chunk u16  send from 'resume', at most 'window' bytes
//                                                past the last ACK, 'chunk' data bytes per DATA
//   82 ACK     written u32                         bytes safely in flash (every 2 KB and at the end)
//   83 DONE                                        written, sha256 verified: installed
//   84 RESEND  offset u32                          a packet was missed or didn't fit: go back there
//   85 STATUS  state u8, received u32, written u32, size u32
//   8F ERROR   code u8, text                       the session is over (codes: ERR_* below)
// A BEGIN with the same size and sha256 while a session is open resumes it (the
// session survives a dropped link for 90 s): READY then says where to continue.
//
// Threading: NimBLE callbacks run on its host task. They only fill mailboxes and
// the install ring buffer; the main loop (bleService) does the work (WiFi, clock,
// flash). Everything the main loop wants to send (key reports, status, battery,
// install replies) goes through one event on NimBLE's own queue and is sent from
// the host task: NimBLE's characteristic code keeps its subscriber lists in plain
// std::vectors that the host task changes on (un)subscribe and disconnect, so
// calling notify() from the main loop could walk a vector being reallocated.
//
// Start / stop safety (a "crash in Remote" was traced to these): NimBLE's init
// aborts the board on any failed allocation or a controller left half up, so
// startStack() only runs with enough heap and an idle controller, and retries
// every few seconds otherwise (bleError() says why). WiFi gets modem sleep first
// (the WiFi driver aborts without it once Bluetooth runs). Both are idempotent;
// a stop while linked lets ble_hs_stop() drop the links itself, with advertising
// on disconnect turned off, and everything is forgotten only once deinit is done.

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <sys/time.h>
#include "common.h"
#include "clock.h"
#include "ble.h"

namespace {

const char *const DEV_NAME = "TTGO-Arcade";
const char *const UUID_SVC = "7160c0de-5e11-4c0d-8a7c-7160a2cade00";
const char *const UUID_MSG = "7160c0de-5e11-4c0d-8a7c-7160a2cade01";
const char *const UUID_TREAT = "7160c0de-5e11-4c0d-8a7c-7160a2cade02";
const char *const UUID_WIFI = "7160c0de-5e11-4c0d-8a7c-7160a2cade03";
const char *const UUID_TIME = "7160c0de-5e11-4c0d-8a7c-7160a2cade04";
const char *const UUID_STATUS = "7160c0de-5e11-4c0d-8a7c-7160a2cade05";
const char *const UUID_INSTALL = "7160c0de-5e11-4c0d-8a7c-7160a2cade06";

const uint8_t RID_KEYBOARD = 1, RID_MEDIA = 2;
const uint16_t APPEARANCE_KEYBOARD = 0x03C1;
const uint16_t NO_CONN = 0xFFFF;
const int MSG_MAX = 60;
const uint32_t USE_TIMEOUT_MS = 3000, STATUS_MS = 5000, BATTERY_MS = 20000;
// 247 = one ATT packet per 251-byte link-layer packet with data length extension
const uint16_t ATT_MTU = 247;
// NimBLE's init aborts the board (ESP_ERROR_CHECK, mempool asserts) when an
// allocation fails, so it is only started with this much heap to spare: the
// controller, host and our GATT server take ~40 KB, and WiFi, the sprite
// pushes and the HID reports still need some ~20 KB afterwards
const uint32_t MIN_FREE_HEAP = 60000, MIN_BLOCK = 16000;
const uint32_t LOW_HEAP_AFTER = 20000;           // below this after a start: say so in the log
const uint32_t RETRY_MS = 5000;                  // a failed start / stop is retried this often

// install
const uint32_t RING_SIZE = 8192;                 // received, not yet written to flash
const uint32_t ACK_STEP = 2048;
const uint32_t WRITE_BUDGET = 4096;              // flash bytes per loop pass, keeps the screen alive
const uint32_t RX_TIMEOUT_MS = 60000, ORPHAN_MS = 90000;
const int MIN_INSTALL_BATTERY = 15;              // % when not on USB
const uint8_t OP_BEGIN = 0x01, OP_DATA = 0x02, OP_END = 0x03, OP_ABORT = 0x04, OP_STATUS = 0x05;
const uint8_t EV_READY = 0x81, EV_ACK = 0x82, EV_DONE = 0x83, EV_RESEND = 0x84, EV_STATUS = 0x85, EV_ERROR = 0x8F;
enum InstErr : uint8_t {
  ERR_BUSY = 1, ERR_NO_MEMORY, ERR_NOT_ENCRYPTED, ERR_BAD_REQUEST, ERR_FLASH, ERR_WRITE,
  ERR_VERIFY, ERR_BATTERY, ERR_TIMEOUT, ERR_ABORTED, ERR_STOPPED,
};

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

// ---------------- stack state (main loop only) ----------------
bool enabled = false, running = false, used = false, suspended = false;
uint32_t usedAt = 0, failAt = 0;                 // failAt: last failed start / stop, 0 = none
const char *errText = "";

NimBLEServer *server = nullptr;
NimBLEHIDDevice *hid = nullptr;
NimBLECharacteristic *keyInput = nullptr, *mediaInput = nullptr, *statusChr = nullptr, *instChr = nullptr;
uint32_t lastStatus = 0, lastBattery = 0;
int batteryPct = -1, batteryMv = 0;

// ---------------- mailboxes (host task <-> main loop), guarded by mux ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile int conns = 0;
volatile uint16_t hidConn = NO_CONN;             // connection that subscribed to key reports
volatile uint8_t hidSubs = 0;                    // bit 0: keyboard report, bit 1: media report
volatile uint16_t compConn = NO_CONN;            // the companion page (subscribed to status)
volatile bool evConnect = false, evDisconnect = false, evAuth = false;
char msgBox[MSG_MAX + 1];
bool msgWaiting = false;
int treats = 0;
char wifiSsid[33], wifiPass[65];
bool wifiWaiting = false;
uint32_t epochBox = 0;
bool epochWaiting = false;

// main loop -> host task: sent by txHandler() on NimBLE's event queue
ble_npl_event txEv;
volatile bool txReady = false;                   // the event may be posted (stack up)

const int NUM_REPORTS = 8;
struct Report { uint8_t rid, len, data[8]; };
Report reports[NUM_REPORTS];
int repHead = 0, repCount = 0;

char statusBox[200];
size_t statusLen = 0;
bool statusWaiting = false;
int batteryBox = -1;

const int NUM_OUT = 8;
struct OutMsg { uint8_t len, data[40]; };
OutMsg outbox[NUM_OUT];
int outHead = 0, outCount = 0;

// ---------------- install session ----------------
// rxNext / flashed are offsets into the image: the ring holds bytes
// [flashed, rxNext) at offset % RING_SIZE. The host task appends (and only
// touches the free part), the main loop writes to flash and frees; both
// indexes and the state change under mux.
volatile uint8_t iState = BLE_INST_IDLE;
uint8_t *ring = nullptr;
uint32_t rxNext = 0, flashed = 0;
uint32_t iSize = 0, iBuild = 0;
uint8_t iSha[32];
uint16_t iConn = NO_CONN;
bool iOrphan = false, nackSent = false, nackWaiting = false;
uint32_t nackOff = 0;                            // offset of the packet that caused the last RESEND
bool endWaiting = false, abortWaiting = false, statusReqWaiting = false, encErrWaiting = false;
bool beginWaiting = false, badWaiting = false;
uint16_t beginConn = NO_CONN;
uint32_t beginSize = 0, beginBuild = 0;
uint8_t beginSha[32];
// main loop only
uint32_t holdUntil = 0;                          // keeps the stack up briefly after a session ends
bool holdOn = false;
uint32_t lastAck = 0, lastRxSeen = 0, lastRxAt = 0, stepAt = 0, orphanAt = 0;
bool orphanSeen = false;
char iMsg[28] = "";

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

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

// ---------------- host task: sending ----------------
char hostBuf[sizeof statusBox];                  // host task only

void txHandler(ble_npl_event *ev) {
  (void)ev;
  if (!txReady) return;
  for (;;) {                                     // key reports, in order
    Report r;
    bool got = false;
    portENTER_CRITICAL(&mux);
    if (repCount) {
      r = reports[repHead];
      repHead = (repHead + 1) % NUM_REPORTS;
      repCount--;
      got = true;
    }
    portEXIT_CRITICAL(&mux);
    if (!got) break;
    NimBLECharacteristic *c = r.rid == RID_KEYBOARD ? keyInput : mediaInput;
    if (c) {
      c->setValue(r.data, r.len);
      c->notify();
    }
  }

  int bat;
  size_t slen = 0;
  portENTER_CRITICAL(&mux);
  bat = batteryBox;
  batteryBox = -1;
  if (statusWaiting) {
    slen = statusLen;
    memcpy(hostBuf, statusBox, slen);
    statusWaiting = false;
  }
  portEXIT_CRITICAL(&mux);
  if (bat >= 0 && hid) {
    hid->setBatteryLevel((uint8_t)bat);
    hid->batteryLevel()->notify();
  }
  if (slen && statusChr) {
    statusChr->setValue((const uint8_t *)hostBuf, slen);
    statusChr->notify();
  }

  for (;;) {                                     // install replies
    OutMsg m;
    bool got = false;
    portENTER_CRITICAL(&mux);
    if (outCount) {
      m = outbox[outHead];
      outHead = (outHead + 1) % NUM_OUT;
      outCount--;
      got = true;
    }
    portEXIT_CRITICAL(&mux);
    if (!got) break;
    if (instChr) instChr->notify(m.data, m.len);
  }
}

// main loop: have the host task run txHandler (once, however often it's asked)
void kick() {
  if (txReady) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &txEv);
}

// ---------------- host task: callbacks ----------------
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    conns = conns + 1;
    evConnect = true;
    s->setDataLen(desc->conn_handle, 251);       // longer link-layer packets: faster installs
    // room for a second peer (e.g. the companion page while the phone holds HID)
    if (conns < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    portENTER_CRITICAL(&mux);
    if (conns > 0) conns = conns - 1;
    if (desc->conn_handle == hidConn || conns == 0) { hidSubs = 0; hidConn = NO_CONN; }
    if (desc->conn_handle == compConn || conns == 0) compConn = NO_CONN;
    if (desc->conn_handle == iConn) iOrphan = true;   // the install waits for the page to come back
    portEXIT_CRITICAL(&mux);
    evDisconnect = true;
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override { evAuth = desc->sec_state.encrypted; }
};

class InputCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *c, ble_gap_conn_desc *desc, uint16_t subValue) override {
    uint8_t bit = c == keyInput ? 1 : 2;
    portENTER_CRITICAL(&mux);
    if (subValue) { hidSubs |= bit; hidConn = desc->conn_handle; }
    else if (desc->conn_handle == hidConn) { hidSubs &= ~bit; if (!hidSubs) hidConn = NO_CONN; }
    portEXIT_CRITICAL(&mux);
  }
};

class StatusCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *c, ble_gap_conn_desc *desc, uint16_t subValue) override {
    if (subValue) compConn = desc->conn_handle;
    else if (desc->conn_handle == compConn) compConn = NO_CONN;
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
    treats = treats + n > 999 ? 999 : treats + n;
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
    portENTER_CRITICAL(&mux);
    epochBox = le32(v.data());
    epochWaiting = true;
    portEXIT_CRITICAL(&mux);
  }
};

class InstallCB : public NimBLECharacteristicCallbacks {
  // DATA: append to the ring if it is the next expected piece and fits; anything
  // else asks the page (once per gap) to go back to rxNext
  void data(uint16_t conn, const uint8_t *d, size_t len) {
    if (len < 6) return;
    uint32_t off = le32(d + 1);
    size_t n = len - 5;
    portENTER_CRITICAL(&mux);
    if (iState == BLE_INST_RECEIVING && ring && conn == iConn) {
      if (off == rxNext && off + n <= iSize && n <= RING_SIZE - (rxNext - flashed)) {
        uint32_t at = off % RING_SIZE, first = RING_SIZE - at < n ? RING_SIZE - at : n;
        memcpy(ring + at, d + 5, first);
        if (first < n) memcpy(ring, d + 5 + first, n - first);
        rxNext += n;
        nackSent = false;
      } else if (off < rxNext) {
        // a duplicate (sent again after a RESEND): already have it
      } else if (off + n > iSize) {
        badWaiting = true;
      } else if (!nackSent || off <= nackOff) {
        // a gap, or no room: resend from rxNext. Once per gap: packets already in
        // flight behind it are dropped quietly, but a gap in the resent run (the
        // page went back: off <= nackOff) asks again
        nackSent = nackWaiting = true;
        nackOff = off;
      }
    }
    portEXIT_CRITICAL(&mux);
  }

  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {
    NimBLEAttValue v = c->getValue();
    const uint8_t *d = v.data();
    size_t len = v.length();
    if (!len) return;
    if (!desc->sec_state.encrypted) { encErrWaiting = true; return; }   // WRITE_ENC already guards it
    switch (d[0]) {
      case OP_DATA:
        data(desc->conn_handle, d, len);
        break;
      case OP_BEGIN:
        if (len < 41) { badWaiting = true; break; }
        portENTER_CRITICAL(&mux);
        beginConn = desc->conn_handle;
        beginSize = le32(d + 1);
        beginBuild = le32(d + 5);
        memcpy(beginSha, d + 9, 32);
        beginWaiting = true;
        portEXIT_CRITICAL(&mux);
        break;
      case OP_END: endWaiting = true; break;
      case OP_ABORT: abortWaiting = true; break;
      case OP_STATUS: statusReqWaiting = true; break;
      default: badWaiting = true; break;
    }
  }
};

// static: NimBLE doesn't own characteristic callbacks, and deinit deletes the server
ServerCB serverCB;
InputCB inputCB;
StatusCB statusCB;
MsgCB msgCB;
TreatCB treatCB;
WifiCB wifiCB;
TimeCB timeCB;
InstallCB installCB;

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
    if (pct != batteryPct) {
      portENTER_CRITICAL(&mux);
      batteryBox = pct;
      portEXIT_CRITICAL(&mux);
    }
    batteryPct = pct;
  }
  char buf[sizeof statusBox], ip[20];
  char *o = buf, *end = buf + sizeof buf - 1;
  bool wc = wifiConnected();
  wifiIp(ip, sizeof ip);
  o += snprintf(o, end - o, "{\"bat\":%d,\"mv\":%d,\"usb\":%s,\"wifi\":%s,\"ssid\":", batteryPct, batteryMv,
                onUsbPower(batteryMv) ? "true" : "false", wc ? "true" : "false");
  jsonStr(o, end - 40, wc ? WiFi.SSID().c_str() : wifiSavedSsid());
  snprintf(o, end - o, ",\"ip\":\"%s\",\"on\":%s,\"ble\":true}", ip, wifiEnabled() ? "true" : "false");
  portENTER_CRITICAL(&mux);
  statusLen = strlen(buf);
  memcpy(statusBox, buf, statusLen);
  statusWaiting = true;
  portEXIT_CRITICAL(&mux);
  kick();
  lastStatus = now;
}

// ---------------- install (main loop) ----------------
void sendOut(const uint8_t *d, size_t n) {
  if (n > sizeof outbox[0].data) n = sizeof outbox[0].data;
  portENTER_CRITICAL(&mux);
  if (outCount < NUM_OUT) {
    OutMsg &m = outbox[(outHead + outCount) % NUM_OUT];
    m.len = n;
    memcpy(m.data, d, n);
    outCount++;
  }
  portEXIT_CRITICAL(&mux);
  kick();
}

void send32(uint8_t ev, uint32_t v) {
  uint8_t b[5] = {ev};
  put32(b + 1, v);
  sendOut(b, sizeof b);
}

void sendError(uint8_t code, const char *text) {
  uint8_t b[40] = {EV_ERROR, code};
  size_t n = strlen(text);
  if (n > sizeof b - 2) n = sizeof b - 2;
  memcpy(b + 2, text, n);
  sendOut(b, n + 2);
}

void sendReady(uint32_t resume, uint16_t conn) {
  uint16_t mtu = server ? server->getPeerMTU(conn) : 23;
  int chunk = (int)mtu - 3 - 5;                  // ATT write header, then our opcode + offset
  if (chunk < 16) chunk = 16;
  uint8_t b[12] = {EV_READY};
  put32(b + 1, resume);
  put32(b + 5, RING_SIZE);
  b[9] = chunk;
  b[10] = chunk >> 8;
  sendOut(b, 11);
  dbg("ble: install ready at %lu, chunk %d (mtu %u)\n", (unsigned long)resume, chunk, mtu);
}

void sendStatus() {
  uint8_t b[14] = {EV_STATUS, iState};
  portENTER_CRITICAL(&mux);
  put32(b + 2, rxNext);
  put32(b + 6, flashed);
  portEXIT_CRITICAL(&mux);
  put32(b + 10, iSize);
  sendOut(b, sizeof b);
}

void setMsg(const char *s) { snprintf(iMsg, sizeof iMsg, "%s", s); }

// leaves the session: frees the ring once the host task can no longer touch it
void closeSession(uint8_t state) {
  portENTER_CRITICAL(&mux);
  bool wasOpen = iState == BLE_INST_RECEIVING || iState == BLE_INST_CHECKING;
  iState = state;
  uint8_t *r = ring;
  ring = nullptr;
  portEXIT_CRITICAL(&mux);
  free(r);
  holdOn = true;                                 // let the last reply go out before a stop
  holdUntil = millis() + 3000;
  if (wasOpen && state != BLE_INST_DONE) cartInstallAbort();
  if (server && iConn != NO_CONN && !iOrphan) server->updateConnParams(iConn, 24, 48, 0, 500);   // relax the link again
}

void failInstall(uint8_t code, const char *text) {
  dbg("ble: install failed (%d) %s at %lu/%lu, heap %u\n", code, text, (unsigned long)flashed, (unsigned long)iSize,
      ESP.getFreeHeap());
  closeSession(BLE_INST_FAILED);
  setMsg(text);
  sendError(code, text);
}

bool installActive() {
  return iState == BLE_INST_PREPARING || iState == BLE_INST_RECEIVING || iState == BLE_INST_CHECKING;
}

void handleBegin(uint32_t now) {
  uint16_t conn;
  uint32_t size, build;
  uint8_t sha[32];
  portENTER_CRITICAL(&mux);
  beginWaiting = false;
  conn = beginConn;
  size = beginSize;
  build = beginBuild;
  memcpy(sha, beginSha, 32);
  portEXIT_CRITICAL(&mux);

  if (installActive()) {
    if (iState == BLE_INST_RECEIVING && size == iSize && !memcmp(sha, iSha, 32)) {
      portENTER_CRITICAL(&mux);                  // same image: carry on where it stopped
      iConn = conn;
      iOrphan = nackSent = nackWaiting = false;
      uint32_t resume = rxNext;
      portEXIT_CRITICAL(&mux);
      orphanSeen = false;
      lastRxAt = now;
      dbg("ble: install resumed at %lu\n", (unsigned long)resume);
      if (server) server->updateConnParams(conn, 6, 12, 0, 500);
      sendReady(resume, conn);
    } else {
      sendError(ERR_BUSY, "Another install is running");
    }
    return;
  }
  if (size == 0) { sendError(ERR_BAD_REQUEST, "Empty image"); return; }
  iSize = size;
  iBuild = build;
  memcpy(iSha, sha, 32);
  iConn = conn;
  iOrphan = false;
  rxNext = flashed = 0;
  iState = BLE_INST_PREPARING;                   // the slot is erased on a later pass, after the screen says so
  stepAt = now;
  setMsg("Preparing...");
  dbg("ble: install request %lu bytes, build %lu\n", (unsigned long)size, (unsigned long)build);
}

void prepare(uint32_t now) {
  int mv = batteryMilliVolts();
  if (!onUsbPower(mv) && batteryPercent(mv) < MIN_INSTALL_BATTERY) { failInstall(ERR_BATTERY, "Battery low: plug USB"); return; }
  uint8_t *r = (uint8_t *)malloc(RING_SIZE);
  if (!r) { failInstall(ERR_NO_MEMORY, "Low memory"); return; }
  char err[40] = "";
  dbg("ble: install begin, heap %u (block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (!cartInstallBegin(iSize, iSha, iBuild, err, sizeof err)) {
    free(r);
    iState = BLE_INST_FAILED;                    // nothing begun: no abort needed
    setMsg(err[0] ? err : "Can't start");
    sendError(ERR_FLASH, iMsg);
    dbg("ble: cartInstallBegin failed: %s\n", iMsg);
    return;
  }
  portENTER_CRITICAL(&mux);
  ring = r;
  rxNext = flashed = 0;
  nackSent = nackWaiting = endWaiting = false;
  iState = BLE_INST_RECEIVING;
  portEXIT_CRITICAL(&mux);
  lastAck = lastRxSeen = 0;
  lastRxAt = now;
  orphanSeen = false;
  setMsg("Receiving...");
  if (server) server->updateConnParams(iConn, 6, 12, 0, 500);   // 7.5-15 ms intervals while streaming
  sendReady(0, iConn);
}

void installService(uint32_t now) {
  if (encErrWaiting) { encErrWaiting = false; sendError(ERR_NOT_ENCRYPTED, "Pair the phone first"); }
  if (badWaiting) {
    badWaiting = false;
    if (installActive()) failInstall(ERR_BAD_REQUEST, "Bad data");
    else sendError(ERR_BAD_REQUEST, "Bad request");
  }
  if (beginWaiting) handleBegin(now);
  if (abortWaiting) {
    abortWaiting = false;
    if (installActive()) failInstall(ERR_ABORTED, "Cancelled");
  }
  if (statusReqWaiting) { statusReqWaiting = false; sendStatus(); }

  if (iState == BLE_INST_PREPARING) {
    if ((int32_t)(now - stepAt) >= 60) prepare(now);
    return;
  }
  if (iState == BLE_INST_CHECKING) {
    if ((int32_t)(now - stepAt) < 60) return;
    char err[40] = "";
    bool ok = cartInstallEnd(err, sizeof err);
    portENTER_CRITICAL(&mux);
    iState = ok ? BLE_INST_DONE : BLE_INST_FAILED;   // ended either way: no abort
    uint8_t *r = ring;
    ring = nullptr;
    portEXIT_CRITICAL(&mux);
    free(r);
    holdOn = true;
    holdUntil = millis() + 3000;
    if (ok) {
      setMsg("Installed");
      uint8_t b[1] = {EV_DONE};
      sendOut(b, 1);
      dbg("ble: install done, build %lu\n", (unsigned long)iBuild);
    } else {
      setMsg(err[0] ? err : "Check failed");
      sendError(ERR_VERIFY, iMsg);
      dbg("ble: install check failed: %s\n", iMsg);
    }
    if (server && !iOrphan) server->updateConnParams(iConn, 24, 48, 0, 500);
    return;
  }
  if (iState != BLE_INST_RECEIVING) { endWaiting = nackWaiting = false; return; }

  // write what arrived, a few KB per pass
  uint32_t rx;
  portENTER_CRITICAL(&mux);
  rx = rxNext;
  portEXIT_CRITICAL(&mux);
  uint32_t budget = WRITE_BUDGET;
  while (flashed < rx && budget) {
    uint32_t at = flashed % RING_SIZE, n = rx - flashed;
    if (n > RING_SIZE - at) n = RING_SIZE - at;
    if (n > budget) n = budget;
    if (!cartInstallWrite(ring + at, n)) { failInstall(ERR_WRITE, "Flash write failed"); return; }
    portENTER_CRITICAL(&mux);
    flashed += n;
    portEXIT_CRITICAL(&mux);
    budget -= n;
  }
  if (rx != lastRxSeen) { lastRxSeen = rx; lastRxAt = now; }
  if (flashed - lastAck >= ACK_STEP || (flashed == iSize && lastAck != iSize)) {
    lastAck = flashed;
    send32(EV_ACK, flashed);
  }
  if (nackWaiting) {
    portENTER_CRITICAL(&mux);
    nackWaiting = false;
    rx = rxNext;
    portEXIT_CRITICAL(&mux);
    send32(EV_RESEND, rx);
  }
  if (endWaiting) {
    if (rx < iSize) { endWaiting = false; send32(EV_RESEND, rx); }   // early END: say what is missing
    else if (flashed == iSize) {
      endWaiting = false;
      iState = BLE_INST_CHECKING;
      stepAt = now;
      setMsg("Checking...");
    }
  }

  // a lost page gets 90 s to come back; a silent one 60 s
  if (iOrphan) {
    if (!orphanSeen) { orphanSeen = true; orphanAt = now; setMsg("Waiting for phone..."); }
    else if ((int32_t)(now - orphanAt) > (int32_t)ORPHAN_MS) failInstall(ERR_TIMEOUT, "Phone gone");
  } else {
    if (orphanSeen) { orphanSeen = false; setMsg("Receiving..."); }
    if ((int32_t)(now - lastRxAt) > (int32_t)RX_TIMEOUT_MS && flashed < iSize) failInstall(ERR_TIMEOUT, "Timed out");
  }
}

// ---------------- stack ----------------
bool wanted() {
  if (holdOn && (int32_t)(millis() - holdUntil) >= 0) holdOn = false;
  return !suspended && (enabled || used || holdOn || installActive());
}

void noteFail(uint32_t now, const char *why) {
  failAt = now ? now : 1;
  errText = why;
}

void startStack(uint32_t now) {
  if (running) return;
  if (failAt && (int32_t)(now - failAt) < (int32_t)RETRY_MS) return;
  uint32_t heapBefore = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  if (heapBefore < MIN_FREE_HEAP || block < MIN_BLOCK) {
    if (errText[0] == 0) dbg("ble: not started, heap %u (block %u) too low\n", heapBefore, block);
    noteFail(now, "Low memory");
    return;
  }
  dbg("ble: starting, heap %u (block %u), wifi mode %d\n", heapBefore, block, (int)WiFi.getMode());
  // a controller left half-up by a failed stop would make init abort
  if (!NimBLEDevice::getInitialized() && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
    dbg("ble: controller not idle (%d), not started\n", (int)esp_bt_controller_get_status());
    noteFail(now, "Radio busy");
    return;
  }
  // WiFi and Bluetooth share the radio: the WiFi driver aborts unless modem sleep is on
  if (WiFi.getMode() != WIFI_OFF) WiFi.setSleep(true);

  NimBLEDevice::init(DEV_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);          // bonding, no MITM ("just works"), secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(ATT_MTU);

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB, false);
  server->advertiseOnDisconnect(true);                       // stopStack() turns it off
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
  statusChr->setCallbacks(&statusCB);
  instChr = svc->createCharacteristic(UUID_INSTALL, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                                                    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY);
  instChr->setCallbacks(&installCB);
  svc->start();

  portENTER_CRITICAL(&mux);
  conns = 0;
  hidSubs = 0;
  hidConn = compConn = NO_CONN;
  repCount = outCount = 0;
  statusWaiting = false;
  batteryBox = -1;
  portEXIT_CRITICAL(&mux);
  ble_npl_event_init(&txEv, txHandler, nullptr);             // fresh: a stop may have left it marked queued
  txReady = true;

  // advertising: flags + name + appearance + HID (16-bit) fit the 31 bytes; the
  // 128-bit companion UUID goes in the scan response (Web Bluetooth filters on it)
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(APPEARANCE_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  NimBLEAdvertisementData scanData;
  scanData.setCompleteServices(NimBLEUUID(UUID_SVC));
  adv->setScanResponseData(scanData);
  adv->start();

  running = true;
  failAt = 0;
  errText = "";
  batteryPct = -1;                                           // fresh reading at the next service pass
  uint32_t heapAfter = ESP.getFreeHeap();
  dbg("ble: started, heap %u -> %u (block %u, min %u)\n", heapBefore, heapAfter, ESP.getMaxAllocHeap(),
      ESP.getMinFreeHeap());
  if (heapAfter < LOW_HEAP_AFTER) dbg("ble: warning, only %u bytes of heap left\n", heapAfter);
}

void stopStack(uint32_t now) {
  if (!running) return;
  if (failAt && (int32_t)(now - failAt) < (int32_t)RETRY_MS) return;
  dbg("ble: stopping, %d link(s), heap %u\n", (int)conns, ESP.getFreeHeap());
  if (installActive()) failInstall(ERR_STOPPED, "Bluetooth stopped");   // the page sees the link drop
  // the host must not advertise again while the stop drops the links
  if (server) server->advertiseOnDisconnect(false);
  NimBLEDevice::stopAdvertising();
  if (conns > 0) delay(40);                      // let queued replies (that error) go out first
  txReady = false;                               // nothing more for the host task to send
  // ble_hs_stop() inside deinit ends the connections itself and waits for it
  NimBLEDevice::deinit(true);                    // also deletes the server, its services and characteristics
  if (NimBLEDevice::getInitialized()) {
    // the host refused to stop: everything still exists, keep using it and retry later
    dbg("ble: stop failed, retrying in %lus\n", (unsigned long)(RETRY_MS / 1000));
    txReady = true;
    if (server) server->advertiseOnDisconnect(true);
    NimBLEDevice::startAdvertising();
    noteFail(now, "");
    return;
  }
  delete hid;                                    // its services went with the server
  hid = nullptr;
  server = nullptr;
  keyInput = mediaInput = statusChr = instChr = nullptr;
  running = false;
  failAt = 0;
  portENTER_CRITICAL(&mux);
  conns = 0;
  hidSubs = 0;
  hidConn = compConn = NO_CONN;
  portEXIT_CRITICAL(&mux);
  dbg("ble: stopped, heap %u (block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void apply(uint32_t now) {
  if (wanted()) startStack(now);
  else stopStack(now);
}

// only a report the host subscribed to: notifying an unsubscribed one does nothing
bool queueReport(uint8_t rid, const uint8_t *data, uint8_t len) {
  if (!running || !txReady || !(hidSubs & (rid == RID_KEYBOARD ? 1 : 2))) return false;
  portENTER_CRITICAL(&mux);
  bool ok = repCount < NUM_REPORTS;
  if (ok) {
    Report &r = reports[(repHead + repCount) % NUM_REPORTS];
    r.rid = rid;
    r.len = len;
    memcpy(r.data, data, len);
    repCount++;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

}  // namespace

// ---------------- API (common.h) ----------------
void initBle() {
  enabled = prefs.getBool("ble_on", false);
  apply(millis());
}

bool bleEnabled() { return enabled; }

void setBleEnabled(bool on) {
  if (prefs.getBool("ble_on", false) != on) prefs.putBool("ble_on", on);   // no flash write when unchanged
  enabled = on;
  failAt = 0;                                    // the user asked: try right away
  apply(millis());
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
  if (used && (int32_t)(now - usedAt) > (int32_t)USE_TIMEOUT_MS) used = false;   // the tool that held it is gone
  apply(now);                                    // start / stop as wanted (failed attempts retry every few s)
  if (!running) return;

  if (evConnect) {
    evConnect = false;
    dbg("ble: connected (%d), heap %u\n", (int)conns, ESP.getFreeHeap());
    updateStatus(now, batteryPct < 0);           // fresh status for the companion's first read
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
    lastStatus = now - STATUS_MS + 1500;         // report the outcome soon
  }
  if (doTime && epoch > 1600000000UL) {
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    clockNoteSynced(CLOCK_SRC_PHONE);            // the clock app shows "phone 5m ago"
    dbg("ble: clock set from phone, epoch %lu\n", (unsigned long)epoch);
  }

  installService(now);

  // no battery readings while streaming: they cost 5 ms each
  if (conns > 0 && (int32_t)(now - lastStatus) >= (int32_t)STATUS_MS && iState != BLE_INST_RECEIVING)
    updateStatus(now, batteryPct < 0 || (int32_t)(now - lastBattery) >= (int32_t)BATTERY_MS);
}

// ---------------- API (ble.h) ----------------
void bleUse(uint32_t now) {
  usedAt = now;
  if (!used) {
    used = true;
    failAt = 0;
    apply(now);
  }
}

void bleRelease() {
  if (!used) return;
  used = false;
  apply(millis());
}

void bleSuspend(bool on) {
  if (on == suspended) return;
  suspended = on;
  failAt = 0;
  dbg("ble: %s\n", on ? "suspended" : "resumed");
  apply(millis());
}

bool bleSuspended() { return suspended; }
bool bleRunning() { return running; }
bool bleHidReady() { return running && hidSubs; }
bool bleCompanionLinked() { return running && compConn != NO_CONN; }
// suspended: an update or download holds the heap; it comes back by itself
const char *bleError() { return running ? "" : suspended ? "Paused" : errText; }

bool bleTapMedia(BleMedia key) {
  uint8_t down = 1 << key, up = 0;
  if (!queueReport(RID_MEDIA, &down, 1)) return false;
  queueReport(RID_MEDIA, &up, 1);
  kick();
  return true;
}

bool bleTapKey(uint8_t usage, uint8_t modifiers) {
  uint8_t r[8] = {modifiers, 0, usage, 0, 0, 0, 0, 0};
  if (!queueReport(RID_KEYBOARD, r, sizeof r)) return false;
  memset(r, 0, sizeof r);
  queueReport(RID_KEYBOARD, r, sizeof r);
  kick();
  return true;
}

int bleInstallState() { return iState; }
bool bleInstallActive() { return installActive(); }

void bleInstallBytes(uint32_t *done, uint32_t *total) {
  if (done) *done = flashed;
  if (total) *total = iSize;
}

int bleInstallPercent() {
  if (iState == BLE_INST_DONE) return 100;
  return iSize ? (int)((uint64_t)flashed * 100 / iSize) : 0;
}

const char *bleInstallMessage() { return iMsg; }

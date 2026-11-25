// Updates over WiFi: manifest check, background downloads, Settings > Updates.
//
// The CI (.github/workflows/build.yml) publishes fw/manifest.json, firmware.bin
// (launcher) and cartridge.bin (Big Games) on GitHub Pages for every push to
// main. The launcher checks the manifest once an hour (only from the launcher,
// Settings and the cartridge screen, never in a game): WiFi runs on demand, so
// the check holds it, waits for the connection, runs and lets go; the time of
// the last check survives deep sleep, so waking the board doesn't check again.
// A newer launcher shows a badge in the launcher and "New" on the
// Updates row. Installing streams the image into the other app slot (cart.h
// writer: SHA-256 against the manifest, image validation), then reboots.
// A dev build (FW_BUILD 0) never nags; a manual install still works.
//
// Updates screen:
//   Tap L : next row          Tap R : check / install / toggle / back
//   Hold L: back to Settings  (during a download: Tap L cancels)
//   Confirm: Tap L no · Tap R yes

#include "common.h"
#include "cart.h"
#include "updates.h"
#include "ble.h"
#include "tigo.h"
#include "themeui.h"
#include "netcerts.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_bt.h"

namespace updates {

static const char MANIFEST_URL[] = "https://iyotee.github.io/TTGO-Arcade/fw/manifest.json";
static const char FW_BASE[] = "https://iyotee.github.io/TTGO-Arcade/fw/";   // for relative URLs
static const uint32_t CHECK_EVERY_MS = 3600000UL;    // hourly
static const uint32_t FIRST_CHECK_MS = 20000UL;      // after boot, when none ran in the last hour
static const uint32_t CONNECT_WAIT_MS = 45000UL;     // the hourly check gives WiFi this long to connect
static const uint32_t RETRY_MS = 600000UL;           // after a failed check
static const uint32_t NO_WIFI_RETRY_MS = 1800000UL;  // the network was out of reach: spare the battery
static const uint32_t STALL_MS = 20000UL;            // no data for this long: give up
// TLS needs ~40-50 KB (two 16 KB records + handshake) plus the task's stack
static const uint32_t NEED_FREE_HEAP = 62000, NEED_BLOCK = 24000;
static const uint32_t TASK_STACK = 10240;
static const uint32_t CHECK_HOLD_MS = 120000;        // a check's WiFi hold ends by itself after this
static const size_t MANIFEST_MAX = 2048;

// prefs keys (<= 15 chars)
static const char K_LATEST[] = "upd_lb", K_LATEST_CART[] = "upd_cb", K_AUTO[] = "upd_auto";

static Manifest man;                                 // zero-initialised: valid = false
static uint32_t latestFw = 0, latestCart = 0;
static bool autoOn = true;
static uint32_t nextCheckAt = 0, checkedAt = 0;
static bool checkedOnce = false;
static RTC_DATA_ATTR time_t rtcCheckedAt = 0;        // time() of the last good check, kept through deep sleep
// the hourly check's WiFi hold: waiting for the connection, then the job itself
static const char HOLDER[] = "upd", JOB_HOLDER[] = "updjob";   // every job holds WiFi until it ends
static bool bgWaiting = false;
static uint32_t bgSince = 0;

// ---- job state: written by the task, read by the main loop ----
static volatile int curJob = JOB_NONE, curPhase = PH_IDLE;
static volatile bool running = false, taskEnded = false, taskOk = false, cancelReq = false, manifestOk = false;
static volatile uint32_t gotBytes = 0, totalBytes = 0;
static bool wasCancelled = false;
static char errText[40] = "";
static Manifest fresh;                               // the task's copy, taken over in service()
static char body[MANIFEST_MAX + 1];
static size_t bodyLen = 0;
static uint8_t chunk[1024];
// restored when the job ends
static bool blePaused = false, sleepWasOff = false;

static void setErr(const char *fmt, int v = 0) {
  snprintf(errText, sizeof errText, fmt, v);
  dbg("upd: %s\n", errText);
}

// ---------------- manifest parsing ----------------
// The CI writes it, so a small tolerant scanner is enough: no JSON library.
static const char *skipWs(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ':')) p++;
  return p;
}

// value of the first "key" in [p, end) (top level or not: our keys are unique per object)
static const char *findKey(const char *p, const char *end, const char *key) {
  char pat[24];
  int pl = snprintf(pat, sizeof pat, "\"%s\"", key);
  for (const char *q = p; q + pl <= end; q++)
    if (!memcmp(q, pat, pl)) return skipWs(q + pl, end);
  return nullptr;
}

// p at '{' or '[': the matching closing bracket (strings skipped), or nullptr
static const char *closing(const char *p, const char *end) {
  int depth = 0;
  bool str = false;
  for (const char *q = p; q < end; q++) {
    char c = *q;
    if (str) {
      if (c == '\\') q++;
      else if (c == '"') str = false;
    } else if (c == '"') str = true;
    else if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') { if (--depth == 0) return q; }
  }
  return nullptr;
}

static uint32_t numAt(const char *p) {
  if (!p) return 0;
  if (*p == '"') p++;                                // tolerate "123"
  return strtoul(p, nullptr, 10);
}

static bool strAt(const char *p, const char *end, char *out, size_t n) {
  if (!p || p >= end || *p != '"') return false;
  size_t i = 0;
  for (p++; p < end && *p != '"'; p++) {
    if (*p == '\\' || i + 1 >= n) return false;      // no escapes in our URLs / ids
    out[i++] = *p;
  }
  out[i] = 0;
  return p < end;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseImage(const char *p, const char *end, Image &im) {
  memset(&im, 0, sizeof im);
  im.build = numAt(findKey(p, end, "build"));
  im.size = numAt(findKey(p, end, "size"));
  char hex[68];
  if (!strAt(findKey(p, end, "sha256"), end, hex, sizeof hex) || strlen(hex) != 64) return false;
  for (int i = 0; i < 32; i++) {
    int hi = hexVal(hex[2 * i]), lo = hexVal(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    im.sha[i] = hi << 4 | lo;
  }
  char url[sizeof im.url];
  if (!strAt(findKey(p, end, "url"), end, url, sizeof url)) return false;
  if (strncmp(url, "https://", 8)) snprintf(im.url, sizeof im.url, "%s%s", FW_BASE, url);
  else snprintf(im.url, sizeof im.url, "%s", url);
  return im.size > 0;
}

static bool parseManifest(const char *p, size_t n, Manifest &m) {
  const char *end = p + n;
  memset(&m, 0, sizeof m);
  const char *l = findKey(p, end, "launcher");
  const char *le = l && *l == '{' ? closing(l, end) : nullptr;
  if (!le || !parseImage(l, le, m.launcher)) return false;
  const char *a = findKey(p, end, "cartridges");
  const char *ae = a && *a == '[' ? closing(a, end) : nullptr;
  for (const char *q = a; ae && q && q < ae; q++) {
    if (*q != '{') continue;
    const char *qe = closing(q, ae + 1);
    if (!qe) break;
    char id[16];
    if (strAt(findKey(q, qe, "id"), qe, id, sizeof id) && !strcmp(id, CART_ID)) {
      parseImage(q, qe, m.cart);                     // a broken entry just leaves size 0
      break;
    }
    q = qe;
  }
  m.valid = true;
  return true;
}

// ---------------- HTTPS streaming (task side) ----------------
enum Sink { SINK_BODY, SINK_CART, SINK_FW };
static int sinkKind = SINK_BODY;

static bool sink(const uint8_t *d, size_t n) {
  switch (sinkKind) {
    case SINK_BODY:
      if (bodyLen + n > MANIFEST_MAX) { setErr("manifest too big"); return false; }
      memcpy(body + bodyLen, d, n);
      bodyLen += n;
      body[bodyLen] = 0;
      return true;
    case SINK_CART:
      if (cartInstallWrite(d, n)) return true;
      break;
    default:
      if (fwInstallWrite(d, n)) return true;
      break;
  }
  setErr("flash write failed");
  return false;
}

static bool pump(WiFiClient *s, int len) {
  uint32_t got = 0, last = millis();
  while (len < 0 || got < (uint32_t)len) {
    if (cancelReq) { setErr("cancelled"); return false; }
    size_t av = s->available();
    if (av) {
      int n = s->read(chunk, av < sizeof chunk ? av : sizeof chunk);
      if (n > 0) {
        if (!sink(chunk, n)) return false;
        got += n;
        if (sinkKind != SINK_BODY) gotBytes = got;
        last = millis();
      }
    } else if (!s->connected()) {
      break;
    } else if ((int32_t)(millis() - last) > (int32_t)STALL_MS) {
      setErr("download stalled");
      return false;
    } else {
      delay(2);
    }
  }
  if (len >= 0 && got != (uint32_t)len) { setErr("connection lost"); return false; }
  return true;
}

static bool httpsGet(const char *url, uint32_t expect) {
  WiFiClientSecure *cli = new WiFiClientSecure;
  HTTPClient *http = new HTTPClient;
  bool ok = false;
  cli->setCACert(NET_ROOT_CAS);
  cli->setHandshakeTimeout(20);                      // seconds
  http->setConnectTimeout(10000);
  http->setTimeout(15000);
  http->useHTTP10(true);                             // plain body, never chunked
  http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http->setUserAgent("TTGO-Arcade/" + String(FW_BUILD));
  if (!http->begin(*cli, url)) {
    setErr("bad URL");
  } else {
    int code = http->GET();
    if (code != 200) {
      if (code < 0) {
        // the whole story goes to the log: HTTP client error, TLS error, memory
        char tls[96] = "";
        int te = cli->lastError(tls, sizeof tls);
        dbg("upd: GET %s failed %d (%s) tls %d (%s) heap %u block %u\n", url, code,
            HTTPClient::errorToString(code).c_str(), te, tls, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (te) setErr("TLS error %d", te);
        else setErr("can't reach server (%d)", code);
      } else setErr("server said %d", code);
    } else {
      int len = http->getSize();
      if (expect && len >= 0 && (uint32_t)len != expect) setErr("size differs from manifest");
      else ok = pump(http->getStreamPtr(), len);
    }
    http->end();
  }
  delete http;
  delete cli;
  return ok;
}

static bool runJob(int j) {
  curPhase = PH_CONNECT;
  char url[sizeof MANIFEST_URL + 16];
  snprintf(url, sizeof url, "%s?t=%lu", MANIFEST_URL, (unsigned long)millis());   // past stale caches
  sinkKind = SINK_BODY;
  bodyLen = 0;
  body[0] = 0;
  if (!httpsGet(url, 0)) return false;
  if (!parseManifest(body, bodyLen, fresh)) { setErr("bad manifest"); return false; }
  manifestOk = true;
  if (j == JOB_CHECK) return true;

  const Image &im = j == JOB_CART ? fresh.cart : fresh.launcher;
  if (!im.size) { setErr("not published yet"); return false; }
  if (im.size > cartSlotSize()) { setErr("not enough space"); return false; }
  char err[32];
  bool begun = j == JOB_CART ? cartInstallBegin(im.size, im.sha, im.build, err, sizeof err)
                             : fwInstallBegin(im.size, im.sha, err, sizeof err);
  if (!begun) {
    snprintf(errText, sizeof errText, "%s", err);
    return false;
  }
  totalBytes = im.size;
  gotBytes = 0;
  curPhase = PH_DOWNLOAD;
  sinkKind = j == JOB_CART ? SINK_CART : SINK_FW;
  if (!httpsGet(im.url, im.size)) {
    if (j == JOB_CART) cartInstallAbort();
    else fwInstallAbort();
    return false;
  }
  curPhase = PH_VERIFY;
  bool ok = j == JOB_CART ? cartInstallEnd(err, sizeof err) : fwInstallEnd(err, sizeof err);
  if (!ok) snprintf(errText, sizeof errText, "%s", err);
  return ok;
}

static void jobTask(void *arg) {
  int j = (int)(intptr_t)arg;
  bool ok = runJob(j);
  dbg("upd: job %d %s, heap %u\n", j, ok ? "done" : "failed", ESP.getFreeHeap());
  taskOk = ok;
  taskEnded = true;                                  // service() wraps up in the main loop
  vTaskDelete(nullptr);
}

static bool heapOk() { return ESP.getFreeHeap() >= NEED_FREE_HEAP && ESP.getMaxAllocHeap() >= NEED_BLOCK; }

// ---------------- main-loop side ----------------
// The full-screen sprite is 64.8 KB at 16 bits. A download the user asked for
// may switch it to 8 bits (32 KB back) when TLS would not fit otherwise; the
// update screens still draw fine, just with fewer colours.
static bool spriteSmall = false;

static void recreateSprite(uint8_t depth) {
  int w = spr.width(), h = spr.height();
  spr.deleteSprite();
  spr.setColorDepth(depth);
  if (!spr.createSprite(w, h) && depth == 16) {    // no room for 16 bits back: stay at 8
    spr.setColorDepth(8);
    spr.createSprite(w, h);
  }
}

static void shrinkSprite() {
  if (spriteSmall || spr.getColorDepth() != 16) return;
  recreateSprite(8);
  spriteSmall = true;
  dbg("upd: screen buffer at 8 bits for the download, heap %u\n", ESP.getFreeHeap());
}

static void restoreSprite() {
  if (!spriteSmall) return;
  spriteSmall = false;
  recreateSprite(16);
}

static void restoreRadios() {
  restoreSprite();
  WiFi.setSleep(true);                               // also when WiFi dropped meanwhile: the next start needs it
  sleepWasOff = false;
  if (blePaused) bleSuspend(false);                  // the saved setting was never touched
  blePaused = false;
}

static void takeOver(const Manifest &m) {
  man = m;
  checkedAt = millis();
  checkedOnce = true;
  // remember the newest builds (rewritten only when they change, to spare the flash)
  if (m.launcher.build != latestFw) { latestFw = m.launcher.build; prefs.putUInt(K_LATEST, latestFw); }
  uint32_t cb = m.cart.size ? m.cart.build : 0;
  if (cb != latestCart) { latestCart = cb; prefs.putUInt(K_LATEST_CART, latestCart); }
  dbg("upd: manifest launcher %u (this %u), cartridge %u\n", (unsigned)latestFw, (unsigned)FW_BUILD, (unsigned)latestCart);
}

bool start(int j, char *err, size_t n, bool user) {
  if (running) { snprintf(err, n, "busy"); return false; }
  if (!wifiConnected()) { snprintf(err, n, "no WiFi"); return false; }
  if (!heapOk()) {
    // only something the user asked for may stop Bluetooth or shrink the screen for a while
    if (!user) { snprintf(err, n, "low memory"); return false; }
    // suspend, not setBleEnabled(false): a reboot before restoreRadios() keeps the setting
    if (bleRunning() && !bleSuspended()) {
      bleSuspend(true);
      blePaused = true;
      delay(100);
      dbg("upd: Bluetooth paused for the download, heap %u\n", ESP.getFreeHeap());
    }
    if (!heapOk()) shrinkSprite();
    if (!heapOk()) {
      uint32_t freeK = ESP.getFreeHeap() / 1024, blockK = ESP.getMaxAllocHeap() / 1024;
      restoreRadios();
      snprintf(err, n, "low memory %uK/%uK", (unsigned)freeK, (unsigned)blockK);
      return false;
    }
  }
  // modem sleep slows downloads; it must stay on while Bluetooth runs
  if (j != JOB_CHECK && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    WiFi.setSleep(false);
    sleepWasOff = true;
  }
  curJob = j;
  curPhase = PH_CONNECT;
  gotBytes = totalBytes = 0;
  errText[0] = 0;
  cancelReq = wasCancelled = false;
  taskEnded = manifestOk = false;
  running = true;
  // a check ends by itself even if no service() wraps it up (a game started); downloads run on their screen
  if (j == JOB_CHECK) wifiAcquireFor(JOB_HOLDER, CHECK_HOLD_MS);
  else wifiAcquire(JOB_HOLDER);
  // core 1 with the loop task: core 0's idle task is watched by the task watchdog
  if (xTaskCreatePinnedToCore(jobTask, "upd", TASK_STACK, (void *)(intptr_t)j, 1, nullptr, 1) != pdPASS) {
    running = false;
    curPhase = PH_FAILED;
    restoreRadios();
    wifiRelease(JOB_HOLDER);
    snprintf(err, n, "no memory for the task");
    return false;
  }
  dbg("upd: job %d started, heap %u\n", j, ESP.getFreeHeap());
  return true;
}

void service(uint32_t now) {
  if (running && taskEnded) {
    running = false;
    wasCancelled = cancelReq;
    if (manifestOk) takeOver(fresh);                 // every job starts with a fresh manifest
    if (curJob == JOB_CHECK) {
      nextCheckAt = now + (taskOk ? CHECK_EVERY_MS : RETRY_MS);
      if (taskOk) {
        rtcCheckedAt = time(nullptr) | 1;
        if (bgWaiting) { bgWaiting = false; wifiRelease(HOLDER); }   // a manual check just did it
      }
    }
    restoreRadios();
    wifiRelease(JOB_HOLDER);
    curPhase = taskOk ? PH_DONE : PH_FAILED;
  }

  // the hourly check: hold WiFi, wait for the connection, run, let go
  if (running) return;
  if (!bgWaiting) {
    if (!autoOn || (int32_t)(now - nextCheckAt) < 0) return;
    if (!wifiEnabled() || !wifiSavedSsid()[0]) return;   // due: runs once WiFi may connect
    bgWaiting = true;
    bgSince = now;
    wifiAcquireFor(HOLDER, CONNECT_WAIT_MS + 5000);  // timed: a game started meanwhile can't keep it up
    return;
  }
  if (wifiConnected()) {
    bgWaiting = false;
    char err[24];
    if (start(JOB_CHECK, err, sizeof err, false)) {
      nextCheckAt = now + CHECK_EVERY_MS;            // set again when it ends
    } else {
      dbg("upd: hourly check not started: %s\n", err);
      nextCheckAt = now + RETRY_MS;
    }
    wifiRelease(HOLDER);                             // the job holds it now
  } else if (!autoOn || !wifiEnabled() || !wifiHeldBy(HOLDER) || (int32_t)(now - bgSince) > (int32_t)CONNECT_WAIT_MS) {
    dbg("upd: no WiFi for the hourly check\n");
    bgWaiting = false;
    wifiRelease(HOLDER);
    nextCheckAt = now + NO_WIFI_RETRY_MS;
  }
}

void begin() {
  latestFw = prefs.getUInt(K_LATEST, 0);
  latestCart = prefs.getUInt(K_LATEST_CART, 0);
  autoOn = prefs.getBool(K_AUTO, true);
  // a check in the last hour (before a deep sleep): the next one when that hour is up
  uint32_t now = millis();
  nextCheckAt = now + FIRST_CHECK_MS;
  if (rtcCheckedAt) {
    time_t age = time(nullptr) - rtcCheckedAt;
    if (age >= 0 && age < (time_t)(CHECK_EVERY_MS / 1000)) nextCheckAt = now + CHECK_EVERY_MS - (uint32_t)age * 1000;
  }
}

const Manifest &manifest() { return man; }
uint32_t latestBuild() { return latestFw; }
uint32_t latestCartBuild() { return latestCart; }
bool updateAvailable() { return FW_BUILD > 0 && latestFw > (uint32_t)FW_BUILD; }
bool autoCheck() { return autoOn; }

void setAutoCheck(bool on) {
  autoOn = on;
  prefs.putBool(K_AUTO, on);
  if (on) nextCheckAt = millis() + 2000;             // check soon after turning it on
}

int32_t checkedAgoS() { return checkedOnce ? (int32_t)((millis() - checkedAt) / 1000) : -1; }
bool busy() { return running; }
int job() { return curJob; }
int phase() { return curPhase; }
void progress(uint32_t *done, uint32_t *total) { *done = gotBytes; *total = totalBytes; }
const char *error() { return errText; }
bool cancelled() { return wasCancelled; }
void cancel() { if (running) cancelReq = true; }

// ---------------- shared drawing ----------------
void fmtSize(char *buf, size_t n, uint32_t bytes) {
  if (bytes >= 1024UL * 1024UL) snprintf(buf, n, "%u.%u MB", (unsigned)(bytes >> 20), (unsigned)((bytes % (1024UL * 1024UL)) * 10 >> 20));
  else snprintf(buf, n, "%u KB", (unsigned)((bytes + 1023) / 1024));
}

void drawWrapped(const char *s, int y, uint16_t col) {
  char a[23], b[23];
  size_t n = strlen(s);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(col);
  if (n <= 22) { spr.drawString(s, 67, y, 1); return; }
  int cut = 22;
  while (cut > 8 && s[cut] != ' ') cut--;            // break at a space when there is one
  if (s[cut] != ' ') cut = 22;
  snprintf(a, sizeof a, "%.*s", cut, s);
  snprintf(b, sizeof b, "%s", s + cut + (s[cut] == ' '));
  spr.drawString(a, 67, y, 1);
  spr.drawString(b, 67, y + 11, 1);
}

void drawTitle(const char *title) {
  const Theme &th = theme();
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.accent);
  spr.drawString(title, 67, 6, 2);
  spr.drawFastHLine(8, 26, 119, th.frame);
}

void drawProgressBar(int y, uint32_t done, uint32_t total, uint32_t now) {
  const Theme &th = theme();
  int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
  const int bx = 8, bw = 119, bh = 14;
  int tx = constrain(bx + bw * pct / 100, bx + 14, bx + bw - 14);
  drawTigo(tx, y - 6, 34, TIGO_WALK, now, false);    // Tigo walks along with it
  drawThemedBox(bx, y, bw, bh, 5, th.frameSel, true);
  int fw = (bw - 4) * pct / 100;
  if (fw > 0) spr.fillRoundRect(bx + 2, y + 2, fw, bh - 4, 3, th.accent);
  char buf[24];
  snprintf(buf, sizeof buf, "%d%%", pct);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.accent);
  spr.drawString(buf, 67, y + 22, 4);
  snprintf(buf, sizeof buf, "%u / %u KB", (unsigned)(done / 1024), (unsigned)(total / 1024));
  spr.setTextColor(th.textDim);
  spr.drawString(buf, 67, y + 52, 1);
}

void drawJob(const char *what, uint32_t now) {
  const Theme &th = theme();
  char dots[4] = "...";
  dots[(now / 400) % 4] = 0;
  char buf[32];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.text);
  spr.drawString(what, 67, 36, 2);
  if (curPhase == PH_DOWNLOAD) {
    uint32_t d, t;
    progress(&d, &t);
    drawProgressBar(118, d, t, now);
    spr.setTextColor(th.keyHint);
    spr.drawString("Keep it powered", 67, 186, 1);
  } else {
    drawTigo(67, 128, 48, curPhase == PH_VERIFY ? TIGO_EAT : TIGO_WALK, now, false);
    snprintf(buf, sizeof buf, "%s%s", curPhase == PH_VERIFY ? "Checking" : "Connecting", dots);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(themeSoft());
    spr.drawString(buf, 34, 150, 2);                 // left-aligned: the dots don't shift it
  }
  if (curPhase == PH_CONNECT || curPhase == PH_DOWNLOAD) drawThemedFooter("cancel", nullptr);
}

}  // namespace updates

// ---------------- Settings > Updates ----------------
namespace updui {

using namespace updates;

enum State { U_MAIN, U_CONFIRM, U_RUN, U_DONE, U_ERROR };
enum Row { R_CHECK, R_INSTALL, R_AUTO, R_BACK, NUM_ROWS };
static const int ROW_Y = 112, ROW_PITCH = 26, ROW_H = 22;
static const uint32_t FRAME_MS = 50;

static State state = U_MAIN;
static int row = R_CHECK;
static bool pressedL = false, pressedR = false;
static uint32_t lastFrame = 0, lastInput = 0, doneAt = 0;
static int mv = 0;
static char note[32] = "";                           // one-line status under the builds
static bool noteBad = false;
static bool checkWanted = false;                     // "Check now" waits for WiFi to connect
static const char HOLD_UI[] = "updui";               // WiFi is held while this screen is open

static bool canInstall() {
  uint32_t l = latestBuild();
  return l && (FW_BUILD == 0 || l > (uint32_t)FW_BUILD);
}
static bool batteryOk() { return onUsbPower(mv) || batteryPercent(mv) > 30; }

static void setNote(const char *s, bool bad) {
  snprintf(note, sizeof note, "%.22s", s);           // one line of font 1
  noteBad = bad;
}

static void fmtBuild(char *buf, size_t n, uint32_t b) {
  if (b) snprintf(buf, n, "build %u", (unsigned)b);
  else snprintf(buf, n, "dev build");
}

static void drawRow(int r, const char *label, const char *value, uint16_t valCol, bool usable) {
  const Theme &th = theme();
  int y = ROW_Y + r * ROW_PITCH;
  bool sel = r == row;
  drawSelBox(6, y, 123, ROW_H, 6, sel);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(sel && usable ? th.text : th.textDim);
  spr.drawString(label, 14, y + 3, 2);
  if (value) {
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(sel && usable ? valCol : th.textDim);
    spr.drawString(value, 121, y + 3, 2);
  }
}

static void renderMain(uint32_t now) {
  const Theme &th = theme();
  char buf[32];
  drawThemedBox(6, 32, 123, 74, 6, th.frame, false);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(themeSoft());
  spr.drawString("This board", 14, 38, 1);
  spr.drawString("Latest", 14, 64, 1);
  fmtBuild(buf, sizeof buf, FW_BUILD);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(th.text);
  spr.drawString(buf, 121, 46, 2);
  uint32_t l = latestBuild();
  if (checkWanted || (busy() && job() == JOB_CHECK)) {
    char dots[4] = "...";
    dots[(now / 400) % 4] = 0;
    snprintf(buf, sizeof buf, "checking%s", dots);
  } else if (l) {
    snprintf(buf, sizeof buf, "build %u", (unsigned)l);
  } else {
    snprintf(buf, sizeof buf, "unknown");
  }
  spr.setTextColor(updateAvailable() ? th.ok : th.text);
  spr.drawString(buf, 121, 72, 2);

  // status: a note from the last action, else when it was checked
  spr.setTextDatum(TC_DATUM);
  if (note[0]) {
    spr.setTextColor(noteBad ? TFT_ORANGE : th.ok);
    spr.drawString(note, 67, 92, 1);
  } else {
    int32_t ago = checkedAgoS();
    if (!wifiEnabled()) snprintf(buf, sizeof buf, "WiFi is off");
    else if (!wifiSavedSsid()[0]) snprintf(buf, sizeof buf, "no WiFi network saved");
    else if (!wifiConnected()) snprintf(buf, sizeof buf, "%s", wifiFailReason()[0] ? wifiFailReason() : "WiFi connecting...");
    else if (ago < 0) snprintf(buf, sizeof buf, "not checked yet");
    else if (ago < 120) snprintf(buf, sizeof buf, "checked just now");
    else if (ago < 7200) snprintf(buf, sizeof buf, "checked %ld min ago", (long)(ago / 60));
    else snprintf(buf, sizeof buf, "checked %ld h ago", (long)(ago / 3600));
    spr.setTextColor(th.textDim);
    spr.drawString(buf, 67, 92, 1);
  }

  drawRow(R_CHECK, "Check now", nullptr, 0, wifiEnabled());
  char lb[16] = "-";
  if (canInstall()) snprintf(lb, sizeof lb, "b%u", (unsigned)l);
  drawRow(R_INSTALL, "Install", lb, th.ok, canInstall());
  drawRow(R_AUTO, "Auto-check", autoCheck() ? "On" : "Off", autoCheck() ? th.ok : themeSoft(), true);
  drawRow(R_BACK, "Back", nullptr, 0, true);
  const char *act = row == R_BACK ? "back" : row == R_AUTO ? "toggle" : row == R_CHECK ? "check" : "install";
  drawThemedFooter("next", act);
}

static void renderConfirm() {
  const Theme &th = theme();
  char buf[32];
  drawTigo(67, 92, 44, TIGO_IDLE, millis(), false);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.text);
  snprintf(buf, sizeof buf, "Install build %u?", (unsigned)latestBuild());
  spr.drawString(buf, 67, 104, 2);
  int y = 128;
  if (cartInstalledBuild()) {                          // the update goes where the cartridge is
    spr.setTextColor(TFT_ORANGE);
    spr.drawString("Big Games get erased", 67, y, 1);
    spr.drawString("(download them again)", 67, y + 11, 1);
    y += 28;
  }
  if (!batteryOk()) {
    snprintf(buf, sizeof buf, "Battery %d%%: too low", batteryPercent(mv));
    spr.setTextColor(TFT_RED);
    spr.drawString(buf, 67, y, 1);
    spr.drawString("charge or plug in USB", 67, y + 11, 1);
    drawThemedFooter("back", nullptr);
  } else {
    spr.setTextColor(themeSoft());
    spr.drawString("It restarts when done", 67, y, 1);
    drawThemedFooter("no", "yes");
  }
}

static void render(uint32_t now) {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  drawTitle("Updates");
  switch (state) {
    case U_MAIN: renderMain(now); break;
    case U_CONFIRM: renderConfirm(); break;
    case U_RUN: drawJob(phase() == PH_CONNECT ? "Launcher update" : "Downloading", now); break;
    case U_DONE:
      drawTigo(67, 128, 56, TIGO_HAPPY, now, false);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(th.ok);
      spr.drawString("Installed!", 67, 146, 4);
      spr.setTextColor(th.text);
      spr.drawString("Restarting...", 67, 176, 2);
      break;
    case U_ERROR:
      drawTigo(67, 120, 50, TIGO_SAD, now, false);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(theme().neon ? 0xF8B2 : TFT_RED);
      spr.drawString("Update failed", 67, 136, 2);
      drawWrapped(error(), 158, th.text);
      drawThemedFooter("back", "retry");
      break;
  }
  spr.pushSprite(0, 0);
  lastFrame = now ? now : 1;
}

void open() {
  state = U_MAIN;
  row = R_CHECK;
  pressedL = pressedR = false;
  note[0] = 0;
  checkWanted = false;
  wifiAcquire(HOLD_UI, true);
  mv = batteryMilliVolts();
  lastInput = millis();
  render(lastInput);
}

static bool leave() {
  pressedL = pressedR = false;
  checkWanted = false;
  wifiRelease(HOLD_UI);                              // a job still running holds WiFi itself
  return false;
}

static void act(uint32_t now) {
  char err[32];
  switch (row) {
    case R_CHECK:
      if (!wifiEnabled()) setNote("turn WiFi on first", true);
      else if (!wifiSavedSsid()[0]) setNote("set up WiFi first", true);
      else if (!wifiConnected()) { checkWanted = true; note[0] = 0; }   // starts once connected
      else if (busy()) setNote("already busy", true);
      else if (!start(JOB_CHECK, err, sizeof err)) setNote(err, true);
      else note[0] = 0;
      break;
    case R_INSTALL:
      if (!canInstall()) setNote(latestBuild() ? "already up to date" : "check first", true);
      else if (!wifiConnected()) setNote(wifiEnabled() ? "wait for WiFi" : "turn WiFi on first", true);
      else { mv = batteryMilliVolts(); state = U_CONFIRM; }
      break;
    case R_AUTO:
      setAutoCheck(!autoCheck());
      break;
    default:
      break;
  }
}

bool update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  // Hold L leaves (not while downloading: L cancels there)
  if (state != U_RUN && state != U_DONE && pressedL && bL.stable && (int32_t)(now - bL.t0) >= (int32_t)TOOL_HOLD_MS)
    return leave();
  bool tapL = eL == 2 && pressedL, tapR = eR == 2 && pressedR;
  if (eL == 2) pressedL = false;
  if (eR == 2) pressedR = false;

  switch (state) {
    case U_MAIN: {
      static bool wasChecking = false;                 // report the end of a manual check
      bool checking = busy() && job() == JOB_CHECK;
      if (wasChecking && !checking && phase() == PH_FAILED) setNote(error(), true);
      else if (wasChecking && !checking) setNote(updateAvailable() ? "update available!" : "up to date", false);
      wasChecking = checking;
      if (checkWanted && !wifiEnabled()) checkWanted = false;
      if (checkWanted && wifiConnected() && !busy()) {
        char err[32];
        checkWanted = false;
        if (!start(JOB_CHECK, err, sizeof err)) setNote(err, true);
      }
      if (tapL) { row = (row + 1) % NUM_ROWS; note[0] = 0; }
      if (tapR) {
        if (row == R_BACK) return leave();
        act(now);
      }
      break;
    }
    case U_CONFIRM:
      if (tapL) state = U_MAIN;
      if (tapR && batteryOk()) {
        char err[32];
        if (start(JOB_FW, err, sizeof err)) state = U_RUN;
        else { snprintf(errText, sizeof errText, "%s", err); state = U_ERROR; }
      }
      break;
    case U_RUN:
      if (tapL && (phase() == PH_CONNECT || phase() == PH_DOWNLOAD)) cancel();
      if (!busy() && phase() == PH_DONE) { state = U_DONE; doneAt = now; }
      else if (!busy() && phase() == PH_FAILED) {
        if (cancelled()) { state = U_MAIN; setNote("cancelled", true); }
        else state = U_ERROR;
      }
      break;
    case U_DONE:
      if ((int32_t)(now - doneAt) > 1500) {
        tigo::save(now, true);                         // keep Tigo's needs across the reboot
        dbg("upd: rebooting into the new launcher\n");
        if (usbLogOn()) Serial.flush();
        esp_restart();
      }
      break;
    case U_ERROR:
      if (tapL) { state = U_MAIN; note[0] = 0; }
      if (tapR) { mv = batteryMilliVolts(); state = U_CONFIRM; }
      break;
  }

  if ((state == U_MAIN || state == U_CONFIRM || state == U_ERROR) && idleTooLong(now, lastInput)) {
    leave();                                           // the screen is gone: let WiFi go
    goToSleep();                                       // back via the launcher
    return true;
  }
  if (lastFrame == 0 || (int32_t)(now - lastFrame) >= (int32_t)FRAME_MS) render(now);
  return true;
}

}  // namespace updui

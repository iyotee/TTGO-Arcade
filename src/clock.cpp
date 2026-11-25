// Time service: applies the saved time zone and keeps the clock synced with
// NTP. WiFi runs on demand: the launcher holds it for the sync at boot when
// the time is unknown and every 6 h, and lets go once synced or after a
// minute. No controls (see clockapp.cpp for the Clock tool).
//
// The ESP32 RTC keeps counting through deep sleep, so the time survives it;
// only the TZ setting is lost at boot and is re-applied here. The time can
// also come from the phone (Bluetooth calls settimeofday + clockNoteSynced).

#include "common.h"
#include "clock.h"
#include <sys/time.h>
#include "esp_sntp.h"

namespace timesvc {

struct Zone { const char *city, *label, *tz; };
static const Zone ZONES[CLOCK_NUM_ZONES] = {
  {"Zurich",   "Europe/Zurich",       "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"London",   "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Lisbon",   "Europe/Lisbon",       "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Athens",   "Helsinki / Athens",   "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Moscow",   "Europe/Moscow",       "MSK-3"},
  {"Dubai",    "Asia/Dubai",          "GST-4"},
  {"India",    "India (Kolkata)",     "IST-5:30"},
  {"Bangkok",  "Asia/Bangkok",        "ICT-7"},
  {"Shanghai", "Shanghai / Singapore", "CST-8"},
  {"Tokyo",    "Asia/Tokyo",          "JST-9"},
  {"Sydney",   "Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"New York", "America/New_York",    "EST5EDT,M3.2.0,M11.1.0"},
  {"Chicago",  "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0"},
  {"Denver",   "America/Denver",      "MST7MDT,M3.2.0,M11.1.0"},
  {"L.A.",     "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"UTC",      "UTC",                 "UTC0"},
};

static const time_t RESYNC_S = 6L * 3600;      // SNTP polls hourly by itself; this catches long outages
static const uint32_t RETRY_MS = 60000;        // restart SNTP if a start got no answer in this time
static const uint32_t HOLD_MS = 60000;         // WiFi is held this long for a sync at most
// a sync that got nowhere (no network around) waits this long before the next try
static const uint32_t TRY_UNSET_MS = 15UL * 60000, TRY_SET_MS = 60UL * 60000;
static const char HOLDER[] = "clock";
static const char *PREF_ZONE = "clk_zone";

static bool inited = false, sntpStarted = false;
static int zone = 0;
static uint32_t startedAt = 0;
#if !TTGO_CARTRIDGE
static uint32_t nextTryAt = 0;                 // the next WiFi hold for a sync
static bool tried = false;
#endif
static volatile bool syncFlag = false;         // set from the lwIP task
static RTC_DATA_ATTR time_t lastSync = 0;
static RTC_DATA_ATTR uint8_t lastSrc = CLOCK_SRC_NONE;

static int clampZone(int i) { return i >= 0 && i < CLOCK_NUM_ZONES ? i : 0; }

static void applyTz() {
  setenv("TZ", ZONES[zone].tz, 1);
  tzset();
}

// lazy, so it works whoever asks first (prefs is open before loop() runs)
static void ensureInit() {
  if (inited) return;
  inited = true;
  zone = clampZone(prefs.getUChar(PREF_ZONE, 0));
  applyTz();
}

static void onSync(struct timeval *tv) { syncFlag = true; }

static void startSntp(uint32_t now) {
  sntp_set_time_sync_notification_cb(onSync);
  configTzTime(ZONES[zone].tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  sntpStarted = true;
  startedAt = now;
  dbg("time: SNTP started (%s)\n", ZONES[zone].city);
}

static bool needsSync() {
  if (!timeIsSet() || lastSync == 0) return true;
  time_t age = time(nullptr) - lastSync;
  return age < 0 || age > RESYNC_S;
}

}  // namespace timesvc

// ---------------- public API ----------------
void timeService(uint32_t now) {
  using namespace timesvc;
  ensureInit();
  if (syncFlag) {
    syncFlag = false;
    lastSync = time(nullptr);
    lastSrc = CLOCK_SRC_NTP;
    struct tm tm;
    localTimeNow(&tm);
    dbg("time: NTP sync %02d:%02d:%02d %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, ZONES[zone].city);
  }
#if !TTGO_CARTRIDGE
  // WiFi's grace time and retries; here because every loop (screensaver too) runs this
  wifiService(now);
  bool need = needsSync();
  if (!need) {
    if (wifiHeldBy(HOLDER)) wifiRelease(HOLDER);   // synced (NTP or the phone)
  } else if (!wifiHeldBy(HOLDER) && wifiEnabled() && wifiSavedSsid()[0] &&
             (!tried || (int32_t)(now - nextTryAt) >= 0)) {
    tried = true;
    nextTryAt = now + (timeIsSet() ? TRY_SET_MS : TRY_UNSET_MS);
    wifiAcquireFor(HOLDER, HOLD_MS);
  }
#endif
  if (!wifiConnected() || !needsSync()) return;
  if (!sntpStarted || (int32_t)(now - startedAt) >= (int32_t)RETRY_MS) startSntp(now);
}

bool timeIsSet() { return time(nullptr) > 1700000000; }

bool localTimeNow(struct tm *out) {
  timesvc::ensureInit();
  if (!timeIsSet()) return false;
  time_t t = time(nullptr);
  localtime_r(&t, out);
  return true;
}

int clockZone() {
  timesvc::ensureInit();
  return timesvc::zone;
}

void setClockZone(int idx, bool save) {
  using namespace timesvc;
  ensureInit();
  zone = clampZone(idx);
  applyTz();
  if (save) prefs.putUChar(PREF_ZONE, zone);
  dbg("time: zone %s\n", ZONES[zone].label);
}

const char *clockZoneCity(int idx) { return timesvc::ZONES[timesvc::clampZone(idx)].city; }
const char *clockZoneLabel(int idx) { return timesvc::ZONES[timesvc::clampZone(idx)].label; }
const char *clockZoneTz(int idx) { return timesvc::ZONES[timesvc::clampZone(idx)].tz; }

int clockUtcOffsetMin() {
  timesvc::ensureInit();
  time_t t = timeIsSet() ? time(nullptr) : 1700000000;   // any date works for the standard offset
  struct tm lt, gt;
  localtime_r(&t, &lt);
  gmtime_r(&t, &gt);
  int days = lt.tm_year != gt.tm_year ? (lt.tm_year > gt.tm_year ? 1 : -1) : lt.tm_yday - gt.tm_yday;
  return days * 1440 + (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
}

void clockFmtOffset(char *buf, size_t n) {
  int off = clockUtcOffsetMin();
  int a = off < 0 ? -off : off;
  if (a % 60) snprintf(buf, n, "UTC%c%d:%02d", off < 0 ? '-' : '+', a / 60, a % 60);
  else if (a) snprintf(buf, n, "UTC%c%d", off < 0 ? '-' : '+', a / 60);
  else snprintf(buf, n, "UTC");
}

time_t clockLastSync() { return timesvc::lastSync; }
ClockSource clockSyncSource() { return (ClockSource)timesvc::lastSrc; }
bool clockSyncing() {
#if !TTGO_CARTRIDGE
  if (wifiHeldBy(timesvc::HOLDER) && timesvc::needsSync()) return true;   // WiFi coming up for it
#endif
  return timesvc::sntpStarted && wifiConnected() && timesvc::needsSync();
}

void clockNoteSynced(ClockSource src) {
  timesvc::ensureInit();
  timesvc::lastSync = time(nullptr);
  timesvc::lastSrc = src;
  dbg("time: set from %s\n", src == CLOCK_SRC_PHONE ? "phone" : "NTP");
}

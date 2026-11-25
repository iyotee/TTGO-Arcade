// USB logging (switchable in Settings) and a record of the last crash.
//
// The crash record survives the reset: the active menu item is kept in RTC
// memory, which a crash or watchdog reset doesn't clear, and on the next boot
// the reset reason plus that item are saved to NVS for the Settings screen.
// The item's name is kept too: a crash in the Big Games cartridge is read by
// the launcher, whose item list (and so its indexes) is not the cartridge's.

#include "common.h"
#include <stdarg.h>
#include "crashrec.h"

#ifndef FW_BUILD
#define FW_BUILD 0
#endif

static bool usbLog = false;

static const uint32_t RTC_MAGIC = 0xC0DEBEEF;           // tells real values from power-on garbage
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static int32_t rtcItem;
// after the older fields, so their places stay the same for older images
static const uint32_t RTC_NAME_MAGIC = 0x4E414D45;      // "NAME": rtcName is valid
RTC_NOINIT_ATTR static uint32_t rtcNameMagic;
RTC_NOINIT_ATTR static char rtcName[16];
static const char *(*nameOf)(int) = nullptr;

// the last ~2 KB of log lines stay in RAM even with the USB log off, so the
// OTA screen's diagnostics page (otaui.cpp, /log) can show them over WiFi
static const size_t RING_SIZE = 2048;
static char ring[RING_SIZE];
static size_t ringHead = 0;
static bool ringFull = false;
static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;

static void ringPut(const char *s) {
  portENTER_CRITICAL(&ringMux);                // the update task logs too
  for (; *s; s++) {
    ring[ringHead++] = *s;
    if (ringHead == RING_SIZE) { ringHead = 0; ringFull = true; }
  }
  portEXIT_CRITICAL(&ringMux);
}

size_t dbgRingCopy(char *out, size_t n) {
  if (!n) return 0;
  size_t k = 0;
  portENTER_CRITICAL(&ringMux);
  size_t start = ringFull ? ringHead : 0, len = ringFull ? RING_SIZE : ringHead;
  for (size_t i = 0; i < len && k + 1 < n; i++) out[k++] = ring[(start + i) % RING_SIZE];
  portEXIT_CRITICAL(&ringMux);
  out[k] = 0;
  return k;
}

void dbg(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  char stamp[16];
  snprintf(stamp, sizeof stamp, "%7.1f ", millis() / 1000.0f);
  ringPut(stamp);
  ringPut(buf);
  if (usbLog) Serial.print(buf);
}

bool usbLogOn() { return usbLog; }

void setUsbLog(bool on) {
  if (on == usbLog) return;
  if (on) Serial.begin(115200);
  else { Serial.flush(); Serial.end(); }
  usbLog = on;
  prefs.putBool("usblog", on);
  dbg("USB log on\n");
}

void initDiag(const char *(*itemName)(int)) {
  nameOf = itemName;
  usbLog = prefs.getBool("usblog", false);
  if (usbLog) Serial.begin(115200);

  esp_reset_reason_t why = esp_reset_reason();
  const char *kind = nullptr;
  switch (why) {
    case ESP_RST_PANIC:    kind = "crash"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      kind = "watchdog"; break;
    case ESP_RST_BROWNOUT: kind = "brownout"; break;
    case ESP_RST_POWERON:
      // RTC memory survives a pulse on the EN (reset) line, e.g. from the USB
      // chip, but not a real power loss: so a valid record means "reset"
      if (rtcMagic == RTC_MAGIC && rtcItem >= 0) kind = "reset";
      break;
    default: break;
  }
  // the name written by the firmware that ran (launcher or cartridge), else our own list
  const char *item = "?";
  if (rtcMagic == RTC_MAGIC) {
    rtcName[sizeof rtcName - 1] = 0;
    item = rtcNameMagic == RTC_NAME_MAGIC && rtcName[0] ? rtcName : itemName(rtcItem);
  }
  if (kind) {
    char buf[40];
    snprintf(buf, sizeof buf, "%s: %s b%u", kind, item, (unsigned)FW_BUILD);   // "! crash: Settings b5" fits 135 px
    prefs.putString("lastcrash", buf);
    if (why != ESP_RST_POWERON) crashRecOnBoot(item);   // backtrace from the panic handler, if any
  }
  dbg("\n=== boot: reset reason %d%s%s ===\n", (int)why, kind ? ", " : "", kind ? kind : "");
  noteActiveItem(-1);
}

void noteActiveItem(int item) {
  rtcItem = item;
  snprintf(rtcName, sizeof rtcName, "%s", nameOf ? nameOf(item) : "");
  rtcNameMagic = RTC_NAME_MAGIC;
  rtcMagic = RTC_MAGIC;
}

bool lastCrash(char *buf, size_t n) {
  return prefs.getString("lastcrash", buf, n) > 0;
}

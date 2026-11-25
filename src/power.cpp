// Backlight brightness, battery voltage, screensaver / sleep delays and deep
// sleep for the T-Display.
//
// Idle flow: no input for the screensaver delay -> goToSleep() shows Tigo's
// screensaver; any button wakes it (then the PIN lock if on, then the menu).
// After the sleep delay in the screensaver -> deep sleep. With the screensaver
// Off, the sleep delay is the idle time before deep sleep.

#include "common.h"
#include "power.h"
#include "lockscr.h"
#include "ble.h"
#include "esp_sleep.h"

#define ADC_EN_PIN  14                // switches the battery voltage divider on
#define BATT_PIN    34                // battery voltage / 2
#define BL_CHANNEL  0                 // LEDC (PWM) channel for the backlight

static const uint8_t BRIGHT_DUTY[NUM_BRIGHTNESS] = {8, 25, 60, 130, 255};   // roughly even steps to the eye
static int brightness = NUM_BRIGHTNESS - 1;

// ---------------- idle delays ----------------
// saved under new keys; the old "sleep" key (30s/60s/2m/5m/Off) is ignored
static const uint32_t SAVER_MS[NUM_SAVER_OPTS] = {
  0UL, 5000UL, 8000UL, 10000UL, 15000UL, 20000UL, 30000UL, 60000UL, 120000UL, 300000UL};
static const char *SAVER_LABELS[NUM_SAVER_OPTS] = {"Off", "5s", "8s", "10s", "15s", "20s", "30s", "1m", "2m", "5m"};
static const int SAVER_DEFAULT = 4;        // 15s

static const uint32_t SLEEPDLY_MS[NUM_SLEEPDLY_OPTS] = {
  5000UL, 8000UL, 10000UL, 15000UL, 20000UL, 30000UL, 60000UL, 120000UL, 300000UL, 600000UL, 0UL};
static const char *SLEEPDLY_LABELS[NUM_SLEEPDLY_OPTS] = {"5s", "8s", "10s", "15s", "20s", "30s", "1m", "2m", "5m", "10m", "Never"};
static const int SLEEPDLY_DEFAULT = 6;     // 1m

static int saverOpt = SAVER_DEFAULT;
static int sleepDlyOpt = SLEEPDLY_DEFAULT;

void initPower() {
  // undo the pin holds from the previous deep sleep
  gpio_hold_dis((gpio_num_t)TFT_BL);
  gpio_hold_dis((gpio_num_t)ADC_EN_PIN);
  gpio_deep_sleep_hold_dis();
  pinMode(ADC_EN_PIN, OUTPUT);
  digitalWrite(ADC_EN_PIN, LOW);       // divider only on while measuring
}

void initBacklight() {
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  setBrightness(prefs.getUChar("bright", NUM_BRIGHTNESS - 1), false);
  saverOpt = constrain((int)prefs.getUChar("saverdly", SAVER_DEFAULT), 0, NUM_SAVER_OPTS - 1);
  sleepDlyOpt = constrain((int)prefs.getUChar("sleepdly", SLEEPDLY_DEFAULT), 0, NUM_SLEEPDLY_OPTS - 1);
}

void setBrightness(int level, bool save) {
  brightness = constrain(level, 0, NUM_BRIGHTNESS - 1);
  ledcWrite(BL_CHANNEL, BRIGHT_DUTY[brightness]);
  if (save) prefs.putUChar("bright", brightness);
}

int brightnessLevel() { return brightness; }

int saverOption() { return saverOpt; }
void setSaverOption(int opt, bool save) {
  saverOpt = constrain(opt, 0, NUM_SAVER_OPTS - 1);
  if (save) prefs.putUChar("saverdly", saverOpt);
}
const char *saverLabel() { return SAVER_LABELS[saverOpt]; }
uint32_t saverDelayMs() { return SAVER_MS[saverOpt]; }

int sleepDelayOption() { return sleepDlyOpt; }
void setSleepDelayOption(int opt, bool save) {
  sleepDlyOpt = constrain(opt, 0, NUM_SLEEPDLY_OPTS - 1);
  if (save) prefs.putUChar("sleepdly", sleepDlyOpt);
}
const char *sleepDelayLabel() { return SLEEPDLY_LABELS[sleepDlyOpt]; }
uint32_t sleepDelayMs() { return SLEEPDLY_MS[sleepDlyOpt]; }

// common.h names kept for compatibility: they now mean the sleep delay
// (0 .. NUM_SLEEPDLY_OPTS - 1, not NUM_SLEEP_OPTS)
int sleepOption() { return sleepDelayOption(); }
void setSleepOption(int opt, bool save) { setSleepDelayOption(opt, save); }
const char *sleepLabel() { return sleepDelayLabel(); }

// idle time before goToSleep() fires, 0 = never
uint32_t idleSleepMs() { return saverDelayMs() ? saverDelayMs() : sleepDelayMs(); }

int batteryMilliVolts() {
  digitalWrite(ADC_EN_PIN, HIGH);
  delay(5);                            // let the divider settle
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(BATT_PIN);   // calibrated reading
  digitalWrite(ADC_EN_PIN, LOW);
  return sum / 16 * 2;
}

int batteryPercent(int mv) {
  // typical LiPo discharge curve under light load
  static const int16_t MV[]  = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int8_t  PCT[] = {   0,    5,   10,   20,   35,   50,   65,   80,   90,  100};
  if (mv <= MV[0]) return 0;
  for (int i = 1; i < 10; i++)
    if (mv < MV[i]) return PCT[i - 1] + (PCT[i] - PCT[i - 1]) * (mv - MV[i - 1]) / (MV[i] - MV[i - 1]);
  return 100;
}

// with USB connected the pin sees the ~5 V supply instead of the battery
bool onUsbPower(int mv) { return mv >= 4350; }

// ---------------- sleep ----------------
void goToSleep() {
#if !TTGO_CARTRIDGE
  if (bleInstallActive()) return;      // the phone is writing a cartridge: stay awake
#endif
  if (!saverDelayMs()) { deepSleepNow(); return; }

  dbg("screensaver\n");
  int level = brightnessLevel();       // the screensaver may dim the backlight
  uint32_t start = millis();
  saver::begin();
  for (;;) {
    uint32_t now = millis();
    if (!saver::update(now)) break;    // a button woke it
    timeService(now);
    bleService(now);
    uint32_t lim = sleepDelayMs();
    if (lim && (int32_t)(now - start) >= (int32_t)lim) {
#if !TTGO_CARTRIDGE
      // an install that began behind the screensaver: wait another sleep delay
      if (bleInstallActive()) start = now;
      else
#endif
        deepSleepNow();
    }
    delay(2);
  }
  setBrightness(level, false);
  dbg("screensaver woken\n");

  // the waking press is still down: the lock screen and the menu only act on
  // presses that start on them (fresh pressed flags), so it can't select anything
  if (lockscr::enabled()) lockscr::run();
  returnToMenu();
}

void deepSleepNow() {
  dbg("going to sleep\n");
  noteActiveItem(-1);                  // a wake by the RST button is no "reset in <game>"
  wifiShutdown();
  if (usbLogOn()) Serial.flush();
  spr.fillSprite(TFT_BLACK);
  spr.pushSprite(0, 0);
  tft.writecommand(0x28);              // DISPOFF
  tft.writecommand(0x10);              // SLPIN: panel controller into its own sleep mode
  delay(5);

  // backlight and battery divider firmly off, held through deep sleep
  ledcDetachPin(TFT_BL);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  digitalWrite(ADC_EN_PIN, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL);
  gpio_hold_en((gpio_num_t)ADC_EN_PIN);
  gpio_deep_sleep_hold_en();

  while (digitalRead(WAKE_PIN) == LOW) delay(10);   // don't wake instantly

  // ext1 (unlike ext0) lets the RTC peripherals power down; the button has
  // an external pull-up on the board, so no internal pull-up is needed
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();
}

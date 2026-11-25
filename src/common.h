// Shared hardware, input and drawing helpers for the T-Display games.
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "driver/gpio.h"
#include <time.h>

// ---------------- configuration ----------------
#define SCREEN_ROTATION 0        // 0 or 2 (both portrait). Use 2 if the picture is upside down.

#if SCREEN_ROTATION == 0
  #define PIN_LEFT  0
  #define PIN_RIGHT 35
#else
  #define PIN_LEFT  35
  #define PIN_RIGHT 0
#endif
// If left/right feel swapped in your orientation, just swap the two pin numbers above.

#define WAKE_PIN        GPIO_NUM_35
#define DEBOUNCE_MS     15
#define IDLE_SLEEP_MS   60000UL

// True after idleSleepMs() without input (0 = auto-sleep disabled).
// Signed difference: lastInput may be a millis() taken *after* `now`
// (e.g. stamped at game over during this loop), and must then count as
// "just now", not as an unsigned wrap to ~49 days.
uint32_t idleSleepMs();
static inline bool idleTooLong(uint32_t now, uint32_t lastInput) {
  uint32_t lim = idleSleepMs();
  return lim > 0 && (int32_t)(now - lastInput) > (int32_t)lim;
}

extern TFT_eSPI tft;
extern TFT_eSprite spr;
extern Preferences prefs;

// ---------------- buttons ----------------
struct Btn {
  uint8_t pin;
  bool raw = false, stable = false;
  uint32_t rawT = 0, t0 = 0, nextRepeat = 0;
  bool held = false, consumed = false;
  explicit Btn(uint8_t p) : pin(p) {}
};
extern Btn bL, bR;

// returns 0 = nothing, 1 = pressed, 2 = released
int updateBtn(Btn &b, uint32_t now);

// Button events waiting for the next fixed game tick. The loop runs faster than
// the 16 ms tick, so an event seen in a loop pass without a tick would be lost;
// queued, it reaches the next tick instead. Holds two (a press and its release).
struct EvQ {
  int8_t q[2];
  EvQ() { clear(); }
  void push(int e) { if (!e) return; if (!q[0]) q[0] = e; else q[1] = e; }
  int pop() { int e = q[0]; q[0] = q[1]; q[1] = 0; return e; }
  void clear() { q[0] = q[1] = 0; }
};

void drawCell(int px, int py, int sz, uint16_t col);
// 0 -> 1 -> 0 over `period` ms, for back-and-forth icon animations
float triWave(uint32_t t, uint32_t period);

// ---------------- power (power.cpp) ----------------
static const int NUM_BRIGHTNESS = 5;
static const int NUM_SLEEP_OPTS = 5;
void initPower();                     // first thing in setup()
void initBacklight();                 // after tft.init() and prefs.begin()
void setBrightness(int level, bool save);
int brightnessLevel();                // 0 .. NUM_BRIGHTNESS - 1
int sleepOption();                    // 0 .. NUM_SLEEP_OPTS - 1
void setSleepOption(int opt, bool save);
const char *sleepLabel();
int batteryMilliVolts();
int batteryPercent(int mv);
bool onUsbPower(int mv);
void goToSleep();                     // idle action: screensaver (if on), later deep sleep
void deepSleepNow();                  // deep sleep right away; the WAKE_PIN button restarts the board

void returnToMenu();
int numGames();
const char *gameInfo(int idx, const char **hiKey);

// ---------------- WiFi (wifi.cpp) ----------------
enum WpsState { WPS_IDLE, WPS_RUNNING, WPS_GOT_CREDS, WPS_FAILED, WPS_TIMEOUT };
void initWifi();                      // in setup(), after prefs.begin(): rejoins the saved network if WiFi is on
bool wifiEnabled();
void setWifiEnabled(bool on);         // saved; off also powers the radio down
bool wifiConnected();
const char *wifiSavedSsid();          // "" if none
int wifiRssi();                       // dBm, -127 when not connected
void wifiIp(char *buf, size_t n);
void wifiNoteConnected();             // call once connected, to remember the network name
const char *wifiFailReason();         // "" or a short hint after a failed attempt
void wifiStartScan();                 // asynchronous
int wifiScanResult();                 // -1 running, -2 failed, else number of networks
void wifiScanFree();
void wifiConnect(const char *ssid, const char *pass);
void wifiStartWps();                  // push-button WPS, the router side times out after 2 min
int wifiWpsState();
void wifiStopWps();
void wifiForget();
void wifiShutdown();                  // before deep sleep

// ---------------- themes (theme.cpp) ----------------
// UI palette; "Original" is the default, "Neon" goes with neon Tigo
struct Theme {
  bool neon;
  uint16_t bg, panel, frame, frameSel, text, textDim, accent, accent2, ok, keyHint;
};
static const int NUM_THEMES = 2;
const Theme &theme();
int themeIndex();
void setThemeIndex(int i);            // saved
const char *themeName(int i);
// an outlined rounded box; in the neon theme it gets a soft glow around it
void drawThemedBox(int x, int y, int w, int h, int r, uint16_t col, bool glow);

// ---------------- cartridges (cart.cpp) ----------------
// Heavy games (Abyx, Crypt, ...) live in a second firmware image, the "Big Games"
// cartridge, stored in the other app slot. The launcher greys them out until the
// cartridge is installed; starting one reboots into the cartridge, which always
// re-arms the launcher as the next boot.
enum CartState { CART_NONE, CART_READY, CART_OUTDATED };
bool cartIsHeavy(const char *hiKey);  // this game lives in the cartridge
int cartState();
bool cartLaunch(const char *hiKey);   // reboots into the cartridge on success (does not return)
// streaming install into the other app slot (WiFi downloader and Bluetooth use it)
bool cartInstallBegin(uint32_t size, const uint8_t *sha256, uint32_t build, char *err, size_t errN);
bool cartInstallWrite(const uint8_t *data, size_t n);
bool cartInstallEnd(char *err, size_t errN);
void cartInstallAbort();
int cartInstallProgress();            // 0..100

// ---------------- OTA (otaui.cpp, opened from Settings) ----------------
namespace otaui { void open(); bool update(uint32_t now); }   // update(): false when leaving

// ---------------- idle: screensaver and PIN lock ----------------
namespace saver { void begin(); bool update(uint32_t now); }     // update(): false once a button woke it
namespace lockscr { bool enabled(); void begin(); bool update(uint32_t now); }   // update(): false once unlocked

// ---------------- Tigo, the hermit-crab mascot (tigo.cpp) ----------------
enum TigoPose { TIGO_IDLE, TIGO_WALK, TIGO_EAT, TIGO_SLEEP, TIGO_HAPPY, TIGO_SAD, TIGO_PINCH, TIGO_HIDE };
// (cx, cy) = centre of the bottom of the shell, size = overall height in px, t = animation clock in ms
void drawTigo(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft);

// ---------------- time (clock.cpp) ----------------
void timeService(uint32_t now);       // called from loop(): NTP sync once WiFi is up
bool timeIsSet();
bool localTimeNow(struct tm *out);    // false until the time is known

// ---------------- Bluetooth (ble.cpp) ----------------
void initBle();                       // in setup(): starts BLE if it was left on
void bleService(uint32_t now);        // called from loop()
bool bleEnabled();
void setBleEnabled(bool on);          // saved
bool bleConnected();
bool bleTakeMessage(char *buf, size_t n);   // text sent from the companion page, for Tigo's bubbles
int bleTakeTreats();                  // treats sent from the phone since the last call

// ---------------- diagnostics (diag.cpp) ----------------
void initDiag(const char *(*itemName)(int));   // after prefs.begin(); item -1 = menu
void dbg(const char *fmt, ...);       // printf to USB serial when USB logging is on
bool usbLogOn();
void setUsbLog(bool on);
void noteActiveItem(int item);        // remembered across a crash reset
bool lastCrash(char *buf, size_t n);  // e.g. "crash in Flappy"; false if none recorded

// ---------------- shared screens ----------------
static const int HEADER_H = 38;  // games keep rows 0..37 for score text

// "Score N" / "Best N" in the header area (cleared first)
void drawScoreHeader(uint32_t score, uint32_t best);
// box over the playfield after a game ends
void drawGameOverBox(const char *title, uint32_t score, uint32_t best, bool newHigh);
// Title screens: any button starts. Game-over screens: L = back to the menu
// (already done on return), R = play again. Acts on the release of a press
// that started on the screen.
enum EndChoice { END_NONE, END_PLAY, END_MENU };
EndChoice startOrEnd(bool gameOver, int eL, int eR, bool &armed);
// Tools: "L <l>   R <r>" at the bottom, with a "hold L: menu" hint above it
void drawToolFooter(const char *l, const char *r);
static const uint32_t TOOL_HOLD_MS = 600;   // hold L this long to leave a tool

// ---------------- games ----------------
// Each game: begin() shows its title screen, update() runs one loop iteration,
// drawIcon() draws a menu icon of at most ~24x30 centred on (cx, cy); t is an
// animation clock in ms (0 = still picture), drawing is clipped to the icon area.
#define DECLARE_GAME(ns) \
  namespace ns { \
    void begin(); \
    void update(uint32_t now); \
    void drawIcon(int cx, int cy, bool dim, uint32_t t = 0); \
  }
DECLARE_GAME(tetris)
DECLARE_GAME(snake)
DECLARE_GAME(race)
DECLARE_GAME(flappy)
DECLARE_GAME(tennis)
DECLARE_GAME(bricks)
DECLARE_GAME(stack)
DECLARE_GAME(shooter)
DECLARE_GAME(crossy)
DECLARE_GAME(pinball)
DECLARE_GAME(doodle)
DECLARE_GAME(falldown)
DECLARE_GAME(invaders)
DECLARE_GAME(rhythm)
DECLARE_GAME(dino)
DECLARE_GAME(cave)
DECLARE_GAME(game2048)
DECLARE_GAME(columns)
DECLARE_GAME(asteroids)
DECLARE_GAME(simon)
DECLARE_GAME(ski)
DECLARE_GAME(catchgame)
DECLARE_GAME(missile)
DECLARE_GAME(lunar)
DECLARE_GAME(golf)
DECLARE_GAME(memory)
DECLARE_GAME(mines)
DECLARE_GAME(maze)
DECLARE_GAME(bumper)
DECLARE_GAME(climb)
DECLARE_GAME(river)
DECLARE_GAME(pong2p)
DECLARE_GAME(whack)
DECLARE_GAME(duet)
DECLARE_GAME(blackjack)
DECLARE_GAME(tron)
DECLARE_GAME(frog)
DECLARE_GAME(pop)
DECLARE_GAME(lights)
DECLARE_GAME(hilow)
DECLARE_GAME(mathblitz)
DECLARE_GAME(codebreak)
DECLARE_GAME(drop4)
DECLARE_GAME(chute)
DECLARE_GAME(plinko)
DECLARE_GAME(swing)
DECLARE_GAME(tank)
DECLARE_GAME(gravity)
DECLARE_GAME(slots)
DECLARE_GAME(subgame)
DECLARE_GAME(knife)
DECLARE_GAME(dodge)
DECLARE_GAME(fish)
DECLARE_GAME(bowl)
DECLARE_GAME(reversi)
DECLARE_GAME(fringe)
DECLARE_GAME(pool)
DECLARE_GAME(abyx)
DECLARE_GAME(cryptgame)               // "crypt" alone clashes with libc crypt()

DECLARE_GAME(timer)                   // tools (use the same entry points)
namespace timer { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(torch)
namespace torch { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(dice)
namespace dice { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(tally)
namespace tally { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(minute)
namespace minute { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(morse)
namespace morse { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(breathe)
namespace breathe { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(tigoapp)                 // Tigo: feed / play / clean the mascot
namespace tigoapp { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(clockapp)
namespace clockapp { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(companionapp)            // QR code to the companion web page + Bluetooth status
namespace companionapp { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(support)                 // QR code to the Buy Me a Coffee page
namespace support { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(remote)                  // Bluetooth remote (camera / music / slides)
namespace remote { void menuInfo(char *buf, size_t n); }
DECLARE_GAME(settings)                // not a game, but uses the same entry points
namespace settings { void menuInfo(char *buf, size_t n); }

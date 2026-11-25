// "Big Games" cartridge firmware (env 'cartridge'): the heavy games without
// the launcher. The launcher names the game in "cart_game" and reboots into
// this slot; we re-arm the launcher as the next boot first thing, run that one
// game, and reboot into the launcher on returnToMenu() (L on the game-over
// screen, or after the screensaver). A reset, a crash or a wake from deep
// sleep also ends up in the launcher. Scores, theme and settings live in the
// shared NVS, so they are the same as in the launcher.
//
//   Controls: the game's own. "Back to launcher" screen: none (reboots by itself).
//
// WiFi and Bluetooth are not linked in: the stubs below answer "off" for the
// shared code (screensaver, power) that asks.

#include "common.h"
#include "cart.h"
#include "ui_common.h"

namespace cartmain {

struct Entry { void (*begin)(); void (*update)(uint32_t now); };
// same order as CART_GAMES (cart.h)
static const Entry ENTRIES[] = {
  {abyx::begin, abyx::update},
  {cryptgame::begin, cryptgame::update},
};
static_assert(sizeof ENTRIES / sizeof ENTRIES[0] == CART_NUM_GAMES, "one entry per CART_GAMES row");

static int active = -1;

static const char *itemName(int i) { return i >= 0 && i < CART_NUM_GAMES ? CART_GAMES[i].name : "cartridge"; }

}  // namespace cartmain

// ---------------- common.h hooks ----------------
void returnToMenu() { cartExitToLauncher(); }
int numGames() { return CART_NUM_GAMES; }

const char *gameInfo(int idx, const char **hiKey) {
  if (idx < 0 || idx >= CART_NUM_GAMES) {
    if (hiKey) *hiKey = nullptr;
    return "";
  }
  if (hiKey) *hiKey = CART_GAMES[idx].hiKey;
  return CART_GAMES[idx].name;
}

// ---------------- no radios in the cartridge ----------------
void initWifi() {}
bool wifiEnabled() { return false; }
void setWifiEnabled(bool) {}
bool wifiConnected() { return false; }
const char *wifiSavedSsid() { return ""; }
int wifiRssi() { return -127; }
void wifiIp(char *buf, size_t n) { snprintf(buf, n, "-"); }
void wifiNoteConnected() {}
const char *wifiFailReason() { return ""; }
void wifiShutdown() {}
void initBle() {}
void bleService(uint32_t) {}
bool bleEnabled() { return false; }
void setBleEnabled(bool) {}
bool bleConnected() { return false; }
bool bleTakeMessage(char *, size_t) { return false; }
int bleTakeTreats() { return 0; }

// ---------------- main ----------------
void setup() {
  cartRearmLauncher();             // before anything can crash: the next boot is the launcher's
  bootHardware();
  initDiag(cartmain::itemName);
  initBacklight();

  using namespace cartmain;
  active = cartGameIndex(cartRequestedGame());
  dbg("cartridge build %u: game '%s'\n", (unsigned)FW_BUILD, cartRequestedGame());
  if (active < 0) cartExitToLauncher();        // started without a request: straight back
  noteActiveItem(active);
  ENTRIES[active].begin();
}

void loop() {
  uint32_t now = millis();
  cartmain::ENTRIES[cartmain::active].update(now);
  timeService(now);
  delay(2);
}

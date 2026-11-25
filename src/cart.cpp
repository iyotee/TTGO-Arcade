// Cartridges (cart.h): the "Big Games" image in the app slot the launcher is
// not running from.
//
// Launcher: streaming installs into the other slot (the WiFi downloader in
// updates.cpp, Bluetooth in ble.cpp) checked against the manifest's SHA-256 and
// validated by esp_ota_end(), a record of what is installed (prefs), and the
// hand-over: cartLaunch() names the game in "cart_game", makes the other slot
// the boot partition and restarts.
// Cartridge: re-arms the launcher as the next boot at once (so any reset,
// crash or wake from deep sleep lands in the launcher) and goes back to it on
// returnToMenu().
//
// No controls: the screens that use it read the buttons.

#include "common.h"
#include "cart.h"
#include "tigo.h"
#include "themeui.h"
#include "orient.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#if !TTGO_CARTRIDGE
#include "updates.h"
#endif

namespace cart {

// prefs keys (<= 15 chars)
static const char K_BUILD[] = "cart_build";   // u32, build of the installed cartridge
static const char K_SIZE[]  = "cart_size";    // u32, its file size
static const char K_SHA[]   = "cart_sha";     // 32 bytes, SHA-256 of the file (as in the manifest)
static const char K_PSHA[]  = "cart_psha";    // 32 bytes, esp_partition_get_sha256() right after install
static const char K_SLOT[]  = "cart_slot";    // label of the partition it went into
static const char K_GAME[]  = "cart_game";    // launcher -> cartridge: hiKey of the game to start
static const char K_BACK[]  = "cart_back";    // cartridge -> launcher: returned cleanly

// with two OTA slots the "next update" partition is simply the other one
static const esp_partition_t *otherSlot() { return esp_ota_get_next_update_partition(nullptr); }

// "Starting / Abyx", "Back to / Launcher": shown while the board restarts
static void handoverScreen(const char *top, const char *name) {
  const Theme &th = theme();
  spr.resetViewport();
  resetOrient();                                // a landscape game may not have turned the panel back
  spr.fillSprite(th.bg);
  drawTigo(67, 112, 50, TIGO_WALK, millis(), false);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(themeSoft());
  spr.drawString(top, 67, 132, 2);
  spr.setTextColor(th.neon ? th.frameSel : th.text);
  spr.drawString(name, 67, 152, 4);
  spr.pushSprite(0, 0);
}

#if !TTGO_CARTRIDGE
// ---------------- install record ----------------
static bool recLoaded = false;
static uint32_t recBuild = 0;
static uint8_t recPsha[32];

static void loadRecord() {
  if (recLoaded) return;
  recLoaded = true;
  recBuild = 0;
  const esp_partition_t *o = otherSlot();
  if (!o || !prefs.isKey(K_BUILD) || !prefs.isKey(K_SLOT) || !prefs.isKey(K_PSHA)) return;
  char slot[17] = "";
  prefs.getString(K_SLOT, slot, sizeof slot);
  if (strcmp(slot, o->label)) return;           // the slots swapped: a launcher update went in since
  if (prefs.getBytes(K_PSHA, recPsha, sizeof recPsha) != sizeof recPsha) return;
  recBuild = prefs.getUInt(K_BUILD, 0);
}

static void forget() {
  static const char *const KEYS[] = {K_BUILD, K_SIZE, K_SHA, K_PSHA, K_SLOT};
  for (const char *k : KEYS)
    if (prefs.isKey(k)) prefs.remove(k);
  recLoaded = true;
  recBuild = 0;
}

// ---------------- slot writer (cartridge or launcher update) ----------------
enum Kind { W_NONE, W_CART, W_FW };
static volatile int wKind = W_NONE;
static portMUX_TYPE wMux = portMUX_INITIALIZER_UNLOCKED;
static esp_ota_handle_t wHandle = 0;
static const esp_partition_t *wPart = nullptr;
static uint32_t wSize = 0, wBuild = 0;
static volatile uint32_t wDone = 0;
static bool wFailed = false, wHaveSha = false;
static uint8_t wSha[32];
static mbedtls_sha256_context wCtx;
static char wErr[32] = "";

static bool fail(char *err, size_t n, const char *msg) {
  if (err && n) snprintf(err, n, "%s", msg);
  dbg("cart: %s\n", msg);
  return false;
}

static void release() {
  mbedtls_sha256_free(&wCtx);
  wKind = W_NONE;
}

static bool writerBegin(int kind, uint32_t size, const uint8_t *sha, uint32_t build, char *err, size_t n) {
  portENTER_CRITICAL(&wMux);                    // WiFi task and main loop (Bluetooth) may race here
  bool busy = wKind != W_NONE;
  if (!busy) wKind = kind;
  portEXIT_CRITICAL(&wMux);
  if (busy) return fail(err, n, "already installing");

  const esp_partition_t *p = otherSlot();
  if (!p || p == esp_ota_get_running_partition()) { wKind = W_NONE; return fail(err, n, "no free slot"); }
  if (!size) { wKind = W_NONE; return fail(err, n, "empty image"); }
  if (size > p->size) { wKind = W_NONE; return fail(err, n, "not enough space"); }

  forget();                                     // its flash is about to be overwritten
  // sequential writes: erased sector by sector as data comes, no long freeze up front
  esp_err_t e = esp_ota_begin(p, OTA_WITH_SEQUENTIAL_WRITES, &wHandle);
  if (e != ESP_OK) {
    wKind = W_NONE;
    char msg[24];
    snprintf(msg, sizeof msg, "flash error %d", (int)e);
    return fail(err, n, msg);
  }
  wPart = p;
  wSize = size;
  wBuild = build;
  wDone = 0;
  wFailed = false;
  wErr[0] = 0;
  wHaveSha = sha != nullptr;
  if (sha) memcpy(wSha, sha, sizeof wSha);
  mbedtls_sha256_init(&wCtx);
  mbedtls_sha256_starts_ret(&wCtx, 0);
  dbg("cart: %s install, %u bytes into %s\n", kind == W_CART ? "cartridge" : "launcher", (unsigned)size, p->label);
  return true;
}

static bool writerWrite(int kind, const uint8_t *data, size_t n) {
  if (wKind != kind || wFailed) return false;
  if (wDone + n > wSize) {
    wFailed = true;
    snprintf(wErr, sizeof wErr, "more data than announced");
    return false;
  }
  if (esp_ota_write(wHandle, data, n) != ESP_OK) {
    wFailed = true;
    snprintf(wErr, sizeof wErr, "flash write failed");
    return false;
  }
  mbedtls_sha256_update_ret(&wCtx, data, n);
  wDone += n;
  return true;
}

static void writerAbort(int kind) {
  if (wKind != kind) return;
  esp_ota_abort(wHandle);
  release();
  dbg("cart: install aborted\n");
}

static bool writerEnd(int kind, char *err, size_t n) {
  if (wKind != kind) return fail(err, n, "not installing");
  if (wFailed || wDone != wSize) {
    char msg[32];
    snprintf(msg, sizeof msg, "%s", wFailed ? wErr : "download incomplete");
    writerAbort(kind);
    return fail(err, n, msg);
  }
  uint8_t got[32];
  mbedtls_sha256_finish_ret(&wCtx, got);
  if (wHaveSha && memcmp(got, wSha, sizeof got)) {
    writerAbort(kind);
    return fail(err, n, "checksum mismatch");
  }
  esp_err_t e = esp_ota_end(wHandle);           // validates the image too (segments, appended hash)
  mbedtls_sha256_free(&wCtx);
  if (e != ESP_OK) {
    wKind = W_NONE;
    return fail(err, n, e == ESP_ERR_OTA_VALIDATE_FAILED ? "not a valid image" : "flash error");
  }

  if (kind == W_FW) {
    e = esp_ota_set_boot_partition(wPart);
    wKind = W_NONE;
    if (e != ESP_OK) return fail(err, n, "boot switch failed");
    dbg("cart: launcher update ready in %s\n", wPart->label);
    return true;
  }

  // the partition's own hash, cheap to recompute before each launch
  uint8_t psha[32];
  if (esp_partition_get_sha256(wPart, psha) != ESP_OK) {
    wKind = W_NONE;
    return fail(err, n, "slot check failed");
  }
  prefs.putUInt(K_SIZE, wSize);
  prefs.putBytes(K_SHA, got, sizeof got);
  prefs.putBytes(K_PSHA, psha, sizeof psha);
  prefs.putString(K_SLOT, wPart->label);
  prefs.putUInt(K_BUILD, wBuild ? wBuild : 1);  // 0 means "none": a dev cartridge counts as build 1
  recLoaded = false;
  wKind = W_NONE;
  dbg("cart: cartridge build %u installed in %s\n", (unsigned)wBuild, wPart->label);
  return true;
}
#endif  // !TTGO_CARTRIDGE

#if TTGO_CARTRIDGE
static bool rearmed = false;
#endif

}  // namespace cart

using namespace cart;

// ---------------- both firmwares ----------------
int cartGameIndex(const char *hiKey) {
  if (!hiKey) return -1;
  for (int i = 0; i < CART_NUM_GAMES; i++)
    if (!strcmp(CART_GAMES[i].hiKey, hiKey)) return i;
  return -1;
}

bool cartIsHeavy(const char *hiKey) { return cartGameIndex(hiKey) >= 0; }

#if !TTGO_CARTRIDGE
// ---------------- launcher: common.h API ----------------
int cartState() {
  uint32_t b = cartInstalledBuild();
  if (!b) return CART_NONE;
  return updates::latestCartBuild() > b ? CART_OUTDATED : CART_READY;
}

bool cartLaunch(const char *hiKey) {
  int gi = cartGameIndex(hiKey);
  if (gi < 0 || !cartInstalledBuild() || cartInstallActive()) return false;
  handoverScreen("Starting", CART_GAMES[gi].name);
  const esp_partition_t *p = otherSlot();
  uint8_t sha[32];
  if (!p || esp_partition_get_sha256(p, sha) != ESP_OK || memcmp(sha, recPsha, sizeof sha)) {
    dbg("cart: slot content changed, forgetting the cartridge\n");
    forget();
    return false;
  }
  prefs.putString(K_GAME, hiKey);
  if (prefs.isKey(K_BACK)) prefs.remove(K_BACK);
  if (esp_ota_set_boot_partition(p) != ESP_OK) {
    dbg("cart: boot switch failed\n");
    prefs.remove(K_GAME);
    return false;
  }
  tigo::save(millis(), true);                   // keep Tigo's needs across the reboot
  dbg("cart: starting %s\n", hiKey);
  if (usbLogOn()) Serial.flush();
  esp_restart();
}

bool cartInstallBegin(uint32_t size, const uint8_t *sha256, uint32_t build, char *err, size_t errN) {
  return writerBegin(W_CART, size, sha256, build, err, errN);
}
bool cartInstallWrite(const uint8_t *data, size_t n) { return writerWrite(W_CART, data, n); }
bool cartInstallEnd(char *err, size_t errN) { return writerEnd(W_CART, err, errN); }
void cartInstallAbort() { writerAbort(W_CART); }

int cartInstallProgress() {
  if (wKind != W_CART || !wSize) return 0;
  return (int)((uint64_t)wDone * 100 / wSize);
}

// ---------------- launcher: cart.h API ----------------
uint32_t cartInstalledBuild() {
  loadRecord();
  return recBuild;
}

uint32_t cartSlotSize() {
  const esp_partition_t *p = otherSlot();
  return p ? p->size : 0;
}

bool cartInstallActive() { return wKind == W_CART; }

void cartInstallBytes(uint32_t *done, uint32_t *total) {
  bool on = wKind == W_CART;
  *done = on ? wDone : 0;
  *total = on ? wSize : 0;
}

void cartForget() {
  if (cartInstalledBuild()) dbg("cart: other slot overwritten, cartridge forgotten\n");
  forget();
}

bool fwInstallBegin(uint32_t size, const uint8_t *sha256, char *err, size_t errN) {
  return writerBegin(W_FW, size, sha256, 0, err, errN);
}
bool fwInstallWrite(const uint8_t *data, size_t n) { return writerWrite(W_FW, data, n); }
bool fwInstallEnd(char *err, size_t errN) { return writerEnd(W_FW, err, errN); }
void fwInstallAbort() { writerAbort(W_FW); }

CartReturn cartBootReturn(char *game, size_t n) {
  if (n) game[0] = 0;
  if (!prefs.isKey(K_GAME)) return CART_RET_NONE;
  prefs.getString(K_GAME, game, n);
  bool clean = prefs.getBool(K_BACK, false);
  prefs.remove(K_GAME);
  if (prefs.isKey(K_BACK)) prefs.remove(K_BACK);
  return clean ? CART_RET_CLEAN : CART_RET_UNCLEAN;
}

#else
// ---------------- cartridge side ----------------
void cartRearmLauncher() {
  const esp_partition_t *l = otherSlot();
  rearmed = l && esp_ota_set_boot_partition(l) == ESP_OK;
}

const char *cartRequestedGame() {
  static char game[16] = "";
  if (!game[0] && prefs.isKey(K_GAME)) prefs.getString(K_GAME, game, sizeof game);
  return game;
}

void cartExitToLauncher() {
  prefs.putBool(K_BACK, true);
  tigo::save(millis(), true);
  if (!rearmed) cartRearmLauncher();
  if (rearmed) {
    handoverScreen("Back to", "Launcher");
  } else {
    // no valid launcher in the other slot: only a USB flash can bring it back
    handoverScreen("No launcher!", "Flash USB");
    delay(3000);
  }
  dbg("cart: back to the launcher\n");
  if (usbLogOn()) Serial.flush();
  delay(400);
  esp_restart();
}
#endif

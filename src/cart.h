// Cartridge helpers beyond common.h (cart.cpp): the heavy games table, the
// launcher update writer, the launcher <-> cartridge hand-over and boot hooks.
//
// Flash layout (min_spiffs.csv): two 1.9 MB app slots. The launcher runs from
// one; the other holds either the "Big Games" cartridge or a launcher update
// on its way in. Installing a launcher update (WiFi, ArduinoOTA) erases the
// cartridge, so the launcher forgets it (cartForget) and offers it again.
// Both firmwares share the same NVS: scores, theme and settings carry over.
#pragma once
#include "common.h"

#ifndef FW_BUILD
#define FW_BUILD 0                    // CI passes -D FW_BUILD=<run number>; 0 = local dev build
#endif
#ifndef TTGO_CARTRIDGE
#define TTGO_CARTRIDGE 0              // 1 in the 'cartridge' env
#endif

// ---------------- the cartridge's games ----------------
// Same names and high-score keys as in the launcher's menu; the cartridge's
// own table (cartmain.cpp) maps them to their code, in this order.
struct CartGame { const char *name; const char *hiKey; };
static const CartGame CART_GAMES[] = {
  {"Abyx",  "abyx_hi"},
  {"Crypt", "crypt_hi"},
};
static const int CART_NUM_GAMES = sizeof CART_GAMES / sizeof CART_GAMES[0];
static const char CART_ID[] = "big";            // manifest id
static const char CART_TITLE[] = "Big Games";
int cartGameIndex(const char *hiKey);            // -1 if not a cartridge game

// ---------------- launcher side ----------------
uint32_t cartInstalledBuild();        // 0 = no (valid) cartridge record
uint32_t cartSlotSize();              // bytes available in the other app slot
bool cartInstallActive();             // between cartInstallBegin and End / Abort
void cartInstallBytes(uint32_t *done, uint32_t *total);   // of the install running (0 / 0 when none)
void cartForget();                    // the other slot got overwritten (launcher update, ArduinoOTA)

// A launcher update into the other slot, verified like a cartridge. Begin
// forgets the cartridge (its flash is about to be erased); End sets the new
// image as the boot partition, the caller then restarts.
bool fwInstallBegin(uint32_t size, const uint8_t *sha256, char *err, size_t errN);
bool fwInstallWrite(const uint8_t *data, size_t n);
bool fwInstallEnd(char *err, size_t errN);
void fwInstallAbort();

// At launcher boot, after initDiag(): did this boot follow a cartridge
// session? Clears the hand-over keys. clean = the cartridge returned through
// returnToMenu() (not a crash, a reset or a wake from deep sleep).
enum CartReturn { CART_RET_NONE, CART_RET_CLEAN, CART_RET_UNCLEAN };
CartReturn cartBootReturn(char *game, size_t n);

// ---------------- cartridge side ----------------
void cartRearmLauncher();             // first thing in the cartridge's setup()
const char *cartRequestedGame();      // hiKey the launcher asked for, "" if none
[[noreturn]] void cartExitToLauncher();   // marks a clean return, reboots into the launcher

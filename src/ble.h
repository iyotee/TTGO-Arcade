// Bluetooth helpers beyond common.h (ble.cpp): HID key sending for the Remote tool,
// a way for a tool to keep the radio on without changing the saved setting, a
// suspend switch for heavy work, and the cartridge install over Bluetooth.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Consumer-control keys = bits of HID report 2 (see the report map in ble.cpp)
enum BleMedia : uint8_t {
  BLE_MEDIA_NEXT = 0, BLE_MEDIA_PREV, BLE_MEDIA_STOP, BLE_MEDIA_PLAY_PAUSE,
  BLE_MEDIA_MUTE, BLE_MEDIA_VOL_UP, BLE_MEDIA_VOL_DOWN,
};

// HID keyboard usages (page 0x07) for bleTapKey()
static const uint8_t BLE_KEY_RIGHT = 0x4F, BLE_KEY_LEFT = 0x50, BLE_KEY_DOWN = 0x51, BLE_KEY_UP = 0x52;
static const uint8_t BLE_KEY_PAGE_UP = 0x4B, BLE_KEY_PAGE_DOWN = 0x4E, BLE_KEY_SPACE = 0x2C;

// Keep-alive: call every loop while a tool needs Bluetooth. Starts the stack if
// the setting is off; it is stopped again ~3 s after the last call (or at once
// with bleRelease()), so a tool left by the screensaver can't leave it running.
void bleUse(uint32_t now);
void bleRelease();
bool bleRunning();                    // stack up (setting on, held by a tool, or installing)
bool bleHidReady();                   // a phone/computer listens to our key reports
bool bleCompanionLinked();            // the companion web page is connected (status subscribed)
// "" or why the stack could not start ("Low memory", "Radio busy"); it retries every few seconds
const char *bleError();
// Press + release; false when no host is listening
bool bleTapMedia(BleMedia key);
bool bleTapKey(uint8_t usage, uint8_t modifiers = 0);

// Suspend: stops the stack whatever the setting or tools say (an HTTPS download
// needs the ~40-50 KB of heap it holds); bleSuspend(false) brings it back as it
// was. The saved setting is not touched. A Bluetooth install in progress is aborted.
void bleSuspend(bool on);
bool bleSuspended();

// ---------------- cartridge install over Bluetooth ----------------
// The companion page streams a cartridge image into the other app slot through
// cartInstallBegin/Write/End (cart.cpp), from bleService() on the main loop.
enum BleInstallState {
  BLE_INST_IDLE,                      // nothing yet
  BLE_INST_PREPARING,                 // cartInstallBegin() (erasing the slot) is next
  BLE_INST_RECEIVING,                 // data flowing (or waiting for the page to reconnect)
  BLE_INST_CHECKING,                  // all bytes written, cartInstallEnd() verifies them
  BLE_INST_DONE,                      // installed
  BLE_INST_FAILED,                    // see bleInstallMessage()
};
int bleInstallState();
bool bleInstallActive();              // PREPARING, RECEIVING or CHECKING: don't sleep or reboot
int bleInstallPercent();              // 0..100 of the bytes written to flash
void bleInstallBytes(uint32_t *done, uint32_t *total);
const char *bleInstallMessage();      // short text for the screen: "Installed", "Low battery", ...

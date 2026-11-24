// Bluetooth helpers beyond common.h (ble.cpp): HID key sending for the Remote tool,
// and a way for a tool to keep the radio on without changing the saved setting.
#pragma once

#include <stdint.h>

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
bool bleRunning();                    // stack up (setting on, or held by a tool)
bool bleHidReady();                   // a phone/computer listens to our key reports
// Press + release; false when no host is listening
bool bleTapMedia(BleMedia key);
bool bleTapKey(uint8_t usage, uint8_t modifiers = 0);

// Shared helpers that live in both firmwares (ui_common.cpp): the globals
// (display, sprite, prefs, buttons), the drawing / input helpers common.h
// declares, the hardware start-up and the Sleep entry.
#pragma once
#include "common.h"

void bootHardware();                  // start of setup(): power, buttons, display, sprite, prefs
void drawMoon(int x, int y, int r, uint16_t col, uint16_t bg);
void sleepIcon(int x, int y, bool dim, uint32_t t);
void sleepInfo(char *buf, size_t n);
void sleepNow();                      // "Good night" screen, then deep sleep

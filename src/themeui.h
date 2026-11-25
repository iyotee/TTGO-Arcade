// Extra theme helpers shared by the menu, Settings and WiFi screens
// (implemented in theme.cpp). common.h has the core Theme API.
#pragma once
#include "common.h"

// secondary text: subtitles, values, hints (Original: light grey, Neon: pale lavender)
uint16_t themeSoft();
// col faded towards the theme background (a = 0 .. 255), for dimmed icons and halos
uint16_t themeFade(uint8_t a, uint16_t col);
// a list row / menu box outline: Original = grey, or a double yellow frame when
// selected; Neon = dim purple, or a glowing neon outline when selected
// (selCol 0 = the theme's frameSel)
void drawSelBox(int x, int y, int w, int h, int r, bool sel, uint16_t selCol = 0);
// "L <l>   R <r>" button hints along the bottom (verbs 12 px right of each key)
void drawThemedFooter(const char *l, const char *r, int y = 222, int rx = 70);

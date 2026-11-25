// Screen orientation for games that offer landscape play (shared helper).
//
// TFT_eSPI only rotates 1-bit sprites (TFT_eSprite::setRotation() returns early
// for 16-bit ones), so landscape works by turning the PANEL instead: the panel
// rotation changes and the full-screen sprite is recreated as 240x135. Portrait
// puts both back. spr.width() / spr.height() then give the logical size.
// The two buttons sit on one short side of the landscape picture: one is the
// "top" button and the other the "bottom" button.
#pragma once
#include "common.h"

enum Orient { OR_PORTRAIT, OR_LAND_RIGHT, OR_LAND_LEFT, NUM_ORIENTS };

// "buttons on the right": board turned a quarter turn counter-clockwise
// "buttons on the left":  board turned a quarter turn clockwise
// If a picture shows upside down on a board, the player simply picks the other one.
inline const char *orientName(int o) {
  return o == OR_LAND_RIGHT ? "Landscape >" : o == OR_LAND_LEFT ? "< Landscape" : "Portrait";
}

// current orientation, shared by every file (inline function: one static for the program)
inline int &orientCurrent() {
  static int cur = OR_PORTRAIT;
  return cur;
}

inline void orientRecreateSprite(int w, int h) {
  uint8_t depth = spr.getColorDepth();
  spr.deleteSprite();
  spr.setColorDepth(depth);
  if (!spr.createSprite(w, h)) {               // same size as before, so this should not fail
    spr.setColorDepth(8);
    spr.createSprite(w, h);
  }
}

// Turns the panel and resizes the sprite; the sprite's content is lost.
inline void applyOrient(int o) {
  if (o == orientCurrent()) return;
  bool land = o != OR_PORTRAIT, wasLand = orientCurrent() != OR_PORTRAIT;
  int rot = o == OR_LAND_RIGHT ? (SCREEN_ROTATION + 1) & 3
          : o == OR_LAND_LEFT  ? (SCREEN_ROTATION + 3) & 3
          : SCREEN_ROTATION;
  tft.setRotation(rot);
  if (land != wasLand) orientRecreateSprite(tft.width(), tft.height());
  orientCurrent() = o;
}

// Always call before returning to the menu (cheap when already portrait).
inline void resetOrient() { applyOrient(OR_PORTRAIT); }

// In landscape: the button nearer the top of the picture, and the other one.
// In portrait these are simply L and R.
inline Btn &orientTopBtn(int o) { return o == OR_LAND_RIGHT ? bR : bL; }
inline Btn &orientBottomBtn(int o) { return o == OR_LAND_RIGHT ? bL : bR; }

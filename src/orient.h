// Screen orientation for games that offer landscape play (shared helper).
//
// Portrait is the normal menu orientation. Landscape draws through the sprite's
// own rotation (the panel itself stays in SCREEN_ROTATION), so pushSprite(0, 0)
// still works. The two buttons then sit on one short side of the picture: one
// is the "top" button and the other the "bottom" button.
#pragma once
#include "common.h"

enum Orient { OR_PORTRAIT, OR_LAND_RIGHT, OR_LAND_LEFT, NUM_ORIENTS };

// "buttons on the right": board turned a quarter turn counter-clockwise
// "buttons on the left":  board turned a quarter turn clockwise
// If a picture shows upside down on a board, the player simply picks the other one.
static inline const char *orientName(int o) {
  return o == OR_LAND_RIGHT ? "Landscape >" : o == OR_LAND_LEFT ? "< Landscape" : "Portrait";
}

// Applies the drawing rotation; spr.width()/spr.height() then give the logical size.
static inline void applyOrient(int o) {
  spr.setRotation(o == OR_LAND_RIGHT ? 1 : o == OR_LAND_LEFT ? 3 : 0);
}

// Always call before returning to the menu.
static inline void resetOrient() { spr.setRotation(0); }

// In landscape: the button nearer the top of the picture, and the other one.
// In portrait these are simply L and R.
static inline Btn &orientTopBtn(int o) { return o == OR_LAND_RIGHT ? bR : bL; }
static inline Btn &orientBottomBtn(int o) { return o == OR_LAND_RIGHT ? bL : bR; }

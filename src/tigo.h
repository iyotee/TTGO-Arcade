// Tigo, the hermit-crab mascot: shared state (needs, shells) and extra drawing
// helpers used by the screensaver (saver.cpp) and the Tigo app (tigoapp.cpp).
// drawTigo() itself is declared in common.h.
#pragma once
#include "common.h"

namespace tigo {

// ---------------- palette (RGB565, see the README style guide) ----------------
static const uint16_t COL_PLUM = 0x38E7, COL_CORAL = 0xFB49, COL_CLAW = 0xE245, COL_PEACH = 0xFD91;
static const uint16_t COL_SHELL = 0xF6B4, COL_TEAL = 0x2D95;

// ---------------- needs (0 = desperate .. 100 = perfectly fine) ----------------
struct Needs {
  int hunger;   // 100 = full tummy
  int fun;
  int clean;
  int energy;
};
Needs &needs();
void load();                          // reads prefs once (later calls do nothing)
void tick(uint32_t now);              // needs slowly drop while the board is awake
void save(uint32_t now, bool force);  // writes at most every few minutes unless forced
void feed(int amount);
void play(int amount);
void wash();
void rest(int amount);                // energy, used while he naps
int mood();                           // 0..100, average of the needs

// ---------------- shells (unlocked by high scores) ----------------
static const int NUM_SHELLS = 5;
extern const uint8_t SHELL_AT[NUM_SHELLS];     // games with a best score needed: 0, 5, 15, 30, 45
const char *shellName(int i);
int gamesWithScore();                 // games whose best score is > 0
int shellsUnlocked();                 // 1 .. NUM_SHELLS
int shell();                          // the shell he wears (the best unlocked one)
bool takeNewShell();                  // true once after a new shell was unlocked

// ---------------- drawing ----------------
// drawTigo with an explicit shell style and a dim level (255 = normal, lower = darker)
void drawTigoEx(int cx, int cy, int size, TigoPose pose, uint32_t t, bool faceLeft, int shellStyle, uint8_t dim);
// a lone shell (no crab) centred on (cx, cy) with radius r
void drawShell(float cx, float cy, float r, int style, bool faceLeft = false);
// one big eye (for the periscope gag): look direction lx, ly in -1..1
void drawEye(float x, float y, float r, float lx, float ly, bool closed);
// soft translucent shadow under Tigo standing at (cx, cy)
void drawShadow(int cx, int cy, int size);
// x offset (px, facing right) of Tigo's claw tips from cx, for placing props
float clawReach(int size);
// Speech bubble above Tigo's head: word-wrapped (font 2, up to 4 lines), typed out
// over `ms` since it appeared; the bubble's top stays at or below minY.
void drawBubble(const char *txt, int headX, int headTop, int minY, uint32_t ms);

}  // namespace tigo

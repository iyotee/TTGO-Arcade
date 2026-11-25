// Games for LilyGO TTGO T-Display V1.1 (ESP32, ST7789 135x240)
//
// 56 two-button games and 10 pocket apps (see README.md), WiFi, OTA and themes
// in Settings. On boot a launcher offers Games, Apps, Settings and Sleep; Games
// and Apps open alphabetical lists:
//   Launcher : Tap L next · Tap R open
//   Lists    : Tap L next · Hold L fast scroll · Tap R start / open
//              Hold R back to the launcher (or pick the "< Back" row)
// The launcher remembers the last category and item, and after a game or app
// ends you are back in the list it came from. Press the board's RST button (or
// let it go to sleep and wake it) to return to the launcher.
//
// High scores and settings are stored in NVS (survive power-off).
// On menu / title / game-over screens, after the screensaver delay of
// inactivity (Settings) Tigo's screensaver starts, and after the sleep delay
// the board goes into deep sleep; press the GPIO35 button to wake it. With the
// PIN lock on, the code is asked at boot and when leaving the screensaver.

#include "common.h"
#include "lockscr.h"
#include "themeui.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
Preferences prefs;
Btn bL{PIN_LEFT}, bR{PIN_RIGHT};

int updateBtn(Btn &b, uint32_t now) {
  bool r = digitalRead(b.pin) == LOW;
  if (r != b.raw) { b.raw = r; b.rawT = now; }
  if (r != b.stable && now - b.rawT >= DEBOUNCE_MS) {
    b.stable = r;
    if (r) { b.t0 = now; b.held = false; b.consumed = false; return 1; }
    return 2;
  }
  return 0;
}

void drawCell(int px, int py, int sz, uint16_t col) {
  spr.fillRect(px, py, sz - 1, sz - 1, col);
  uint16_t lt = tft.alphaBlend(120, TFT_WHITE, col);
  uint16_t dk = tft.alphaBlend(110, TFT_BLACK, col);
  spr.drawFastHLine(px, py, sz - 1, lt);
  spr.drawFastVLine(px, py, sz - 1, lt);
  spr.drawFastHLine(px, py + sz - 2, sz - 1, dk);
  spr.drawFastVLine(px + sz - 2, py, sz - 1, dk);
}

float triWave(uint32_t t, uint32_t period) {
  float f = (float)(t % period) / period;
  return f < 0.5f ? 2 * f : 2 - 2 * f;
}

// ---------------- shared screens (themed) ----------------
void drawScoreHeader(uint32_t score, uint32_t best) {
  char buf[32];
  const Theme &th = theme();
  spr.fillRect(0, 0, 135, HEADER_H, th.bg);
  if (th.neon) spr.drawFastHLine(0, HEADER_H - 2, 135, themeFade(110, th.frameSel));   // neon underline
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.text);
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(th.neon ? th.accent : TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  spr.drawString(buf, 2, 24, 1);
}

void drawGameOverBox(const char *title, uint32_t score, uint32_t best, bool newHigh) {
  char buf[32];
  const Theme &th = theme();
  uint16_t edge = th.neon ? th.frameSel : TFT_RED;
  if (th.neon) {
    spr.fillRoundRect(6, 92, 123, 84, 6, th.panel);
    drawThemedBox(6, 92, 123, 84, 6, edge, true);
  } else {
    spr.fillRect(6, 92, 123, 84, TFT_BLACK);
    spr.drawRect(6, 92, 123, 84, TFT_RED);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(edge);
  spr.drawString(title, 67, 98, 2);
  spr.setTextColor(th.text);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
  spr.drawString(buf, 67, 116, 2);
  if (newHigh) {
    spr.setTextColor(th.neon ? th.accent2 : TFT_YELLOW);
    spr.drawString("NEW BEST!", 67, 134, 2);
  } else {
    spr.setTextColor(themeSoft());
    snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
    spr.drawString(buf, 67, 134, 2);
  }
  // button hints: "L menu   R again"
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(th.keyHint);
  spr.drawString("L", 16, 154, 2);
  spr.drawString("R", 68, 154, 2);
  spr.setTextColor(th.neon ? th.accent : TFT_GREEN);
  spr.drawString("menu", 28, 154, 2);
  spr.drawString("again", 80, 154, 2);
}

EndChoice startOrEnd(bool gameOver, int eL, int eR, bool &armed) {
  if (eL == 1 || eR == 1) armed = true;              // need a fresh press...
  if (!armed || (eL != 2 && eR != 2)) return END_NONE;   // ...and its release
  armed = false;
  if (gameOver && eL == 2) {
    returnToMenu();
    return END_MENU;
  }
  return END_PLAY;
}

void drawToolFooter(const char *l, const char *r) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(theme().textDim);
  spr.drawString("hold L: menu", 67, 206, 1);
  drawThemedFooter(l, r, 220, 70);
}

// ---------------- sleep entry ----------------
#if PIN_RIGHT == 35
  #define WAKE_BTN "R"
#else
  #define WAKE_BTN "L"
#endif

void drawMoon(int x, int y, int r, uint16_t col, uint16_t bg) {
  spr.fillCircle(x, y, r, col);
  spr.fillCircle(x + r / 2, y - r / 3, r - 1, bg);    // bite out a crescent
}

void sleepIcon(int x, int y, bool dim, uint32_t t) {
  const Theme &th = theme();
  drawMoon(x - 2, y, 11, dim ? themeFade(80, TFT_YELLOW) : TFT_YELLOW, th.bg);
  // the two stars twinkle in turn
  auto star = [&](uint32_t phase) -> uint16_t {
    if (dim) return th.textDim;
    if (!t) return TFT_WHITE;
    return themeFade(60 + (int)(195 * triWave(t + phase, 1600)), TFT_WHITE);
  };
  spr.fillCircle(x + 9, y + 6, 1, star(800));
  spr.drawPixel(x + 5, y - 10, star(0));
}

void sleepInfo(char *buf, size_t n) { snprintf(buf, n, WAKE_BTN " wakes it up"); }

void sleepNow() {
  const Theme &th = theme();
  spr.fillSprite(th.bg);
  drawMoon(67, 80, 24, TFT_YELLOW, th.bg);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.text);
  spr.drawString("Good night", 67, 130, 4);
  spr.setTextColor(themeSoft());
  spr.drawString("Press " WAKE_BTN " to wake", 67, 164, 2);
  spr.pushSprite(0, 0);
  delay(1500);
  deepSleepNow();
}

// ---------------- items ----------------
// One flat array: the games (alphabetical, case-insensitive; digits first),
// then the apps (alphabetical), then Settings and Sleep. Crash records keep
// an index into it (noteActiveItem / itemName), numGames() / gameInfo() count
// the leading games, so Settings' reset list is alphabetical too.
struct MenuItem {
  const char *name;              // font 4, must stay under ~90 px to clear the icon
  const char *hiKey;             // NVS key of a game's high score; nullptr if not a game
  void (*begin)();
  void (*update)(uint32_t now);  // nullptr: begin() never returns
  void (*drawIcon)(int cx, int cy, bool dim, uint32_t t);
  void (*info)(char *buf, size_t n);   // subtitle when there is no high score
  const char *action;            // footer verb for R
};
#define GAME(name, key, ns) {name, key, ns::begin, ns::update, ns::drawIcon, nullptr, "play"}
#define APP(name, ns) {name, nullptr, ns::begin, ns::update, ns::drawIcon, ns::menuInfo, "open"}
static const MenuItem ITEMS[] = {
  GAME("2048",    "g2048_hi",   game2048),
  GAME("21",      "bj21_hi",    blackjack),
  GAME("Abyx",    "abyx_hi",    abyx),
  GAME("Bowl",    "bowl_hi",    bowl),
  GAME("Bricks",  "bricks_hi",  bricks),
  GAME("Bumper",  "bumper_hi",  bumper),
  GAME("Catch",   "catch_hi",   catchgame),
  GAME("Cave",    "cave_hi",    cave),
  GAME("Chute",   "chute_hi",   chute),
  GAME("Climb",   "climb_hi",   climb),
  GAME("Code",    "code_hi",    codebreak),
  GAME("Columns", "columns_hi", columns),
  GAME("Crossy",  "crossy_hi",  crossy),
  GAME("Crypt",   "crypt_hi",   cryptgame),
  GAME("Dino",    "dino_hi",    dino),
  GAME("Dodge",   "dodge_hi",   dodge),
  GAME("Doodle",  "doodle_hi",  doodle),
  GAME("Drop 4",  "drop4_hi",   drop4),
  GAME("Duet",    "duet_hi",    duet),
  GAME("Falldown","falldown_hi",falldown),
  GAME("Fish",    "fish_hi",    fish),
  GAME("Flappy",  "flappy_hi",  flappy),
  GAME("Fringe",  "fringe_hi",  fringe),
  GAME("Frog",    "frog_hi",    frog),
  GAME("Golf",    "golf_hi",    golf),
  GAME("Gravity", "gravity_hi", gravity),
  GAME("Hi-Lo",   "hilow_hi",   hilow),
  GAME("Invaders","invaders_hi",invaders),
  GAME("Knife",   "knife_hi",   knife),
  GAME("Lights",  "lights_hi",  lights),
  GAME("Lunar",   "lunar_hi",   lunar),
  GAME("Math",    "math_hi",    mathblitz),
  GAME("Maze",    "maze_hi",    maze),
  GAME("Memory",  "memory_hi",  memory),
  GAME("Meteors", "aster_hi",   asteroids),
  GAME("Mines",   "mines_hi",   mines),
  GAME("Missile", "missile_hi", missile),
  GAME("Pinball", "pinball_hi", pinball),
  GAME("Plinko",  "plinko_hi",  plinko),
  GAME("Pong 2P", "pong2p_hi",  pong2p),
  GAME("Pool",    "pool_hi",    pool),
  GAME("Pop",     "pop_hi",     pop),
  GAME("Race",    "race_hi",    race),
  GAME("Reversi", "reversi_hi", reversi),
  GAME("Rhythm",  "rhythm_hi",  rhythm),
  GAME("River",   "river_hi",   river),
  GAME("Shooter", "shooter_hi", shooter),
  GAME("Simon",   "simon_hi",   simon),
  GAME("Ski",     "ski_hi",     ski),
  GAME("Slots",   "slots_hi",   slots),
  GAME("Snake",   "snake_hi",   snake),
  GAME("Stack",   "stack_hi",   stack),
  GAME("Sub",     "sub_hi",     subgame),
  GAME("Swing",   "swing_hi",   swing),
  GAME("Tank",    "tank_hi",    tank),
  GAME("Tennis",  "tennis_hi",  tennis),
  GAME("Tetris",  "hi",         tetris),
  GAME("Tron",    "tron_hi",    tron),
  GAME("Whack",   "whack_hi",   whack),
  // apps
  APP("Breathe", breathe),
  APP("Clock",   clockapp),
  APP("Dice",    dice),
  APP("Minute",  minute),
  APP("Morse",   morse),
  APP("Remote",  remote),
  APP("Tally",   tally),
  APP("Tigo",    tigoapp),
  APP("Timer",   timer),
  APP("Torch",   torch),
  // launcher entries
  {"Settings", nullptr, settings::begin, settings::update, settings::drawIcon, settings::menuInfo, "open"},
  {"Sleep",    nullptr, sleepNow,        nullptr,          sleepIcon,          sleepInfo,          "sleep"},
};
#undef GAME
#undef APP
static const int NUM_ITEMS = sizeof ITEMS / sizeof ITEMS[0];
static const int ITEM_SETTINGS = NUM_ITEMS - 2, ITEM_SLEEP = NUM_ITEMS - 1;

// The games in their order before the alphabetical sort, by high-score key:
// the old "game" pref held an index into this list (read once, then replaced
// by "menuLast")
static const char *const OLD_GAME_ORDER[] = {
  "hi", "snake_hi", "race_hi", "flappy_hi", "tennis_hi", "bricks_hi",
  "stack_hi", "shooter_hi", "crossy_hi", "pinball_hi", "doodle_hi", "falldown_hi",
  "invaders_hi", "rhythm_hi", "dino_hi", "cave_hi", "g2048_hi", "columns_hi",
  "aster_hi", "simon_hi", "ski_hi", "catch_hi", "missile_hi", "lunar_hi",
  "golf_hi", "memory_hi", "mines_hi", "maze_hi", "bumper_hi", "climb_hi",
  "river_hi", "pong2p_hi", "whack_hi", "duet_hi", "bj21_hi", "tron_hi",
  "frog_hi", "pop_hi", "lights_hi", "hilow_hi", "math_hi", "code_hi",
  "drop4_hi", "chute_hi", "plinko_hi", "swing_hi", "tank_hi", "gravity_hi",
  "slots_hi", "sub_hi", "knife_hi", "dodge_hi", "fish_hi", "bowl_hi",
  "reversi_hi", "fringe_hi",
};
static const int OLD_GAME_COUNT = sizeof OLD_GAME_ORDER / sizeof OLD_GAME_ORDER[0];

const char *itemName(int i) { return i >= 0 && i < NUM_ITEMS ? ITEMS[i].name : "menu"; }

int numGames() {
  int n = 0;
  while (n < NUM_ITEMS && ITEMS[n].hiKey) n++;
  return n;
}

const char *gameInfo(int idx, const char **hiKey) {
  if (idx < 0 || idx >= numGames()) {
    if (hiKey) *hiKey = nullptr;
    return "";
  }
  if (hiKey) *hiKey = ITEMS[idx].hiKey;
  return ITEMS[idx].name;
}

// ---------------- launcher ----------------
namespace launcher {

enum View { VIEW_LAUNCHER, VIEW_GAMES, VIEW_APPS };
enum Entry { ENT_GAMES, ENT_APPS, ENT_SETTINGS, ENT_SLEEP, NUM_ENTRIES };
static const char *const ENTRY_NAME[NUM_ENTRIES] = {"Games", "Apps", "Settings", "Sleep"};

static View view = VIEW_LAUNCHER;
static View startedFrom = VIEW_LAUNCHER;   // where returnToMenu() goes back to
static int active = -1;                    // ITEMS index running, -1 = in the launcher
static int entrySel = ENT_GAMES;
static int listSel[2] = {1, 1};            // per list (games, apps): row 0 = "< Back"
static int listScroll[2] = {0, 0};
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastInput = 0, lastAnim = 0, lastFrame = 0;
static uint32_t selectedAt = 0;            // the selected icon's animation starts here
static int savedCat = -1, savedItem = -1;  // what "menuCat" / "menuLast" hold (skip rewrites)

// launcher boxes
static const int LAUNCH_Y = 39, LAUNCH_PITCH = 44, LAUNCH_BOX_H = 41;
// the lists scroll inside a viewport between the header and the footer
static const int MENU_LIST_Y = 26, MENU_LIST_H = 186;       // screen rows 26..211
static const int BOX_H = 56, BOX_PITCH = 62;      // 3 boxes fit in the viewport
static const int ICON_X = 114, ICON_HALF_W = 13, ICON_HALF_H = 16;   // icon clip area around (ICON_X, box centre)
static const uint32_t ICON_FRAME_MS = 33;         // the selected icon animates at ~30 fps

// ---------------- list geometry ----------------
static int listIdx() { return view == VIEW_APPS ? 1 : 0; }
static int firstItem(int l) { return l ? numGames() : 0; }
static int itemCount(int l) { return l ? ITEM_SETTINGS - numGames() : numGames(); }
static int rowCount(int l) { return itemCount(l) + 1; }      // + "< Back"
static int maxScroll(int l) { return max(0, rowCount(l) * BOX_PITCH - 6 + 4 - MENU_LIST_H); }
static int rowItem(int l, int r) { return r > 0 ? firstItem(l) + r - 1 : -1; }   // -1 = "< Back"

// smallest change to the current scroll that shows the selected box completely
static int scrollTarget(int l) {
  int top = listSel[l] * BOX_PITCH, bottom = top + BOX_H + 4;
  int t = listScroll[l];
  if (bottom > t + MENU_LIST_H) t = bottom - MENU_LIST_H;
  if (top < t) t = top;
  return constrain(t, 0, maxScroll(l));
}

// ---------------- remembered selection ----------------
static int findItem(const char *key) {               // by high-score key or name
  if (!key || !*key) return -1;
  for (int i = 0; i < NUM_ITEMS; i++)
    if ((ITEMS[i].hiKey && !strcmp(ITEMS[i].hiKey, key)) || !strcmp(ITEMS[i].name, key)) return i;
  return -1;
}

// item -1: only the category (Settings keeps the last game / app remembered);
// unchanged values are not rewritten, to spare the flash
static void remember(int cat, int item) {
  if (cat != savedCat) {
    prefs.putUChar("menuCat", cat);
    savedCat = cat;
  }
  if (item >= 0 && item != savedItem) {
    prefs.putString("menuLast", ITEMS[item].hiKey ? ITEMS[item].hiKey : ITEMS[item].name);
    savedItem = item;
  }
}

// point the list (games or apps) that holds the item at it
static void selectItem(int item) {
  for (int l = 0; l < 2; l++) {
    int f = firstItem(l);
    if (item >= f && item < f + itemCount(l)) listSel[l] = item - f + 1;
  }
}

static void restore() {
  int item = -1;
  if (prefs.isKey("menuLast")) {
    item = findItem(prefs.getString("menuLast", "").c_str());
  } else if (prefs.isKey("game")) {
    // older firmware: an index into the old (unsorted) games order
    int old = prefs.getUChar("game", 255);
    if (old < OLD_GAME_COUNT) item = findItem(OLD_GAME_ORDER[old]);
    if (item >= 0) remember(ENT_GAMES, item);
    prefs.remove("game");
  }
  if (item >= ITEM_SETTINGS) item = -1;                        // only games and apps are kept
  if (item >= 0) selectItem(item);
  int cat = prefs.getUChar("menuCat", ENT_GAMES);
  entrySel = cat < NUM_ENTRIES && cat != ENT_SLEEP ? cat : ENT_GAMES;   // never wake up on "Sleep"
  savedCat = cat;
  savedItem = item;
  for (int l = 0; l < 2; l++) listScroll[l] = scrollTarget(l);   // in view at once, no glide
  dbg("menu: %d games, %d apps, last %s\n", itemCount(0), itemCount(1), item >= 0 ? ITEMS[item].name : "-");
}

// ---------------- drawing ----------------
// battery / USB badge with its left edge at x = 6 and its top at y, WiFi arcs after it
static void drawBadges(uint32_t now, int y) {
  static int mv = 0;
  static uint32_t mvAt = 0;
  if (!mvAt || (int32_t)(now - mvAt) > 5000) {
    mv = batteryMilliVolts();
    mvAt = now;
  }
  uint16_t outline = themeSoft();
  spr.drawRoundRect(6, y, 16, 9, 2, outline);
  spr.fillRect(22, y + 3, 2, 3, outline);
  if (onUsbPower(mv)) {
    spr.fillRect(8, y + 2, 12, 5, 0x2D6A);
  } else {
    int pct = batteryPercent(mv);
    uint16_t bcol = pct > 40 ? TFT_GREEN : pct > 15 ? TFT_YELLOW : TFT_RED;
    spr.fillRect(8, y + 2, max(1, 12 * pct / 100), 5, bcol);
  }
  if (wifiConnected()) {
    uint16_t wc = theme().neon ? theme().accent : TFT_CYAN;
    for (int r = 3; r <= 9; r += 3) spr.drawCircleHelper(37, y + 9, r, 1 | 2, wc);
    spr.fillCircle(37, y + 8, 1, wc);
  }
}

// ---- launcher icons (same contract as the games' drawIcon) ----
static void gamepadIcon(int cx, int cy, bool dim, uint32_t t) {
  const Theme &th = theme();
  uint16_t body = th.neon ? th.panel : 0x4208, edge = th.neon ? th.frameSel : TFT_LIGHTGREY;
  uint16_t pad = th.neon ? th.accent : TFT_WHITE;
  uint16_t a = th.neon ? th.accent2 : TFT_GREEN, b = th.neon ? th.keyHint : TFT_RED;
  if (dim) { edge = themeFade(80, edge); pad = themeFade(80, pad); a = themeFade(80, a); b = themeFade(80, b); body = th.bg; }
  spr.fillCircle(cx - 8, cy + 4, 5, edge);                     // grips
  spr.fillCircle(cx + 8, cy + 4, 5, edge);
  spr.fillRoundRect(cx - 12, cy - 7, 25, 13, 5, edge);
  spr.fillRoundRect(cx - 11, cy - 6, 23, 11, 4, body);
  spr.fillRect(cx - 9, cy - 1, 7, 2, pad);                     // d-pad
  spr.fillRect(cx - 6, cy - 4, 2, 7, pad);
  // the two buttons take turns lighting up
  float p = t ? triWave(t, 900) : 1.0f;
  spr.fillCircle(cx + 4, cy + 1, 2, tft.alphaBlend(90 + (int)(165 * p), a, body));
  spr.fillCircle(cx + 8, cy - 2, 2, tft.alphaBlend(90 + (int)(165 * (1 - p)), b, body));
}

static void appsIcon(int cx, int cy, bool dim, uint32_t t) {
  static const uint16_t ORIG[9] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_BLUE,
                                   TFT_MAGENTA, TFT_PINK, TFT_WHITE};
  static const uint16_t NEON[9] = {0xF81F, 0x07FF, 0xAFE5, 0x901F, 0xFFE0, 0xFB2C, 0x07F0, 0x5C3F, 0xFFFF};
  const uint16_t *pal = theme().neon ? NEON : ORIG;
  int lit = t ? (int)(t / 160 % 9) : -1;                       // one tile at a time pops
  for (int i = 0; i < 9; i++) {
    int x = cx - 11 + (i % 3) * 8, y = cy - 11 + (i / 3) * 8;
    uint16_t col = dim ? themeFade(70, pal[i]) : i == lit ? pal[i] : themeFade(170, pal[i]);
    spr.fillRoundRect(x, y, 7, 7, 2, col);
    if (i == lit) spr.drawRoundRect(x - 1, y - 1, 9, 9, 2, TFT_WHITE);
  }
}

static void backIcon(int cx, int cy, bool dim, uint32_t t) {
  const Theme &th = theme();
  uint16_t col = dim ? th.textDim : th.neon ? th.accent : TFT_LIGHTGREY;
  int dx = t ? -(int)(4 * triWave(t, 800)) : 0;                // nudges to the left
  spr.fillTriangle(cx - 9 + dx, cy, cx - 1 + dx, cy - 8, cx - 1 + dx, cy + 8, col);
  spr.fillRect(cx - 1 + dx, cy - 3, 10, 6, col);
}

static void entryIcon(int e, int cx, int cy, bool dim, uint32_t t) {
  if (e == ENT_GAMES) gamepadIcon(cx, cy, dim, t);
  else if (e == ENT_APPS) appsIcon(cx, cy, dim, t);
  else ITEMS[e == ENT_SETTINGS ? ITEM_SETTINGS : ITEM_SLEEP].drawIcon(cx, cy, dim, t);
}

// clips the icon drawing to its area around (ICON_X, cy) and to rows
// [clipTop, clipBottom) in absolute coordinates; false if nothing is visible
static bool clipIcon(int cy, int clipTop, int clipBottom) {
  int top = max(cy - ICON_HALF_H, clipTop), bottom = min(cy + ICON_HALF_H, clipBottom);
  if (bottom <= top) return false;
  spr.setViewport(ICON_X - ICON_HALF_W + 1, top, 2 * ICON_HALF_W - 1, bottom - top, false);
  return true;
}

// signed: selectedAt may be a millis() taken after now
static uint32_t animClock(uint32_t now) { return (int32_t)(now - selectedAt) > 0 ? now - selectedAt : 0; }

static void renderLauncher(uint32_t now) {
  char buf[32];
  const Theme &th = theme();
  spr.fillSprite(th.bg);

  // header: title, badges under it, Tigo on the right
  spr.setTextDatum(TL_DATUM);
  if (th.neon) {
    spr.setTextColor(th.accent);                                 // cyan shadow, pink face
    spr.drawString("TTGO ARCADE", 7, 5, 2);
    spr.setTextColor(th.frameSel);
  } else {
    spr.setTextColor(TFT_LIGHTGREY);
  }
  spr.drawString("TTGO ARCADE", 6, 4, 2);
  drawBadges(now, 23);
  spr.setViewport(92, 0, 43, LAUNCH_Y - 3, false);
  TigoPose pose = (now / 1000) % 8 == 0 ? TIGO_HAPPY : TIGO_IDLE;   // a little cheer now and then
  drawTigo(114, LAUNCH_Y - 4, 30, pose, now, true);
  spr.resetViewport();
  if (th.neon) spr.drawFastHLine(0, LAUNCH_Y - 3, 135, themeFade(90, th.frameSel));

  for (int e = 0; e < NUM_ENTRIES; e++) {
    bool sel = e == entrySel;
    int y = LAUNCH_Y + e * LAUNCH_PITCH;
    drawSelBox(6, y, 123, LAUNCH_BOX_H, 6, sel);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(sel ? th.text : th.textDim);
    spr.drawString(ENTRY_NAME[e], 12, y + 2, 4);
    if (e == ENT_GAMES) snprintf(buf, sizeof buf, "%d games", itemCount(0));
    else if (e == ENT_APPS) snprintf(buf, sizeof buf, "%d apps", itemCount(1));
    else ITEMS[e == ENT_SETTINGS ? ITEM_SETTINGS : ITEM_SLEEP].info(buf, sizeof buf);
    spr.setTextColor(!sel ? th.textDim : th.neon ? th.accent : TFT_LIGHTGREY);
    spr.drawString(buf, 12, y + 30, 1);
    if (clipIcon(y + 20, y + 1, y + LAUNCH_BOX_H - 1)) entryIcon(e, ICON_X, y + 20, !sel, sel ? animClock(now) : 0);
    spr.resetViewport();
  }

  drawThemedFooter("next", entrySel == ENT_SLEEP ? "sleep" : "open", 218, 74);
  spr.pushSprite(0, 0);
}

static void renderList(uint32_t now) {
  char buf[32];
  const Theme &th = theme();
  int l = listIdx(), scroll = listScroll[l], rows = rowCount(l);
  spr.fillSprite(th.bg);
  drawBadges(now, 9);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(th.neon ? th.frameSel : TFT_LIGHTGREY);
  spr.drawString(l ? "APPS" : "GAMES", 67, 6, 2);
  if (th.neon) spr.drawFastHLine(0, MENU_LIST_Y - 2, 135, themeFade(90, th.frameSel));

  // boxes, clipped to the list area (viewport coordinates start at its top)
  spr.setViewport(0, MENU_LIST_Y, 135, MENU_LIST_H);
  spr.setTextDatum(TL_DATUM);
  for (int r = 0; r < rows; r++) {
    bool sel = r == listSel[l];
    int y = 2 + r * BOX_PITCH - scroll;
    if (y + BOX_H < 0 || y >= MENU_LIST_H) continue;
    int g = rowItem(l, r);
    drawSelBox(6, y, 123, BOX_H, 6, sel);

    spr.setTextColor(sel ? th.text : th.textDim);
    spr.drawString(g < 0 ? "< Back" : ITEMS[g].name, 12, y + 6, 4);
    spr.setTextColor(!sel ? th.textDim : th.neon ? th.accent : TFT_LIGHTGREY);
    if (g < 0) snprintf(buf, sizeof buf, "hold R: back");
    else if (ITEMS[g].hiKey) snprintf(buf, sizeof buf, "Best %lu", (unsigned long)prefs.getUInt(ITEMS[g].hiKey, 0));
    else ITEMS[g].info(buf, sizeof buf);
    spr.drawString(buf, 12, y + 34, 2);

    // icon, clipped to its own area within the list (absolute coordinates);
    // only the selected one moves
    int cy = MENU_LIST_Y + y + 28;
    uint32_t t = sel ? animClock(now) : 0;
    if (clipIcon(cy, MENU_LIST_Y, MENU_LIST_Y + MENU_LIST_H)) {
      if (g < 0) backIcon(ICON_X, cy, !sel, t);
      else ITEMS[g].drawIcon(ICON_X, cy, !sel, t);
    }
    spr.setViewport(0, MENU_LIST_Y, 135, MENU_LIST_H);
  }
  spr.resetViewport();

  // arrows when there is more list above / below
  uint16_t arrow = th.neon ? th.accent : TFT_LIGHTGREY;
  if (scroll > 0) spr.fillTriangle(124, 15, 132, 15, 128, 9, arrow);
  if (scroll < maxScroll(l)) spr.fillTriangle(124, 222, 132, 222, 128, 228, arrow);

  int g = rowItem(l, listSel[l]);
  drawThemedFooter("next", g < 0 ? "back" : ITEMS[g].action, 218, 74);
  spr.pushSprite(0, 0);
}

// ---------------- navigation ----------------
static void startItem(int g, View from) {
  active = g;
  startedFrom = from;
  noteActiveItem(g);
  dbg("start %s\n", ITEMS[g].name);
  if (g != ITEM_SLEEP) remember(from == VIEW_GAMES ? ENT_GAMES : from == VIEW_APPS ? ENT_APPS : ENT_SETTINGS,
                                g < ITEM_SETTINGS ? g : -1);
  ITEMS[g].begin();
}

static void showView(View v, uint32_t now) {
  view = v;
  if (v != VIEW_LAUNCHER) listScroll[listIdx()] = scrollTarget(listIdx());
  pressedL = pressedR = false;
  selectedAt = now;
  dirty = true;
}

static void nextEntry(uint32_t now) {
  if (view == VIEW_LAUNCHER) entrySel = (entrySel + 1) % NUM_ENTRIES;
  else listSel[listIdx()] = (listSel[listIdx()] + 1) % rowCount(listIdx());
  selectedAt = now;
  dirty = true;
}

static void openSelected(uint32_t now) {
  if (view == VIEW_LAUNCHER) {
    if (entrySel == ENT_GAMES) showView(VIEW_GAMES, now);
    else if (entrySel == ENT_APPS) showView(VIEW_APPS, now);
    else startItem(entrySel == ENT_SETTINGS ? ITEM_SETTINGS : ITEM_SLEEP, VIEW_LAUNCHER);
    return;
  }
  int g = rowItem(listIdx(), listSel[listIdx()]);
  if (g < 0) showView(VIEW_LAUNCHER, now);
  else startItem(g, view);
}

// fresh input and animation clocks (boot, after the lock screen)
void touch() {
  pressedL = pressedR = false;
  lastInput = selectedAt = millis();
  dirty = true;
}

void backFromItem() {
  // goToSleep() also lands here while the launcher is showing: stay put then
  if (active >= 0) view = startedFrom;
  active = -1;
  noteActiveItem(-1);
  dbg("back to menu\n");
  touch();
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  // act on release, but only for presses that started in the menu;
  // holding L auto-repeats to scroll faster through the list
  if (eL == 1) { pressedL = true; bL.nextRepeat = now + 380; }
  if (eR == 1) pressedR = true;
  if (bL.stable && pressedL && (int32_t)(now - bL.nextRepeat) >= 0) {
    bL.nextRepeat = now + 150;
    bL.consumed = true;
    nextEntry(now);
  }
  if (eL == 2 && pressedL) {
    pressedL = false;
    if (!bL.consumed) nextEntry(now);
  }
  // hold R in a list: back to the launcher (showView drops the press, so its
  // release does nothing)
  if (view != VIEW_LAUNCHER && bR.stable && pressedR && (int32_t)(now - bR.t0) >= (int32_t)TOOL_HOLD_MS) {
    bR.consumed = true;
    showView(VIEW_LAUNCHER, now);
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    openSelected(now);
    if (active >= 0) return;                 // the item drew its own screen
  }

  // glide towards the selected box, ~60 fps
  if (view != VIEW_LAUNCHER) {
    int l = listIdx(), target = scrollTarget(l);
    if (listScroll[l] != target && (int32_t)(now - lastAnim) >= 16) {
      lastAnim = now;
      int d = (target - listScroll[l]) / 3;
      listScroll[l] += d ? d : (target > listScroll[l] ? 1 : -1);
      dirty = true;
    }
  }

  if ((int32_t)(now - lastFrame) >= (int32_t)ICON_FRAME_MS) dirty = true;

  if (idleTooLong(now, lastInput)) goToSleep();
  if (dirty) {
    lastFrame = now;
    if (view == VIEW_LAUNCHER) renderLauncher(now);
    else renderList(now);
    dirty = false;
  }
}

void begin() {
  restore();
  view = VIEW_LAUNCHER;
  touch();
}

int activeItem() { return active; }

}  // namespace launcher

void returnToMenu() { launcher::backFromItem(); }

// ---------------- main ----------------
void setup() {
  initPower();

  pinMode(0, INPUT_PULLUP);
  pinMode(35, INPUT);              // input-only pin, pulled up on the board

  // a button still held from the wake-up press must not count as a press
  bL.raw = bL.stable = digitalRead(bL.pin) == LOW;
  bR.raw = bR.stable = digitalRead(bR.pin) == LOW;

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  spr.setColorDepth(16);
  if (!spr.createSprite(tft.width(), tft.height())) {
    spr.setColorDepth(8);          // fallback if heap is tight
    spr.createSprite(tft.width(), tft.height());
  }

  prefs.begin("tetris", false);
  initDiag(itemName);
  initBacklight();
  initWifi();
  initBle();
  launcher::begin();               // remembered category and item, lists scrolled to them

  // PIN lock before the menu works (boot and wake from deep sleep); the
  // wake-up press is still held and doesn't count (stable already set above)
  if (lockscr::enabled()) {
    lockscr::run();
    launcher::touch();             // fresh idle / animation clocks after the lock screen
  }
}

void loop() {
  uint32_t now = millis();
  int item = launcher::activeItem();
  if (item >= 0) ITEMS[item].update(now);
  else launcher::update(now);
  timeService(now);
  bleService(now);

  static uint32_t lastLog = 0;
  if (usbLogOn() && now - lastLog >= 5000) {
    lastLog = now;
    dbg("t=%lus %s heap=%u minheap=%u stackfree=%u\n", (unsigned long)(now / 1000), itemName(launcher::activeItem()),
        ESP.getFreeHeap(), ESP.getMinFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
  delay(2);
}

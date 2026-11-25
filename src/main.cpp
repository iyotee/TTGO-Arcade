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
// Abyx and Crypt live in the "Big Games" cartridge (cart.h): greyed in the
// Games list with "Download" until it is installed in the other app slot,
// then starting one reboots into the cartridge, which comes back here.
// Updates (updates.h): an hourly manifest check while WiFi is connected; a
// newer launcher shows an arrow badge in the launcher header.
//
// High scores and settings are stored in NVS (survive power-off).
// On menu / title / game-over screens, after the screensaver delay of
// inactivity (Settings) Tigo's screensaver starts, and after the sleep delay
// the board goes into deep sleep; press the GPIO35 button to wake it. With the
// PIN lock on, the code is asked at boot and when leaving the screensaver.

#include "common.h"
#include "lockscr.h"
#include "themeui.h"
#include "ui_common.h"
#include "cart.h"
#include "updates.h"

// ---------------- items ----------------
// One flat array: the games (alphabetical, case-insensitive; digits first),
// then the apps (alphabetical), then Settings and Sleep. Crash records keep
// an index into it (noteActiveItem / itemName), numGames() / gameInfo() count
// the leading games, so Settings' reset list is alphabetical too.
// The cartridge's games have no code here: their entries open the cartridge
// screen (cartui), which starts them or offers the download.
namespace launcher {
int activeItem();
void abyxIcon(int cx, int cy, bool dim, uint32_t t);
void cryptIcon(int cx, int cy, bool dim, uint32_t t);
}
static void heavyBegin();
static void heavyUpdate(uint32_t now);

struct MenuItem {
  const char *name;              // font 4 (font 2 when wider than the box allows)
  const char *hiKey;             // NVS key of a game's high score; nullptr if not a game
  void (*begin)();
  void (*update)(uint32_t now);  // nullptr: begin() never returns
  void (*drawIcon)(int cx, int cy, bool dim, uint32_t t);
  void (*info)(char *buf, size_t n);   // subtitle when there is no high score
  const char *action;            // footer verb for R
};
#define GAME(name, key, ns) {name, key, ns::begin, ns::update, ns::drawIcon, nullptr, "play"}
#define HEAVY(name, key, icon) {name, key, heavyBegin, heavyUpdate, icon, nullptr, "play"}
#define APP(name, ns) {name, nullptr, ns::begin, ns::update, ns::drawIcon, ns::menuInfo, "open"}
static const MenuItem ITEMS[] = {
  GAME("2048",    "g2048_hi",   game2048),
  GAME("21",      "bj21_hi",    blackjack),
  HEAVY("Abyx",   "abyx_hi",    launcher::abyxIcon),
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
  HEAVY("Crypt",  "crypt_hi",   launcher::cryptIcon),
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
  APP("Comp.",   companionapp),
  APP("Dice",    dice),
  APP("Minute",  minute),
  APP("Morse",   morse),
  APP("Remote",  remote),
  APP("Support", support),
  APP("Tally",   tally),
  APP("Tigo",    tigoapp),
  APP("Timer",   timer),
  APP("Torch",   torch),
  // launcher entries
  {"Settings", nullptr, settings::begin, settings::update, settings::drawIcon, settings::menuInfo, "open"},
  {"Sleep",    nullptr, sleepNow,        nullptr,          sleepIcon,          sleepInfo,          "sleep"},
};
#undef GAME
#undef HEAVY
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

static bool isHeavy(int i) { return i >= 0 && i < NUM_ITEMS && ITEMS[i].begin == heavyBegin; }

static void heavyBegin() { cartui::open(ITEMS[launcher::activeItem()].hiKey); }
static void heavyUpdate(uint32_t now) {
  if (!cartui::update(now)) returnToMenu();
}

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
static const int BOX_H = 46, BOX_PITCH = 50;      // ~3.7 boxes fit in the viewport
static const int ICON_X = 114, ICON_HALF_W = 13, ICON_HALF_H = 16;   // icon clip area around (ICON_X, box centre)
static const uint32_t ICON_FRAME_MS = 33;         // the selected icon animates at ~30 fps
static const int NAME_MAX_W = ICON_X - ICON_HALF_W - 14;   // names wider than this are cut

// names are drawn in font 2, doubled one pixel to the right so they read bold
static void drawBold(const char *s, int x, int y) {
  spr.drawString(s, x, y, 2);
  spr.drawString(s, x + 1, y, 2);
}

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
// battery / USB badge with its left edge at x = 6 and its top at y, WiFi arcs
// after it, then (launcher header only) an arrow when a launcher update is out
static void drawBadges(uint32_t now, int y, bool updateBadge) {
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
  if (updateBadge && updates::updateAvailable()) {
    uint16_t uc = (now / 600) % 2 ? theme().ok : themeFade(150, theme().ok);   // a slow blink
    spr.fillTriangle(48, y + 4, 52, y, 56, y + 4, uc);
    spr.fillRect(51, y + 4, 3, 5, uc);
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

// ---- the cartridge's games, drawn here since their code is not in the launcher ----
// small "download" badge on an icon whose game is not installed
static void downloadBadge(int cx, int cy, bool dim) {
  const Theme &th = theme();
  uint16_t c = dim ? themeFade(110, th.accent) : th.accent;
  spr.fillCircle(cx + 7, cy + 9, 5, c);
  spr.fillRect(cx + 6, cy + 5, 3, 4, th.bg);
  spr.fillTriangle(cx + 4, cy + 9, cx + 10, cy + 9, cx + 7, cy + 12, th.bg);
}

void abyxIcon(int cx, int cy, bool dim, uint32_t t) {     // an anglerfish and its lure
  const Theme &th = theme();
  uint16_t body = dim ? themeFade(90, 0x3A7F) : 0x3A7F;
  uint16_t glow = dim ? themeFade(90, TFT_YELLOW) : TFT_YELLOW;
  float p = t ? triWave(t, 1200) : 1.0f;
  spr.fillEllipse(cx - 1, cy + 3, 9, 7, body);
  spr.fillTriangle(cx + 6, cy + 3, cx + 12, cy - 3, cx + 12, cy + 9, body);    // tail
  spr.fillCircle(cx - 4, cy, 2, dim ? th.textDim : TFT_WHITE);                  // eye
  spr.drawPixel(cx - 4, cy, TFT_BLACK);
  spr.drawFastHLine(cx - 10, cy + 6, 6, th.bg);                                 // mouth and teeth
  spr.drawPixel(cx - 9, cy + 5, TFT_WHITE);
  spr.drawPixel(cx - 7, cy + 5, TFT_WHITE);
  spr.drawLine(cx - 3, cy - 4, cx - 6, cy - 10, body);                          // the lure's stalk
  spr.drawLine(cx - 6, cy - 10, cx - 9, cy - 9, body);
  if (!dim) spr.drawCircle(cx - 9, cy - 8, 3 + (int)(2 * p), themeFade(60 + (int)(120 * p), glow));
  spr.fillCircle(cx - 9, cy - 8, 2, glow);
  if (cartState() == CART_NONE) downloadBadge(cx, cy, dim);
}

void cryptIcon(int cx, int cy, bool dim, uint32_t t) {    // a gravestone and a wisp
  const Theme &th = theme();
  uint16_t stone = dim ? themeFade(90, 0x9CF3) : 0x9CF3;
  uint16_t ground = dim ? themeFade(80, 0x2444) : 0x2444;
  uint16_t wisp = dim ? themeFade(90, 0x87F0) : 0x87F0;
  spr.fillRect(cx - 12, cy + 10, 24, 4, ground);
  spr.fillRoundRect(cx - 8, cy - 11, 16, 16, 7, stone);
  spr.fillRect(cx - 8, cy - 2, 16, 13, stone);
  spr.fillRect(cx - 1, cy - 7, 3, 12, th.bg);                                   // the cross
  spr.fillRect(cx - 4, cy - 4, 9, 3, th.bg);
  int bob = t ? (int)(4 * triWave(t, 1400)) : 2;
  spr.fillCircle(cx + 9, cy - 9 + bob, 2, wisp);
  spr.drawPixel(cx + 9, cy - 6 + bob, wisp);
  if (cartState() == CART_NONE) downloadBadge(cx, cy, dim);
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
  drawBadges(now, 23, true);
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
    drawBold(ENTRY_NAME[e], 12, y + 7);
    if (e == ENT_GAMES) snprintf(buf, sizeof buf, "%d games", itemCount(0));
    else if (e == ENT_APPS) snprintf(buf, sizeof buf, "%d apps", itemCount(1));
    else ITEMS[e == ENT_SETTINGS ? ITEM_SETTINGS : ITEM_SLEEP].info(buf, sizeof buf);
    spr.setTextColor(!sel ? th.textDim : th.neon ? th.accent : TFT_LIGHTGREY);
    spr.drawString(buf, 12, y + 27, 1);
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
  drawBadges(now, 9, false);
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

    // a cartridge game that is not installed is greyed
    int cs = isHeavy(g) ? cartState() : CART_READY;
    bool grey = cs == CART_NONE;
    const char *name = g < 0 ? "< Back" : ITEMS[g].name;
    uint16_t nameCol = sel ? th.text : th.textDim;
    if (grey) nameCol = sel ? th.textDim : themeFade(140, th.textDim);
    spr.setTextColor(nameCol);
    drawBold(name, 12, y + 8);
    spr.setTextColor(!sel ? th.textDim : th.neon ? th.accent : TFT_LIGHTGREY);
    uint32_t best = g >= 0 && ITEMS[g].hiKey ? prefs.getUInt(ITEMS[g].hiKey, 0) : 0;
    if (g < 0) snprintf(buf, sizeof buf, "hold R: back");
    else if (cs == CART_NONE) snprintf(buf, sizeof buf, "Download");
    else if (cs == CART_OUTDATED) snprintf(buf, sizeof buf, "Update");
    else if (isHeavy(g) && !best) snprintf(buf, sizeof buf, "Ready");
    else if (ITEMS[g].hiKey) snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
    else ITEMS[g].info(buf, sizeof buf);
    if (sel && cs != CART_READY) spr.setTextColor(th.neon ? th.accent2 : TFT_CYAN);
    spr.drawString(buf, 12, y + 30, 1);

    // icon, clipped to its own area within the list (absolute coordinates);
    // only the selected one moves
    int cy = MENU_LIST_Y + y + BOX_H / 2;
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
  const char *act = g < 0 ? "back" : isHeavy(g) && cartState() == CART_NONE ? "get" : ITEMS[g].action;
  drawThemedFooter("next", act, 218, 74);
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

// back from the cartridge: straight into the Games list, on the game it ran
void showGames() { showView(VIEW_GAMES, millis()); }

}  // namespace launcher

void returnToMenu() { launcher::backFromItem(); }

// ---------------- main ----------------
// A cartridge session that did not end through returnToMenu(): diag.cpp read
// the cartridge's RTC record with the launcher's item list, so name the game
// from the hand-over key instead.
static void noteCartCrash(const char *before, const char *hiKey) {
  const char *kind = nullptr;
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:    kind = "crash"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      kind = "watchdog"; break;
    case ESP_RST_BROWNOUT: kind = "brownout"; break;
    default: break;
  }
  char now[40] = "";
  bool fresh = lastCrash(now, sizeof now) && strcmp(now, before);   // diag recorded something this boot
  if (!kind && !fresh) return;
  if (!kind) kind = "reset";
  int gi = cartGameIndex(hiKey);
  char buf[40];
  snprintf(buf, sizeof buf, "%s in %s", kind, gi >= 0 ? CART_GAMES[gi].name : CART_TITLE);
  prefs.putString("lastcrash", buf);
}

void setup() {
  bootHardware();                  // power, buttons, display, sprite, prefs

  char crashBefore[40] = "";
  lastCrash(crashBefore, sizeof crashBefore);
  initDiag(itemName);
  char cartGame[16];
  CartReturn back = cartBootReturn(cartGame, sizeof cartGame);
  if (back == CART_RET_UNCLEAN) noteCartCrash(crashBefore, cartGame);
  dbg("launcher build %u%s\n", (unsigned)FW_BUILD, back == CART_RET_CLEAN ? ", back from the cartridge" : "");

  initBacklight();
  initWifi();
  initBle();
  updates::begin();
  launcher::begin();               // remembered category and item, lists scrolled to them
  if (back == CART_RET_CLEAN) launcher::showGames();

  // PIN lock before the menu works (boot and wake from deep sleep); the
  // wake-up press is still held and doesn't count (stable already set above).
  // Not after a clean return from the cartridge: it was unlocked to start it
  // (a reset or a wake from sleep in the cartridge is not "clean").
  bool unlocked = back == CART_RET_CLEAN && esp_reset_reason() == ESP_RST_SW;
  if (lockscr::enabled() && !unlocked) {
    lockscr::run();
    launcher::touch();             // fresh idle / animation clocks after the lock screen
  }
}

void loop() {
  uint32_t now = millis();
  int item = launcher::activeItem();
  if (item >= 0) ITEMS[item].update(now);
  else launcher::update(now);
  item = launcher::activeItem();
  // update checks and their downloads: launcher, Settings, cartridge screen; never in a game
  if (item < 0 || item == ITEM_SETTINGS || isHeavy(item)) updates::service(now);
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

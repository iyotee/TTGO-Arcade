// Slots (3-Reel Fruit Machine)
//
// Controls (two buttons):
//   Tap L  : change bet (1 / 2 / 5 / 10)
//   Tap R  : spin; while spinning, tap R to stop the next reel
//   Hold L : cash out (ends the game)
// Start with 100 credits. Three of a kind pays up to 100x; cherries pay too.
// Best score tracks your peak credit stack.

#include "common.h"

namespace slots {

static const int REELS = 3, STRIP = 20, NSYM = 6;
static const uint32_t TICK_MS = 16;
static const uint32_t CASHOUT_MS = 700;

// symbols, cheapest first
enum Sym { CHERRY, LEMON, BELL, BAR, GEM, SEVEN };
static const uint8_t PAY3[NSYM] = {5, 8, 12, 20, 40, 100};   // x bet for three of a kind
static const int BETS[] = {1, 2, 5, 10};
static const int NUM_BETS = sizeof BETS / sizeof BETS[0];

// each reel strip: cherry x6, lemon x5, bell x4, bar x3, gem x1, seven x1 (shuffled per game)
static uint8_t strip[REELS][STRIP];

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float pos[REELS], spd[REELS];   // reel position in symbols (fractional while moving)
static int stopAt[REELS];              // target index once stopping, -1 = free spin
static int spinT, betIdx, win, winFlash, nextStop;
static bool spinning;
static uint32_t credits = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false, cashedOut = false;
static bool lHeld = false;             // L press started in play (for hold = cash out)
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void saveBest() {
  if (credits > hiScore) {
    hiScore = credits;
    newHigh = true;
    prefs.putUInt("slots_hi", hiScore);
  }
}

static void gameOver(bool cash) {
  state = GAMEOVER;
  armed = false;
  cashedOut = cash;
  saveBest();
  lastInput = millis();
  dirty = true;
}

static void shuffleStrips() {
  static const uint8_t COUNT[NSYM] = {6, 5, 4, 3, 1, 1};
  for (int r = 0; r < REELS; r++) {
    int k = 0;
    for (int s = 0; s < NSYM; s++)
      for (int c = 0; c < COUNT[s]; c++) strip[r][k++] = s;
    for (int i = STRIP - 1; i > 0; i--) {
      int j = rnd(i + 1);
      uint8_t tmp = strip[r][i];
      strip[r][i] = strip[r][j];
      strip[r][j] = tmp;
    }
  }
}

static void newGame() {
  credits = 100;
  betIdx = 0;
  win = winFlash = 0;
  spinning = false;
  newHigh = false;
  lHeld = false;
  shuffleStrips();
  for (int r = 0; r < REELS; r++) { pos[r] = rnd(STRIP); spd[r] = 0; stopAt[r] = -1; }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static int bet() { return BETS[betIdx]; }
static uint8_t symAt(int r, int idx) { return strip[r][((idx % STRIP) + STRIP) % STRIP]; }
static uint8_t payline(int r) { return symAt(r, (int)roundf(pos[r])); }

static void startSpin() {
  if (credits < (uint32_t)bet()) return;
  credits -= bet();
  win = winFlash = 0;
  spinning = true;
  spinT = 0;
  nextStop = 0;
  for (int r = 0; r < REELS; r++) { spd[r] = 0.42f + r * 0.03f; stopAt[r] = -1; }
}

// the reel lands where a random draw says, a few symbols ahead, so the stop
// timing only changes how soon it lands, not what it shows
static void stopReel(int r) {
  stopAt[r] = (int)floorf(pos[r]) + 3 + rnd(STRIP);
}

static void settle() {
  uint8_t a = payline(0), b = payline(1), c = payline(2);
  int mult = 0;
  if (a == b && b == c) mult = PAY3[a];
  else {
    int cherries = (a == CHERRY) + (b == CHERRY) + (c == CHERRY);
    if (cherries == 2) mult = 2;
    else if (a == CHERRY) mult = 1;              // a cherry on the first reel returns the bet
  }
  win = mult * bet();
  credits += win;
  winFlash = win ? (mult >= 20 ? 120 : 60) : 0;
  saveBest();
  // keep the bet affordable
  while (betIdx > 0 && (uint32_t)bet() > credits) betIdx--;
  if (credits == 0) gameOver(false);
}

static void tick(int eL, int eR) {
  if (winFlash > 0) winFlash--;

  if (!spinning) {
    if (eL == 2 && !bL.consumed) {
      // next bet the stack can cover, wrapping to the smallest
      do betIdx = (betIdx + 1) % NUM_BETS; while (betIdx > 0 && (uint32_t)bet() > credits);
    }
    if (eR == 1) startSpin();
    return;
  }

  spinT++;
  if (eR == 1 && spinT > 12 && nextStop < REELS) stopReel(nextStop++);
  // auto-stop one after another if the player doesn't
  if (nextStop < REELS && spinT > 50 + nextStop * 18) stopReel(nextStop++);

  bool moving = false;
  for (int r = 0; r < REELS; r++) {
    if (spd[r] == 0) continue;
    if (stopAt[r] >= 0) {
      float left = stopAt[r] - pos[r];
      if (left <= 0.02f) {
        pos[r] = (float)(stopAt[r] % STRIP);
        spd[r] = 0;
        continue;
      }
      spd[r] = constrain(left * 0.12f, 0.05f, 0.45f);  // ease into the slot
    }
    pos[r] += spd[r];
    if (stopAt[r] < 0 && pos[r] >= STRIP) pos[r] -= STRIP;   // wrap only while free-spinning
    moving = true;
  }
  if (!moving && nextStop >= REELS) {
    spinning = false;
    settle();
  }
}

// ---------------- rendering ----------------
static const int REEL_X0 = 8, REEL_W = 37, REEL_GAP = 4;
static const int ROW_H = 28, WIN_Y = 70, WIN_H = ROW_H * 3;

static void drawSym(uint8_t s, int x, int y) {      // centred on (x, y), ~22 px
  switch (s) {
    case CHERRY:
      spr.drawLine(x - 4, y + 3, x + 1, y - 8, TFT_GREEN);
      spr.drawLine(x + 5, y + 3, x + 1, y - 8, TFT_GREEN);
      spr.fillCircle(x - 4, y + 5, 4, TFT_RED);
      spr.fillCircle(x + 5, y + 5, 4, TFT_RED);
      break;
    case LEMON:
      spr.fillEllipse(x, y, 9, 6, TFT_YELLOW);
      spr.fillCircle(x + 9, y, 2, TFT_YELLOW);
      break;
    case BELL:
      spr.fillCircle(x, y - 3, 6, TFT_GOLD);
      spr.fillTriangle(x - 9, y + 6, x + 9, y + 6, x, y - 6, TFT_GOLD);
      spr.fillCircle(x, y + 7, 2, TFT_ORANGE);
      break;
    case BAR:
      spr.fillRoundRect(x - 11, y - 6, 22, 12, 2, TFT_WHITE);
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(TFT_BLACK);
      spr.drawString("BAR", x, y + 1, 1);
      break;
    case GEM:
      spr.fillTriangle(x - 9, y - 3, x + 9, y - 3, x, y + 9, TFT_CYAN);
      spr.fillTriangle(x - 9, y - 3, x - 5, y - 8, x + 5, y - 8, TFT_CYAN);
      spr.fillTriangle(x - 9, y - 3, x + 9, y - 3, x + 5, y - 8, TFT_CYAN);
      spr.drawLine(x - 4, y - 3, x, y + 8, TFT_WHITE);
      break;
    default:   // SEVEN
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(TFT_RED);
      spr.drawString("7", x, y + 1, 4);
      break;
  }
}

static void drawGame() {
  char buf[24];
  spr.fillSprite(0x1082);

  // cabinet + reel windows (symbols clipped to each window)
  bool flash = winFlash > 0 && (winFlash / 6) & 1;
  spr.fillRoundRect(2, WIN_Y - 8, 131, WIN_H + 16, 6, flash ? TFT_GOLD : 0x8200);
  for (int r = 0; r < REELS; r++) {
    int x = REEL_X0 + r * (REEL_W + REEL_GAP);
    spr.fillRect(x, WIN_Y, REEL_W, WIN_H, 0xEF7D);
    spr.setViewport(x, WIN_Y, REEL_W, WIN_H);
    int base = (int)floorf(pos[r]);
    float frac = pos[r] - base;
    for (int k = -2; k <= 2; k++) {
      // a higher position shows later symbols: the strip scrolls downward
      int cy = WIN_Y + ROW_H + ROW_H / 2 + (int)roundf((frac - k) * ROW_H) - WIN_Y;
      drawSym(symAt(r, base + k), REEL_W / 2, cy);
    }
    spr.resetViewport();
  }
  // payline
  int py = WIN_Y + ROW_H + ROW_H / 2;
  spr.drawFastHLine(2, py, 4, TFT_RED);
  spr.drawFastHLine(129, py, 4, TFT_RED);
  spr.drawRect(REEL_X0 - 2, WIN_Y + ROW_H - 1, 3 * REEL_W + 2 * REEL_GAP + 4, ROW_H + 2, TFT_RED);

  // win / bet line
  spr.setTextDatum(TC_DATUM);
  if (win > 0) {
    spr.setTextColor(flash ? TFT_WHITE : TFT_YELLOW);
    snprintf(buf, sizeof buf, "WIN %d!", win);
    spr.drawString(buf, 67, 172, 4);
  } else if (!spinning) {
    spr.setTextColor(TFT_GREEN);
    spr.drawString(credits >= (uint32_t)bet() ? "R spin" : "", 67, 178, 2);
  }
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Bet %d", bet());
  spr.drawString(buf, 67, 202, 2);
  spr.setTextColor(TFT_DARKGREY);
  spr.drawString("L bet  hold L cash out", 67, 224, 1);

  // header: credits instead of a score
  spr.fillRect(0, 0, 135, HEADER_H, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "Credits %lu", (unsigned long)credits);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 2, 24, 1);

  if (state == GAMEOVER) drawGameOverBox(cashedOut ? "CASHED OUT" : "BROKE!", credits, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GOLD);
  spr.drawString("SLOTS", 67, 18, 4);

  drawSym(SEVEN, 41, 64);
  drawSym(SEVEN, 67, 64);
  drawSym(SEVEN, 93, 64);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R", "Hold L"};
  const char *acts[] = {"change bet", "spin / stop", "cash out"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 92 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 92 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("777 pays 100x the bet", 67, 158, 1);
  spr.drawString("Best = peak credits", 67, 170, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void render() {
  if (state == TITLE) drawTitle();
  else drawGame();
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  spr.fillRoundRect(x - 12, y - 10, 24, 20, 3, c(0x8200));
  spr.fillRect(x - 10, y - 7, 20, 14, c(TFT_WHITE));
  // the three little reels roll a digit at a time
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(c(TFT_RED));
  for (int i = 0; i < 3; i++) {
    char d[2] = {(char)(t ? '0' + (t / (90 + i * 40)) % 10 : '7'), 0};
    if (t && t % 3000 > 2000) d[0] = '7';   // ...then line up 777
    spr.drawString(d, x - 6 + i * 6, y + 1, 1);
  }
  spr.setTextDatum(TL_DATUM);                     // the menu draws its labels with TL
  spr.fillRect(x + 12, y - 8, 2, 8, c(TFT_LIGHTGREY));
  spr.fillCircle(x + 13, y - 9, 2, c(TFT_RED));
}

void begin() {
  hiScore = prefs.getUInt("slots_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    // hold L to cash out (only a press that started during play)
    if (eL == 1) lHeld = true;
    if (lHeld && bL.stable && !bL.consumed && !spinning && now - bL.t0 >= CASHOUT_MS) {
      bL.consumed = true;
      lHeld = false;
      gameOver(true);
    }
    // events wait for the next tick, so one between two ticks still counts
    static int qL = 0, qR = 0;
    if (eL == 2 && lHeld) { qL = 2; lHeld = false; }
    if (eR == 1) qR = 1;
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(qL, qR);
      qL = qR = 0;
      dirty = true;
    }
    if (state == PLAY && winFlash == 0 && !spinning && idleTooLong(now, lastInput)) goToSleep();
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace slots

// Fish (Deep Sea Angler)
//
// Controls (two buttons):
//   Hold L / R : move the boat left / right (the hook follows)
//   L + R      : reel in now
// The hook sinks by itself and reels up once it catches a fish. Deeper fish are
// worth more. Jellyfish steal a hooked fish; a shark that crosses your line cuts
// it (-5 s). Catch as much as you can in 60 seconds.

#include "common.h"

namespace fish {

static const int MAX_FISH = 9;
static const int SURF_Y = HEADER_H + 22, BOTTOM_Y = 230;
static const int GAME_TICKS = 60 * 1000 / 16;
static const uint32_t TICK_MS = 16;

// kinds: 0 small, 1 medium, 2 deep, 3 jellyfish, 4 shark
struct Fish { float x, y, vx; uint8_t kind; bool active; };
static const int VALUE[3] = {10, 25, 60};
static const uint16_t FCOL[5] = {TFT_YELLOW, TFT_ORANGE, TFT_MAGENTA, 0xFB9F, TFT_LIGHTGREY};

enum State { TITLE, PLAY, GAMEOVER };
enum Hook { SINK, REEL };
static State state = TITLE;
static Hook hook = SINK;

static Fish fishes[MAX_FISH];
static float boatX, hookY;
static int caught;                                 // kind on the hook, -1 = none
static int timeLeft, frame, popT, popVal, cutT;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("fish_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void spawnFish(int i) {
  Fish &f = fishes[i];
  int r = rnd(100);
  if (r < 34) { f.kind = 0; f.y = SURF_Y + 14 + rnd(50); }
  else if (r < 60) { f.kind = 1; f.y = SURF_Y + 64 + rnd(56); }
  else if (r < 74) { f.kind = 2; f.y = SURF_Y + 120 + rnd(BOTTOM_Y - SURF_Y - 130); }
  else if (r < 90) { f.kind = 3; f.y = SURF_Y + 30 + rnd(110); }
  else { f.kind = 4; f.y = SURF_Y + 50 + rnd(100); }
  float spd = f.kind == 4 ? 2.0f : f.kind == 3 ? 0.35f : 0.6f + f.kind * 0.35f + rnd(5) * 0.1f;
  bool fromLeft = rnd(2);
  f.x = fromLeft ? -12 : 147;
  f.vx = fromLeft ? spd : -spd;
  f.active = true;
}

static void newGame() {
  score = 0;
  boatX = 67;
  hookY = SURF_Y;
  hook = SINK;
  caught = -1;
  timeLeft = GAME_TICKS;
  frame = popT = cutT = 0;
  newHigh = false;
  for (int i = 0; i < MAX_FISH; i++) {
    spawnFish(i);
    fishes[i].x = 10 + rnd(115);                   // start with a stocked sea
  }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  frame++;
  if (popT > 0) popT--;
  if (cutT > 0) cutT--;
  if (--timeLeft <= 0) { timeLeft = 0; gameOver(); return; }

  bool l = bL.stable, r = bR.stable;
  if (l && !r) boatX -= 1.6f;
  if (r && !l) boatX += 1.6f;
  boatX = constrain(boatX, 10.0f, 125.0f);
  if (l && r && hook == SINK) hook = REEL;

  if (hook == SINK) {
    hookY += 1.2f;
    if (hookY >= BOTTOM_Y - 4) hook = REEL;
  } else {
    hookY -= caught >= 0 ? 1.9f : 2.8f;
    if (hookY <= SURF_Y) {
      hookY = SURF_Y;
      if (caught >= 0) { score += VALUE[caught]; popVal = VALUE[caught]; popT = 45; }
      caught = -1;
      hook = SINK;
    }
  }

  for (int i = 0; i < MAX_FISH; i++) {
    Fish &f = fishes[i];
    if (!f.active) { spawnFish(i); continue; }
    f.x += f.vx;
    float fy = f.y + (f.kind == 3 ? sinf(frame * 0.05f + i) * 6 : 0);
    if (f.x < -16 || f.x > 151) { spawnFish(i); continue; }

    if (f.kind == 4) {
      // a shark crossing the line cuts it
      if (fabsf(f.x - boatX) < 7 && fy > SURF_Y && fy < hookY) {
        caught = -1;
        hook = SINK;
        hookY = SURF_Y;
        timeLeft = max(1, timeLeft - 5 * 1000 / 16);
        cutT = 50;
      }
      continue;
    }
    bool touch = fabsf(f.x - boatX) < 8 && fabsf(fy - hookY) < 6;
    if (!touch) continue;
    if (f.kind == 3) {
      if (caught >= 0) { caught = -1; cutT = 30; }  // the jellyfish stings it free
      continue;
    }
    if (caught < 0) {
      caught = f.kind;
      hook = REEL;
      spawnFish(i);
    }
  }
}

// ---------------- rendering ----------------
static void drawFish(int x, int y, uint8_t kind, int dir, uint32_t f) {
  uint16_t col = FCOL[kind];
  if (kind == 3) {                                 // jellyfish
    spr.fillCircle(x, y, 5, col);
    spr.fillRect(x - 5, y + 1, 11, 5, 0x0011);
    for (int k = -4; k <= 4; k += 4) spr.drawLine(x + k, y + 1, x + k + ((f / 8) & 1 ? 1 : -1), y + 8, col);
    return;
  }
  int len = kind == 4 ? 12 : 5 + kind * 2;
  spr.fillEllipse(x, y, len, kind == 4 ? 4 : 3, col);
  spr.fillTriangle(x - dir * len, y, x - dir * (len + 5), y - 4, x - dir * (len + 5), y + 4, col);
  if (kind == 4) spr.fillTriangle(x, y - 3, x - dir * 4, y - 3, x - dir * 2, y - 9, col);
  spr.drawPixel(x + dir * (len - 3), y - 1, TFT_BLACK);
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(0x0011);
  spr.fillRect(0, HEADER_H, 135, SURF_Y - HEADER_H, 0x5D7F);                 // sky
  spr.fillRect(0, SURF_Y, 135, 50, 0x0179);
  spr.fillRect(0, SURF_Y + 50, 135, 60, 0x00F4);
  spr.fillRect(0, BOTTOM_Y, 135, 240 - BOTTOM_Y, 0x6A43);                    // sand

  for (int i = 0; i < MAX_FISH; i++) {
    const Fish &f = fishes[i];
    if (!f.active) continue;
    int fy = (int)(f.y + (f.kind == 3 ? sinf(frame * 0.05f + i) * 6 : 0));
    drawFish((int)f.x, fy, f.kind, f.vx > 0 ? 1 : -1, frame);
  }

  // boat, line and hook
  int bx = (int)boatX;
  spr.fillTriangle(bx - 12, SURF_Y - 6, bx + 12, SURF_Y - 6, bx + 7, SURF_Y + 1, 0x8A22);
  spr.fillTriangle(bx - 12, SURF_Y - 6, bx - 7, SURF_Y + 1, bx + 7, SURF_Y + 1, 0x8A22);
  spr.drawFastVLine(bx + 4, SURF_Y - 16, 10, TFT_WHITE);                     // rod
  int hy = (int)hookY;
  spr.drawLine(bx + 4, SURF_Y - 16, bx, SURF_Y - 2, TFT_WHITE);
  spr.drawFastVLine(bx, SURF_Y - 2, hy - SURF_Y + 2, cutT > 0 && (cutT / 4) & 1 ? TFT_RED : TFT_LIGHTGREY);
  spr.drawLine(bx, hy, bx, hy + 3, TFT_WHITE);
  spr.drawLine(bx, hy + 3, bx + 3, hy + 1, TFT_WHITE);
  if (caught >= 0) drawFish(bx, hy + 7, caught, 1, frame);

  if (popT > 0) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_YELLOW);
    snprintf(buf, sizeof buf, "+%d", popVal);
    spr.drawString(buf, bx, SURF_Y - 30 + popT / 4, 2);
  }

  drawScoreHeader(score, hiScore);
  int secs = (timeLeft * 16 + 999) / 1000;
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(secs <= 10 ? TFT_RED : TFT_WHITE);
  snprintf(buf, sizeof buf, "%ds", secs);
  spr.drawString(buf, 132, 4, 4);

  if (state == GAMEOVER) drawGameOverBox("TIME UP!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("FISH", 67, 18, 4);

  drawFish(52, 70, 0, 1, 0);
  drawFish(84, 62, 2, -1, 0);
  spr.drawFastVLine(67, 48, 24, TFT_LIGHTGREY);
  spr.drawLine(67, 72, 70, 70, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"move boat", "reel in"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Deeper fish pay more", 67, 142, 1);
  spr.drawString("Jellies steal, sharks", 67, 154, 1);
  spr.drawString("cut the line (-5 s)", 67, 166, 1);

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
  int hy = y - 4 + (int)(10 * triWave(t, 1600));
  spr.drawFastVLine(x + 2, y - 14, hy - (y - 14), c(TFT_LIGHTGREY));
  spr.drawLine(x + 2, hy, x + 2, hy + 3, c(TFT_WHITE));
  spr.drawLine(x + 2, hy + 3, x + 5, hy + 1, c(TFT_WHITE));
  int fx = x - 12 + (int)((t / 40) % 24);
  spr.fillEllipse(fx, y + 9, 5, 3, c(TFT_ORANGE));
  spr.fillTriangle(fx - 5, y + 9, fx - 9, y + 6, fx - 9, y + 12, c(TFT_ORANGE));
}

void begin() {
  hiScore = prefs.getUInt("fish_hi", 0);
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
      dirty = true;
    }
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace fish

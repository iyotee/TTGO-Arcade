// Gravity (Wall Flip)
//
// Controls (two buttons):
//   Tap L : gravity pulls to the left wall
//   Tap R : gravity pulls to the right wall
// Spikes on the walls rush at you: flip to the other wall in time. Saws float in
// the middle of the shaft, so cross between them. Grab gems for +10. Speeds up.

#include "common.h"

namespace gravity {

static const int MAX_OBS = 10;
static const int WALL_W = 6, PL = 10;              // wall thickness, player size
static const int PLAYER_Y = 184;                   // top of the player box
static const int LEFT_X = WALL_W, RIGHT_X = 135 - WALL_W - PL;
static const uint32_t TICK_MS = 16;

// kind: 0 = spike strip on the left wall, 1 = on the right wall, 2 = saw, 3 = gem
struct Obs { float y; int16_t x, len; uint8_t kind; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Obs obs[MAX_OBS];
static float plX, plVx, speed, nextGap, scrolled, travel;
static int grav;                                   // -1 = left wall, +1 = right wall
static int lastSide;                               // wall of the last spike strip
static uint32_t score = 0, hiScore = 0, frame = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("gravity_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  frame = 0;
  plX = LEFT_X;
  plVx = 0;
  grav = -1;
  lastSide = 1;
  speed = 2.0f;
  scrolled = travel = 0;
  nextGap = 60;
  newHigh = false;
  for (int i = 0; i < MAX_OBS; i++) obs[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void addObs(uint8_t kind, int x, int len) {
  for (int i = 0; i < MAX_OBS; i++) {
    if (obs[i].active) continue;
    obs[i] = {(float)(HEADER_H - len), (int16_t)x, (int16_t)len, kind, true};
    return;
  }
}

// One "row" of the course: a spike strip, sometimes with a saw or a gem
static void spawnRow() {
  // mostly alternate walls so a flip is needed, sometimes repeat a side
  int side = rnd(100) < 70 ? 1 - lastSide : lastSide;
  lastSide = side;
  int len = 26 + rnd(30);
  addObs(side, 0, len);
  int r = rnd(100);
  if (score > 15 && r < 35) addObs(2, 34 + rnd(68), 14);          // saw mid-shaft
  else if (r < 60) addObs(3, 30 + rnd(76), 10);                   // gem
  // gaps shrink with speed, but a flip across always fits
  nextGap = len + max(46.0f, 96.0f - score * 0.5f) + rnd(30);
}

static bool overlap(float ax, float ay, int aw, int ah, float bx, float by, int bw, int bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void tick(int eL, int eR) {
  frame++;
  if (eL == 1) grav = -1;
  if (eR == 1) grav = 1;

  // sideways gravity, landing sticks to the wall
  plVx += grav * 0.9f;
  plVx = constrain(plVx, -8.0f, 8.0f);
  plX += plVx;
  if (plX <= LEFT_X) { plX = LEFT_X; plVx = 0; }
  if (plX >= RIGHT_X) { plX = RIGHT_X; plVx = 0; }

  speed = min(4.6f, 2.0f + score * 0.012f);
  scrolled += speed;
  travel += speed;
  if (travel >= 24) { travel -= 24; score++; }
  if (scrolled >= nextGap) { scrolled = 0; spawnRow(); }

  for (int i = 0; i < MAX_OBS; i++) {
    Obs &o = obs[i];
    if (!o.active) continue;
    o.y += speed;
    if (o.y > 240) { o.active = false; continue; }
    bool hit = false;
    if (o.kind <= 1) {
      int sx = o.kind == 0 ? WALL_W : 135 - WALL_W - 12;
      hit = overlap(plX + 1, PLAYER_Y + 1, PL - 2, PL - 2, sx, o.y, 12, o.len);
    } else if (o.kind == 2) {
      float cx = plX + PL / 2.0f - o.x, cy = PLAYER_Y + PL / 2.0f - (o.y + 7);
      hit = cx * cx + cy * cy < 11.0f * 11.0f;
    } else if (overlap(plX, PLAYER_Y, PL, PL, o.x - 5, o.y, 10, 10)) {
      o.active = false;
      score += 10;
    }
    if (hit) { gameOver(); return; }
  }
}

// ---------------- rendering ----------------
static void drawSpikes(int kind, int y, int len) {
  int base = kind == 0 ? WALL_W : 135 - WALL_W;
  int tip = kind == 0 ? WALL_W + 12 : 135 - WALL_W - 12;
  for (int sy = y; sy < y + len; sy += 8) {
    int h = min(8, y + len - sy);
    spr.fillTriangle(base, sy, base, sy + h, tip, sy + h / 2, TFT_RED);
  }
}

static void drawSaw(int x, int y, uint32_t f) {
  spr.fillCircle(x, y, 7, TFT_LIGHTGREY);
  for (int k = 0; k < 6; k++) {                    // spinning teeth
    float a = k * PI / 3 + f * 0.25f;
    spr.fillCircle(x + (int)roundf(cosf(a) * 7), y + (int)roundf(sinf(a) * 7), 2, TFT_LIGHTGREY);
  }
  spr.fillCircle(x, y, 2, TFT_DARKGREY);
}

static void drawGem(int x, int y, uint16_t col) {
  spr.fillTriangle(x - 5, y + 4, x + 5, y + 4, x, y, col);
  spr.fillTriangle(x - 5, y + 4, x + 5, y + 4, x, y + 10, col);
}

static void drawGame() {
  spr.fillSprite(0x0841);
  // walls with moving stripes so speed is visible
  int off = (int)(frame * speed) % 16;
  for (int y = HEADER_H - 16 + off; y < 240; y += 16) {
    spr.fillRect(0, y, WALL_W, 8, 0x4208);
    spr.fillRect(0, y + 8, WALL_W, 8, 0x2945);
    spr.fillRect(135 - WALL_W, y, WALL_W, 8, 0x4208);
    spr.fillRect(135 - WALL_W, y + 8, WALL_W, 8, 0x2945);
  }

  for (int i = 0; i < MAX_OBS; i++) {
    const Obs &o = obs[i];
    if (!o.active) continue;
    if (o.kind <= 1) drawSpikes(o.kind, (int)o.y, o.len);
    else if (o.kind == 2) drawSaw(o.x, (int)o.y + 7, frame);
    else drawGem(o.x, (int)o.y, TFT_CYAN);
  }

  // player, with a short trail while crossing
  int px = (int)plX;
  if (fabsf(plVx) > 0.5f) {
    spr.fillRect(px - (int)plVx * 2, PLAYER_Y + 2, PL - 4, PL - 4, 0x0410);
    spr.fillRect(px - (int)plVx, PLAYER_Y + 1, PL - 2, PL - 2, 0x0618);
  }
  spr.fillRect(px, PLAYER_Y, PL, PL, TFT_GREEN);
  // eye looks towards the wall gravity pulls to
  spr.fillRect(px + (grav < 0 ? 2 : PL - 4), PLAYER_Y + 3, 2, 3, TFT_BLACK);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("SPIKED!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("GRAVITY", 67, 18, 4);

  spr.fillRect(30, 50, 4, 30, 0x4208);
  spr.fillRect(101, 50, 4, 30, 0x4208);
  drawSpikes(1, 52, 16);
  spr.fillRect(34, 66, 10, 10, TFT_GREEN);
  spr.drawLine(48, 71, 62, 71, TFT_DARKGREY);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"fall left", "fall right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Dodge spikes and saws,", 67, 142, 1);
  spr.drawString("grab gems for +10", 67, 154, 1);

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
  spr.fillRect(x - 12, y - 14, 3, 28, c(0x4208));
  spr.fillRect(x + 10, y - 14, 3, 28, c(0x4208));
  // the cube hops from wall to wall
  int bx = (int)roundf(-9 + 13 * triWave(t, 1400));
  spr.fillRect(x + bx, y + 2, 8, 8, c(TFT_GREEN));
  spr.fillTriangle(x + 10, y - 12, x + 10, y - 4, x + 4, y - 8, c(TFT_RED));
}

void begin() {
  hiScore = prefs.getUInt("gravity_hi", 0);
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
    // presses wait for the next tick, so a press between two ticks still counts
    static int qL = 0, qR = 0;
    if (eL == 1) qL = 1;
    if (eR == 1) qR = 1;
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(qL, qR);
      qL = qR = 0;
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

}  // namespace gravity

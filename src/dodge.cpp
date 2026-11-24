// Dodge (Bullet Storm)
//
// Controls (two buttons):
//   Hold L / R : move left / right
// A turret at the top fires changing bullet patterns. Survive as long as you
// can; bullets that skim past you (grazes) give bonus points. One hit ends it.

#include "common.h"

namespace dodge {

static const int MAX_BUL = 100;
static const int PLAYER_Y = 212, TURRET_Y = 54;
static const float HIT_R = 4.0f, GRAZE_R = 11.0f;  // bullet radius 2.5 + player core
static const uint32_t TICK_MS = 16;

struct Bullet { float x, y, vx, vy; uint16_t col; bool active, grazed; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Bullet buls[MAX_BUL];
static float plX, turX, turVx, spiralA;
static int frame, pattern, grazes;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static const int PATTERN_T = 540;                  // ticks per pattern (~8.6 s)
static const uint16_t PAT_COL[5] = {TFT_MAGENTA, TFT_ORANGE, TFT_CYAN, TFT_YELLOW, TFT_GREENYELLOW};

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("dodge_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  frame = grazes = 0;
  pattern = 0;
  plX = 67;
  turX = 67;
  turVx = 0.6f;
  spiralA = 0;
  newHigh = false;
  for (int i = 0; i < MAX_BUL; i++) buls[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void fire(float x, float y, float ang, float spd) {
  for (int i = 0; i < MAX_BUL; i++) {
    if (buls[i].active) continue;
    buls[i] = {x, y, cosf(ang) * spd, sinf(ang) * spd, PAT_COL[pattern], true, false};
    return;
  }
}

static void tick() {
  frame++;
  float diff = min(1.0f, frame / 7200.0f);         // 0 .. 1 over ~2 minutes
  int pt = frame % PATTERN_T;
  if (pt == 0) pattern = (frame / PATTERN_T) % 5;
  bool calm = pt < 45;                             // a short breather between patterns

  if (bL.stable && !bR.stable) plX -= 2.4f;
  if (bR.stable && !bL.stable) plX += 2.4f;
  plX = constrain(plX, 5.0f, 130.0f);

  turX += turVx;
  if (turX < 30 || turX > 105) turVx = -turVx;

  float spd = 1.3f + diff * 0.8f;
  if (!calm) {
    switch (pattern) {
      case 0:                                      // double spiral
        if (frame % max(4, 7 - (int)(diff * 3)) == 0) {
          spiralA += 0.33f;
          fire(turX, TURRET_Y, spiralA, spd);
          fire(turX, TURRET_Y, spiralA + PI, spd);
        }
        break;
      case 1:                                      // aimed fan
        if (frame % max(28, 48 - (int)(diff * 20)) == 0) {
          float a = atan2f(PLAYER_Y - TURRET_Y, plX - turX);
          int n = 5 + (int)(diff * 2);
          for (int k = 0; k < n; k++) fire(turX, TURRET_Y, a + (k - (n - 1) / 2.0f) * 0.2f, spd + 0.6f);
        }
        break;
      case 2:                                      // rain from the top edge
        if (frame % max(3, 6 - (int)(diff * 3)) == 0)
          fire(4 + rnd(127), HEADER_H + 2, PI / 2 + (rnd(21) - 10) * 0.01f, spd + rnd(8) * 0.1f);
        break;
      case 3:                                      // expanding rings with a gap
        if (frame % max(40, 64 - (int)(diff * 24)) == 0) {
          int n = 18;
          float off = rnd(100) * 0.0628f;
          for (int k = 0; k < n; k++) fire(turX, TURRET_Y, off + k * 2 * PI / n, spd);
        }
        break;
      default:                                     // sweeping sprinkler
        if (frame % 4 == 0) {
          float a = PI / 2 + sinf(frame * 0.03f) * 1.1f;
          fire(turX, TURRET_Y, a, spd + 0.4f);
        }
        break;
    }
  }

  for (int i = 0; i < MAX_BUL; i++) {
    Bullet &b = buls[i];
    if (!b.active) continue;
    b.x += b.vx;
    b.y += b.vy;
    if (b.y > 244 || b.y < HEADER_H - 2 || b.x < -4 || b.x > 139) { b.active = false; continue; }
    float dx = b.x - plX, dy = b.y - PLAYER_Y, d2 = dx * dx + dy * dy;
    if (d2 < HIT_R * HIT_R) { gameOver(); return; }
    if (!b.grazed && d2 < GRAZE_R * GRAZE_R) { b.grazed = true; grazes++; }
  }
  score = frame / 6 + grazes * 3;                  // ~10 points a second
}

// ---------------- rendering ----------------
static void drawShip(int x, int y, uint16_t col) {
  spr.fillTriangle(x, y - 7, x - 6, y + 5, x + 6, y + 5, col);
  spr.fillCircle(x, y, 2, TFT_WHITE);              // the hit box is this core
}

static void drawGame() {
  spr.fillSprite(0x0821);
  // slow star field
  for (int i = 0; i < 16; i++) {
    int sx = (i * 47 + 11) % 135, sy = HEADER_H + ((i * 83 + frame / 2) % (240 - HEADER_H));
    spr.drawPixel(sx, sy, TFT_DARKGREY);
  }

  int tx = (int)turX;
  spr.fillRoundRect(tx - 10, TURRET_Y - 8, 20, 12, 3, 0x7BEF);
  spr.fillCircle(tx, TURRET_Y, 4, PAT_COL[pattern]);

  for (int i = 0; i < MAX_BUL; i++) {
    const Bullet &b = buls[i];
    if (!b.active) continue;
    spr.fillCircle((int)b.x, (int)b.y, 2, b.col);
    spr.drawPixel((int)b.x, (int)b.y, TFT_WHITE);
  }

  drawShip((int)plX, PLAYER_Y, TFT_GREEN);

  drawScoreHeader(score, hiScore);
  char buf[16];
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_CYAN);
  snprintf(buf, sizeof buf, "Graze %d", grazes);
  spr.drawString(buf, 132, 24, 1);

  if (state == GAMEOVER) drawGameOverBox("HIT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_MAGENTA);
  spr.drawString("DODGE", 67, 18, 4);

  for (int k = 0; k < 8; k++) {
    float a = k * PI / 4;
    spr.fillCircle(67 + (int)(cosf(a) * 16), 62 + (int)(sinf(a) * 12), 2, PAT_COL[k % 5]);
  }
  drawShip(67, 84, TFT_GREEN);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Hold L/R", 8, 106, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("move", 68, 106, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Only the white core", 67, 140, 1);
  spr.drawString("can be hit. Skim", 67, 152, 1);
  spr.drawString("bullets for a bonus!", 67, 164, 1);

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
  float a0 = t * 0.003f;
  for (int k = 0; k < 6; k++) {
    float a = a0 + k * PI / 3;
    float r = 6 + (t / 30) % 8;
    spr.fillCircle(x + (int)(cosf(a) * r), y - 5 + (int)(sinf(a) * r), 1, c(PAT_COL[k % 5]));
  }
  spr.fillTriangle(x, y + 6, x - 5, y + 14, x + 5, y + 14, c(TFT_GREEN));
}

void begin() {
  hiScore = prefs.getUInt("dodge_hi", 0);
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

}  // namespace dodge

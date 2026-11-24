// Knife (Spinning Log)
//
// Controls (two buttons):
//   Tap L or R : throw a knife
// Stick every knife into the spinning log without hitting another knife.
// Apples on the rim are worth +5. The log changes its spin each stage, and every
// 5th stage is a fast boss log. One miss ends the game.

#include "common.h"

namespace knife {

static const int LOG_X = 67, LOG_Y = 104, LOG_R = 25;
static const int KNIFE_LEN = 24, THROW_Y = 214;
static const int MAX_STUCK = 16, MAX_APPLES = 2;
static const float HIT_ANG = 0.19f;               // knives closer than this collide
static const uint32_t TICK_MS = 16;

enum State { TITLE, PLAY, GAMEOVER };
enum Phase { AIM, FLYING, BOUNCE, CLEAR };
static State state = TITLE;
static Phase phase = AIM;

static float stuck[MAX_STUCK];                     // angles relative to the log
static int nStuck;
static float apple[MAX_APPLES];
static bool appleOn[MAX_APPLES];
static float rot, omega, baseOmega, knifeY;
static float bx, by, bvx, bvy, bspin, bang;        // a knife bouncing off
static int stage, knivesLeft, spinMode, phaseT, levelT;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static float wrapAng(float a) {
  while (a < 0) a += 2 * PI;
  while (a >= 2 * PI) a -= 2 * PI;
  return a;
}
static float angDiff(float a, float b) {
  float d = fabsf(wrapAng(a) - wrapAng(b));
  return d > PI ? 2 * PI - d : d;
}

static bool angleFree(float a, float gap) {
  for (int i = 0; i < nStuck; i++) if (angDiff(a, stuck[i]) < gap) return false;
  return true;
}

static void startStage() {
  bool boss = stage % 5 == 0;
  nStuck = 0;
  // a few knives are already in the log from the second stage on
  int pre = stage < 2 ? 0 : rnd(min(3, stage / 2) + 1);
  for (int tries = 0; nStuck < pre && tries < 40; tries++) {
    float a = rnd(628) / 100.0f;
    if (angleFree(a, 0.6f)) stuck[nStuck++] = a;
  }
  for (int i = 0; i < MAX_APPLES; i++) {
    appleOn[i] = false;
    if (rnd(100) < (i == 0 ? 55 : 20)) {
      float a = rnd(628) / 100.0f;
      if (angleFree(a, 0.5f)) { apple[i] = a; appleOn[i] = true; }
    }
  }
  knivesLeft = 6 + min(4, stage / 2) + (boss ? 2 : 0);
  spinMode = boss ? 4 : stage % 4;
  baseOmega = min(0.075f, 0.034f + stage * 0.003f) * (rnd(2) ? 1 : -1);
  if (boss) baseOmega *= 1.5f;
  levelT = 0;
  phase = AIM;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("knife_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  stage = 1;
  rot = 0;
  newHigh = false;
  startStage();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick(bool throwNow) {
  levelT++;
  // spin patterns: steady, swinging back and forth, steady with jerks, surging, boss
  switch (spinMode) {
    case 0: omega = baseOmega; break;
    case 1: omega = baseOmega * 1.5f * sinf(levelT * 0.018f); break;
    case 2: omega = baseOmega * ((levelT / 90) % 3 == 2 ? -1.6f : 1.0f); break;
    case 3: omega = baseOmega * (0.3f + 1.2f * fabsf(sinf(levelT * 0.014f))); break;
    default: omega = baseOmega * (1.0f + 0.6f * sinf(levelT * 0.03f)) * ((levelT / 120) & 1 ? -1 : 1); break;
  }
  if (phase != CLEAR) rot = wrapAng(rot + omega);

  if (phase == AIM) {
    if (throwNow) { phase = FLYING; knifeY = THROW_Y; }
    return;
  }

  if (phase == FLYING) {
    knifeY -= 11;
    if (knifeY <= LOG_Y + LOG_R) {
      // the knife meets the rim at the bottom of the log (world angle PI/2)
      float rel = wrapAng(PI / 2 - rot);
      if (!angleFree(rel, HIT_ANG)) {
        phase = BOUNCE;
        phaseT = 0;
        bx = LOG_X;
        by = LOG_Y + LOG_R + 8;
        bvx = (rnd(2) ? 1 : -1) * (1.0f + rnd(10) * 0.1f);
        bvy = 1.5f;
        bspin = bvx * 0.12f;
        bang = -PI / 2;
        return;
      }
      if (nStuck < MAX_STUCK) stuck[nStuck++] = rel;
      score += 1;
      for (int i = 0; i < MAX_APPLES; i++) {
        if (appleOn[i] && angDiff(rel, apple[i]) < 0.28f) { appleOn[i] = false; score += 5; }
      }
      if (--knivesLeft <= 0) {
        score += 5 * stage;
        phase = CLEAR;
        phaseT = 0;
      } else {
        phase = AIM;
      }
    }
    return;
  }

  if (phase == BOUNCE) {
    bvy += 0.25f;
    bx += bvx;
    by += bvy;
    bang += bspin;
    if (++phaseT > 50) gameOver();
    return;
  }

  // CLEAR: the log bursts, then the next stage
  if (++phaseT > 55) {
    stage++;
    startStage();
  }
}

// ---------------- rendering ----------------
static void thickLine(float x0, float y0, float x1, float y1, int w, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.1f) return;
  float nx = -dy / len, ny = dx / len;
  for (int k = 0; k < w; k++) {
    float o = k - (w - 1) / 2.0f;
    spr.drawLine((int)roundf(x0 + nx * o), (int)roundf(y0 + ny * o),
                 (int)roundf(x1 + nx * o), (int)roundf(y1 + ny * o), col);
  }
}

// a knife whose tip is at (x, y), pointing along angle a (towards the tip)
static void drawKnife(float x, float y, float a) {
  float ux = -cosf(a), uy = -sinf(a);              // from the tip towards the handle
  thickLine(x, y, x + ux * 13, y + uy * 13, 3, TFT_LIGHTGREY);
  thickLine(x + ux * 13, y + uy * 13, x + ux * 15, y + uy * 15, 5, TFT_DARKGREY);
  thickLine(x + ux * 15, y + uy * 15, x + ux * KNIFE_LEN, y + uy * KNIFE_LEN, 3, 0x8A22);
}

static void drawLog(uint32_t t) {
  if (phase == CLEAR) {
    // four chunks flying apart
    float d = phaseT * 2.2f;
    for (int k = 0; k < 4; k++) {
      float a = k * PI / 2 + PI / 4;
      int cx = LOG_X + (int)(cosf(a) * d), cy = LOG_Y + (int)(sinf(a) * d + phaseT * phaseT * 0.04f);
      spr.fillCircle(cx, cy, 10, 0x9A60);
      spr.fillCircle(cx, cy, 5, 0xC4A6);
    }
    return;
  }
  spr.fillCircle(LOG_X, LOG_Y, LOG_R, 0x9A60);
  spr.drawCircle(LOG_X, LOG_Y, LOG_R - 7, 0x7140);
  spr.drawCircle(LOG_X, LOG_Y, LOG_R - 15, 0x7140);
  spr.fillCircle(LOG_X, LOG_Y, 4, 0xC4A6);
  for (int k = 0; k < 3; k++) {                    // grain marks show the spin
    float a = rot + k * 2 * PI / 3;
    spr.drawLine(LOG_X + (int)(cosf(a) * 6), LOG_Y + (int)(sinf(a) * 6),
                 LOG_X + (int)(cosf(a) * (LOG_R - 3)), LOG_Y + (int)(sinf(a) * (LOG_R - 3)), 0x7140);
  }
  for (int i = 0; i < MAX_APPLES; i++) {
    if (!appleOn[i]) continue;
    float a = apple[i] + rot;
    int ax = LOG_X + (int)roundf(cosf(a) * (LOG_R + 4)), ay = LOG_Y + (int)roundf(sinf(a) * (LOG_R + 4));
    spr.fillCircle(ax, ay, 4, TFT_RED);
    spr.drawPixel(ax - 1, ay - 2, TFT_WHITE);
  }
  for (int i = 0; i < nStuck; i++) {
    float a = stuck[i] + rot;
    // tip sits a little inside the rim, the knife points at the centre
    drawKnife(LOG_X + cosf(a) * (LOG_R - 5), LOG_Y + sinf(a) * (LOG_R - 5), a + PI);
  }
}

static void drawGame(uint32_t now) {
  char buf[20];
  spr.fillSprite(0x18C3);
  drawLog(now);

  if (state == PLAY && phase == AIM) drawKnife(LOG_X, THROW_Y, -PI / 2);
  if (phase == FLYING) drawKnife(LOG_X, knifeY, -PI / 2);
  if (phase == BOUNCE) drawKnife(bx, by, bang);

  // knives still to throw
  for (int i = 0; i < knivesLeft - (phase == AIM ? 1 : 0); i++)
    thickLine(8, 228 - i * 7, 20, 228 - i * 7, 3, TFT_LIGHTGREY);

  if (phase == CLEAR) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_YELLOW);
    spr.drawString("STAGE CLEAR", 67, 150, 2);
  }

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(stage % 5 == 0 ? TFT_RED : TFT_YELLOW);
  if (stage % 5 == 0) snprintf(buf, sizeof buf, "BOSS %d", stage);
  else snprintf(buf, sizeof buf, "Stage %d", stage);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("CLANG!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("KNIFE", 67, 18, 4);

  spr.fillCircle(67, 66, 14, 0x9A60);
  spr.drawCircle(67, 66, 8, 0x7140);
  drawKnife(67, 50, PI / 2);
  drawKnife(80, 72, PI + 0.4f);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Tap L/R", 8, 106, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("throw", 68, 106, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Don't hit the knives!", 67, 140, 1);
  spr.drawString("Apples on the rim: +5", 67, 152, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void render(uint32_t now) {
  if (state == TITLE) drawTitle();
  else drawGame(now);
  spr.pushSprite(0, 0);
}

void drawIcon(int x, int y, bool dim, uint32_t t) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  float r = t * 0.004f;
  spr.fillCircle(x, y - 3, 9, c(0x9A60));
  spr.drawCircle(x, y - 3, 5, c(0x7140));
  for (int k = 0; k < 2; k++) {
    float a = r + k * PI;
    spr.drawLine(x + (int)(cosf(a) * 8), y - 3 + (int)(sinf(a) * 8),
                 x + (int)(cosf(a) * 15), y - 3 + (int)(sinf(a) * 15), c(TFT_LIGHTGREY));
  }
  spr.drawFastVLine(x, y + 9, 6, c(TFT_LIGHTGREY));
  spr.drawFastVLine(x + 1, y + 9, 6, c(TFT_LIGHTGREY));
}

void begin() {
  hiScore = prefs.getUInt("knife_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

static EvQ qL, qR;                 // button events waiting for the next tick

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    qL.push(eL);
    qR.push(eR);
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      int a = qL.pop(), b = qR.pop();
      tick(a == 1 || b == 1);
      dirty = true;
    }
  } else {
    qL.clear();
    qR.clear();
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render(now);
    dirty = false;
  }
}

}  // namespace knife

// Missile (City Defense)
//
// Controls (two buttons):
//   Hold L / R : sweep defense turret left / right
// Turret fires explosive flak shells automatically. Protect your 3 cities!

#include "common.h"

namespace missile {

static const int MAX_EN = 12, MAX_SH = 5, MAX_EXP = 8;
static const int GROUND_Y = 230;
static const int CITY_X[3] = {24, 67, 110};
static const uint32_t TICK_MS = 16;

struct En { float x, y, vx, vy; bool active; };
struct Shell { float x, y, vx, vy, tx, ty; bool active; };
struct Exp { float x, y, r, maxR; int8_t dir; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static bool cityAlive[3];
static float aimAng;
static En ens[MAX_EN];
static Shell shs[MAX_SH];
static Exp exps[MAX_EXP];
static int spawnT, fireT;
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
    prefs.putUInt("missile_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  aimAng = -PI / 2;
  spawnT = 30;
  fireT = 0;
  newHigh = false;
  for (int i = 0; i < 3; i++) cityAlive[i] = true;
  for (int i = 0; i < MAX_EN; i++) ens[i].active = false;
  for (int i = 0; i < MAX_SH; i++) shs[i].active = false;
  for (int i = 0; i < MAX_EXP; i++) exps[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawnExp(float x, float y, float maxR) {
  for (int i = 0; i < MAX_EXP; i++) {
    if (!exps[i].active) {
      exps[i] = {x, y, 2.0f, maxR, 1, true};
      return;
    }
  }
}

static void tick() {
  if (bL.stable && !bR.stable) aimAng = max(-(float)PI + 0.25f, aimAng - 0.045f);
  if (bR.stable && !bL.stable) aimAng = min(-0.25f, aimAng + 0.045f);

  // Auto-fire flak shells toward aim direction
  if (++fireT >= 22) {
    fireT = 0;
    for (int i = 0; i < MAX_SH; i++) {
      if (shs[i].active) continue;
      float dist = 115.0f;
      float tx = constrain(67.0f + cosf(aimAng) * dist, 10.0f, 125.0f);
      float ty = constrain((float)GROUND_Y + sinf(aimAng) * dist, (float)HEADER_H + 16, 175.0f);
      float dx = tx - 67.0f, dy = ty - GROUND_Y;
      float d = sqrtf(dx * dx + dy * dy);
      shs[i] = {67.0f, (float)GROUND_Y - 6, dx / d * 4.2f, dy / d * 4.2f, tx, ty, true};
      break;
    }
  }

  // Spawn incoming enemy missiles
  if (--spawnT <= 0) {
    spawnT = max(22, 65 - (int)(score / 30));
    for (int i = 0; i < MAX_EN; i++) {
      if (ens[i].active) continue;
      float sx = 8 + rnd(119);
      int targetIdx = rnd(3);
      float tx = CITY_X[targetIdx];
      float dx = tx - sx, dy = GROUND_Y - HEADER_H;
      float d = sqrtf(dx * dx + dy * dy);
      float spd = min(1.5f, 0.55f + score * 0.0008f);
      ens[i] = {sx, (float)HEADER_H, dx / d * spd, dy / d * spd, true};
      break;
    }
  }

  for (int i = 0; i < MAX_SH; i++) {
    if (!shs[i].active) continue;
    shs[i].x += shs[i].vx;
    shs[i].y += shs[i].vy;
    if (shs[i].y <= shs[i].ty) {
      shs[i].active = false;
      spawnExp(shs[i].tx, shs[i].ty, 16.0f);
    }
  }

  for (int i = 0; i < MAX_EXP; i++) {
    if (!exps[i].active) continue;
    exps[i].r += exps[i].dir * 0.9f;
    if (exps[i].r >= exps[i].maxR) exps[i].dir = -1;
    if (exps[i].r <= 0) exps[i].active = false;
  }

  for (int i = 0; i < MAX_EN; i++) {
    if (!ens[i].active) continue;
    ens[i].x += ens[i].vx;
    ens[i].y += ens[i].vy;

    for (int e = 0; e < MAX_EXP; e++) {
      if (!exps[e].active) continue;
      float dx = ens[i].x - exps[e].x, dy = ens[i].y - exps[e].y;
      if (dx * dx + dy * dy <= exps[e].r * exps[e].r) {
        ens[i].active = false;
        score += 25;
        spawnExp(ens[i].x, ens[i].y, 10.0f);
        break;
      }
    }
    if (!ens[i].active) continue;

    if (ens[i].y >= GROUND_Y) {
      ens[i].active = false;
      spawnExp(ens[i].x, GROUND_Y - 2, 12.0f);
      for (int c = 0; c < 3; c++) {
        if (cityAlive[c] && fabsf(ens[i].x - CITY_X[c]) < 15) cityAlive[c] = false;
      }
      if (!cityAlive[0] && !cityAlive[1] && !cityAlive[2]) { gameOver(); return; }
    }
  }
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);
  spr.fillRect(0, GROUND_Y, 135, 10, 0x7AE3);

  // Cities
  for (int i = 0; i < 3; i++) {
    if (!cityAlive[i]) continue;
    int cx = CITY_X[i];
    spr.fillRect(cx - 8, GROUND_Y - 7, 5, 7, TFT_CYAN);
    spr.fillRect(cx - 2, GROUND_Y - 11, 5, 11, TFT_BLUE);
    spr.fillRect(cx + 4, GROUND_Y - 6, 5, 6, TFT_CYAN);
  }

  // Turret & aim line
  spr.fillTriangle(61, GROUND_Y, 73, GROUND_Y, 67, GROUND_Y - 8, TFT_YELLOW);
  int ax = 67 + (int)(cosf(aimAng) * 28), ay = GROUND_Y + (int)(sinf(aimAng) * 28);
  spr.drawLine(67, GROUND_Y - 4, ax, ay, 0x39E7);

  for (int i = 0; i < MAX_EN; i++) {
    if (!ens[i].active) continue;
    spr.drawLine((int)ens[i].x, (int)ens[i].y, (int)(ens[i].x - ens[i].vx * 10), (int)(ens[i].y - ens[i].vy * 10), TFT_RED);
    spr.fillCircle((int)ens[i].x, (int)ens[i].y, 2, TFT_WHITE);
  }

  for (int i = 0; i < MAX_SH; i++) {
    if (!shs[i].active) continue;
    spr.fillCircle((int)shs[i].x, (int)shs[i].y, 2, TFT_GREEN);
  }

  for (int i = 0; i < MAX_EXP; i++) {
    if (!exps[i].active) continue;
    spr.fillCircle((int)exps[i].x, (int)exps[i].y, (int)exps[i].r, TFT_ORANGE);
    spr.drawCircle((int)exps[i].x, (int)exps[i].y, (int)exps[i].r, TFT_YELLOW);
  }

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("CITIES LOST", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_RED);
  spr.drawString("MISSILE", 67, 18, 4);

  spr.fillCircle(67, 62, 10, TFT_ORANGE);
  spr.drawCircle(67, 62, 12, TFT_YELLOW);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "Auto"};
  const char *acts[] = {"aim turret", "fires flak"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 70, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Defend the 3 cities!", 67, 148, 1);

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
  spr.drawLine(x - 8, y - 10, x + 2, y + 4, c(TFT_RED));
  int r = 3 + ((t / 120) % 6);
  spr.drawCircle(x + 2, y + 4, r, c(TFT_ORANGE));
  spr.fillRect(x - 9, y + 8, 18, 3, c(TFT_CYAN));
}

void begin() {
  hiScore = prefs.getUInt("missile_hi", 0);
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

}  // namespace missile

// Asteroids
//
// Controls (two buttons):
//   Hold L / R : rotate left / right
//   L + R      : thrust forward
// Laser fires automatically. Blast asteroids into smaller pieces; 3 lives.

#include "common.h"

namespace asteroids {

static const int MAX_AST = 16;
static const int MAX_BUL = 6;
static const uint32_t TICK_MS = 16;

struct Ast { float x, y, vx, vy; int8_t size; bool active; };  // size: 3=big, 2=med, 1=small
struct Bul { float x, y, vx, vy; uint8_t life; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float sx, sy, svx, svy, sAng;
static int lives, wave, invulnT, fireT;
static Ast asts[MAX_AST];
static Bul buls[MAX_BUL];
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static TFT_eSprite shipSpr(&tft);

static int rnd(int n) { return esp_random() % n; }

static void wrapPos(float &x, float &y) {
  if (x < 0) x += 135;
  if (x >= 135) x -= 135;
  if (y < HEADER_H) y += (240 - HEADER_H);
  if (y >= 240) y -= (240 - HEADER_H);
}

static void spawnAst(float x, float y, int8_t sz) {
  for (int i = 0; i < MAX_AST; i++) {
    if (asts[i].active) continue;
    float a = rnd(360) * DEG_TO_RAD;
    float spd = (0.5f + rnd(6) * 0.1f) * (4 - sz) * 0.55f;
    asts[i] = {x, y, cosf(a) * spd, sinf(a) * spd, sz, true};
    return;
  }
}

static void startWave() {
  for (int i = 0; i < MAX_AST; i++) asts[i].active = false;
  int cnt = min(6, 2 + wave);
  for (int i = 0; i < cnt; i++) {
    float x = rnd(135), y = HEADER_H + rnd(240 - HEADER_H);
    if (fabsf(x - sx) < 28 && fabsf(y - sy) < 28) x = fmodf(x + 67, 135.0f);
    spawnAst(x, y, 3);
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("aster_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  wave = 1;
  sx = 67;
  sy = 140;
  svx = svy = 0;
  sAng = -PI / 2;
  invulnT = 90;
  fireT = 0;
  newHigh = false;
  for (int i = 0; i < MAX_BUL; i++) buls[i].active = false;
  startWave();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (invulnT > 0) invulnT--;

  if (bL.stable && !bR.stable) sAng -= 0.09f;
  if (bR.stable && !bL.stable) sAng += 0.09f;
  if (bL.stable && bR.stable) {
    svx += cosf(sAng) * 0.11f;
    svy += sinf(sAng) * 0.11f;
  }
  svx *= 0.985f;
  svy *= 0.985f;
  float spd = sqrtf(svx * svx + svy * svy);
  if (spd > 3.5f) { svx *= 3.5f / spd; svy *= 3.5f / spd; }
  sx += svx;
  sy += svy;
  wrapPos(sx, sy);

  // Auto-fire
  if (++fireT >= 14) {
    fireT = 0;
    for (int i = 0; i < MAX_BUL; i++) {
      if (!buls[i].active) {
        buls[i] = {sx + cosf(sAng) * 7, sy + sinf(sAng) * 7, cosf(sAng) * 4.6f, sinf(sAng) * 4.6f, 40, true};
        break;
      }
    }
  }

  for (int i = 0; i < MAX_BUL; i++) {
    if (!buls[i].active) continue;
    buls[i].x += buls[i].vx;
    buls[i].y += buls[i].vy;
    wrapPos(buls[i].x, buls[i].y);
    if (--buls[i].life == 0) buls[i].active = false;
  }

  int activeAst = 0;
  for (int i = 0; i < MAX_AST; i++) {
    if (!asts[i].active) continue;
    activeAst++;
    asts[i].x += asts[i].vx;
    asts[i].y += asts[i].vy;
    wrapPos(asts[i].x, asts[i].y);

    float rad = asts[i].size * 4.0f + 1.0f;
    for (int b = 0; b < MAX_BUL; b++) {
      if (!buls[b].active) continue;
      float dx = buls[b].x - asts[i].x, dy = buls[b].y - asts[i].y;
      if (dx * dx + dy * dy <= rad * rad) {
        buls[b].active = false;
        asts[i].active = false;
        score += (4 - asts[i].size) * 20;
        if (asts[i].size > 1) {
          spawnAst(asts[i].x, asts[i].y, asts[i].size - 1);
          spawnAst(asts[i].x, asts[i].y, asts[i].size - 1);
        }
        break;
      }
    }

    if (asts[i].active && invulnT == 0) {
      float dx = sx - asts[i].x, dy = sy - asts[i].y;
      if (dx * dx + dy * dy <= (rad + 4) * (rad + 4)) {
        if (--lives <= 0) { gameOver(); return; }
        sx = 67; sy = 140; svx = svy = 0;
        invulnT = 100;
      }
    }
  }

  if (activeAst == 0) {
    wave++;
    startWave();
  }
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);
  spr.drawFastHLine(0, HEADER_H, 135, TFT_DARKGREY);

  for (int i = 0; i < MAX_AST; i++) {
    if (!asts[i].active) continue;
    int r = asts[i].size * 4;
    spr.drawCircle((int)asts[i].x, (int)asts[i].y, r, TFT_LIGHTGREY);
    spr.drawCircle((int)asts[i].x, (int)asts[i].y, max(1, r - 1), 0x7BEF);
  }

  for (int i = 0; i < MAX_BUL; i++) {
    if (buls[i].active) spr.fillCircle((int)buls[i].x, (int)buls[i].y, 1, TFT_YELLOW);
  }

  if ((invulnT & 4) == 0 && state == PLAY) {
    if (shipSpr.width() == 0) {
      shipSpr.setColorDepth(spr.getColorDepth());
      shipSpr.createSprite(18, 18);
    }
    shipSpr.fillSprite(TFT_BLACK);
    shipSpr.fillTriangle(15, 9, 3, 4, 3, 14, TFT_WHITE);
    shipSpr.fillTriangle(9, 9, 3, 6, 3, 12, 0x18E3);
    if (bL.stable && bR.stable) shipSpr.fillTriangle(3, 7, 3, 11, 0, 9, TFT_ORANGE);
    spr.setPivot((int)sx, (int)sy);
    shipSpr.pushRotated(&spr, (int)roundf(sAng * RAD_TO_DEG), TFT_BLACK);
  }

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) {
    int lx = 128 - i * 10;
    spr.fillTriangle(lx, 5, lx - 3, 12, lx + 3, 12, TFT_WHITE);
  }
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("ASTEROIDS", 67, 18, 4);

  spr.drawCircle(67, 62, 10, TFT_LIGHTGREY);
  spr.fillTriangle(67, 56, 62, 68, 72, 68, TFT_WHITE);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"rotate", "thrust"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Laser fires automatically", 67, 148, 1);

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
  spr.drawCircle(x - 5, y - 6, 5, c(TFT_LIGHTGREY));
  spr.drawCircle(x + 7, y + 6, 4, c(TFT_LIGHTGREY));
  float a = t * 0.003f;
  int x0 = x + (int)roundf(cosf(a) * 5), y0 = y + (int)roundf(sinf(a) * 5);
  int x1 = x + (int)roundf(cosf(a + 2.4f) * 4), y1 = y + (int)roundf(sinf(a + 2.4f) * 4);
  int x2 = x + (int)roundf(cosf(a - 2.4f) * 4), y2 = y + (int)roundf(sinf(a - 2.4f) * 4);
  spr.fillTriangle(x0, y0, x1, y1, x2, y2, c(TFT_WHITE));
}

void begin() {
  hiScore = prefs.getUInt("aster_hi", 0);
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

}  // namespace asteroids

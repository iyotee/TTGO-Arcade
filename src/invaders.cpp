// Invaders (Space Invaders)
//
// Controls (two buttons):
//   Hold L : move left
//   Hold R : move right
// Cannon fires automatically. Clear waves of marching aliens behind
// 3 destructible shields. 3 lives.

#include "common.h"

namespace invaders {

static const int COLS = 6, ROWS = 4;
static const int ALIEN_W = 12, ALIEN_H = 8, PITCH_X = 18, PITCH_Y = 14;
static const int SHIELD_Y = 198, SHIELD_W = 22, SHIELD_H = 10;
static const int SHIP_Y = 224;
static const int MAX_EBUL = 6;
static const uint32_t TICK_MS = 16;

struct Bullet { float x, y; bool active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static bool alive[ROWS][COLS];
static uint8_t shield[3][SHIELD_H][SHIELD_W];
static float formX, formY, formDir;
static float shipX;
static Bullet pBul;
static Bullet eBul[MAX_EBUL];
static int lives, wave, aliveCnt, invulnT;
static uint32_t moveTimer, fireTimer, enemyFireTimer;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static const uint16_t ROW_COL[ROWS] = {TFT_MAGENTA, TFT_CYAN, TFT_GREEN, TFT_YELLOW};

static int rnd(int n) { return esp_random() % n; }

static void initShields() {
  for (int s = 0; s < 3; s++)
    for (int r = 0; r < SHIELD_H; r++)
      for (int c = 0; c < SHIELD_W; c++) {
        bool arch = (r >= 6 && c >= 6 && c < SHIELD_W - 6);
        bool corner = (r == 0 && (c < 2 || c >= SHIELD_W - 2));
        shield[s][r][c] = (!arch && !corner) ? 1 : 0;
      }
}

static void initWave() {
  formX = 14;
  formY = 52 + min(24, (wave - 1) * 4);
  formDir = 1.0f;
  aliveCnt = ROWS * COLS;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) alive[r][c] = true;
  pBul.active = false;
  for (int i = 0; i < MAX_EBUL; i++) eBul[i].active = false;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("invaders_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lives = 3;
  wave = 1;
  shipX = 67;
  invulnT = 0;
  newHigh = false;
  initShields();
  initWave();
  moveTimer = fireTimer = enemyFireTimer = 0;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool hitShield(float bx, float by) {
  if (by < SHIELD_Y || by >= SHIELD_Y + SHIELD_H) return false;
  const int sx[3] = {15, 56, 97};
  for (int s = 0; s < 3; s++) {
    int lx = (int)bx - sx[s], ly = (int)by - SHIELD_Y;
    if (lx >= 0 && lx < SHIELD_W && ly >= 0 && ly < SHIELD_H && shield[s][ly][lx]) {
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
          int nr = ly + dr, nc = lx + dc;
          if (nr >= 0 && nr < SHIELD_H && nc >= 0 && nc < SHIELD_W) shield[s][nr][nc] = 0;
        }
      return true;
    }
  }
  return false;
}

static void tick() {
  if (bL.stable && !bR.stable) shipX -= 2.6f;
  if (bR.stable && !bL.stable) shipX += 2.6f;
  shipX = constrain(shipX, 8.0f, 127.0f);

  if (invulnT > 0) invulnT--;

  // Auto-fire player bullet
  if (!pBul.active && ++fireTimer >= 14) {
    fireTimer = 0;
    pBul = {shipX, (float)SHIP_Y - 6, true};
  }

  if (pBul.active) {
    pBul.y -= 5.0f;
    if (hitShield(pBul.x, pBul.y) || pBul.y < HEADER_H) pBul.active = false;
    else {
      for (int r = 0; r < ROWS && pBul.active; r++)
        for (int c = 0; c < COLS && pBul.active; c++) {
          if (!alive[r][c]) continue;
          float ax = formX + c * PITCH_X, ay = formY + r * PITCH_Y;
          if (pBul.x >= ax && pBul.x <= ax + ALIEN_W && pBul.y >= ay && pBul.y <= ay + ALIEN_H) {
            alive[r][c] = false;
            pBul.active = false;
            aliveCnt--;
            score += (ROWS - r) * 10;
            if (aliveCnt == 0) {
              wave++;
              initWave();
              return;
            }
          }
        }
    }
  }

  // March formation (speeds up as aliens are cleared)
  uint32_t stepDelay = max(4, 20 - (ROWS * COLS - aliveCnt) / 2 - wave * 2);
  if (++moveTimer >= stepDelay) {
    moveTimer = 0;
    float minX = 135, maxX = 0, maxY = 0;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++) {
        if (!alive[r][c]) continue;
        float ax = formX + c * PITCH_X;
        minX = min(minX, ax);
        maxX = max(maxX, ax + ALIEN_W);
        maxY = max(maxY, formY + r * PITCH_Y + ALIEN_H);
      }
    if ((formDir > 0 && maxX + 4 > 132) || (formDir < 0 && minX - 4 < 3)) {
      formDir = -formDir;
      formY += 6;
      if (maxY + 6 >= SHIELD_Y) { gameOver(); return; }
    } else {
      formX += formDir * 4;
    }
  }

  // Enemy fire
  uint32_t eRate = max(18, 50 - wave * 5);
  if (++enemyFireTimer >= eRate) {
    enemyFireTimer = 0;
    for (int i = 0; i < MAX_EBUL; i++) {
      if (eBul[i].active) continue;
      int col = rnd(COLS);
      for (int r = ROWS - 1; r >= 0; r--) {
        if (alive[r][col]) {
          eBul[i] = {formX + col * PITCH_X + ALIEN_W / 2, formY + r * PITCH_Y + ALIEN_H, true};
          break;
        }
      }
      break;
    }
  }

  for (int i = 0; i < MAX_EBUL; i++) {
    if (!eBul[i].active) continue;
    eBul[i].y += 2.6f + min(1.2f, wave * 0.15f);
    if (eBul[i].y > 240 || hitShield(eBul[i].x, eBul[i].y)) {
      eBul[i].active = false;
    } else if (invulnT == 0 && fabsf(eBul[i].x - shipX) < 7 && fabsf(eBul[i].y - SHIP_Y) < 6) {
      eBul[i].active = false;
      if (--lives <= 0) { gameOver(); return; }
      invulnT = 60;
    }
  }
}

static void drawGame() {
  spr.fillSprite(TFT_BLACK);
  spr.drawFastHLine(0, HEADER_H, 135, TFT_DARKGREY);

  // Shields
  const int sx[3] = {15, 56, 97};
  for (int s = 0; s < 3; s++)
    for (int r = 0; r < SHIELD_H; r++)
      for (int c = 0; c < SHIELD_W; c++)
        if (shield[s][r][c]) spr.drawPixel(sx[s] + c, SHIELD_Y + r, TFT_GREEN);

  // Aliens
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      if (!alive[r][c]) continue;
      int ax = (int)(formX + c * PITCH_X), ay = (int)(formY + r * PITCH_Y);
      spr.fillRect(ax + 2, ay, ALIEN_W - 4, ALIEN_H - 2, ROW_COL[r]);
      spr.fillRect(ax, ay + 2, ALIEN_W, 3, ROW_COL[r]);
      spr.drawPixel(ax + 3, ay + 2, TFT_BLACK);
      spr.drawPixel(ax + ALIEN_W - 4, ay + 2, TFT_BLACK);
      spr.fillRect(ax + 1, ay + ALIEN_H - 2, 2, 2, ROW_COL[r]);
      spr.fillRect(ax + ALIEN_W - 3, ay + ALIEN_H - 2, 2, 2, ROW_COL[r]);
    }

  // Bullets
  if (pBul.active) spr.fillRect((int)pBul.x - 1, (int)pBul.y - 3, 2, 5, TFT_WHITE);
  for (int i = 0; i < MAX_EBUL; i++)
    if (eBul[i].active) spr.fillRect((int)eBul[i].x - 1, (int)eBul[i].y, 2, 5, TFT_RED);

  // Player ship
  if ((invulnT & 4) == 0) {
    int x = (int)shipX;
    spr.fillRect(x - 6, SHIP_Y, 13, 5, TFT_GREEN);
    spr.fillRect(x - 1, SHIP_Y - 4, 3, 4, TFT_GREEN);
  }

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillRect(124 - i * 10, 8, 6, 4, TFT_GREEN);
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("INVADERS", 67, 18, 4);

  spr.fillRect(59, 60, 16, 8, TFT_MAGENTA);
  spr.drawPixel(63, 63, TFT_BLACK);
  spr.drawPixel(70, 63, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"move left", "move right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Cannon fires automatically", 67, 148, 1);

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
  int ox = (int)roundf(6 * (triWave(t, 1400) - 0.5f));
  spr.fillRect(x + ox - 5, y - 8, 10, 6, c(TFT_MAGENTA));
  spr.drawPixel(x + ox - 3, y - 6, TFT_BLACK);
  spr.drawPixel(x + ox + 2, y - 6, TFT_BLACK);
  spr.fillRect(x - 6, y + 8, 12, 4, c(TFT_GREEN));
  spr.fillRect(x - 1, y + 5, 2, 3, c(TFT_GREEN));
}

void begin() {
  hiScore = prefs.getUInt("invaders_hi", 0);
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

}  // namespace invaders

// Dino Run
//
// Controls (two buttons):
//   Tap R  : jump
//   Hold L : duck / fast-fall
// Leap over cacti and duck under low-flying pterodactyls. Speeds up over time.

#include "common.h"

namespace dino {

static const int GROUND_Y = 198;
static const int DINO_X = 24;
static const int MAX_OBS = 4;
static const uint32_t TICK_MS = 16;

struct Obs { float x; int w, h, y; bool bird, active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float dinoY, vy, speed, dist;
static bool onGround, ducking;
static Obs obs[MAX_OBS];
static int spawnDist, frame;
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
    prefs.putUInt("dino_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  dist = 0;
  speed = 1.4f;
  dinoY = GROUND_Y;
  vy = 0;
  onGround = true;
  ducking = false;
  spawnDist = 90;
  frame = 0;
  newHigh = false;
  for (int i = 0; i < MAX_OBS; i++) obs[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawnObs() {
  for (int i = 0; i < MAX_OBS; i++) {
    if (obs[i].active) continue;
    bool bird = (score > 200) && (rnd(100) < 28);
    if (bird) {
      int hLevel = rnd(2) ? (GROUND_Y - 26) : (GROUND_Y - 14);
      obs[i] = {140.0f, 14, 9, hLevel, true, true};
    } else {
      int kind = rnd(3);
      int w = kind == 0 ? 7 : kind == 1 ? 11 : 8;
      int h = kind == 2 ? 20 : 15;
      obs[i] = {140.0f, w, h, GROUND_Y - h, false, true};
    }
    return;
  }
}

static void tick(int eR) {
  frame++;
  speed = min(3.8f, 1.4f + score * 0.0014f);
  dist += speed;
  score = (uint32_t)(dist / 6);

  ducking = bL.stable;
  if (eR == 1 && onGround && !ducking) {
    vy = -5.4f;
    onGround = false;
  }
  if (!onGround) {
    vy += ducking ? 0.55f : 0.25f;
    dinoY += vy;
    if (dinoY >= GROUND_Y) {
      dinoY = GROUND_Y;
      vy = 0;
      onGround = true;
    }
  }

  spawnDist -= (int)ceilf(speed);
  if (spawnDist <= 0) {
    spawnObs();
    spawnDist = 85 + rnd(65);
  }

  int dw = (onGround && ducking) ? 16 : 11;
  int dh = (onGround && ducking) ? 10 : 18;
  int dx0 = DINO_X + 2, dy0 = (int)dinoY - dh + 2;
  int dx1 = DINO_X + dw - 2, dy1 = (int)dinoY - 2;

  for (int i = 0; i < MAX_OBS; i++) {
    if (!obs[i].active) continue;
    obs[i].x -= speed + (obs[i].bird ? 0.35f : 0.0f);
    if (obs[i].x + obs[i].w < 0) { obs[i].active = false; continue; }
    if (dx1 > (int)obs[i].x + 1 && dx0 < (int)obs[i].x + obs[i].w - 1 &&
        dy1 > obs[i].y + 1 && dy0 < obs[i].y + obs[i].h - 1) {
      gameOver();
      return;
    }
  }
}

static void drawGame() {
  bool night = ((score / 500) & 1) != 0;
  uint16_t bg = night ? TFT_BLACK : 0xDEFB;
  uint16_t fg = night ? TFT_LIGHTGREY : 0x2945;
  uint16_t cactus = night ? TFT_GREEN : 0x1B42;

  spr.fillSprite(bg);
  spr.drawFastHLine(0, GROUND_Y, 135, fg);
  for (int x = -((int)dist % 18); x < 135; x += 18) {
    spr.drawPixel(x, GROUND_Y + 4, fg);
    spr.drawPixel(x + 7, GROUND_Y + 7, fg);
  }

  // Obstacles
  for (int i = 0; i < MAX_OBS; i++) {
    if (!obs[i].active) continue;
    int ox = (int)obs[i].x, oy = obs[i].y;
    if (obs[i].bird) {
      int flap = (frame / 8) & 1;
      spr.fillRect(ox, oy + 3, obs[i].w, 4, TFT_ORANGE);
      spr.fillTriangle(ox + 4, oy + 4, ox + 10, oy + 4, ox + 7, flap ? oy : oy + 9, TFT_RED);
    } else {
      spr.fillRoundRect(ox, oy, obs[i].w, obs[i].h, 2, cactus);
    }
  }

  // Dino
  int dy = (int)dinoY;
  if (onGround && ducking) {
    spr.fillRoundRect(DINO_X, dy - 10, 16, 8, 2, fg);
    spr.fillRect(DINO_X + 12, dy - 9, 2, 2, bg);
  } else {
    spr.fillRoundRect(DINO_X, dy - 16, 10, 12, 2, fg);
    spr.fillRoundRect(DINO_X + 4, dy - 20, 8, 7, 2, fg);
    spr.fillRect(DINO_X + 9, dy - 18, 2, 2, bg);
    int leg = ((frame / 5) & 1) ? 0 : 3;
    spr.fillRect(DINO_X + 2 + leg, dy - 4, 2, 4, fg);
    spr.fillRect(DINO_X + 6 - leg, dy - 4, 2, 4, fg);
  }

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("DINO", 67, 18, 4);

  spr.fillRoundRect(58, 56, 12, 16, 2, TFT_GREEN);
  spr.fillRect(66, 59, 2, 2, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap R", "Hold L"};
  const char *acts[] = {"jump", "duck / drop"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Duck under flying birds", 67, 148, 1);

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
  int j = (int)roundf(8 * triWave(t, 800));
  spr.drawFastHLine(x - 11, y + 10, 22, c(TFT_LIGHTGREY));
  spr.fillRoundRect(x - 6, y - 3 - j, 8, 10, 2, c(TFT_GREEN));
  spr.fillRect(x + 4, y + 3, 4, 7, c(TFT_DARKGREEN));
}

void begin() {
  hiScore = prefs.getUInt("dino_hi", 0);
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
    bool first = true;
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(first ? eR : 0);
      first = false;
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

}  // namespace dino

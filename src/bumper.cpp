// Bumper (Sumo Ring)
//
// Controls (two buttons):
//   Hold L / R : steer left / right
//   L + R      : turbo ram
// Knock all AI bots off the circular arena to clear waves!

#include "common.h"

namespace bumper {

static const float RING_X = 67.0f, RING_Y = 138.0f, RING_R = 60.0f;
static const int MAX_BOTS = 4;
static const uint32_t TICK_MS = 16;

struct Car { float x, y, vx, vy, ang, r; bool active; uint16_t col; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Car player;
static Car bots[MAX_BOTS];
static int wave, botLeft;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void startWave() {
  player = {RING_X, RING_Y + 25, 0, 0, -PI / 2, 7.0f, true, TFT_CYAN};
  int cnt = min(MAX_BOTS, 1 + wave);
  botLeft = cnt;
  const uint16_t cols[MAX_BOTS] = {TFT_RED, TFT_ORANGE, TFT_MAGENTA, TFT_YELLOW};
  for (int i = 0; i < MAX_BOTS; i++) {
    if (i < cnt) {
      float a = -PI / 2 + (i - (cnt - 1) * 0.5f) * 0.8f;
      bots[i] = {RING_X + cosf(a) * 32, RING_Y - 15 + sinf(a) * 18, 0, 0, a + PI, 7.0f, true, cols[i]};
    } else {
      bots[i].active = false;
    }
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("bumper_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  wave = 1;
  newHigh = false;
  startWave();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void bounceCars(Car &a, Car &b) {
  float dx = b.x - a.x, dy = b.y - a.y;
  float d = sqrtf(dx * dx + dy * dy);
  float minD = a.r + b.r;
  if (d >= minD || d < 1e-4f) return;

  float nx = dx / d, ny = dy / d;
  float overlap = 0.5f * (minD - d);
  a.x -= nx * overlap; a.y -= ny * overlap;
  b.x += nx * overlap; b.y += ny * overlap;

  float rvx = b.vx - a.vx, rvy = b.vy - a.vy;
  float vn = rvx * nx + rvy * ny;
  if (vn < 0) {
    float imp = -1.65f * vn;
    a.vx -= imp * nx * 0.5f;
    a.vy -= imp * ny * 0.5f;
    b.vx += imp * nx * 0.5f;
    b.vy += imp * ny * 0.5f;
  }
}

static void tick() {
  if (bL.stable && !bR.stable) player.ang -= 0.085f;
  if (bR.stable && !bL.stable) player.ang += 0.085f;
  float accel = (bL.stable && bR.stable) ? 0.16f : 0.08f;
  player.vx = (player.vx + cosf(player.ang) * accel) * 0.95f;
  player.vy = (player.vy + sinf(player.ang) * accel) * 0.95f;
  player.x += player.vx;
  player.y += player.vy;

  for (int i = 0; i < MAX_BOTS; i++) {
    if (!bots[i].active) continue;
    // AI aims toward player, or back toward center if near edge
    float dc = hypotf(bots[i].x - RING_X, bots[i].y - RING_Y);
    float tx = (dc > RING_R - 14) ? RING_X : player.x;
    float ty = (dc > RING_R - 14) ? RING_Y : player.y;
    float targetAng = atan2f(ty - bots[i].y, tx - bots[i].x);
    float da = targetAng - bots[i].ang;
    while (da > PI) da -= 2 * PI;
    while (da < -PI) da += 2 * PI;
    bots[i].ang += constrain(da, -0.07f, 0.07f);
    float ba = 0.065f + min(0.05f, wave * 0.008f);
    bots[i].vx = (bots[i].vx + cosf(bots[i].ang) * ba) * 0.95f;
    bots[i].vy = (bots[i].vy + sinf(bots[i].ang) * ba) * 0.95f;
    bots[i].x += bots[i].vx;
    bots[i].y += bots[i].vy;
  }

  for (int i = 0; i < MAX_BOTS; i++) {
    if (!bots[i].active) continue;
    bounceCars(player, bots[i]);
    for (int j = i + 1; j < MAX_BOTS; j++) {
      if (bots[j].active) bounceCars(bots[i], bots[j]);
    }
  }

  if (hypotf(player.x - RING_X, player.y - RING_Y) > RING_R) {
    gameOver();
    return;
  }

  for (int i = 0; i < MAX_BOTS; i++) {
    if (!bots[i].active) continue;
    if (hypotf(bots[i].x - RING_X, bots[i].y - RING_Y) > RING_R) {
      bots[i].active = false;
      score += 100 * wave;
      if (--botLeft <= 0) {
        wave++;
        startWave();
        return;
      }
    }
  }
}

static void drawCar(const Car &c) {
  spr.fillCircle((int)c.x, (int)c.y, (int)c.r, c.col);
  spr.drawCircle((int)c.x, (int)c.y, (int)c.r, TFT_WHITE);
  spr.drawLine((int)c.x, (int)c.y, (int)(c.x + cosf(c.ang) * c.r), (int)(c.y + sinf(c.ang) * c.r), TFT_BLACK);
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(0x1000);
  spr.fillCircle((int)RING_X, (int)RING_Y, (int)RING_R, 0x2945);
  spr.drawCircle((int)RING_X, (int)RING_Y, (int)RING_R, TFT_YELLOW);
  spr.drawCircle((int)RING_X, (int)RING_Y, (int)RING_R - 3, TFT_RED);

  for (int i = 0; i < MAX_BOTS; i++) if (bots[i].active) drawCar(bots[i]);
  drawCar(player);

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_YELLOW);
  snprintf(buf, sizeof buf, "Wave %d", wave);
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox("RING OUT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("BUMPER", 67, 18, 4);

  spr.drawCircle(67, 62, 16, TFT_YELLOW);
  spr.fillCircle(60, 62, 5, TFT_CYAN);
  spr.fillCircle(74, 62, 5, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"steer", "turbo ram"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Knock bots out of the ring!", 67, 148, 1);

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
  spr.drawCircle(x, y, 12, c(TFT_YELLOW));
  int d = (int)roundf(3 * triWave(t, 700));
  spr.fillCircle(x - 4 - d, y, 4, c(TFT_CYAN));
  spr.fillCircle(x + 4 + d, y, 4, c(TFT_RED));
}

void begin() {
  hiScore = prefs.getUInt("bumper_hi", 0);
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

}  // namespace bumper

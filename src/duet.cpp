// Duet (Orbit Dodger)
//
// Controls (two buttons):
//   Hold L : rotate orbit counter-clockwise
//   Hold R : rotate orbit clockwise
// Thread both orbiting spheres safely past falling blocks!

#include "common.h"

namespace duet {

static const float ORBIT_X = 67.0f, ORBIT_Y = 192.0f, ORBIT_R = 28.0f;
static const int BALL_R = 5;
static const int MAX_BLOCKS = 8;
static const uint32_t TICK_MS = 16;

struct Block { float x, y, w, h; bool active, scored; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float ang, speed;
static Block blocks[MAX_BLOCKS];
static int spawnT;
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
    prefs.putUInt("duet_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  ang = 0.0f;
  speed = 1.6f;
  spawnT = 20;
  newHigh = false;
  for (int i = 0; i < MAX_BLOCKS; i++) blocks[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool hitBall(float bx, float by, const Block &b) {
  float cx = constrain(bx, b.x, b.x + b.w);
  float cy = constrain(by, b.y, b.y + b.h);
  float dx = bx - cx, dy = by - cy;
  return dx * dx + dy * dy <= (BALL_R - 1) * (BALL_R - 1);
}

static void tick() {
  if (bL.stable && !bR.stable) ang -= 0.085f;
  if (bR.stable && !bL.stable) ang += 0.085f;

  speed = min(3.4f, 1.6f + score * 0.008f);

  if (--spawnT <= 0) {
    spawnT = max(24, 50 - (int)(score / 8));
    for (int i = 0; i < MAX_BLOCKS; i++) {
      if (blocks[i].active) continue;
      int kind = rnd(3);
      float w = (kind == 0) ? 48.0f : (kind == 1) ? 58.0f : 34.0f;
      float x = (kind == 0) ? 10.0f : (kind == 1) ? (125.0f - w) : (67.0f - w / 2);
      blocks[i] = {x, (float)HEADER_H - 10, w, 12.0f, true, false};
      break;
    }
  }

  float x1 = ORBIT_X + cosf(ang) * ORBIT_R, y1 = ORBIT_Y + sinf(ang) * ORBIT_R;
  float x2 = ORBIT_X - cosf(ang) * ORBIT_R, y2 = ORBIT_Y - sinf(ang) * ORBIT_R;

  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (!blocks[i].active) continue;
    blocks[i].y += speed;
    if (!blocks[i].scored && blocks[i].y > ORBIT_Y + ORBIT_R) {
      blocks[i].scored = true;
      score += 10;
    }
    if (blocks[i].y > 246) { blocks[i].active = false; continue; }
    if (hitBall(x1, y1, blocks[i]) || hitBall(x2, y2, blocks[i])) {
      gameOver();
      return;
    }
  }
}

static void drawGame() {
  spr.fillSprite(0x0821);
  spr.drawCircle((int)ORBIT_X, (int)ORBIT_Y, (int)ORBIT_R, 0x2945);

  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (!blocks[i].active) continue;
    spr.fillRoundRect((int)blocks[i].x, (int)blocks[i].y, (int)blocks[i].w, (int)blocks[i].h, 2, TFT_WHITE);
  }

  float x1 = ORBIT_X + cosf(ang) * ORBIT_R, y1 = ORBIT_Y + sinf(ang) * ORBIT_R;
  float x2 = ORBIT_X - cosf(ang) * ORBIT_R, y2 = ORBIT_Y - sinf(ang) * ORBIT_R;
  spr.fillCircle((int)x1, (int)y1, BALL_R, TFT_CYAN);
  spr.fillCircle((int)x2, (int)y2, BALL_R, TFT_RED);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("SHATTERED", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("DUET", 67, 18, 4);

  spr.drawCircle(67, 64, 14, 0x39E7);
  spr.fillCircle(53, 64, 4, TFT_CYAN);
  spr.fillCircle(81, 64, 4, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"spin CCW", "spin CW"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Keep both spheres safe!", 67, 148, 1);

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
  spr.drawCircle(x, y, 9, c(0x39E7));
  float a = t * 0.004f;
  spr.fillCircle(x + (int)roundf(cosf(a) * 9), y + (int)roundf(sinf(a) * 9), 3, c(TFT_CYAN));
  spr.fillCircle(x - (int)roundf(cosf(a) * 9), y - (int)roundf(sinf(a) * 9), 3, c(TFT_RED));
}

void begin() {
  hiScore = prefs.getUInt("duet_hi", 0);
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

}  // namespace duet

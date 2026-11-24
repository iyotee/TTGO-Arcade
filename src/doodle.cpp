// Doodle Jump
//
// Controls (two buttons):
//   Hold L : move left
//   Hold R : move right
// Wraps horizontally across screen edges. Bounce on platforms to climb;
// cyan platforms move, brown ones break after one bounce, yellow pads boost.

#include "common.h"

namespace doodle {

static const int NUM_PLATS = 10;
static const int PLAT_W = 24, PLAT_H = 5;
static const int PLAYER_W = 10, PLAYER_H = 12;
static const uint32_t TICK_MS = 16;

static const float GRAVITY = 0.16f;
static const float JUMP_V = -4.8f;
static const float BOOST_V = -7.4f;

enum PlatType : uint8_t { P_NORM, P_MOVE, P_BREAK, P_BOOST };
struct Plat { float x, y, vx; PlatType type; bool alive; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static Plat plats[NUM_PLATS];
static float px, py, vx, vy, camY, maxClimb;
static int facing;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void spawnPlat(Plat &p, float y) {
  p.y = y;
  p.x = 4 + rnd(135 - PLAT_W - 8);
  p.alive = true;
  p.vx = 0;
  int r = rnd(100);
  int diff = min(40, (int)(score / 40));
  if (r < 12) {
    p.type = P_BOOST;
  } else if (r < 25 + diff / 2) {
    p.type = P_MOVE;
    p.vx = (rnd(2) ? 1 : -1) * (0.6f + 0.01f * diff);
  } else if (r < 35 + diff) {
    p.type = P_BREAK;
  } else {
    p.type = P_NORM;
  }
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("doodle_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  newHigh = false;
  camY = 0;
  maxClimb = 0;
  px = 67;
  py = 200;
  vx = 0;
  vy = JUMP_V;
  facing = 1;

  // Safe starting platform under the player
  plats[0] = {55.0f, 218.0f, 0.0f, P_NORM, true};
  for (int i = 1; i < NUM_PLATS; i++) {
    spawnPlat(plats[i], 218.0f - i * 21.0f);
  }
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  if (bL.stable && !bR.stable) { vx = -2.8f; facing = -1; }
  else if (bR.stable && !bL.stable) { vx = 2.8f; facing = 1; }
  else vx *= 0.82f;

  px += vx;
  if (px < -PLAYER_W / 2) px += 135;
  if (px >= 135 + PLAYER_W / 2) px -= 135;

  float prevY = py;
  vy += GRAVITY;
  py += vy;

  // Moving platforms & recycle off-screen ones
  float topY = plats[0].y;
  for (int i = 1; i < NUM_PLATS; i++) if (plats[i].y < topY) topY = plats[i].y;

  for (int i = 0; i < NUM_PLATS; i++) {
    Plat &p = plats[i];
    if (p.type == P_MOVE && p.alive) {
      p.x += p.vx;
      if (p.x < 2) { p.x = 2; p.vx = fabsf(p.vx); }
      if (p.x > 133 - PLAT_W) { p.x = 133 - PLAT_W; p.vx = -fabsf(p.vx); }
    }
    if (p.y - camY > 245) {
      float gap = 17.0f + rnd(10);
      spawnPlat(p, topY - gap);
      topY = p.y;
    }
  }

  // Landing on platforms while falling
  if (vy > 0) {
    for (int i = 0; i < NUM_PLATS; i++) {
      Plat &p = plats[i];
      if (!p.alive) continue;
      if (prevY <= p.y + 2 && py >= p.y &&
          px + PLAYER_W / 2 >= p.x && px - PLAYER_W / 2 <= p.x + PLAT_W) {
        if (p.type == P_BREAK) {
          p.alive = false;
          vy = JUMP_V * 0.9f;
        } else if (p.type == P_BOOST) {
          vy = BOOST_V;
        } else {
          vy = JUMP_V;
        }
        py = p.y;
        break;
      }
    }
  }

  // Scroll camera up when player climbs above mid-screen
  float screenY = py - camY;
  if (screenY < 120) {
    camY -= (120 - screenY);
    if (-camY > maxClimb) {
      maxClimb = -camY;
      score = (uint32_t)(maxClimb / 4);
    }
  }

  if (py - camY > 248) gameOver();
}

static void drawGame() {
  spr.fillSprite(0x0842);

  for (int i = 0; i < NUM_PLATS; i++) {
    const Plat &p = plats[i];
    if (!p.alive) continue;
    int sy = (int)(p.y - camY);
    if (sy < HEADER_H - 4 || sy > 240) continue;
    uint16_t col = p.type == P_NORM  ? TFT_GREEN
                 : p.type == P_MOVE  ? TFT_CYAN
                 : p.type == P_BREAK ? 0x9A60
                                     : TFT_YELLOW;
    spr.fillRoundRect((int)p.x, sy, PLAT_W, PLAT_H, 2, col);
    if (p.type == P_BOOST) {
      spr.fillRect((int)p.x + PLAT_W / 2 - 4, sy - 3, 8, 3, TFT_RED);
    }
  }

  // Player character
  int sx = (int)px, sy = (int)(py - camY);
  spr.fillRoundRect(sx - 5, sy - 11, 10, 9, 3, TFT_YELLOW);
  spr.fillRect(sx - 4, sy - 2, 2, 2, TFT_ORANGE);
  spr.fillRect(sx + 2, sy - 2, 2, 2, TFT_ORANGE);
  spr.fillCircle(sx + facing * 2, sy - 8, 1, TFT_BLACK);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("DOODLE", 67, 18, 4);

  spr.fillRoundRect(48, 62, 38, 6, 2, TFT_GREEN);
  spr.fillRoundRect(62, 46, 10, 10, 3, TFT_YELLOW);

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
  spr.drawString("Cyan moves, yellow boosts", 67, 148, 1);

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
  spr.fillRoundRect(x - 10, y + 9, 20, 4, 2, c(TFT_GREEN));
  int jump = (int)roundf(15 * triWave(t, 900));
  spr.fillRoundRect(x - 4, y + 1 - jump, 8, 8, 2, c(TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("doodle_hi", 0);
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

}  // namespace doodle

// Ski (Downhill Slalom)
//
// Controls (two buttons):
//   Hold L : carve left
//   Hold R : carve right
// Dodge trees and rocks; pass between red/blue slalom flags for +50 bonus!

#include "common.h"

namespace ski {

static const int MAX_OBS = 14;
static const int SKIER_Y = 74;
static const uint32_t TICK_MS = 16;

enum ObsKind : uint8_t { OBS_TREE, OBS_ROCK, OBS_GATE };
struct Obs { float x, y; ObsKind kind; bool active, scored; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float sx, steer, speed, dist;
static Obs obs[MAX_OBS];
static int spawnTimer;
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
    prefs.putUInt("ski_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  dist = 0;
  sx = 67;
  steer = 0;
  speed = 1.8f;
  spawnTimer = 15;
  newHigh = false;
  for (int i = 0; i < MAX_OBS; i++) obs[i].active = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawnObs() {
  for (int i = 0; i < MAX_OBS; i++) {
    if (obs[i].active) continue;
    int r = rnd(100);
    if (r < 22) {
      obs[i] = {(float)(16 + rnd(76)), 248.0f, OBS_GATE, true, false};
    } else {
      obs[i] = {(float)(8 + rnd(119)), 248.0f, (r < 75) ? OBS_TREE : OBS_ROCK, true, false};
    }
    return;
  }
}

static void tick() {
  if (bL.stable && !bR.stable) steer = max(-1.0f, steer - 0.14f);
  else if (bR.stable && !bL.stable) steer = min(1.0f, steer + 0.14f);
  else steer *= 0.85f;

  sx = constrain(sx + steer * 2.5f, 6.0f, 129.0f);
  speed = min(3.8f, 1.8f + dist * 0.00025f);
  float vy = speed * (1.0f - 0.25f * fabsf(steer));
  dist += vy;
  if (((int)dist & 7) == 0) score++;

  if (--spawnTimer <= 0) {
    spawnObs();
    spawnTimer = max(10, 22 - (int)(speed * 3));
  }

  for (int i = 0; i < MAX_OBS; i++) {
    if (!obs[i].active) continue;
    obs[i].y -= vy;
    if (obs[i].y < HEADER_H - 16) { obs[i].active = false; continue; }

    if (obs[i].kind == OBS_GATE) {
      if (!obs[i].scored && obs[i].y <= SKIER_Y) {
        obs[i].scored = true;
        if (sx >= obs[i].x && sx <= obs[i].x + 28) score += 50;
      }
    } else {
      float dx = fabsf(sx - obs[i].x), dy = fabsf(SKIER_Y - obs[i].y);
      if (dx < 6.0f && dy < 7.0f) { gameOver(); return; }
    }
  }
}

static void drawGame() {
  spr.fillSprite(0xEF7D);                               // snow white

  for (int i = 0; i < MAX_OBS; i++) {
    if (!obs[i].active) continue;
    int ox = (int)obs[i].x, oy = (int)obs[i].y;
    if (obs[i].kind == OBS_TREE) {
      spr.fillRect(ox - 1, oy + 4, 3, 4, 0x8A22);
      spr.fillTriangle(ox, oy - 8, ox - 6, oy + 4, ox + 6, oy + 4, 0x1B42);
    } else if (obs[i].kind == OBS_ROCK) {
      spr.fillRoundRect(ox - 5, oy - 3, 10, 6, 2, 0x7BEF);
    } else {
      spr.fillRect(ox, oy - 8, 2, 10, TFT_RED);
      spr.fillRect(ox + 28, oy - 8, 2, 10, TFT_BLUE);
      spr.fillTriangle(ox + 2, oy - 8, ox + 7, oy - 6, ox + 2, oy - 4, TFT_RED);
      spr.fillTriangle(ox + 28, oy - 8, ox + 23, oy - 6, ox + 28, oy - 4, TFT_BLUE);
    }
  }

  // Skier & ski tracks
  int x = (int)sx, dx = (int)(steer * 5);
  spr.drawLine(x - 3 - dx, SKIER_Y - 12, x - 3, SKIER_Y, 0xBDD7);
  spr.drawLine(x + 3 - dx, SKIER_Y - 12, x + 3, SKIER_Y, 0xBDD7);
  spr.drawLine(x - 3, SKIER_Y - 4, x - 3 + dx, SKIER_Y + 6, TFT_RED);
  spr.drawLine(x + 3, SKIER_Y - 4, x + 3 + dx, SKIER_Y + 6, TFT_RED);
  spr.fillCircle(x, SKIER_Y - 4, 3, TFT_BLUE);
  spr.fillCircle(x, SKIER_Y - 8, 2, TFT_ORANGE);

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER) drawGameOverBox("WIPEOUT!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("SKI", 67, 18, 4);

  spr.fillTriangle(67, 50, 59, 66, 75, 66, 0x1B42);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"carve left", "carve right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Pass flag gates for +50", 67, 148, 1);

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
  spr.fillTriangle(x - 6, y - 6, x - 11, y + 6, x - 1, y + 6, c(0x1B42));
  int sx = x + 3 + (int)roundf(5 * (triWave(t, 1200) - 0.5f));
  spr.fillCircle(sx, y + 4, 3, c(TFT_CYAN));
}

void begin() {
  hiScore = prefs.getUInt("ski_hi", 0);
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

}  // namespace ski

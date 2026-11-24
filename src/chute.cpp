// Chute (Skydiver)
//
// Controls (two buttons):
//   Hold L / R : steer left / right
//   L + R      : open parachute (below 300 m)
// Collect gold rings, dodge birds, then pop your chute and land on the pad!

#include "common.h"

namespace chute {

static const int MAX_OBJ = 8;
static const int DIVER_Y = 82;
static const uint32_t TICK_MS = 16;

struct Obj { float x, y; bool ring, active; };

enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float dx, alt, fallSpd, padX;
static bool chuteOpen;
static int level, spawnT;
static Obj objs[MAX_OBJ];
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void startJump() {
  dx = 67;
  alt = 900.0f;
  chuteOpen = false;
  fallSpd = 2.1f + min(1.5f, level * 0.25f);
  padX = 24 + rnd(87);
  spawnT = 15;
  for (int i = 0; i < MAX_OBJ; i++) objs[i].active = false;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("chute_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  level = 1;
  newHigh = false;
  startJump();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void tick() {
  float steerSpd = chuteOpen ? 1.8f : 2.8f;
  if (bL.stable && !bR.stable) dx -= steerSpd;
  if (bR.stable && !bL.stable) dx += steerSpd;
  dx = constrain(dx, 8.0f, 127.0f);

  if (!chuteOpen && bL.stable && bR.stable && alt < 350.0f) {
    chuteOpen = true;
    fallSpd = 0.85f;
  }

  alt -= fallSpd;

  if (alt > 160.0f && --spawnT <= 0) {
    spawnT = 22 + rnd(16);
    for (int i = 0; i < MAX_OBJ; i++) {
      if (objs[i].active) continue;
      objs[i] = {(float)(12 + rnd(111)), 246.0f, rnd(100) < 48, true};
      break;
    }
  }

  for (int i = 0; i < MAX_OBJ; i++) {
    if (!objs[i].active) continue;
    objs[i].y -= fallSpd * 1.15f;
    if (objs[i].y < HEADER_H - 10) { objs[i].active = false; continue; }
    if (fabsf(objs[i].x - dx) < 9 && fabsf(objs[i].y - DIVER_Y) < 9) {
      if (objs[i].ring) {
        objs[i].active = false;
        score += 30;
      } else {
        gameOver();
        return;
      }
    }
  }

  if (alt <= 0) {
    if (!chuteOpen) { gameOver(); return; }
    if (fabsf(dx - padX) <= 18.0f) {
      score += 100 * level;
      level++;
      startJump();
    } else {
      gameOver();
    }
  }
}

static void drawGame() {
  char buf[16];
  spr.fillSprite(0x3C9F);                               // sky blue

  for (int i = 0; i < MAX_OBJ; i++) {
    if (!objs[i].active) continue;
    int x = (int)objs[i].x, y = (int)objs[i].y;
    if (objs[i].ring) {
      spr.drawCircle(x, y, 5, TFT_YELLOW);
      spr.drawCircle(x, y, 4, TFT_ORANGE);
    } else {
      spr.fillTriangle(x - 6, y, x + 6, y - 3, x + 6, y + 3, TFT_RED);
    }
  }

  // Ground & target pad appear near bottom
  if (alt < 158.0f) {
    int gy = DIVER_Y + (int)alt + 6;
    spr.fillRect(0, gy, 135, 240 - gy, 0x1B42);
    spr.fillRect((int)padX - 16, gy, 32, 4, TFT_YELLOW);
  }

  // Diver & chute
  int x = (int)dx;
  if (chuteOpen) {
    spr.fillCircle(x, DIVER_Y - 14, 12, TFT_RED);
    spr.fillRect(x - 12, DIVER_Y - 14, 25, 12, 0x3C9F);
    spr.drawLine(x - 11, DIVER_Y - 14, x, DIVER_Y, TFT_WHITE);
    spr.drawLine(x + 11, DIVER_Y - 14, x, DIVER_Y, TFT_WHITE);
  }
  spr.fillCircle(x, DIVER_Y - 2, 3, TFT_YELLOW);
  spr.fillRect(x - 3, DIVER_Y + 1, 7, 7, TFT_ORANGE);

  if (!chuteOpen && alt < 300.0f && state == PLAY) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor((millis() / 150) & 1 ? TFT_RED : TFT_YELLOW);
    spr.drawString("L+R CHUTE!", 67, 140, 2);
  }

  drawScoreHeader(score, hiScore);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "%dm", max(0, (int)alt));
  spr.drawString(buf, 132, 6, 2);

  if (state == GAMEOVER) drawGameOverBox(chuteOpen ? "MISSED PAD!" : "TOO FAST!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("CHUTE", 67, 18, 4);

  spr.fillCircle(67, 58, 11, TFT_RED);
  spr.fillRect(55, 58, 25, 11, TFT_BLACK);
  spr.drawLine(56, 58, 67, 70, TFT_WHITE);
  spr.drawLine(78, 58, 67, 70, TFT_WHITE);
  spr.fillCircle(67, 72, 3, TFT_YELLOW);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L/R", "L + R"};
  const char *acts[] = {"steer", "open chute"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 68, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Land on yellow pad!", 67, 148, 1);

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
  int dy = (int)roundf(3 * (triWave(t, 1200) - 0.5f));
  spr.fillCircle(x, y - 4 + dy, 8, c(TFT_RED));
  spr.fillRect(x - 8, y - 4 + dy, 17, 9, TFT_BLACK);
  spr.drawLine(x - 7, y - 4 + dy, x, y + 6 + dy, c(TFT_WHITE));
  spr.drawLine(x + 7, y - 4 + dy, x, y + 6 + dy, c(TFT_WHITE));
  spr.fillCircle(x, y + 8 + dy, 2, c(TFT_YELLOW));
}

void begin() {
  hiScore = prefs.getUInt("chute_hi", 0);
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

}  // namespace chute
